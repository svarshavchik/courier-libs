/*
** Copyright 2001-2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcp.h"
#include "pcpdtimer.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include "liblock/config.h"
#include "liblock/liblock.h"
#include "numlib/numlib.h"
#include "maildir/maildircache.h"
#include "pcpdauth.h"
#include "pcpdauthtoken.h"
#include "calendardir.h"

PCP_STRERROR

#define exit(_a_) _exit(_a_)

static char *userid;
static char *proxy_userid;
static char *deleted_eventid;
static struct PCP_new_eventid *new_eventid;
static int conflict_flag;
static int force_flag;
static int notbooked;

static struct PCP_commit new_commit;
static struct PCP_event_time *new_commit_times;
static struct PCP_event_participant *new_commit_participants;
static char *new_commit_participants_buf;

static struct pcpdtimer rebook_timeout;

static int termsig;

static int need_rset;
static char *input_buffer=NULL;
static size_t input_buffer_len=0;
static size_t input_line_len;
static time_t prev_time;
static char *inp_ptr;
static int inp_left;

static void inactive(struct PCP *p, void *dummy)
{
	termsig=1;
}

static int inputchar(struct PCP *pcp)
{
	int c;

	while (inp_left == 0)
	{
		time_t new_time;
		struct timeval tv, *tvptr;
		fd_set fds;

		time(&new_time);

		/* Trigger any needed functions */

		while (first_timer &&
		       (new_time < prev_time - 30 ||
			new_time >= first_timer->alarm))
		{
			void (*func)(struct PCP *, void *)
				=first_timer->handler;
			void *arg=first_timer->voidarg;

			pcpdtimer_triggered(first_timer);
			(*func)(pcp, arg);
		}

		if (termsig)
			return (EOF);
			
		if (first_timer)
		{
			tvptr= &tv;
			tv.tv_sec=first_timer->alarm-new_time;
			tv.tv_usec=0;
		}
		else
			tvptr=NULL;

		/*
		** Read more input.  piggy-back after the
		** input buffer :-)
		*/

		if (input_line_len + BUFSIZ >= input_buffer_len)
		{
			size_t n=input_line_len + BUFSIZ;
			char *p=realloc(input_buffer, n);

			if (!p)
			{
				perror("realloc");
				exit(1);
			}
			input_buffer=p;
			input_buffer_len=n;
		}

		inp_ptr=input_buffer + input_line_len;

		FD_ZERO(&fds);
		FD_SET(0, &fds);

		if (fflush(stdout) || ferror(stdout))
		{
			perror("write");
			termsig=1;
			return (EOF);
		}

		if (select(1, &fds, NULL, NULL, tvptr) < 0)
		{
			if (termsig)
				return (EOF);

			if (errno != EINTR)
			{
				perror("select");
				return (EOF);
			}
			inp_left=0;
			continue;
		}

		if (!FD_ISSET(0, &fds))
			continue;

		inp_left=read(0, inp_ptr, BUFSIZ);

		if (termsig || inp_left == 0)
		{
			termsig=1;
			return (EOF);
		}

		if (inp_left < 0)
		{
			perror("read");
			return (EOF);
		}
	}

	c= *inp_ptr++;
	--inp_left;
	return ((int)(unsigned char)c);
}

/* --------------------------------------------------------------- */

struct PCP *open_calendar(const char *p)
{
        struct PCP *pcp;
        struct passwd *pw=getpwuid(getuid());
        const char *cp;

        if (!pw)
        {
                perror("getpwuid");
                exit(1);
        }

	userid=strdup(pw->pw_name);

        if (p && *p)
	{
		if (chdir(p))
		{
			perror(p);
			exit(1);
		}
	}
        else if ((cp=getenv("PCPDIR")) != NULL && *cp)
        {
		if (chdir(cp))
		{
			perror(cp);
			exit(1);
		}
        }

	pcp=pcp_open_dir(".", userid);

	if (pcp && pcp_cleanup(pcp))
	{
		pcp_close(pcp);
		pcp=NULL;
	}

	if (!pcp)
	{
		perror("pcp_open_dir");
		exit(1);
	}
	return (pcp);
}

/* --------------------------------------------------------------- */

static void error(int n)
{
        const char *p;

	switch (n) {
	case PCP_ERR_EVENTNOTFOUND:
		printf("504 Event not found\n");
		return;
	case PCP_ERR_EVENTLOCKED:
		printf("506 This event is temporarily locked\n");
		return;
	}

	p=pcp_strerror(n);

	printf("500 %s\n", p ? p:strerror(errno));
}

/* --------------------------------------------------------------- */

struct proxy_list {
	struct proxy_list *next;
	char *userid;
	char *old_event_id;
	struct PCP *proxy;
	struct PCP_new_eventid *newevent;
	int flags;

#define PROXY_NEW 1
#define PROXY_IGNORE 2

} ;

struct proxy_list *proxy_list=NULL;

static void proxy_list_rset()
{
	struct proxy_list *p;

	while ((p=proxy_list) != NULL)
	{
		proxy_list=p->next;
		if (p->newevent)
			pcp_destroy_eventid(p->proxy, p->newevent);
		pcp_close(p->proxy);
		free(p->userid);
		if (p->old_event_id)
			free(p->old_event_id);
		proxy_list=p->next;
		free(p);
	}
}

/* Compare two e-mail addresses */

static int addrcmp(const char *a, const char *b)
{
	char *aa=NULL;
	const char *h=auth_myhostname();
	int rc;

	if (!h)
		return (1);

	if (strchr(a, '@') == NULL)
	{
		aa=malloc(strlen(a)+strlen(h)+2);

		if (!aa)
		{
			fprintf(stderr, "NOTICE: malloc: out of memory.\n");
			return (1);
		}
		strcat(strcat(strcpy(aa, a), "@"), h);
		rc=addrcmp(aa, b);
		free(aa);
		return (rc);
	}

	if (strchr(b, '@') == NULL)
	{
		aa=malloc(strlen(b)+strlen(h)+2);

		if (!aa)
		{
			fprintf(stderr, "NOTICE: malloc: out of memory.\n");
			return (1);
		}
		strcat(strcat(strcpy(aa, b), "@"), h);
		rc=addrcmp(a, aa);
		free(aa);
		return (rc);
	}

	rc=strcasecmp(a, b);
	return (rc);
}

static struct proxy_list *proxy(const char *proxy_userid, char **errmsg)
{
	struct proxy_list *p;

	if (errmsg)
		*errmsg=0;

	for (p=proxy_list; p; p=p->next)
	{
		if (addrcmp(proxy_userid, p->userid) == 0)
			return (p);
	}

	p=malloc(sizeof(struct proxy_list));
	if (!p)
		return (NULL);
	memset(p, 0, sizeof(*p));

	if ((p->userid=strdup(proxy_userid)) == NULL)
	{
		free(p);
		return (NULL);
	}

	if ((p->proxy=pcp_find_proxy(proxy_userid, NULL, errmsg)) == NULL ||
	    pcp_set_proxy(p->proxy, userid))
	{
		if (p->proxy)
			pcp_close(p->proxy);
		free(p->userid);
		free(p);
		return (NULL);
	}

	p->next=proxy_list;
	proxy_list=p;
	return (p);
}

/* --------------------------------------------------------------- */

static void rset(struct PCP *pcp)
{
	pcpdtimer_triggered(&rebook_timeout);
	if (new_eventid)
		pcp_destroy_eventid(pcp, new_eventid);
	if (new_commit_times)
		free(new_commit_times);
	new_commit_times=NULL;
	if (new_commit_participants)
		free(new_commit_participants);
	if (new_commit_participants_buf)
		free(new_commit_participants_buf);
	new_commit_participants=NULL;
	new_commit_participants_buf=NULL;
	new_commit.event_times=NULL;
	new_commit.n_event_times=0;
	new_eventid=NULL;
	if (deleted_eventid)
		free(deleted_eventid);
	deleted_eventid=NULL;
	notbooked=0;
	proxy_list_rset();
}

struct readnewevent_s {
	FILE *tmpfile;
	int seeneol;
	int seendot;
	int seeneof;
	int sentprompt;
	time_t last_noop_time;

	int cnt;
	struct PCP *pcp;
	struct pcpdtimer inactivity_timeout;
} ;

static int readnewevent_callback(char *p, int n, void *vp)
{
	struct readnewevent_s *rne=(struct readnewevent_s *)vp;
	int cnt=0;

	if (!rne->sentprompt)
	{
		rne->sentprompt=1;
		printf("300 Send event text, terminate by a line with a single dot.\n");
	}

	while (!rne->seeneof && n)
	{
		int c=inputchar(rne->pcp);

		if (c == EOF)
		{
			rne->seeneof=1;
			errno=ETIMEDOUT;
			return (-1);
		}

		if (c == '\r')
			continue;

		if (c == '\n')
		{
			if (rne->seendot)
				rne->seeneof=1;
			rne->seendot=0;
			rne->seeneol=1;
			if (rne->seeneof)
				continue;
		}
		else
		{
			rne->seendot= c == '.' && rne->seeneol;
			rne->seeneol=0;
			if (rne->seendot)
				continue;
		}
		putc(c, rne->tmpfile);
		++cnt;
		*p++ = c;
		--n;

		if (++rne->cnt >= 8192)
		{
			time_t t;

			time(&t);
			if (t >= rne->last_noop_time+300) /* Don't timeout */
			{
				struct proxy_list *p;
				rne->last_noop_time=t;
				for (p=proxy_list; p; p=p->next)
					pcp_noop(p->proxy);
			}

			pcpdtimer_install(&rne->inactivity_timeout, 300);
			rne->cnt=0;
		}
	}
	return (cnt);
}

static void proxy_error(const char *n, const char *msg)
{
	while (*msg)
	{
		printf("500-%s - ", n);
		while (*msg)
		{
			if (*msg == '\n')
			{
				++msg;
				break;
			}
			if (*msg != '\r')
				putchar( *msg);
			++msg;
		}
		printf("\n");
	}

}

static int mkparticipants(struct PCP_save_event *se)
{
	struct proxy_list *p;
	unsigned cnt;
	size_t l=0;

	if (new_commit_participants)
		free(new_commit_participants);
	if (new_commit_participants_buf)
		free(new_commit_participants_buf);
	new_commit_participants=NULL;
	new_commit_participants_buf=NULL;

	se->event_participants=NULL;
	se->n_event_participants=0;

	for (cnt=0, p=proxy_list; p; p=p->next)
	{
		if (!p->newevent)
			continue;

		if (p->flags & PROXY_IGNORE)
			continue;

		++cnt;
		l += strlen(p->userid)+1;

		if (p->newevent->eventid)
			l += strlen(p->newevent->eventid)+1;
	}
	if (cnt == 0)
		return (0);

	if ((new_commit_participants_buf=malloc(l)) == NULL)
		return (-1);
	if ((new_commit_participants
	     =calloc(cnt, sizeof(struct PCP_event_participant))) == NULL)
		return (-1);

	l=0;

	for (cnt=0, p=proxy_list; p; p=p->next)
	{
		if (!p->newevent)
			continue;

		if (p->flags & PROXY_IGNORE)
			continue;

		new_commit_participants[cnt].address=
			strcpy(new_commit_participants_buf+l, p->userid);
		l += strlen(p->userid)+1;

		if (p->newevent->eventid)
		{
			new_commit_participants[cnt].eventid=
				strcpy(new_commit_participants_buf+l,
				       p->newevent->eventid);
			l += strlen(p->newevent->eventid)+1;
		}
		++cnt;
	}
	se->event_participants=new_commit_participants;
	se->n_event_participants=cnt;
	return (0);
}

static struct PCP_new_eventid *readnewevent(struct PCP *pcp)
{
	struct PCP_save_event se;
	struct readnewevent_s rne;
	struct PCP_new_eventid *ne;
	const char *cp;
	struct proxy_list *p;
	int first_save=1;

	memset(&rne, 0, sizeof(rne));
	if (!deleted_eventid)
		proxy_list_rset();

	/* Open new proxy connections */

	while ((cp=strtok(NULL, " ")) != NULL)
	{
		char *errmsg, *q;
		char *n=strdup(cp);
		struct proxy_list *pcp;

		if (!n)
		{
			fprintf(stderr, "ALERT: Out of memory.\n");
			exit(1);
		}

		if (proxy_userid)
		{
			printf("500-Cannot create proxy in proxy mode.\n");
			free(n);
			errno=EIO;
			return (NULL);
		}

		strcpy(n, cp);
		pcp=proxy(n, &errmsg);

		if (pcp)
		{
			pcp->flags |= PROXY_NEW;
			free(n);
			continue;
		}

		if (force_flag)
		{
			pcp->flags |= PROXY_IGNORE;
			free(n);
			continue;
		}

		while (errmsg && (q=strchr(errmsg, '\n')) != 0)
			*q='/';
		printf("500-%s: %s\n", n, errmsg ? errmsg:"Failed to create a proxy connection.");
		free(n);
		proxy_list_rset();
		return (NULL);
	}

	memset(&se, 0, sizeof(se));
	if ((rne.tmpfile=tmpfile()) == NULL)
		return (NULL);
	time(&rne.last_noop_time);

	rne.seeneol=1;
	rne.seendot=0;
	rne.seeneof=0;
	rne.cnt=0;
	rne.pcp=pcp;
	pcpdtimer_init(&rne.inactivity_timeout);
	rne.inactivity_timeout.handler=&inactive;
	pcpdtimer_install(&rne.inactivity_timeout, 300);

	for (p=proxy_list; p; p=p->next)
	{
		struct PCP_save_event se;

		if ( !(p->flags & PROXY_NEW))
			continue;

		if (fseek(rne.tmpfile, 0L, SEEK_SET) < 0
		    || lseek(fileno(rne.tmpfile), 0L, SEEK_SET) < 0)
		{
			int save_errno=errno;
			proxy_list_rset();
			pcpdtimer_triggered(&rne.inactivity_timeout);
			fclose(rne.tmpfile);
			errno=save_errno;
			return (NULL);
		}

		memset(&se, 0, sizeof(se));
		if (first_save)
		{
			se.write_event_func_misc_ptr= &rne;
			se.write_event_func=readnewevent_callback;
		}
		else
			se.write_event_fd=fileno(rne.tmpfile);

		if ((p->newevent=pcp_new_eventid(p->proxy,
						 p->old_event_id,
						 &se)) == NULL)
		{
			pcpdtimer_triggered(&rne.inactivity_timeout);

			if (force_flag)
			{
				/* Force it through */

				p->flags &= ~PROXY_NEW;
				p->flags |= PROXY_IGNORE;
				continue;
			}

			proxy_error(p->userid,
				    pcp_errmsg(p->proxy));
			proxy_list_rset();
			fclose(rne.tmpfile);
			errno=EIO;
			return (NULL);
		}
		if (first_save)
			pcpdtimer_triggered(&rne.inactivity_timeout);
		first_save=0;
	}


	if (first_save)
	{
		se.write_event_func_misc_ptr= &rne;
		se.write_event_func=readnewevent_callback;
	}
	else
		se.write_event_fd=fileno(rne.tmpfile);

	if (mkparticipants(&se) || fseek(rne.tmpfile, 0L, SEEK_SET) < 0
	    || lseek(fileno(rne.tmpfile), 0L, SEEK_SET) < 0)
	{
		int save_errno=errno;

		proxy_list_rset();
		fclose(rne.tmpfile);
		errno=save_errno;
		return (NULL);
	}

	if ((ne=pcp_new_eventid(pcp, deleted_eventid, &se)) == NULL)
	{
		while (!rne.seeneof)
		{
			char buf[512];

			readnewevent_callback(buf, sizeof(buf), &se);
		}
	}
	pcpdtimer_triggered(&rne.inactivity_timeout);
	if (first_save)
	{
		if (fflush(rne.tmpfile) || ferror(rne.tmpfile))
		{
			int save_errno=errno;

			proxy_list_rset();
			fclose(rne.tmpfile);
			errno=save_errno;
			return (NULL);
		}
	}

	notbooked=1;
	fclose(rne.tmpfile);
	return (ne);
}

struct book_time_list {
	struct book_time_list *next;
	struct PCP_event_time times;
} ;

struct report_conflict_info {
	struct report_conflict_info *next;
	char *conflict_eventid;
	char *conflict_addr;
	time_t conflict_start;
	time_t conflict_end;
} ;

struct extra_conflict_info {
	struct report_conflict_info **conflict_list;
	const char *proxy_addr;
} ;

static int do_report_conflict(const char *e, time_t start, time_t end,
			   const char *addr, void *vp)
{
	struct extra_conflict_info *eci=(struct extra_conflict_info *)vp;
	struct report_conflict_info **p=eci->conflict_list;
	struct report_conflict_info *q=malloc(sizeof(**p));

	if (!q)
		return (-1);

	if (eci->proxy_addr)
		addr=eci->proxy_addr;

	if ((q->conflict_eventid=strdup(e)) == NULL)
	{
		free(q);
		return (-1);
	}

	if ((q->conflict_addr=strdup(addr)) == NULL)
	{
		free(q->conflict_eventid);
		free(q);
		return (-1);
	}

	while (*p)
	{
		p= &(*p)->next;
	}

	*p=q;
	q->next=0;
	q->conflict_start=start;
	q->conflict_end=end;
	return (0);
}

static void report_conflict_destroy(struct report_conflict_info *p)
{
	struct report_conflict_info *q;

	while ((q=p) != 0)
	{
		p=q->next;
		free(q->conflict_addr);
		free(q->conflict_eventid);
		free(q);
	}
}

static void rebook(struct PCP *, void *);

static void rebook_installtimeout(struct PCP *pcp)
{
	rebook_timeout.handler=rebook;
	pcpdtimer_install(&rebook_timeout, 15 * 60);
}

static void rebook(struct PCP *pcp, void *vp)
{
	struct proxy_list *p;

	pcp_noop(pcp);

	for (p=proxy_list; p; p=p->next)
		pcp_noop(p->proxy);
	rebook_installtimeout(pcp);
}

static void dobook(struct PCP *pcp)
{
	struct book_time_list *list, **last;
	unsigned n=0;
	const char *p;
	int rc=0;
	struct PCP_event_time *new_times=NULL;
	struct report_conflict_info *conflict_list;

	list=NULL;
	last= &list;

	while ((p=strtok(NULL, " ")) != NULL)
	{
		char from_s[14+1];
		char to_s[14+1];

		if (strlen(p) != 14 + 14 + 1 || p[14] != '-')
		{
			printf("500 Syntax error.\n");
			rc= -1;
			break;
		}

		memcpy(from_s, p, 14);
		memcpy(to_s, p+15, 14);
		from_s[14]=0;
		to_s[14]=0;

		if ( (*last=malloc(sizeof(**last))) == NULL)
		{
			printf("500 %s\n", strerror(errno));
			rc= -1;
			break;
		}

		(*last)->next=NULL;
		if (((*last)->times.start=pcp_gmtime_s(from_s)) == 0 ||
		    ((*last)->times.end=pcp_gmtime_s(to_s)) == 0)
		{
			printf("500 Invalid date/time\n");
			rc= -1;
			break;
		}

		last=&(*last)->next;
		++n;
	}

	if (rc == 0 && n == 0)
	{
		printf("500 Syntax error\n");
		rc= -1;
	}

	if (rc == 0 && (new_times=calloc(n, sizeof(struct PCP_event_time)))
	    == NULL)
	{
		printf("500 %s\n", strerror(errno));
		rc= -1;
	}

	if (rc == 0)
	{
		struct book_time_list *l;
		const struct PCP_event_time *save_times;
		unsigned n_save_times;
		struct proxy_list *pr;
		int is_conflict;
		struct extra_conflict_info eci;

		n=0;
		for (l=list; l; l=l->next)
			new_times[n++]= l->times;

		save_times=new_commit.event_times;
		n_save_times=new_commit.n_event_times;

		new_commit.event_times=new_times;
		new_commit.n_event_times=n;

		eci.conflict_list= &conflict_list;

		conflict_list=NULL;
		new_commit.add_conflict_callback=do_report_conflict;
		new_commit.add_conflict_callback_ptr= &eci;
		new_commit.flags=
			(conflict_flag ? PCP_OK_CONFLICT:0) |
			(force_flag ? PCP_OK_PROXY_ERRORS:0);

		notbooked=1;
		is_conflict=0;

		for (pr=proxy_list; pr; pr=pr->next)
		{
			eci.proxy_addr=pr->userid;

			if (pr->flags & PROXY_NEW)
				if (pcp_book(pr->proxy,
					     pr->newevent, &new_commit))
					is_conflict=1;
		}

		if (proxy_userid)
			new_commit.flags |= PCP_BYPROXY;

		eci.proxy_addr=NULL;
		if (pcp_book(pcp, new_eventid, &new_commit))
			is_conflict=1;

		if (is_conflict)
		{
			new_commit.event_times=save_times;
			new_commit.n_event_times=n_save_times;
			free(new_times);

			if (conflict_list)
			{
				struct report_conflict_info *p;

				for (p=conflict_list; p; p=p->next)
				{
					char from_buf[15];
					char to_buf[15];

					pcp_gmtimestr(p->conflict_start,
						      from_buf);
					pcp_gmtimestr(p->conflict_end, to_buf);

					printf("403%c%s %s %s %s conflicts.\n",
					       p->next ? '-':' ',
					       p->conflict_addr,
					       from_buf,
					       to_buf,
					       p->conflict_eventid);
				}
			}
			else
			{
				error(new_commit.errcode);
			}
			report_conflict_destroy(conflict_list);
		}
		else
		{
			if (new_commit_times)
				free(new_commit_times);
			new_commit_times=new_times;
			printf("200 Ok\n");
			notbooked=0;
		}
		rebook_installtimeout(pcp);
	}

	while (list)
	{
		struct book_time_list *l=list;

		list=l->next;
		free(l);
	}

}

/* ------- LIST ------- */

struct list_struct {
	struct PCP_list_all list_info;
	struct list_item *event_list, **last_event;
} ;

struct list_item {
	struct list_item *next;
	char *event_id;
	time_t start;
	time_t end;
} ;

static int list_callback(struct PCP_list_all *p, void *vp)
{
	struct list_struct *ls=(struct list_struct *)vp;
	char *s=strdup(p->event_id);

	if (!s)
		return (-1);

	if ( ((*ls->last_event)=(struct list_item *)
	      malloc(sizeof(struct list_item))) == NULL)
	{
		free(s);
		return (-1);
	}

	(*ls->last_event)->event_id=s;
	(*ls->last_event)->start=p->event_from;
	(*ls->last_event)->end=p->event_to;
	(*ls->last_event)->next=NULL;

	ls->last_event= & (*ls->last_event)->next;
	return (0);
}
	
static int list(struct PCP *pcp)
{
	struct list_struct ls;
	const char *q;
	struct list_item *e;

	memset(&ls, 0, sizeof(ls));
	ls.list_info.callback_arg= &ls;
	ls.list_info.callback_func= &list_callback;
	ls.last_event= &ls.event_list;

	while ((q=strtok(NULL, " ")) != NULL)
	{
		if (strcasecmp(q, "FROM") == 0)
		{
			char buf[15];

			q=strtok(NULL, " ");
			if (!q)
				return (-1);
			if (ls.list_info.list_from ||
			    ls.list_info.list_to)
				return (-1);

			if (*q != '-')
			{
				if (strlen(q) < 14)
					return (-1);
				memcpy(buf, q, 14);
				buf[14]=0;
				q += 14;
				if ((ls.list_info.list_from
				     =pcp_gmtime_s(buf)) == 0)
					return (-1);
			}
			if (*q)
			{
				if (*q++ != '-' || strlen(q) != 14)
					return (-1);
				memcpy(buf, q, 14);
				buf[14]=0;
				q += 14;
				if ((ls.list_info.list_to
				     =pcp_gmtime_s(buf)) == 0)
					return (-1);
			}
		}
	}

	if (pcp_list_all(pcp, &ls.list_info))
	{
		error(0);
	}
	else
	{
		for (e=ls.event_list; e; e=e->next)
		{
			char from_buf[15], to_buf[15];

			pcp_gmtimestr(e->start, from_buf);
			pcp_gmtimestr(e->end, to_buf);

			printf("105%c%s %s %s event found.\n",
			       e->next ? '-':' ',
			       e->event_id,
			       from_buf,
			       to_buf);
		}

		if (ls.event_list == NULL)
			printf("504 event-id not found.\n");
	}

	while ((e=ls.event_list) != NULL)
	{
		ls.event_list=e->next;
		free(e->event_id);
		free(e);
	}
	return (0);
}

/* ------ RETR ------------ */

struct retrinfo {
	int status;
	struct retrinfo_event_list *event_list;
	const char **event_list_array;
	int text_flag;
	int text_seen_eol;
} ;

struct retrinfo_event_list {
	struct retrinfo_event_list *next;
	char *event_id;
} ;

static int callback_retr_date(struct PCP_retr *r,
			      time_t from, time_t to, void *vp)
{
	char from_buf[15], to_buf[15];

	pcp_gmtimestr(from, from_buf);
	pcp_gmtimestr(to, to_buf);

	printf("105 %s %s %s event found\n",
	       r->event_id, from_buf, to_buf);
	return (0);
}

static int callback_retr_participants(struct PCP_retr *r,
				      const char *n, const char *id,
				      void *vp)
{
	printf("106 %s %s is a participant\n", r->event_id, n);
	return (0);
}

static int callback_retr_status(struct PCP_retr *r,
				int status, void *vp)
{
	char status_buf[256];
	const char *comma="";

	status_buf[0]=0;

	if (status & LIST_CANCELLED)
	{
		strcat(strcat(status_buf, comma), "CANCELLED");
		comma=",";
	}

	if (status & LIST_BOOKED)
	{
		strcat(strcat(status_buf, comma), "BOOKED");
		comma=",";
	}

	if (status & LIST_PROXY)
	{
		strcat(strcat(status_buf, comma), "PROXY");
		comma=",";
	}
	if (status_buf[0])
		printf("110 %s %s\n", r->event_id, status_buf);
	return (0);
}

static int callback_retr_begin(struct PCP_retr *r, void *vp)
{
	struct retrinfo *ri=(struct retrinfo *)vp;

	ri->text_flag=0;
	ri->text_seen_eol=1;

	return (0);
}

static int callback_retr_headers(struct PCP_retr *r,
				 const char *h,
				 const char *v,
				 void *vp)
{
	struct retrinfo *ri=(struct retrinfo *)vp;
	int lastchar;

	if (!ri->text_flag)
	{
		ri->text_flag=1;
		printf("107 %s follows\n", r->event_id);
	}

	if (*h == '.')
		putchar('.');
	printf("%s: ", h);

	while (*v && isspace((int)(unsigned char)*v))
		++v;

	lastchar=' ';

	while (*v)
	{
		if ((int)(unsigned char)*v >= ' ' ||
		    *v == '\n' || *v == '\t')
		{
			putchar(*v);
			lastchar=*v;
		}
		++v;
	}
	if (lastchar != '\n')
		putchar('\n');
	return (0);
}

static int callback_retr_message(struct PCP_retr *r,
				 const char *ptr,
				 int l,
				 void *vp)
{
	struct retrinfo *ri=(struct retrinfo *)vp;

	if (!ri->text_flag)
	{
		ri->text_flag=1;
		printf("107 %s follows\n", r->event_id);
	}

	while (l)
	{
		if (ri->text_seen_eol && *ptr == '.')
			putchar('.');
		ri->text_seen_eol= *ptr == '\n';
		putchar(*ptr);
		++ptr;
		--l;
	}
	return (0);
}

static int callback_retr_end(struct PCP_retr *r, void *vp)
{
	struct retrinfo *ri=(struct retrinfo *)vp;

	if (ri->text_flag)
	{
		if (!ri->text_seen_eol)
			putchar('\n');
		printf(".\n");
	}
	return (0);
}

static int retr(struct PCP *pcp)
{
	struct PCP_retr r;
	struct retrinfo ri;
	const char *q;
	int n;
	struct retrinfo_event_list *p;

	memset(&r, 0, sizeof(r));
	memset(&ri, 0, sizeof(ri));

	r.callback_arg= &ri;

	for (;;)
	{
		if ((q=strtok(NULL, " ")) == NULL)
			return (-1);

		if (strcasecmp(q, "EVENTS") == 0)
			break;

		if (strcasecmp(q, "TEXT") == 0)
		{
			r.callback_begin_func=callback_retr_begin;
			r.callback_rfc822_func=callback_retr_message;
			r.callback_end_func=callback_retr_end;
			continue;
		}

		if (strcasecmp(q, "HEADERS") == 0)
		{
			r.callback_begin_func=callback_retr_begin;
			r.callback_headers_func=callback_retr_headers;
			r.callback_end_func=callback_retr_end;
			continue;
		}

		if (strcasecmp(q, "DATE") == 0)
		{
			r.callback_retr_date=callback_retr_date;
			continue;
		}

		if (strcasecmp(q, "ADDR") == 0)
		{
			r.callback_retr_participants=
				callback_retr_participants;
			continue;
		}

		if (strcasecmp(q, "STATUS") == 0)
		{
			r.callback_retr_status=callback_retr_status;
			continue;
		}
		return (-1);
	}

	if (r.callback_headers_func && r.callback_rfc822_func)
		return (-1);

	n=0;
	while ((q=strtok(NULL, " ")) != NULL)
	{
		char *s=strdup(q);

		if (!s || (p=malloc(sizeof(struct retrinfo_event_list)))
		    == NULL)
		{
			perror("malloc");
			exit(1);
		}
		p->event_id=s;
		p->next=ri.event_list;
		ri.event_list=p;
		++n;
	}

	if (ri.event_list == NULL)
		return (-1);

	if ((ri.event_list_array=malloc((n+1) * sizeof(const char *)))
	    == NULL)
	{
		perror("malloc");
		exit(1);
	}

	n=0;
	for (p=ri.event_list; p; p=p->next)
		ri.event_list_array[n++]=p->event_id;
	ri.event_list_array[n]=0;

	r.event_id_list=ri.event_list_array;

	if (pcp_retr(pcp, &r))
		error(r.errcode);
	else
		printf("108 RETR complete\n");

	while ((p=ri.event_list) != 0)
	{
		ri.event_list=p->next;
		free(p->event_id);
		free(p);
	}
	free(ri.event_list_array);
	return (0);
}

/* ---- UNCANCEL ---- */

struct uncancel_list {
	struct uncancel_list *next;
	char *id;
	time_t from, to;
	char *addr;
} ;

static int uncancel_callback(const char *event, time_t from, time_t to,
			     const char *addr, void *vp)
{
	struct uncancel_list ***tail_ptr=(struct uncancel_list ***)vp;
	struct uncancel_list **tail= *tail_ptr;
	char *s=strdup(event);
	char *a=strdup(addr);
	struct uncancel_list *newptr;

	if (!s || !a || (newptr=(struct uncancel_list *)
			 malloc(sizeof(struct uncancel_list))) == NULL)
	{
		if (a) free(a);
		if (s) free(s);
		return (-1);
	}

	newptr->addr=a;
	newptr->id=s;
	newptr->from=from;
	newptr->to=to;

	*tail=newptr;
	newptr->next=0;

	*tail_ptr= &(*tail)->next;
	return (0);
}

/* --------- ACL LIST ---------- */

struct acl_list {
	struct acl_list *next;
	char *who;
	int flags;
} ;

static int list_acl_callback(const char *who, int flags, void *dummy)
{
	struct acl_list **p=(struct acl_list **)dummy;
	struct acl_list *q=malloc(sizeof(struct acl_list));

	if (!q)
		return (-1);
	if ((q->who=strdup(who)) == NULL)
	{
		free(q);
		return (-1);
	}
	q->flags=flags;
	q->next= *p;
	*p=q;
	return (0);
}

static void listacls(struct PCP *pcp)
{
	char buf[1024];
	struct acl_list *list=NULL;
	struct acl_list *p;

	if (pcp_list_acl(pcp, list_acl_callback, &list) == 0)
	{
		if (list == NULL)
			printf("203 Empty ACL\n");
		else
		{
			for (p=list; p; p=p->next)
			{
				buf[0]=0;
				pcp_acl_name(p->flags, buf);
				printf("103%c%s %s\n",
				       p->next ? '-':' ',
				       p->who, buf);
			}
		}
	}
	else error(0);

	while ((p=list) != NULL)
	{
		list=p->next;
		free(p->who);
		free(p);
	}
}

static int open_event_participant(struct PCP_retr *r,
				  const char *n, const char *id,
				  void *vp)
{
	struct proxy_list *p;
	char *errmsg;

	if (proxy_userid)
	{
		printf("500-Cannot create proxy in proxy mode.\n");
		errno=EIO;
		return (-1);
	}

	p=proxy(n, &errmsg);

	if (p)
	{
		if ((p->old_event_id=strdup(id)) == NULL)
			return (-1);
		return (0);
	}

	if (!force_flag)
	{
		if (errmsg)
		{
			proxy_error(n, errmsg);
			free(errmsg);
		}
		errno=EIO;
		return (-1);
	}
	if (errmsg)
		free(errmsg);

	p->flags |= PROXY_IGNORE;
	return (0);
}

/* ------------------------ */

static int check_acl(int, int);

static int doline(struct PCP *pcp, char *p, int acl_flags)
{
	char *q=strtok(p, " ");

	if (!q)
	{
		printf("500 Syntax error\n");
		return (0);
	}

	if (strcasecmp(q, "QUIT") == 0)
	{
		printf("200 Bye.\n");
		return (-1);
	}

	if (strcasecmp(q, "NOOP") == 0)
	{
		printf("200 Ok.\n");
		return (0);
	}

	if (strcasecmp(q, "CAPABILITY") == 0)
	{
		printf("100-ACL\n");
		printf("100 PCP1\n");
		return (0);
	}

	if (strcasecmp(q, "LIST") == 0)
	{
		if (check_acl(acl_flags, PCP_ACL_LIST))
			return (0);

		if (list(pcp))
			printf("500 Syntax error\n");
		return (0);
	}

	if (strcasecmp(q, "RETR") == 0)
	{
		if (check_acl(acl_flags, PCP_ACL_RETR))
			return (0);

		if (retr(pcp))
			printf("500 Syntax error\n");
		return (0);
	}


	if (strcasecmp(q, "ACL") == 0 && pcp_has_acl(pcp) && !proxy_userid)
	{
		q=strtok(NULL, " ");
		if (q && strcasecmp(q, "SET") == 0)
		{
			const char *who=strtok(NULL, " ");

			if (who)
			{
				int flags=0;

				while ((q=strtok(NULL, " ")) != 0)
					flags |= pcp_acl_num(q);

				if (pcp_acl(pcp, who, flags))
				{
					error(0);
					return (0);
				}
				printf("200 Ok\n");
				return (0);
			}
		}
		else if (q && strcasecmp(q, "LIST") == 0)
		{
			listacls(pcp);
			return (0);
		}
	}

	if (strcasecmp(q, "RSET") == 0)
	{
		conflict_flag=0;
		force_flag=0;
		need_rset=0;
		rset(pcp);
		printf("200 Ok.\n");	
		return (0);
	}

	if (need_rset)
	{
		printf("500 RSET required - calendar in an unknown state.\n");
		return (0);
	}

	if (strcasecmp(q, "DELETE") == 0)
	{
		struct PCP_retr r;
		const char *event_id_list[2];

		char *e=strtok(NULL, " ");

		if (check_acl(acl_flags, PCP_ACL_MODIFY))
			return (0);

		if (e && deleted_eventid == NULL && new_eventid == NULL)
		{
			if ((deleted_eventid=strdup(e)) == NULL)
			{
				perror("strdup");
				exit(1);
			}
			proxy_list_rset();
			memset(&r, 0, sizeof(r));
			r.callback_retr_participants=open_event_participant;
			event_id_list[0]=deleted_eventid;
			event_id_list[1]=NULL;
			r.event_id_list=event_id_list;
			if (pcp_retr(pcp, &r))
			{
				error(r.errcode);
				proxy_list_rset();
			}
			else
				printf("200 Ok.\n");
			return (0);
		}
	}

	if (strcasecmp(q, "NEW") == 0 && new_eventid == NULL)
	{
		if (check_acl(acl_flags, PCP_ACL_MODIFY))
			return (0);

		new_eventid=readnewevent(pcp);

		if (new_eventid == NULL)
		{
			printf("500 %s\n", strerror(errno));
		}
		else
			printf("109 %s ready to be commited.\n",
			       new_eventid->eventid);
		return (0);
	}

	if (strcasecmp(q, "BOOK") == 0 && new_eventid)
	{
		dobook(pcp);
		return (0);
	}

	if (strcasecmp(q, "CONFLICT") == 0)
	{
		q=strtok(NULL, " ");
		if (q && strcasecmp(q, "ON") == 0)
		{
			if (check_acl(acl_flags, PCP_ACL_CONFLICT))
				return (0);

			conflict_flag=1;
		}
		else
			conflict_flag=0;
		printf("200 Ok.\n");
		return (0);
	}

	if (strcasecmp(q, "FORCE") == 0)
	{
		q=strtok(NULL, " ");
		if (q && strcasecmp(q, "ON") == 0)
		{
			force_flag=1;
		}
		else
			force_flag=0;
		printf("200 Ok.\n");
		return (0);
	}

	if (strcasecmp(q, "COMMIT") == 0)
	{
		if (notbooked)
		{
			printf("500 BOOK required.\n");
		}
		else if (new_eventid && new_commit_times)
		{
			struct proxy_list *pl;

			new_commit.add_conflict_callback=NULL;
			new_commit.add_conflict_callback_ptr=NULL;

			new_commit.flags=
				(conflict_flag ? PCP_OK_CONFLICT:0) |
				(force_flag ? PCP_OK_PROXY_ERRORS:0);

			for (pl=proxy_list; pl; pl=pl->next)
			{
				if (pl->flags & PROXY_IGNORE)
					continue;

				if (pl->flags & PROXY_NEW)
				{
					if (pcp_commit(pl->proxy,
						       pl->newevent,
						       &new_commit))
					{
						fprintf(stderr, "CRIT: "
						       "COMMIT failed for PROXY %s\n",
						       pl->userid);

						if (!force_flag)
						{
							pl->flags &=
								~PROXY_NEW;
							error(new_commit
							      .errcode);
							return (0);
						}
					}
				}
				else if (pl->old_event_id)
				{
					struct PCP_delete del;

					memset(&del, 0, sizeof(del));

					del.id=pl->old_event_id;

					if (pcp_delete(pl->proxy, &del))
					{
						fprintf(stderr, "CRIT: "
						       "DELETE failed for PROXY %s\n",
						       pl->userid);
						if (!force_flag)
						{
							error(del.errcode);
							return (0);
						}
						pl->flags |= PROXY_IGNORE;
					}
				}
			}

			if (proxy_userid)
				new_commit.flags |= PCP_BYPROXY;

			if (pcp_commit(pcp, new_eventid, &new_commit))
				error(new_commit.errcode);
			else
			{
				const char *proxy_name=NULL;
				const char *proxy_action=NULL;

				for (pl=proxy_list; pl; pl=pl->next)
				{
					if (proxy_name)
						printf("111-%s %s\n",
						       proxy_action,
						       proxy_name);

					proxy_action=
						!(pl->flags & PROXY_IGNORE)
						&& (pl->flags & PROXY_NEW)
						? "NEW":"DELETE";
					proxy_name=pl->userid;
				}

				if (proxy_name)
					printf("111 %s %s\n",
					       proxy_action,
					       proxy_name);
				else
					printf("200 Ok.\n");
			}	
			rset(pcp);
			return (0);
		}
		else if (!new_eventid && deleted_eventid)
		{
			struct proxy_list *pl;
			struct PCP_delete del;
			const char *proxy_userid;

			for (pl=proxy_list; pl; pl=pl->next)
			{
				if (pl->flags & PROXY_IGNORE)
					continue;

				if (pl->old_event_id)
				{
					memset(&del, 0, sizeof(del));
					del.id=pl->old_event_id;

					if (pcp_delete(pl->proxy, &del))
					{
						fprintf(stderr, "CRIT: "
						       "DELETE failed for PROXY %s\n",
						       pl->userid);
					}
				}
			}

			memset(&del, 0, sizeof(del));
			del.id=deleted_eventid;

			if (pcp_delete(pcp, &del))
			{
				if (del.errcode != PCP_ERR_EVENTNOTFOUND)
				{
					error(del.errcode);
					return (0);
				}
			}

			proxy_userid=NULL;
			for (pl=proxy_list; pl; pl=pl->next)
			{
				if (proxy_userid)
					printf("111-DELETE %s\n",
					       proxy_userid);

				proxy_userid=pl->userid;
			}

			if (proxy_userid)
				printf("111 DELETE %s\n", proxy_userid);
			else
				printf("200 Ok\n");

			rset(pcp);
			return (0);
		}

		printf("500 There's nothing to commit.\n");
		return (0);
	}

	if (strcasecmp(q, "CANCEL") == 0)
	{
		int errcode;

		if (check_acl(acl_flags, PCP_ACL_MODIFY))
			return (0);

		q=strtok(NULL, " ");
		if (!q)
			printf("500 Syntax error\n");
		else if (pcp_cancel(pcp, q, &errcode))
			error(errcode);
		else
			printf("200 Cancelled\n");
		return (0);
	}

	if (strcasecmp(q, "UNCANCEL") == 0)
	{
		struct PCP_uncancel unc;
		struct uncancel_list *list=NULL, **tail= &list;
		struct uncancel_list *p;

		if (check_acl(acl_flags, PCP_ACL_MODIFY))
			return (0);

		memset(&unc, 0, sizeof(unc));
		unc.uncancel_conflict_callback=uncancel_callback;
		unc.uncancel_conflict_callback_ptr=&tail;

		q=strtok(NULL, " ");
		if (!q)
			printf("500 Syntax error\n");
		else if (pcp_uncancel(pcp, q,
				      (conflict_flag ? PCP_OK_CONFLICT:0)|
				      (force_flag ? PCP_OK_PROXY_ERRORS:0),
				      &unc))
		{
			if (unc.errcode == PCP_ERR_CONFLICT && list)
			{
				for (p=list; p; p=p->next)
				{
					char from_buf[15];
					char to_buf[15];

					pcp_gmtimestr(p->from, from_buf);
					pcp_gmtimestr(p->to, to_buf);

					printf("403%c%s %s %s %s conflicts.\n",
					       p->next ? '-':' ',
					       p->addr,
					       from_buf,
					       to_buf,
					       p->id);
				}
			}
			else
				error(unc.errcode);
		}
		else
			printf("200 Uncancelled\n");

		while((p=list) != NULL)
		{
			list=p->next;
			free(p->addr);
			free(p->id);
			free(p);
		}
		return (0);
	}

	printf("500 Syntax error\n");
	return (0);
}

static int check_acl(int flags, int bit)
{
	if (flags & bit)
		return (0);

	printf("500 Permission denied.\n");
	return (1);
}

static RETSIGTYPE catch_sig(int sig_num)
{
	termsig=1;
	signal(SIGALRM, catch_sig);
	alarm(2);

#if RETSIGTYPE != void
	return (0);
#endif
}

static void setsigs()
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler=catch_sig;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
}

struct get_acl {
	const char *who;
	int flags;
};

static int get_acl_callback(const char *w, int f, void *dummy)
{
	struct get_acl *ga=(struct get_acl *)dummy;

	if (addrcmp(w, ga->who) == 0)
		ga->flags=f;
	return (0);
}

static void mainloop(struct PCP *pcp)
{
	int c;
	struct pcpdtimer inactivity_timeout;
	int my_acl_flags=PCP_ACL_MODIFY|PCP_ACL_CONFLICT|
		PCP_ACL_LIST|PCP_ACL_RETR;

	deleted_eventid=NULL;
	memset(&new_commit, 0, sizeof(new_commit));
	new_commit_times=NULL;
	new_commit_participants=NULL;
	new_commit_participants_buf=NULL;

	termsig=0;

	setsigs();

	inp_ptr=0;
	inp_left=0;
	need_rset=0;

	time(&prev_time);

	pcpdtimer_init(&inactivity_timeout);
	inactivity_timeout.handler=inactive;

	if (proxy_userid)
	{
		struct get_acl ga;

		ga.who=proxy_userid;
		ga.flags=0;
		if (pcp_has_acl(pcp))
		{
			if (pcp_list_acl(pcp, get_acl_callback, &ga) == 0)
			{
				if (ga.flags == 0)
				{
					ga.who="public";
					if (pcp_list_acl(pcp, get_acl_callback,
							 &ga))
						ga.flags=0;
				}
			}
			else ga.flags=0;
		}
		my_acl_flags=ga.flags;
	}

	do
	{
		char *p;

		input_line_len=0;
		pcpdtimer_install(&inactivity_timeout, 30 * 60);

		for (;;)
		{


			c=inputchar(pcp);

			if (termsig || c == '\n')
				break;
			input_buffer[input_line_len++]=c;
		}
		if (termsig)
			break;

		input_buffer[input_line_len]=0;

		for (p=input_buffer; *p; p++)
			if (isspace((int)(unsigned char)*p))
				*p=' ';
		pcpdtimer_triggered(&inactivity_timeout);
		/* Cancel inactivity_timeout for the duration of the command */

	} while (doline(pcp, input_buffer, my_acl_flags) == 0 && !termsig);
	alarm(0);
	free(input_buffer);
	rset(pcp);
}

/*
** Start listening on a socket for connections
*/

static void accept_pcpd(int, int, int, int);

static void start()
{
	int pubsock;
	int privsock;

	if ((pubsock=pcp_mksocket(PUBDIR, "PCPDLOCAL")) < 0)
	{
		exit(1);
	}

	if ((privsock=pcp_mksocket(PRIVDIR, "PCPDLOCAL")) < 0)
	{
		exit (1);
	}

#if USE_NOCLDWAIT
	{
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler=SIG_IGN;
		sa.sa_flags=SA_NOCLDWAIT;
		sigaction(SIGCHLD, &sa, NULL);
	}
#else
	signal(SIGCHLD, SIG_IGN);
#endif

	for (;;)
	{
		fd_set fds;
		struct timeval tv;
		int rc;

		FD_ZERO(&fds);
		FD_SET(pubsock, &fds);
		FD_SET(privsock, &fds);

		tv.tv_sec=authtoken_check();
		tv.tv_usec=0;

		if ((rc=select ( (pubsock > privsock ? pubsock:privsock)+1,
				 &fds, NULL, NULL, &tv)) < 0)
		{
			perror("pcpd: select");
			continue;
		}

		if (rc == 0)
			continue;

		if (FD_ISSET(pubsock, &fds))
			accept_pcpd(pubsock, pubsock, privsock, 0);

		if (FD_ISSET(privsock, &fds))
			accept_pcpd(privsock, pubsock, privsock, 1);
	}
}

struct userid_info {
	char *userid;
	int isproxy;
} ;

static int callback_userid(struct userid_callback *a, void *vp)
{
	struct userid_info *uinfo=(struct userid_info *)vp;
	char *u=strdup(a->userid);
	struct stat stat_buf;

	if (!u)
		return (-1);

	if (stat(a->homedir, &stat_buf) < 0)
	{
		free(u);
		return (-1);
	}

	if (stat_buf.st_mode & S_ISVTX)
	{
		free(u);
		errno=EAGAIN;
		return (1);
	}

	uinfo->userid=u;
	return (0);
}

static int callback_login(struct userid_callback *a, void *vp)
{
	struct userid_info *uinfo=(struct userid_info *)vp;
	struct stat stat_buf;
	time_t t;
	char curdir[1024];
	char *token=NULL;

	if (stat(a->homedir, &stat_buf) < 0)
		return (-1);

	if (stat_buf.st_mode & S_ISVTX)
	{
		errno=EAGAIN;
		return (1);
	}

	time(&t);
	if (!uinfo->isproxy)
	{
		token=authtoken_create(uinfo->userid, t);
		if (!token)
		{
			fprintf(stderr, "NOTICE: authtoken_create() failed.\n");
			maildir_cache_cancel();
			return (1);
		}
	}

	maildir_cache_start();

	libmail_changeuidgid(a->uid, getgid());

	if (chdir(a->homedir) < 0)
	{
		free(token);
		fprintf(stderr, "NOTICE: chdir(%s) failed: %s\n", a->homedir,
			strerror(errno));
		maildir_cache_cancel();
		exit(1);
	}

	if (chdir(a->maildir && *a->maildir ? a->maildir:"Maildir") < 0)
	{
		free(token);
		fprintf(stderr, "NOTICE: chdir(Maildir) failed: %s\n",
			strerror(errno));
		maildir_cache_cancel();
		exit(1);
	}

	mkdir("calendar", 0700);
	if (chdir("calendar") < 0)
	{
		free(token);
		fprintf(stderr, "NOTICE: chdir(calendar) failed: %s\n",
			strerror(errno));
		maildir_cache_cancel();
		exit(1);
	}

	curdir[sizeof(curdir)-1]=0;
	if (getcwd(curdir, sizeof(curdir)-1) == 0)
	{
		fprintf(stderr, "NOTICE: getcwd() failed: %s\n",
			strerror(errno));
		maildir_cache_cancel();
		exit(1);
	}

	maildir_cache_save(uinfo->userid, t, curdir, a->uid, getgid());

	alarm(0);
	if (!uinfo->isproxy)
	{
		printf("102 %s logged in.\n", token);
		free(token);
	}
	return (0);
}

static char *login(int, int *);

static int accept_sock(int sock)
{
	struct sockaddr_un saddr;
	socklen_t saddr_len;

	saddr_len=sizeof(saddr);

	return (accept(sock, (struct sockaddr *)&saddr, &saddr_len));
}

static void accept_pcpd(int sock, int pubsock, int privsock, int flag)
{
	int fd;
	pid_t pid;
	struct PCP *pcp;

	if ((fd=accept_sock(sock)) < 0)
		return;

	if (fcntl(fd, F_SETFL, 0) < 0)
	{
		fprintf(stderr, "CRIT: fcntl() failed: %s\n", strerror(errno));
		close(fd);
		return;
	}

	maildir_cache_purge();
	pid=fork();

	if (pid < 0)
	{
		fprintf(stderr, "CRIT: fork() failed: %s\n", strerror(errno));
		close(fd);
		return;
	}

	if (pid)
	{
		close(fd);
		return;	/* Parent resumes listening */
	}

	/* child */
	signal(SIGCHLD, SIG_DFL);

	close(pubsock);
	close(privsock);

	close(0);
	if (dup(fd) != 0)
		exit(0);
	close(1);
	if (dup(fd) != 1)
		exit(0);
	close(fd);
	userid=login(flag, &flag);

	pcp=pcp_open_dir(".", userid);

	if (pcp && flag && pcp_cleanup(pcp))
	{
		pcp_close(pcp);
		fprintf(stderr, "CRIT: pcp_cleanup failed\n");
		pcp=NULL;
	}

	if (!pcp)
	{
		fprintf(stderr, "CRIT: pcp_open_dir failed\n");
		perror("pcp_open_dir");
		exit(1);
	}

	mainloop(pcp);
	exit(0);
}

struct relogin_struct {
	time_t when;
	int needauthtoken;
	const char *userid;
} ;

static int callback_cache_search(uid_t u, gid_t g, const char *dir, void *vp)
{
	struct relogin_struct *rs=(struct relogin_struct *)vp;
	time_t login_time, now;
	int reset_flag;
	char *token=NULL;

	login_time=rs->when;
	time(&now);

	reset_flag= login_time <= now - TIMEOUT;

	if (reset_flag)
	{
		if (rs->needauthtoken)
		{
			token=authtoken_create(rs->userid, now);
			if (!token)
			{
				fprintf(stderr,
				       "ALERT: authtoken_create() failed.\n");
				return (1);
			}
		}
		maildir_cache_start();
	}

	libmail_changeuidgid(u, g);

	if (chdir(dir) < 0)
	{
		maildir_cache_cancel();
		fprintf(stderr, "NOTICE: chdir(%s) failed: %s\n", dir, strerror(errno));
		return (-1);
	}

	alarm(0);
	if (reset_flag)
	{
		maildir_cache_save(rs->userid, now, dir, u, g);
		if (rs->needauthtoken)
		{
			printf("102 %s logged in.\n", token);
			free(token);
		}
	}
	else if (rs->needauthtoken)	/* Not a proxy connection */
		printf("200 Ok\n");
	return (0);
}

static char *login(int isprivate,
		   int *flag   /* Cleanup requested */
		   )
{
	struct userid_info uinfo;

	proxy_userid=NULL;
	*flag=0;
	memset(&uinfo, 0, sizeof(uinfo));
	alarm(300);	/* Better log in in five minutes */
	for (;;)
	{
		int c;
		char *p;

		input_line_len=0;
		for (;;)
		{
			c=inputchar(NULL);
			if (c == EOF)
				exit(0);

			if (c == '\n')
				break;
			input_buffer[input_line_len]=c;
			if (input_line_len < 1024)
				++input_line_len;
		}
		input_buffer[input_line_len]=0;

		for (p=input_buffer; *p &&
			     isspace((int)(unsigned char)*p); p++)
			;

		if (strncasecmp(p, "PASSWORD", 8) == 0 && !isprivate &&
		    isspace((int)(unsigned char)p[8]) && uinfo.userid)
		{
			for (p += 9; isspace((int)(unsigned char)*p); p++)
				;

			if (*p)
			{
				int rc;
				char *q, *r;

				for (q=r=p; *q; q++)
					if (!isspace((int)(unsigned char)*q))
						r=q+1;
				*r=0;

				rc=authpcp_login(uinfo.userid, p,
						 callback_login, &uinfo);

				if (rc)
				{
					printf("%s %s\n",
					       rc < 0 ? "501":"401",
					       strerror(errno));
					continue;
				}
				*flag=1;
				break;
			}
		}

		for (p=input_buffer; *p; p++)
			if (isspace((int)(unsigned char)*p))
				*p=' ';

		p=strtok(input_buffer, " ");

		if (p && strcasecmp(p, "CAPABILITY") == 0)
		{
			printf("100 PCP1\n");
			continue;
		}
		else if (p && strcasecmp(p, "USERID") == 0 &&
		    uinfo.userid == NULL)
		{
			if ((p=strtok(NULL, " ")) != NULL)
			{
				int rc= authpcp_userid(p, callback_userid,
						       &uinfo);

				if (rc)
				{
					printf("%s %s\n",
					       rc < 0 ? "501":"401",
					       strerror(errno));
					continue;
				}

				printf("301 Ok, waiting for password.\n");
				continue;
			}
		}
		else if (p && strcasecmp(p, "PROXY") == 0 && uinfo.userid &&
			 isprivate)
		{
			if ((p=strtok(NULL, " ")) != 0)
			{
				struct relogin_struct rs;
				time_t now;
				int rc;

				if (proxy_userid)
					free(proxy_userid);
				if ((proxy_userid=auth_choplocalhost(p))
				    == NULL)
				{
					printf("400 %s\n", strerror(errno));
					continue;
				}

				rs.needauthtoken=0;
				rs.userid=uinfo.userid;

				time(&now);

				rc=maildir_cache_search(uinfo.userid, now,
							callback_cache_search,
							&rs);
				if (rc == 0)
				{
					alarm(0);
					printf("200 PROXY ok\n");
					break;
				}
				now -= TIMEOUT;

				rc=maildir_cache_search(uinfo.userid, now,
							callback_cache_search,
							&rs);
				if (rc == 0)
				{
					alarm(0);
					printf("200 PROXY ok\n");
					break;
				}

				uinfo.isproxy=1;
				rc=authpcp_userid(uinfo.userid, callback_login,
						  &uinfo);
				if (rc)
				{
					fprintf(stderr,
					       "CRIT: auth_userid() failed\n");
					exit(1);
				}
				alarm(0);
				printf("200 PROXY ok\n");
				break;
			}

		}
		else if (p && strcasecmp(p, "RELOGIN") == 0 && uinfo.userid &&
			 !isprivate)
		{
			if ((p=strtok(NULL, " ")) != 0)
			{
				struct relogin_struct rs;
				int rc;

				rs.needauthtoken=1;
				rs.userid=uinfo.userid;
				if (authtoken_verify(uinfo.userid, p,
						     &rs.when))
				{
					printf("500 Invalid authentication token.\n");
					continue;
				}

				rc=maildir_cache_search(uinfo.userid, rs.when,
							callback_cache_search,
							&rs);
				if (rc == 0)
					break;

				/*
				** Couldn't find anything in the login cache.
				** call the userid function with the login
				** callback.
				** This'll initialize lotsa other stuff, but
				** we don't care.
				*/

				rc=authpcp_userid(uinfo.userid,
						  callback_login,
						  &uinfo);

				if (rc)
				{
					fprintf(stderr,
					       "NOTICE: auth_userid() failed.\n");
					printf("400 Internal failure - try again later.\n");
					continue;
				}
				break;
			}
		}
		else if (p && strcasecmp(p, "QUIT") == 0)
		{
			printf("200 Ok\n");
			exit (0);
		}
		printf("500 Syntax error\n");
	}
	return (uinfo.userid);
}

int main(int argc, char **argv)
{
	int argn=1;
	static const char * const authvars[]={NULL};

	signal(SIGPIPE, SIG_IGN);
	umask(022);

	if (argn >= argc)
	{
		struct PCP *pcp;

		pcp=open_calendar(NULL);

		mainloop(pcp);
		exit(0);
	}

	maildir_cache_init(TIMEOUT * 2, CACHEDIR, LOCALCACHEOWNER, authvars);

	if (strcmp(argv[argn], "server") == 0)
	{
		struct group *gr;

		if (chdir(CALENDARDIR) < 0)
		{
			perror(CALENDARDIR);
			exit(1);
		}
		gr=getgrnam(MAILGROUP);

		if (!gr)
		{
			fprintf(stderr, "Unknown group: %s\n", MAILGROUP);
			exit(1);
		}

		authtoken_init();
		libmail_changeuidgid(getuid(), gr->gr_gid);
		start();
	}
	else if (strcmp(argv[argn], "login") == 0 ||
		 strcmp(argv[argn], "slogin") == 0)
	{
		struct PCP *pcp;
		int flag;
		struct group *gr;

		gr=getgrnam(MAILGROUP);

		if (!gr)
		{
			fprintf(stderr, "Unknown group: %s\n", MAILGROUP);
			exit(1);
		}
		libmail_changeuidgid(getuid(), gr->gr_gid);

		if (chdir(CALENDARDIR) < 0)
		{
			perror(CALENDARDIR);
			exit(1);
		}

		authtoken_init();
		userid=login(strcmp(argv[argn], "login"), &flag);

		pcp=pcp_open_dir(".", userid);

		if (pcp && flag && pcp_cleanup(pcp))
		{
			pcp_close(pcp);
			fprintf(stderr, "CRIT: pcp_cleanup failed\n");
			pcp=NULL;
		}

		if (!pcp)
		{
			fprintf(stderr, "CRIT: pcp_open_dir failed\n");
			perror("pcp_open_dir");
			exit(1);
		}

		mainloop(pcp);
		exit(0);
	}
	else if (strcmp(argv[argn], "open") == 0)
	{
		++argn;
		if (argn < argc)
		{
			struct PCP *pcp;

			pcp=open_calendar(argv[argn]);

			mainloop(pcp);
			exit(0);
		}
	}
	fprintf(stderr, "Usage: %s (server|open [path])\n", argv[0]);
	exit(0); /* exit(1) breaks Courier rpm %preun script */
	return (0);
}
