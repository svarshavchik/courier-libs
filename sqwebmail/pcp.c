/*
** Copyright 2001-2010 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"pcp.h"
#include	"pref.h"
#include	"htmllibdir.h"
#include	"sqconfig.h"
#include	"auth.h"
#include	"addressbook.h"
#include	"pcp/pcp.h"
#include	"cgi/cgi.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc822hdr.h"
#include	"rfc822/rfc2047.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirgetquota.h"
#include	"numlib/numlib.h"
#include	"maildir.h"
#include	"newmsg.h"
#include	"pref.h"
#include	"courierauth.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<ctype.h>
#include	<unistd.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif

#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#define		CACHE	"calendar.cache"
#define		TOKEN	"calendar.authtoken"

#define		CHANGEDFILE "calendar/changed"	/* Hack */

#include	"strftime.h"

extern FILE *open_langform(const char *lang, const char *formname,
			   int print_header);

extern const char *myhostname();
extern void output_attrencoded_oknl_fp(const char *, FILE *);
extern void output_scriptptrget();
extern void output_attrencoded(const char *);
extern void print_safe(const char *);
extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_content_language;
extern void output_form(const char *);
extern void newmsg_preview(const char *);
extern void output_urlencoded(const char *);
extern void attachments_head(const char *, const char *, const char *);
extern char *newmsg_createsentmsg(const char *, int *);
extern const char *sqwebmail_mailboxid;
extern char *scriptptrget();
extern void attach_delete(const char *);
extern int attach_upload(const char *,
			 const char *,
			 const char *);

extern void newmsg_showfp(FILE *, int *);

static struct PCP *calendar=NULL;
static void refreshcache(struct PCP *);
extern size_t get_timeoutsoft();


/*
** CGI process startup
*/

void sqpcp_init()
{
	calendar=NULL;
}

/*
** CGI process shutdown
*/

void sqpcp_close()
{
	if (calendar)
	{
		pcp_close(calendar);
		calendar=NULL;
	}
}

static int checked_calendar_mode=0;
static char calendar_mode[24];

static const char *sqpcp_mode()
{
	if (!checked_calendar_mode)
	{
		FILE *f;
		char *p;

		calendar_mode[0]=0;

		f=fopen(CALENDARMODE, "r");

		if (f)
		{
			if (fgets(calendar_mode, sizeof(calendar_mode),
				  f) == NULL)
			{
				calendar_mode[0]=0;
			}
			fclose(f);
		}

		if ((p=strchr(calendar_mode, '\n')) != 0)
			*p=0;
		checked_calendar_mode=1;
	}

	return (calendar_mode[0] ? calendar_mode:NULL);
}

/*
** Log in to calendar.
*/

static void do_pcplogin(const char *userid, const char *password, int showerr);

void sqpcp_login(const char *userid, const char *password)
{
	do_pcplogin(userid, password, 0);
}

static void do_pcplogin(const char *userid, const char *password, int showerr)
{
	struct PCP *pcp;

	unlink(TOKEN);
	unlink(CACHE);	/* Rebuild it later*/

	if (!sqpcp_mode())
		return;

	mkdir("calendar", 0700);

	if (sqpcp_has_groupware())	/* Groupware mode, login to server */
	{
		char *errflag=0;

		pcp=pcp_open_server(userid, password, &errflag);

		if (!pcp && errflag && showerr)
		{
			printf("%s<div class=\"indent\"><pre class=\"small-error\">",
			       getarg("CALENDARLOGINERR"));
			output_attrencoded_oknl_fp(errflag, stdout);
			printf("</pre></div>\n");
		}
		if (errflag)
			free(errflag);
	}
	else
	{
		pcp=sqpcp_calendar();
	}

	if (pcp)
	{
		const char *p=pcp_authtoken(pcp);

		if (p)
		{
			FILE *f;
			int u=umask(077);

			f=fopen(TOKEN, "w");
			umask(u);
			if (f)
			{
				fprintf(f, "%s\n", p);
				fclose(f);
			}
		}
		pcp_cleanup(pcp);
		refreshcache(pcp);
	}
}

/*
** Format of the CACHE file:
**
**  start<TAB>end<TAB>eventid<TAB>flags<TAB>subject
*/

struct cacherecord {
	time_t start, end;
	char *eventid;
	char *flags;
	char *subject;
} ;

int sqpcp_has_calendar()
{
	return (sqpcp_mode() ? 1:0);
}

int sqpcp_has_groupware()
{
	const char *p=sqpcp_mode();

	return (p && strcmp(p, "net") == 0);
}

static int createcache(struct PCP *,
		       struct cacherecord **, unsigned *, time_t, time_t);
static void destroycache(struct cacherecord *, unsigned);

/*
** Are we logged in?  If so, refresh the calendar event cache that we
** display on the main folders screen, if it is stale.
*/

static int need_refresh();

int sqpcp_loggedin()
{
	struct PCP *pcp;

	if (!sqpcp_mode())
		return (0);

	if (sqpcp_has_groupware())
	{
		struct stat stat_buf;

		if (stat(TOKEN, &stat_buf))
			return (0);	/* Login session dropped */
	}

	if (!need_refresh())
		return (1);
	pcp=sqpcp_calendar();
	if (pcp)
		refreshcache(pcp);
	return (1);
}

/* Check if it's time to rebuild CACHE file */

static const char *parsetimet(const char *buf, time_t *tp)
{
	*tp=libmail_strtotime_t(&buf);

	if (*buf == '\t')
		++buf;
	return buf;
}

static int need_refresh()
{
	FILE *fp;
	struct stat stat_buf;

	/* Check whether it's time to rebuild the cache */

	fp=fopen(CACHE, "r");

	if (stat(CHANGEDFILE, &stat_buf) == 0)
	{
		if (fp)
			fclose(fp);
		return (1);
	}

	if (fp)
	{
		struct stat stat_buf;

		char buffer[BUFSIZ];
		const char *p;

		time_t a, b;

		int ch=getc(fp);

		if (ch == EOF)
		{
			fclose(fp);
			return (0); /* No events scheduled */
		}

		ungetc(ch, fp);
		if (fgets(buffer, sizeof(buffer), fp) &&
		    (p=parsetimet(buffer, &a)) &&
		    parsetimet(p, &b))
		{
			time_t now;

			time(&now);

			if (now < b)	/* Event expired */
			{
				if (fstat(fileno(fp), &stat_buf) == 0)
				{
					/*
					** Check in every TIMEOUTSOFT/2 in
					** any case.
					*/

					if (stat_buf.st_mtime >
					    now - get_timeoutsoft()/2)
					{
						fclose(fp);	/* Not yet */
						return (0);
					}
				}
			}
		}
		fclose(fp);
		unlink(CACHE);
		return (1);
	}
	return (0);
}

static void refreshcache(struct PCP *pcp)
{
	struct cacherecord *recs;
	unsigned n_recs;
	unsigned i;
	char *new_name;
	int new_fd;
	char *p;
	time_t now;
	FILE *new_fp;

	unlink(CHANGEDFILE);
	time(&now);
	if (createcache(pcp, &recs, &n_recs, now, now + 5 * 24 * 60 * 60))
		return;

	new_fd=maildir_createmsg(INBOX, "cache", &new_name);
	if (new_fd < 0 || (new_fp=fdopen(new_fd, "w")) == 0)
	{
		if (new_fd >= 0)        close(new_fd);
		destroycache(recs, n_recs);
		return;
	}

        p=malloc(sizeof("tmp/")+strlen(new_name));
        if (!p)
        {
		fclose(new_fp);
                free(new_name);
		destroycache(recs, n_recs);
                return;
        }
        strcat(strcpy(p, "tmp/"), new_name);
        free(new_name);
        new_name=p;

	/* Save a max of 5 events, for the main page listing */

	for (i=0; i<n_recs && i < 5; i++)
	{
		char buf1[NUMBUFSIZE], buf2[NUMBUFSIZE];

		fprintf(new_fp, "%s\t%s\t%s\t%s\t%s\n",
			libmail_str_time_t(recs[i].start, buf1),
			libmail_str_time_t(recs[i].end, buf2),
			recs[i].eventid,
			recs[i].flags,
			recs[i].subject);
	}

	if (fflush(new_fp) || ferror(new_fp))
	{
		fclose(new_fp);
		unlink(new_name);
                free(new_name);
		destroycache(recs, n_recs);
                return;
        }
	fclose(new_fp);
	rename(new_name, CACHE);
	free(new_name);
	destroycache(recs, n_recs);
	return;
}

struct cacherecord_list {
	struct cacherecord_list *next;
	struct cacherecord rec;
} ;

static int cmp_reclist(const void *a, const void *b)
{
	struct cacherecord_list *aa=*(struct cacherecord_list **)a;
	struct cacherecord_list *bb=*(struct cacherecord_list **)b;

	return ( aa->rec.end < bb->rec.end ? -1:
		 aa->rec.end > bb->rec.end ? 1:
		 aa->rec.start < bb->rec.start ? -1:
		 aa->rec.start > bb->rec.start ? 1:
		 strcmp(aa->rec.eventid, bb->rec.eventid));
}

static int callback_createcache(struct PCP_list_all *, void *);
static int callback_retr_status(struct PCP_retr *, int, void *);
static int callback_retr_headers(struct PCP_retr *, const char *,
				 const char *, void *);

static void destroycache_rec(struct cacherecord *);

struct retr_xinfo {
	struct cacherecord *recs;
	unsigned n_recs;
} ;

static int createcache(struct PCP *pcp,
		       struct cacherecord **recs, unsigned *n_recs,
		       time_t start, time_t end)
{
	struct cacherecord_list *list=NULL, *p, **a;
	struct PCP_list_all la;
	struct PCP_retr r;
	struct retr_xinfo xr;

	unsigned i,n;
	const char **event_ids;

	*recs=0;
	*n_recs=0;

	memset(&la, 0, sizeof(la));
	la.list_from=start;
	la.list_to=end;
	la.callback_func=callback_createcache;
	la.callback_arg= &list;

	if (pcp_list_all(pcp, &la))
	{
		while ((p=list) != NULL)
		{
			list=p->next;
			destroycache_rec(&p->rec);
			free(p);
		}
		return (0);
	}

	/* Sort the returned event list, in chronological order. */


	/* First, create an array out of the list */

	for (n=0, p=list; p; p=p->next)
		++n;

	if (!n)
		return (0);	/* Nothing */

	a=malloc(sizeof(struct cacherecord_list *)*n);
	if (!a)
	{
		fprintf(stderr, "NOTICE: malloc failed\n");

		while ((p=list) != NULL)
		{
			list=p->next;
			destroycache_rec(&p->rec);
			free(p);
		}
		return (-1);
	}
	for (n=0, p=list; p; p=p->next)
		a[n++]=p;

	/* Sort it, copy the sorted array to the return ptr */

	qsort(a, n, sizeof(*a), cmp_reclist);

	if ((*recs=malloc(sizeof(struct cacherecord)*n)) == NULL)
	{
		fprintf(stderr, "NOTICE: malloc failed\n");

		while ((p=list) != NULL)
		{
			list=p->next;
			destroycache_rec(&p->rec);
			free(p);
		}
		free(a);
		return (-1);
	}

	for (i=0; i<n; i++)
		(*recs)[i]= a[i]->rec;
	*n_recs=n;
	free(a);
	while ((p=list) != NULL)
	{
		list=p->next;
		free(p);
	}

	/* Get the subject of all the events */

	event_ids=malloc(sizeof(const char *)*(n+1));

	if (!event_ids)
	{
		fprintf(stderr, "NOTICE: malloc failed\n");

		destroycache(*recs, *n_recs);
		return (-1);
	}

	for (i=0; i< *n_recs; i++)
		event_ids[i]= (*recs)[i].eventid;
	event_ids[i]=NULL;

	memset(&r, 0, sizeof(r));
	r.event_id_list=event_ids;
	r.callback_arg=&xr;
	xr.recs= *recs;
	xr.n_recs= *n_recs;

	r.callback_retr_status=callback_retr_status;
	r.callback_headers_func=callback_retr_headers;

	if (pcp_retr(pcp, &r))
	{
		fprintf(stderr, "NOTICE: pcp_retr: error: %s\n", pcp_errmsg(pcp));
		free(event_ids);
		destroycache(*recs, *n_recs);
		return (-1);
	}
	free(event_ids);
	return (0);
}

/* Callback from PCP LIST - save the event ID in the cacherecord_list */

static int callback_createcache(struct PCP_list_all *a, void *vp)
{
	struct cacherecord_list **listp=(struct cacherecord_list **)vp, *p;

	if ((p=malloc(sizeof(struct cacherecord_list))) == NULL)
		return (-1);

	p->rec.start=a->event_from;
	p->rec.end=a->event_to;

	if ((p->rec.eventid=strdup(a->event_id)) == NULL)
	{
		free(p);
		return (-1);
	}

	/* Initialize the other fields, so all ptrs are live */

	if ((p->rec.flags=strdup("")) == NULL)
	{
		free(p->rec.eventid);
		free(p);
		return (-1);
	}

	if ((p->rec.subject=strdup("")) == NULL)
	{
		free(p->rec.flags);
		free(p->rec.eventid);
		free(p);
		return (-1);
	}

	p->next= *listp;
	*listp=p;
	return (0);
}

static int callback_retr_status(struct PCP_retr *r, int s, void *vp)
{
	struct cacherecord *recs=( (struct retr_xinfo *)vp)->recs;
	unsigned n_recs=( (struct retr_xinfo *)vp)->n_recs;
	unsigned i;

	char stat_buf[256];
	char *p;

	stat_buf[0]=0;

	if (s & LIST_CANCELLED)
	{
		strcat(stat_buf, " CANCELLED");
	}

	if (s & LIST_BOOKED)
	{
		strcat(stat_buf, " BOOKED");
	}

	p=stat_buf;
	if (*p)
		++p;

	for (i=0; i<n_recs; i++)
	{
		if (strcmp(recs[i].eventid, r->event_id) == 0)
		{
			char *s=strdup(p);

			if (!s)
				return (-1);
			free(recs[i].flags);
			recs[i].flags=s;
			break;
		}
	}
	return (0);
}

/* RETR callback for headers - save the Subject: header */

static void collapse_subject(char *s)
{
	/* Collapse multiline subjects */

	char *t, *u;

	for (t=u=s; *t; )
	{
		if (*t != '\n')
		{
			*u++=*t++;
			continue;
		}

		while (*t && isspace((int)(unsigned char)*t))
			++t;
		*u++=' ';
	}
	*u=0;
}

static int callback_retr_headers(struct PCP_retr *r, const char *h,
				 const char *v, void *vp)
{
	struct cacherecord *recs=( (struct retr_xinfo *)vp)->recs;
	unsigned n_recs=( (struct retr_xinfo *)vp)->n_recs;
	unsigned i;

	if (strcasecmp(h, "Subject") || !v)
		return (0);

	for (i=0; i<n_recs; i++)
	{
		if (strcmp(recs[i].eventid, r->event_id) == 0)
		{
			char *s;

			s=strdup(v);

			if (!s)
				return (-1);

			collapse_subject(s);
			free(recs[i].subject);
			recs[i].subject=s;
		}
	}

	return (0);
}

static void destroycache(struct cacherecord *c, unsigned n)
{
	unsigned i;

	for (i=0; i<n; i++)
	{
		destroycache_rec(c+i);
	}
	if (c)
		free(c);
}

static void destroycache_rec(struct cacherecord *c)
{
	free(c->eventid);
	free(c->flags);
	free(c->subject);
}

static void parsecache_rec(char *p, struct cacherecord *cr)
{
	unsigned long a;

	memset(cr, 0, sizeof(*cr));
	cr->eventid="";
	cr->flags="";
	cr->subject="";

	if (!p || sscanf(p, "%lu", &a) <= 0)
		return;
	p=strchr(p, '\t');
	cr->start= (time_t)a;
	if (!p || sscanf(p, "%lu", &a) <= 0)
		return;
	cr->end= (time_t)a;
	p=strchr(p+1, '\t');
	if (!p) return;
	++p;

	cr->eventid=p;
	p=strchr(p, '\t');
	if (!p) return;
	*p++=0;

	cr->flags=p;
	p=strchr(p, '\t');
	if (!p) return;
	*p++=0;

	cr->subject=p;
}

struct PCP *sqpcp_calendar()
{
	const char *p;

	if (!sqpcp_mode())
	{
		errno=ENOENT;
		return (NULL);
	}

	if (calendar)
		return (calendar);

	p=getenv("AUTHADDR");

	if (!p)
	{
		errno=ENOENT;
		return (NULL);
	}

	if (sqpcp_has_groupware())
	{
		char token[256];
		char *pp;

		FILE *f=fopen(TOKEN, "r");

		if (!f)
			return (NULL);

		if (fgets(token, sizeof(token), f) == NULL)
		{
			fclose(f);
			return (NULL);
		}

		if ((pp=strchr(token, '\n')) != 0) *pp=0;

		fclose(f);
		if (token[0] == 0)
		{
			unlink(TOKEN);
			return (NULL);
		}

		calendar=pcp_reopen_server(p, token, NULL);

		if (calendar)
		{
			p=pcp_authtoken(calendar);

			if (p && strcmp(p, token))
			{
				int u=umask(077);

				/* Save new authentication token */

				f=fopen(TOKEN, "w");
				umask(u);
				if (f)
				{
					fprintf(f, "%s\n", token);
					fclose(f);
				}
			}
		}
		else
		{
			unlink(TOKEN);
		}
		return (calendar);
	}

	return ((calendar=pcp_open_dir("calendar", p)));
}


/* ---------------------------------------------------------------- */
/* Display event information */
/* ---------------------------------------------------------------- */

static struct PCP_event_time *event_time_list=0;
static unsigned n_event_time_list=0;
static FILE *openoldfp(const char *p, unsigned long *prev_size);

struct my_participant {
	struct my_participant *next;
	char *name;
	char *address;
} ;

static struct my_participant *my_participant_list=0;

static void add_my_participant(const char *h)
{
	struct my_participant *m=
		malloc(sizeof(struct my_participant));

	if (!m)
		enomem();

	memset(m, 0, sizeof(*m));
	m->next=my_participant_list;
	my_participant_list=m;

	if ((m->address=strdup(h)) == NULL)
		enomem();
}

static char *from_buf=0, *subj_buf=0;

void sqpcp_eventstart()
{
	const char *p=cgi("draftmessage");
	FILE *oldfp;
	struct rfc822hdr h;
	struct PCP_event_time_list {
		struct PCP_event_time_list *next;
		struct PCP_event_time t;
	} *event_time_listp=NULL, **lastp= &event_time_listp;
	unsigned event_time_list_cnt=0;

	sqpcp_eventend();

	if (!p || !*p)
		return;

	oldfp=openoldfp(p, NULL);
	if (!oldfp)
		return;

	rfc822hdr_init(&h, 8192);
	while (rfc822hdr_read(&h, oldfp, NULL, 0) == 0)
	{
		if (strcasecmp(h.header, "X-Event-Time") == 0 && h.value)
		{
			unsigned long n1, n2;

			if (sscanf(h.value, "%lu %lu", &n1, &n2) == 2)
			{
				struct PCP_event_time_list *t=
					malloc(sizeof(**lastp));

				if (!t)
				{
					rfc822hdr_free(&h);
					sqpcp_eventend();
					enomem();
					return;
				}
				*lastp=t;
				t->next=NULL;
				t->t.start=(time_t)n1;
				t->t.end=(time_t)n2;
				++event_time_list_cnt;
				lastp=&t->next;
			}
		}
		else if (strcasecmp(h.header, "X-Event-Participant") == 0 && h.value)
		{
			add_my_participant(h.value);
		}
		else if (strcasecmp(h.header, "from") == 0 && h.value)
		{
			rfc822hdr_collapse(&h);
			if (from_buf)
				free(from_buf);

			from_buf=NULL;

			if ((from_buf=
			     rfc822_display_hdrvalue_tobuf(h.header,
							   h.value,
							   sqwebmail_content_charset,
							   NULL, NULL)) == NULL)
				from_buf=strdup(h.value);

			if (from_buf)
				cgi_put("from", from_buf);
		}
		else if (strcasecmp(h.header, "subject") == 0 && h.value)
		{
			rfc822hdr_collapse(&h);
			if (subj_buf)
				free(subj_buf);

			subj_buf=rfc822_display_hdrvalue_tobuf(h.header,
							       h.value,
							       sqwebmail_content_charset,
							       NULL, NULL);
			if (!subj_buf)
				subj_buf=strdup(subj_buf);

			if (subj_buf)
				cgi_put("headersubject", subj_buf);
		}
	}
	rfc822hdr_free(&h);
	if (event_time_list_cnt)
	{
		struct PCP_event_time *list1;
		struct PCP_event_time_list *p;

		list1=malloc(sizeof(struct PCP_event_time_list)
			     *event_time_list_cnt);
		if (!list1)
		{
			sqpcp_eventend();
			enomem();
			return;
		}
		event_time_list_cnt=0;
		while ((p=event_time_listp) != NULL)
		{
			list1[event_time_list_cnt]=p->t;
			++event_time_list_cnt;
			event_time_listp=p->next;
			free(p);
		}

		event_time_list=list1;
		n_event_time_list=event_time_list_cnt;
	}
}

void sqpcp_eventend()
{
	struct my_participant *m;

	while ((m=my_participant_list) != NULL)
	{
		my_participant_list=m->next;
		if (m->name)
			free(m->name);
		if (m->address)
			free(m->address);
		free(m);
	}

	if (event_time_list)
		free(event_time_list);
	event_time_list=NULL;
	n_event_time_list=0;

}

void sqpcp_eventtimes()
{
	char buffer[512];
	unsigned i;

	for (i=0; i<n_event_time_list; i++)
	{
		if (i)
			printf("<br />\n");
		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     event_time_list[i].start,
				     event_time_list[i].end))
			continue;
		printf("<span class=\"tt\">");
		print_safe(buffer);
		printf("&nbsp;&nbsp;</span><a href=\"");
		output_scriptptrget();
		printf("&amp;form=newevent&amp;draftmessage=");
		output_urlencoded(cgi("draftmessage"));
		printf("&amp;do.deleventtime=%s-%s\"><font size=\"-2\">"
		       "(%s)</font></a>",
		       libmail_str_time_t(event_time_list[i].start, buffer),
		       libmail_str_time_t(event_time_list[i].end, buffer+NUMBUFSIZE),
		       getarg("REMOVE")
		       );
	}
}

static int save_participant_names(const char *addr, const char *name,
				  void *vp)
{
	struct my_participant *p=(struct my_participant *)vp;

	for ( ; name && p; p=p->next)
		if (strcasecmp(p->address, addr) == 0 && p->name == 0)
			p->name=strdup(name);
	return (0);
}

void sqpcp_eventparticipants()
{
	struct my_participant *m;

	if (!my_participant_list)
		return;

	(void)ab_get_nameaddr(save_participant_names, my_participant_list);

	printf("<table border=\"0\">");

	for (m=my_participant_list; m; m=m->next)
	{
		printf("<tr><td><span class=\"tt\">");
		if (m->address)
			ab_nameaddr_show(m->name, m->address);

		printf("</span></td><td>&nbsp;&nbsp;<a href=\"");

		output_scriptptrget();
		printf("&amp;form=newevent&amp;draftmessage=");
		output_urlencoded(cgi("draftmessage"));
		printf("&amp;do.delparticipant=");
		output_urlencoded(m->address);
		printf("\"><font size=\"-2\">(%s)</font></a></td></tr>\n",
		       getarg("REMOVE"));
	}
	printf("</table>");
}

void sqpcp_eventfrom()
{
	if (auth_getoptionenvint("wbnochangingfrom"))
	{
		printf("<span class=\"tt\">");
		print_safe(login_fromhdr());
		printf("</span>");
	}
	else
	{
		const char *p=cgi("headerfrom");

		if (!p || !*p)
			p=pref_from;
		if (!p || !*p)
			p=login_fromhdr();
		if (!p)
			p="";

		printf("<input type=\"text\" name=\"headerfrom\" size=\"60\" value=\"");
		output_attrencoded(p);
		printf("\" />");
	}
}

void sqpcp_eventtext()
{
	const char *p;

	printf("<textarea name=\"message\" cols=\"%d\" rows=\"15\" wrap=\"soft\">",
	       MYLINESIZE);
	p=cgi("draftmessage");

	if (p && *p)
	{
		FILE *fp=openoldfp(p, NULL);
		int dummy;

		if (fp)
		{
			newmsg_showfp(fp, &dummy);
		}
		fclose(fp);
	}
	printf("</textarea>\n");
}

void sqpcp_eventattach()
{
	const char *p;

	p=cgi("draftmessage");

	if (p && *p)
	{
		attachments_head(NULL, cgi("pos"), p);
	}
}

/* ------- Display the cached event summary -------- */

static void print_event_subject(char *, const char *, unsigned);
static void print_event_link_url(const char *, const char *);

void sqpcp_summary()
{
	FILE *fp;
	int cnt=0;
	time_t now;
	struct tm *tmptr;

	if (!sqpcp_mode())
		return;

	if (*cgi("do.calendarlogin"))
	{
		const char *userid=getenv("AUTHADDR");
		const char *password=cgi("password");

		if (userid && password)
		{
			do_pcplogin(userid, password, 1);
		}
	}

	if (sqpcp_has_groupware())
	{
		if (!sqpcp_loggedin())
		{
			insert_include("calendarlogin");
			return;
		}
	}

	time(&now);
	tmptr=localtime(&now);

	if (tmptr)
	{
		char *p=scriptptrget();
		char *q=malloc(strlen(p)+200);

		if (!q)
		{
			free(p);
			enomem();
		}

		strcpy(q, p);
		free(p);
		strcat(q, "&amp;form=eventdaily&amp;clearcache=1&amp;date=");
		sprintf(q+strlen(q), "%04d%02d%02d",
			tmptr->tm_year + 1900,
			tmptr->tm_mon+1,
			tmptr->tm_mday);

		printf(getarg("CALENDAR"), q);
		free(q);
	}

	printf("<table class=\"eventsummary\" width=\"100%%\">");

	if ((fp=fopen(CACHE, "r")) != NULL)
	{
		char buffer[BUFSIZ];
		int i;
		char last_date[256];
		char date[256];
		char date2[256];
		struct tm *tm;
		char yyyymmdd[9];

		char time1[128];
		char time2[128];

		last_date[0]=0;

		for (;;)
		{
			int c;
			struct cacherecord cr;

			i=0;

			while ((c=getc(fp)) != EOF && c != '\n')
			{
				if (i < sizeof(buffer)-1)
					buffer[i++]=(char)c;
			}
			buffer[i]=0;
			if (i == 0 && c == EOF)
				break;

			parsecache_rec(buffer, &cr);

			if (pcp_fmttime(date, sizeof(date),
					cr.start, FMTTIME_DATE))
				continue;
			if (pcp_fmttime(date2, sizeof(date2),
					cr.end, FMTTIME_DATE))
				continue;

			if (pcp_fmttime(time1, sizeof(time1),
					cr.start,
					FMTTIME_TIME))
				continue;

			if (pcp_fmttime(time2,
					sizeof(time2),
					cr.end,
					FMTTIME_TIME))
				continue;

			tm=localtime(&cr.start);
			if (!tm)
				continue;
			sprintf(yyyymmdd, "%04d%02d%02d",
				tm->tm_year + 1900,
				tm->tm_mon + 1,
				tm->tm_mday);

			i=0;
			if (last_date[0] && strcmp(last_date, date) == 0)
			{
				if (strcmp(last_date, date2) == 0)
				{
					/* Same day as last event */

					printf("<tr><td align=\"left\" width=\"1%%\">&nbsp;</td>"
					       "<td align=\"left\">&nbsp;"
					       "<a href=\"");
					print_event_link_url(cr.eventid, "");
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(time2);
					printf("</a></td>");
					i=1;
				}
				else
				{
					last_date[0]=0;
				}
			}
			else
			{
				last_date[0]=0;
				if (strcmp(date, date2) == 0)
					strcpy(last_date, date);
			}

			if (!i)
			{
				if (cnt)
					printf("<tr><td style=\"line-height: 0px; font-size: 8px\" colspan=\"3\">"
					       "&nbsp;"
					       "</td></tr>\n");
				if (strcmp(date, date2))
				{
					printf("<tr><td colspan=\"2\">"
					       "<a href=\"");
					print_event_link_url(cr.eventid, "");
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(date);
					printf("&nbsp;");
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(date2);
					printf("&nbsp;");
					print_safe(time2);
					printf("</a></td>");
				}
				else
				{
					printf("<tr><td width=\"1%%\">"
					       "<a href=\"");
					print_event_link_url(cr.eventid, "");
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(date);
					printf("</a></td><td>&nbsp;"
					       "<a href=\"");
					print_event_link_url(cr.eventid, "");
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(time2);
					printf("</a></td>");
				}
			}

			printf("<td width=\"100%%\">"
			       "<a href=\"");
			print_event_link_url(cr.eventid, "");
			printf("&amp;date=%s\">&nbsp;&nbsp;", yyyymmdd);
			print_event_subject(cr.flags, cr.subject, 60);
			printf("</a></td></tr>\n");
			++cnt;
		}
		fclose(fp);
	}

	if (cnt == 0)
	{
		printf("<tr><td align=\"center\">%s</td></tr>\n",
		       getarg("NOEVENTS"));
	}

	printf("</table>\n");
	printf("<a href=\"");
	output_scriptptrget();
	printf("&amp;form=newevent\">%s</a>", getarg("NEWEVENT"));
}


static void print_event_subject(char *flags, const char *subject, unsigned w)
{
	unsigned i;
	char *p;

	/* Print event flags first: CANCELLED... */

	for (p=flags; p && (p=strtok(p, " \t\r")) != 0; p=0)
	{
		printf("%s", getarg(p));
	}

	p=rfc822_display_hdrvalue_tobuf("subject",
					subject ? subject:"",
					sqwebmail_content_charset,
					NULL,
					NULL);

	if (!p)
		p=strdup(subject ? subject:"");

	if (!p)
		return;

	if (strlen(p) > w)
	{
		/* Truncate long subject lines */
		i=w-5;
		while (i)
		{
			if (isspace((int)(unsigned char) p[i]))
			{
				strcpy(p+i, "...");
				break;
			}
			--i;
		}
	}
	print_safe(p);
	free(p);
}

/* ------- New event support code -------- */

static int addtime(int);
static void addparticipant(int, const char *);

static FILE *openoldfp(const char *p, unsigned long *prev_size)
{
	struct stat stat_buf;
	char *filename;
	int x;
	FILE *oldfp;

	CHECKFILENAME(p);

	filename=maildir_find(INBOX "." DRAFTS, p);
	if (!filename)
		return (NULL);

	x=maildir_safeopen(filename, O_RDONLY, 0);
	free(filename);
	if (x < 0)
		return (NULL);


	if (fstat(x, &stat_buf) < 0 || (oldfp=fdopen(x, "r")) == NULL)
	{
		close(x);
		return (NULL);
	}

	if (prev_size)
		*prev_size=stat_buf.st_size;
	return (oldfp);
}


/* ------------- Conflict indicators ---------------- */

struct conflict_list {
	struct conflict_list *next;
	char *event_id;
	time_t start, end;
	char *address;
	char *subject;
} ;

static struct conflict_list *conflict_list=NULL;

static void init_save_conflict()
{
	while (conflict_list)
	{
		struct conflict_list *p=conflict_list;

		conflict_list=p->next;
		if (p->event_id)
			free(p->event_id);
		if (p->address)
			free(p->address);
		if (p->subject)
			free(p->subject);
		free(p);
	}
}

static int save_conflict(const char *event_id, time_t start, time_t end,
			  const char *address,
			  void *dummy)
{
	struct conflict_list *p, **ptr;

	for (ptr= &conflict_list; *ptr; ptr=&(*ptr)->next)
	{
		if ( (*ptr)->end > end)
			break;
	}

	if ((p=malloc(sizeof(struct conflict_list))) == NULL)
		return (-1);
	memset(p, 0, sizeof(*p));
	p->next= *ptr;
	*ptr=p;

	p->start=start;
	p->end=end;
	if ((p->event_id=strdup(event_id)) == NULL ||
	    (address && (p->address=strdup(address)) == NULL))
		return (-1);

	return (0);
}

static int save_conflict_subj(struct PCP_retr *, const char *,
			      const char *, void *);

static void show_conflict_error(struct PCP *pcp)
{
	unsigned n;
	struct conflict_list *p;
	const char **l;
	struct PCP_retr r;

	for (n=0, p=conflict_list; p; p=p->next)
		if (p->event_id && (!p->address ||
				    strcmp(p->address, "@") == 0))
			++n;

	if ((l=malloc(sizeof(const char *)*(n+1))) == NULL)
		return;

	for (n=0, p=conflict_list; p; p=p->next)
		if (p->event_id && (!p->address ||
				    strcmp(p->address, "@") == 0))
		{
			l[n]=p->event_id;
			++n;
		}

	l[n]=0;
	memset(&r, 0, sizeof(r));
	r.event_id_list=l;
	r.callback_headers_func=save_conflict_subj;

	if (n == 0 || pcp_retr(pcp, &r) == 0)
	{
		printf("<table border=\"0\" width=\"100%%\" class=\"small-error\">");
		for (p=conflict_list; p; p=p->next)
		{
			char buffer[512];

			printf("<tr><td width=\"30\">&nbsp;</td><td><span class=\"tt\">");
			if (pcp_fmttimerange(buffer, sizeof(buffer),
					     p->start,
					     p->end) == 0)
			{
				print_safe(buffer);
			}
			printf("</span></td><td width=\"30\">&nbsp;</td><td width=\"100%%\"><span class=\"tt\">");
			if (p->address && strcmp(p->address, "@"))
			{
				printf("%s", getarg("CONFLICTERR2"));
				print_safe(p->address);
			}
			else
				print_event_subject("", p->subject
						    ? p->subject:"", 60);
			printf("</span></td></tr>\n");
		}
		printf("<tr><td colspan=\"4\"><hr width=\"90%%\" /></td></tr></table>\n");
	}
	free(l);
	init_save_conflict();
}

static int save_conflict_subj(struct PCP_retr *r, const char *h,
			      const char *v, void *dummy)
{
	struct conflict_list *p;

	if (strcasecmp(h, "subject") || !v)
		return (0);

	for (p=conflict_list; p; p=p->next)
		if (p->event_id && strcmp(p->event_id, r->event_id) == 0)
		{
			if (p->subject)
				free(p->subject);
			if ((p->subject=strdup(v)) != NULL)
				collapse_subject(p->subject);
			break;
		}
	return (0);
}

/* -------------------------------------------------- */

static void showerror()
{
	const char *p;

	p=cgi("error");
	if (strcmp(p, "quota") == 0)
		printf("%s", getarg("QUOTAERR"));
	if (strcmp(p, "time") == 0)
		printf("%s", getarg("NOTIMEERR"));
	if (strcmp(p, "conflict") == 0)
	{
		struct PCP *pcp=sqpcp_calendar();

		printf("%s", getarg("CONFLICTERR"));
		if (pcp)
			show_conflict_error(pcp);
	}
	if (strcmp(p, "calendar") == 0)
	{
		printf(getarg("CALENDARERR"), cgi("pcperror"));
	}
	if (strcmp(p, "locked") == 0)
		printf("%s", getarg("LOCKERR"));
	if (strcmp(p, "notfound") == 0)
		printf("%s", getarg("NOTFOUNDERR"));
	if (strcmp(p, "eventlocked") == 0)
		printf("%s", getarg("EVENTLOCKEDERR"));
}

#if 0
/*
** Create/Estimate the To: header which lists event participants.
*/

static void mktohdr(char *h, size_t *sizep)
{
	int need_toh=1;
	size_t l=0;
	struct my_participant *p;

	*sizep=4;

	for (p=my_participant_list; p; p=p->next)
	{
		const char *c;

		if (*sizep - l > 500)
		{
			if (h)
				*h++='\n';
			++*sizep;
			need_toh=1;
			l= *sizep;
		}

		if (need_toh)
		{
			if (h)
			{
				strcpy(h, "To: ");
				h += 4;
			}
			*sizep += 4;
			need_toh=0;
		}
		else
		{
			if (h)
			{
				strcpy(h, ",\n  ");
				h += 4;
			}
			*sizep += 4;
		}

		if (p->name && *p->name)
		{
			if (h)
				*h++='"';
			++*sizep;

			for (c=p->name; *c; c++)
			{
				if (*c == '"' || *c == '\\')
				{
					if (h)
						*h++ = '\\';
					++*sizep;
				}
				if (h)
					*h++ = *c;
				++*sizep;
			}

			if (h)
			{
				*h++='"';
				*h++=' ';
			}
			*sizep += 2;
		}

		if (h)
			*h++='<';
		++*sizep;

		for (c=p->address; *c; c++)
		{
			if (h)
				*h++= *c;
			++*sizep;
		}
		if (h)
			*h++='>';
		++*sizep;
	}

	if (my_participant_list)
	{
		if (h)
			*h++='\n';
		++*sizep;
	}

	if (h)
		*h++=0;
	++*sizep;
}
#endif

void sqpcp_newevent()
{
	char *draftfilename;
	int newdraftfd;
	const char *p;
	unsigned long prev_size=0;
	FILE *oldfp=NULL;
	int errflag=0;
	int do_newevent= *cgi("do.neweventtime") ? 1:0;
	int do_newparticipant= *cgi("do.addparticipant") ? 1:0;
	int do_delevent=0;
	int do_delparticipant= *cgi("do.delparticipant") ? 1:0;
	time_t delstart=0, delend=0;

	static char *draftmessage_buf=0;

	showerror();

	if (!do_newevent)
	{
		if ((p=cgi("do.deleventtime")) && *p)
		{
			unsigned long a, b;

			if (sscanf(p, "%lu-%lu", &a, &b) == 2)
			{
				delstart=(time_t)a;
				delend=(time_t)b;
				do_delevent=1;
			}
		}
	}

	p=cgi("draftmessage");

	if (!do_newevent && !do_delevent && !do_newparticipant
	    && !do_delparticipant)
	{
		if (*cgi("do.neweventpreview") && p && *p)
		{
			printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"1\" class=\"box-small-outer\"><tr><td>\n");
			printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\" class=\"preview\"><tr><td>\n");
			newmsg_preview(p);
			printf("</td></tr></table>\n");
			printf("</td></tr></table>\n");
		}
		return;
	}

	if (p && *p)
	{
		oldfp=openoldfp(p, &prev_size);
		if (!oldfp)
			p="";
	}

	if (p && *p)
	{
		newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, p, &draftfilename);
	}
	else
	{
		newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, &draftfilename);
		maildir_writemsgstr(newdraftfd, "X-Event: 1\n");

		if (draftmessage_buf)
			free(draftmessage_buf);
		if ((draftmessage_buf=strdup(draftfilename)) != 0)
			cgi_put("draftmessage", draftmessage_buf);
	}

	if (do_newevent || do_delevent || do_newparticipant
	    || do_delparticipant)
	{
		if (oldfp)
		{
			struct rfc822hdr h;

			rfc822hdr_init(&h, 8192);
			while (rfc822hdr_read(&h, oldfp, NULL, 0) == 0)
			{
				unsigned long a, b;
				if (do_delevent && strcasecmp(h.header,
							      "X-Event-Time")
				    == 0 && h.value && sscanf(h.value,
							      "%lu %lu",
							      &a, &b) == 2)

				{
					if ( (time_t)a == delstart &&
					     (time_t)b == delend)
						continue;
				}

				if (do_delparticipant
				    && strcasecmp(h.header, "X-Event-Participant")
				    == 0 && h.value)
				{
					if (strcmp(h.value,
						   cgi("do.delparticipant"))
					    == 0)
						continue;
				}
				if (strcasecmp(h.header,
					       "X-Event-Participant") == 0 &&
				    h.value)
					add_my_participant(h.value);

				if (strcasecmp(h.header, "To") == 0)
					continue;
				/* To: header rebuilt later */

				maildir_writemsgstr(newdraftfd, h.header);
				maildir_writemsgstr(newdraftfd, ": ");
				maildir_writemsgstr(newdraftfd, h.value);
				maildir_writemsgstr(newdraftfd, "\n");
			}
			rfc822hdr_free(&h);

		}

		if (do_newevent && addtime(newdraftfd))
		{
			errflag=1;
			printf("%s", getarg("TIMEERR"));
		}

		if (do_newparticipant)
		{
			const char *p=cgi("addressbookname");

			if (!*p)
				p=cgi("participant1");

			addparticipant(newdraftfd, p);
		}

		(void)ab_get_nameaddr(save_participant_names,
				      my_participant_list);

#if 0
		{
			struct rfc822t *t;
			struct rfc822a *a;
			char *p;
			char *tohdr;
			size_t tohdr_size;

			mktohdr(NULL, &tohdr_size);

			tohdr=malloc(tohdr_size);
			if (!tohdr)
				enomem();

			mktohdr(tohdr, &tohdr_size);

			if ((t=rfc822t_alloc_new(tohdr, NULL, NULL)) == NULL)
			{
				free(tohdr);
				enomem();
			}

			if ((a=rfc822a_alloc(t)) == NULL)
			{
				rfc822t_free(t);
				free(tohdr);
				enomem();
			}

			p=rfc2047_encode_header(a,sqwebmail_content_charset);

			if (!p)
			{
				rfc822a_free(a);
				rfc822t_free(t);
				free(tohdr);
				enomem();
			}
			free(tohdr);
			tohdr=p;
			rfc822a_free(a);
			rfc822t_free(t);

			maildir_writemsgstr(newdraftfd, tohdr);
			maildir_writemsgstr(newdraftfd, "\n");
			free(tohdr);
		}
#endif

		sqpcp_eventend(); /* Deallocate participant list */

		maildir_writemsgstr(newdraftfd, "\n");
	}

	if (oldfp)
	{
		char buf[BUFSIZ];
		int n;

		while ((n=fread(buf, 1, sizeof(buf), oldfp)) > 0)
			maildir_writemsg(newdraftfd, buf, n);
	}

	if (oldfp)
		fclose(oldfp);

	if (errflag)
	{
		maildir_deletenewmsg(newdraftfd, INBOX "." DRAFTS, draftfilename);
	}
	else if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 1,
				  prev_size))
	{
		printf("%s", getarg("QUOTAERR"));
	}

	free(draftfilename);
}

/* Split apart date/time string into space-separated words */

static char **mkargv(char *p, int *argc)
{
	int pass;
	char **argv=0;
	char *q;

	/* Two passes - count words, then make them */

	for (pass=0; pass<2; pass++)
	{
		if (pass)
		{
			if ((argv=malloc(sizeof(char *)* (*argc+1))) == NULL)
				return (NULL);
		}
		*argc=0;

		for (q=p; *q; )
		{
			if (isspace((int)(unsigned char)*q))
			{
				++q;
				continue;	/* Skip leading space */
			}

			if (pass)
				argv[ *argc ] = q;
			++*argc;

			while (*q)	/* Look for next space */
			{
				if (isspace((int)(unsigned char)*q))
				{
					if (pass)
						*q=0;
					++q;
					break;
				}
				++q;
			}
		}
	}
	argv[ *argc ] = 0;
	return (argv);
}

static int savetime(time_t, time_t, void *);

static int addtime(int newdraftfd)
{
	struct pcp_parse_datetime_info pdi;
	char *t_buf;
	char **t_argv;
	time_t starttime, endtime;
	int argn, argc;
	int h, m;

	pdi.today_name=cgi("today");		/* Locale string */
	pdi.tomorrow_name=cgi("tomorrow");	/* Locale string */

	t_buf=strdup(cgi("starttime"));
	if (!t_buf)
		return (-1);
	t_argv=mkargv(t_buf, &argc);
	if (!t_argv)
	{
		free(t_buf);
		return (-1);
	}

	argn=0;
	starttime=pcp_parse_datetime(&argn, argc, t_argv, &pdi);
	free(t_argv);
	free(t_buf);
	if (!starttime)
		return (-1);
	h=atoi(cgi("hours"));
	m=atoi(cgi("mins"));
	if (h < 0 || m < 0 || h > 999 || m > 59)
		return (-1);

	endtime=starttime + h * 60 * 60 + m * 60;

	argn=0;
	t_buf=strdup(cgi("endtime"));
	if (!t_buf)
		return (-1);
	t_argv=mkargv(t_buf, &argc);
	if (!t_argv)
	{
		free(t_buf);
		return (-1);
	}

	if (argc == 0)	/* Not a weekly event */
	{
		savetime(starttime, endtime, &newdraftfd);
	}
	else
	{
		if (pcp_parse_datetime_until(starttime, endtime, &argn, argc,
					     t_argv,
					     atoi(cgi("recurring")),
					     savetime, &newdraftfd))
		{
			free(t_argv);
			free(t_buf);
			return (-1);
		}
	}
	free(t_argv);
	free(t_buf);
	return (0);
}

static int savetime(time_t from, time_t to, void *dummy)
{
	char buf[NUMBUFSIZE];
	int fd= *(int *)dummy;

	maildir_writemsgstr(fd, "X-Event-Time: ");
	maildir_writemsgstr(fd, libmail_str_time_t(from, buf));
	maildir_writemsgstr(fd, " ");
	maildir_writemsgstr(fd, libmail_str_time_t(to, buf));
	maildir_writemsgstr(fd, "\n");
	return (0);
}

static void addparticipant(int fd, const char *n)
{
	char *nn, *p, *q;

	const char *domain;

	domain=getenv("AUTHADDR");

	if (domain)
		domain=strrchr(domain, '@');

	if (domain)
		++domain;
	else
		domain=myhostname();

	while (n && isspace((int)(unsigned char)*n))
		++n;

	if (!n || !*n)
		return;

	if (strchr(n, '\n') || strchr(n, '\r'))
		return;

	if ((nn=malloc(strlen(n)+strlen(domain)+2)) == NULL)
		enomem();

	strcpy(nn, n);

	for (p=q=nn; *p; p++)
		if (!isspace((int)(unsigned char)*p))
			q=p+1;
	*q=0;

	if (strchr(nn, '@') == 0)
		strcat(strcat(nn, "@"), domain);


	maildir_writemsgstr(fd, "X-Event-Participant: ");
	maildir_writemsgstr(fd, nn);
	maildir_writemsgstr(fd, "\n");
	add_my_participant(nn);
	free(nn);
}

/* ------------- Save text ------------- */

static char *savedraft()
{
	const char *p=cgi("draftmessage");
	char *msg, *filename;

	if (p && *p)
	{
		CHECKFILENAME(p);
	}

	filename=p && *p ? maildir_find(INBOX "." DRAFTS, p):NULL;

	msg=newmsg_createdraft_do(filename, cgi("message"), NEWMSG_PCP);
	if (filename)
		free(filename);

	if (!msg)
		enomem();
	return (msg);
}

static void previewdraft(char *msg, void (*func)(const char *))
{
	char *msg2, *msg2p;

	msg2=maildir_find(INBOX "." DRAFTS, msg);
	free(msg);
	if (!msg2)
		enomem();
	if ((msg2p=strrchr(msg2, '/')) != 0)
		++msg2p;
	else
		msg2p=msg2;

	cgi_put("draftmessage", msg2p);
	if (func)
		(*func)(msg2p);
	output_form("newevent.html");
	free(msg2);
}

void sqpcp_preview()
{
	char *msg;

	msg=savedraft();
	previewdraft(msg, NULL);
}

void sqpcp_postpone()
{
	char *msg;

	msg=savedraft();
	free(msg);
	output_form("folders.html");
}

static void deleteattach(const char *);

void sqpcp_deleteattach()
{
	char *msg;

	msg=savedraft();
	previewdraft(msg, deleteattach);
}

static void deleteattach(const char *msg)
{
	attach_delete(msg);
}

static void doupload(const char *);

void sqpcp_uploadattach()
{
	char *msg;

	msg=savedraft();
	previewdraft(msg, doupload);
}

static void doupload(const char *msg)
{
	int flag;

	flag=attach_upload(msg, NULL, NULL);

	if (flag)
		cgi_put("error", "quota");
}


void sqpcp_attachpubkey()
{
	char *msg;

	msg=savedraft();
	previewdraft(msg, NULL);
}

void sqpcp_attachprivkey()
{
	char *msg;

	msg=savedraft();
	previewdraft(msg, NULL);
}

/* ---------------- Save event ------------------ */

struct participant_list {
	struct participant_list *next;
	char *address;
} ;

struct saveinfo {
	struct PCP_event_time *times;
	unsigned n_times;
	struct PCP_event_participant *participants;
	unsigned n_participants;
	struct participant_list *participant_list;

	char *old_eventid;
} ;

static int init_saveinfo(struct saveinfo *si, FILE *fp)
{
	struct rfc822hdr h;

	struct savetimelist {
		struct savetimelist *next;
		struct PCP_event_time event_time;
	} *tlist=NULL, *p;
	unsigned tcnt=0;
	struct participant_list *l;


	si->times=NULL;
	si->n_times=0;
	si->old_eventid=0;
	si->participants=NULL;
	si->n_participants=0;
	si->participant_list=NULL;

	rfc822hdr_init(&h, BUFSIZ);
	while (rfc822hdr_read(&h, fp, NULL, 0) == 0)
	{
		unsigned long a, b;

		if (strcasecmp(h.header, "X-Event-Participant") == 0 && h.value)
		{
			l=malloc(sizeof(struct participant_list));
			if (!l || (l->address=strdup(h.value)) == NULL)
			{
				if (l)
					free(l);

				while ((l=si->participant_list) != NULL)
				{
					si->participant_list=l->next;
					free(l->address);
					free(l);
				}
				while (tlist)
				{
					p=tlist;
					tlist=p->next;
					free(p);
				}
				rfc822hdr_free(&h);
				return (-1);
			}
			l->next=si->participant_list;
			si->participant_list=l;
			++si->n_participants;
		}
		else if (strcasecmp(h.header, "X-Event-Time") == 0 &&
			 h.value && sscanf(h.value, "%lu %lu", &a, &b) == 2)
		{
			if ((p=malloc(sizeof(struct savetimelist))) == NULL)
			{
				while ((l=si->participant_list) != NULL)
				{
					si->participant_list=l->next;
					free(l->address);
					free(l);
				}
				while (tlist)
				{
					p=tlist;
					tlist=p->next;
					free(p);
				}
				rfc822hdr_free(&h);
				return (-1);
			}
			p->next=tlist;
			tlist=p;
			p->event_time.start=a;
			p->event_time.end=b;
			++tcnt;
		}

		if (strcasecmp(h.header, "X-Old-EventId") == 0 && h.value)
		{
			if (si->old_eventid)
				free(si->old_eventid);
			si->old_eventid=strdup(h.value);
			if (!si->old_eventid)
			{
				rfc822hdr_free(&h);
				return (-1);
			}
		}
	}
	rfc822hdr_free(&h);

	if (si->n_participants)
	{
		unsigned n=0;

		if ((si->participants
		     =calloc(sizeof(struct PCP_event_participant),
			     si->n_participants)) == NULL)
		{
			while ((l=si->participant_list) != NULL)
			{
				si->participant_list=l->next;
				free(l->address);
				free(l);
			}
			while (tlist)
			{
				p=tlist;
				tlist=p->next;
				free(p);
			}
			return (-1);
		}

		for (l=si->participant_list; l; l=l->next)
		{
			si->participants[n].address=l->address;
			++n;
		}
	}

	if (tcnt)
	{
		si->n_times=tcnt;
		if ((si->times=malloc(sizeof(struct PCP_event_time)
				      *tcnt)) == NULL)
		{
			while ((l=si->participant_list) != NULL)
			{
				si->participant_list=l->next;
				free(l->address);
				free(l);
			}
			while (tlist)
			{
				p=tlist;
				tlist=p->next;
				free(p);
			}
			if (si->old_eventid)
				free(si->old_eventid);
			return (-1);
		}
		tcnt=0;
		while (tlist)
		{
			p=tlist;
			tlist=p->next;
			si->times[tcnt]=p->event_time;
			free(p);
			++tcnt;
		}
	}
	return (0);
}

static void free_saveinfo(struct saveinfo *si)
{
	struct participant_list *l;

	if (si->participants)
		free(si->participants);

	while ((l=si->participant_list) != NULL)
	{
		si->participant_list=l->next;
		free(l->address);
		free(l);
	}

	if (si->times)
		free(si->times);
	if (si->old_eventid)
		free(si->old_eventid);
}

static void dropquota(const char *filename, int fd)
{
	unsigned long filesize=0;

	if (maildir_parsequota(filename, &filesize))
	{
		struct stat stat_buf;

		if (fstat(fd, &stat_buf))
			stat_buf.st_size=0;
		filesize=stat_buf.st_size;
	}

	maildir_quota_deleted(".", (int64_t)-filesize, -1);
}

/* ------------------------------------------------------------------------
**
** Save a calendar event
** ----------------------------------------------------------------------*/

static int dosave(FILE *, struct saveinfo *);

void sqpcp_save()
{
	char *msg, *sentmsg, *p;
	FILE *fp;
	struct saveinfo si;
	int isgpgerr;

	msg=savedraft();
	if (*cgi("error"))	/* Error, go back to the screen */
	{
		previewdraft(msg, NULL);
		return;
	}

	fp=openoldfp(msg, NULL);
	if (!fp)
	{
		free(msg);
		enomem();
	}

	if (init_saveinfo(&si, fp))
	{
		fclose(fp);
		free(msg);
		enomem();
	}

	if (si.times == 0)
	{
		fclose(fp);
		cgi_put("error", "time");
		previewdraft(msg, NULL);
		return;
	}
	fclose(fp);

	sentmsg=newmsg_createsentmsg(msg, &isgpgerr);

	/* Immediately remove the formatted event text from the sent folder */

	if (sentmsg)
	{
		p=maildir_find(INBOX "." SENT, sentmsg);
		free(sentmsg);
		sentmsg=p;
	}

	if (!sentmsg)
	{
		cgi_put("error", "quota");	/* TODO: gpgerr */
		free_saveinfo(&si);
		previewdraft(msg, NULL);
		return;
	}


	fp=fopen(sentmsg, "r");
	if (!fp)
	{
		free(sentmsg);
		free(msg);
		free_saveinfo(&si);
		enomem();
		return;
	}

	fcntl(fileno(fp), F_SETFD, FD_CLOEXEC);

	unlink(sentmsg);
	dropquota(sentmsg, fileno(fp));
	free(sentmsg);

	if (dosave(fp, &si))
	{
		fclose(fp);
		free_saveinfo(&si);
		previewdraft(msg, NULL);
		return;
	}
	fclose(fp);

	p=maildir_find(INBOX "." DRAFTS, msg);
	free(msg);

	fp=p ? fopen(p, "r"):NULL;
	unlink(p);
	if (fp)
	{
		dropquota(p, fileno(fp));
		fclose(fp);
	}
	free(p);
	free_saveinfo(&si);
	output_form("folders.html");
}

/* With all the preliminaries out of the way, put it on the calendar */

static void saveerror(struct PCP *pcp, const int *xerror)
{
	static char *errmsgbuf=0;
	const char *p;

	if (xerror)
		switch (*xerror) {
		case PCP_ERR_SYSERR:	/* Can be deliberate, see addacl() */
			cgi_put("error", "calendar");
			cgi_put("pcperror", strerror(errno));
			return;
		case PCP_ERR_LOCK:
			cgi_put("error", "locked");
			return;
		case PCP_ERR_CONFLICT:
			cgi_put("error", "conflict");
			return;
		case PCP_ERR_EVENTNOTFOUND:
			cgi_put("error", "notfound");
			return;
		case PCP_ERR_EVENTLOCKED:
			cgi_put("error", "eventlocked");
			return;
		}

	cgi_put("error", "calendar");

	if (errmsgbuf)
		free(errmsgbuf);

	/*
	** Save err msg into a static buffer, because err msg text memory
	** may go away after the handle is closed.
	*/

	p=pcp_errmsg(pcp);
	errmsgbuf=strdup(p ? p:"");
	cgi_put("pcperror", errmsgbuf);
}

struct proxy_list_entry {
	struct proxy_list_entry *next;
	char *userid;
} ;

struct proxy_update_list {
	struct proxy_list_entry *new_list;
	struct proxy_list_entry *delete_list;
} ;

static void proxy_update_list_save(const char *action,
				   const char *userid,
				   void *voidarg)
{
	struct proxy_update_list *pul=(struct proxy_update_list *)voidarg;

	struct proxy_list_entry **eptr, *e;

	if (strcmp(action, "NEW") == 0)
		eptr= &pul->new_list;
	else if (strcmp(action, "DELETE") == 0)
		eptr= &pul->delete_list;
	else
		return;

	while (*eptr && strcmp( (*eptr)->userid, userid) == 0)
		eptr= &(*eptr)->next;

	if ((e=malloc(sizeof(struct proxy_list_entry))) == NULL ||
	    (e->userid=strdup(userid)) == NULL)
	{
		if (e)
			free(e);
		fprintf(stderr, "CRIT: out of memory.\n");
		return;
	}

	e->next= *eptr;
	*eptr=e;
}

static void proxy_update_list_free(struct proxy_update_list *p)
{
	struct proxy_list_entry *e;

	while ((e=p->new_list) != NULL)
	{
		p->new_list=e->next;
		free(e->userid);
		free(e);
	}

	while ((e=p->delete_list) != NULL)
	{
		p->delete_list=e->next;
		free(e->userid);
		free(e);
	}
}

static void proxy_notify_email_msg(FILE *, struct proxy_list_entry *,
				   const char *,
				   const struct PCP_event_time *,
				   unsigned);

static void proxy_notify_email(FILE *f, struct proxy_update_list *pul,
			       const struct PCP_event_time *t,
			       unsigned tn)
{
	proxy_notify_email_msg(f, pul->new_list, "eventnotifynew.txt",
			       t, tn);
	proxy_notify_email_msg(f, pul->delete_list, "eventnotifydelete.txt",
			       NULL, 0);
}

static void dosendnotice(FILE *, FILE *, FILE *, struct proxy_list_entry *,
			 const char *, const struct PCP_event_time *,
			 unsigned);

static void proxy_notify_email_msg(FILE *f, struct proxy_list_entry *l,
				   const char *template,
				   const struct PCP_event_time *t,
				   unsigned tn)
{
	FILE *tmpfp;
	pid_t p, p2;
	int waitstat;
	int pipefd[2];
	FILE *tofp;
	const char *returnaddr=login_returnaddr();
	char subjectlabel[100];

	if (!l)
		return;

	if (fseek(f, 0L, SEEK_SET) < 0
	    || lseek(fileno(f), 0L, SEEK_SET) < 0)
	{
		fprintf(stderr, "CRIT: seek failed: %s\n", strerror(errno));
		return;
	}

	subjectlabel[0]=0;

	if ((tmpfp=open_langform(sqwebmail_content_language,
				 "eventnotifysubject.txt", 0)) != NULL)
	{
		if (fgets(subjectlabel, sizeof(subjectlabel), tmpfp) == NULL)
			subjectlabel[0]=0;
		else
		{
			char *p=strchr(subjectlabel, '\n');

			if (p) *p=0;
		}
		fclose(tmpfp);
	}
	if (subjectlabel[0] == 0)
		strcpy(subjectlabel, "[calendar]");

	if ((tmpfp=open_langform(sqwebmail_content_language, template, 0))
	    == NULL)
	{
		fprintf(stderr, "CRIT: %s: %s\n", template, strerror(errno));
		return;
	}

	signal(SIGPIPE, SIG_IGN);

	if (pipe(pipefd) < 0)
	{
		fclose(tmpfp);
		fprintf(stderr, "CRIT: pipe: %s\n", strerror(errno));
		return;
	}

	p=fork();

	if (p < 0)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		fclose(tmpfp);
		fprintf(stderr, "CRIT: fork: %s\n", strerror(errno));
		return;
	}

	if (p == 0)
	{
		dup2(pipefd[0], 0);
		close(pipefd[0]);
		close(pipefd[1]);
		execl(SENDITSH, "sendit.sh", returnaddr,
                                sqwebmail_mailboxid, NULL);
		fprintf(stderr, "CRIT: exec " SENDITSH ": %s\n", strerror(errno));
		exit(1);
	}
	close(pipefd[0]);
	if ((tofp=fdopen(pipefd[1], "w")) == NULL)
	{
		fprintf(stderr, "CRIT: exec " SENDITSH ": %s\n", strerror(errno));
	}
	else
	{
		dosendnotice(tofp, tmpfp, f, l, subjectlabel, t, tn);
	}
	fclose(tofp);
	close(pipefd[1]);
	fclose(tmpfp);

	waitstat=256;
	while ((p2=wait(&waitstat)) != p && p2 >= 0)
		;

	if (!WIFEXITED(waitstat) || WEXITSTATUS(waitstat))
		fprintf(stderr, "CRIT: event notify mail failed\n");
}

/*
** Ok, the preliminaries are out of the way, now spit it out.
*/

static void dosendnotice(FILE *tofp,	/* Pipe to sendit.sh */
			 FILE *tmpfp,	/*
					** Template file with MIME headers and
					** canned verbiage
					*/
			 FILE *eventfp,	/* Original event */
			 struct proxy_list_entry *idlist,
			 const char *subjectlabel,
			 const struct PCP_event_time *time_list,
			 unsigned n_time_list)
{
	struct rfc822hdr h;
	const char *p;
	int c;
	unsigned u;

	rfc822hdr_init(&h, 8192);

	while (rfc822hdr_read(&h, eventfp, NULL, 0) == 0)
	{
		if (strcasecmp(h.header, "From") == 0 ||
		    strcasecmp(h.header, "Date") == 0)
		{
			fprintf(tofp, "%s: %s\n", h.header,
				h.value ? h.value:"");
		}
		else if (strcasecmp(h.header, "Subject") == 0)
		{
			fprintf(tofp, "%s: %s %s\n", h.header,
				subjectlabel,
				h.value ? h.value:"");
		}
	}
	rfc822hdr_free(&h);

	p="To: ";

	while (idlist)
	{
		fprintf(tofp, "%s%s", p, idlist->userid);
		p=",\n  ";
		idlist=idlist->next;
	}
	fprintf(tofp, "\n");

	while ((c=getc(tmpfp)) != EOF)
		putc(c, tofp);

	for (u=0; u<n_time_list; u++)
	{
		char buffer[200];

		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     time_list[u].start,
				     time_list[u].end) == 0)
		{
			fprintf(tofp, "     %s\n", buffer);
		}
	}
}

static int dosave(FILE *fp, struct saveinfo *si)
{
	struct PCP *pcp=sqpcp_calendar();
	struct PCP_save_event se;
	struct PCP_new_eventid *nei;
	struct PCP_commit c;

	struct proxy_update_list pul;

	if (!pcp)
	{
		cgi_put("error", "calendar");	/* TODO: actual error */
		cgi_put("pcperror", "");
		return (-1);
	}

	memset(&se, 0, sizeof(se));
	se.write_event_fd=fileno(fp);
	se.event_participants=si->participants;
	se.n_event_participants=si->n_participants;

	if (*cgi("okconflict"))
		se.flags |= PCP_OK_CONFLICT;
	if (*cgi("okerrors"))
		se.flags |= PCP_OK_PROXY_ERRORS;

	nei=pcp_new_eventid(pcp, si->old_eventid, &se);
	if (!nei)
	{
		saveerror(pcp, NULL);
		return (-1);
	}

	memset(&c, 0, sizeof(c));
	c.event_times=si->times;
	c.n_event_times=si->n_times;
	c.flags=se.flags;

	init_save_conflict();
	c.add_conflict_callback=save_conflict;

	memset(&pul, 0, sizeof(pul));

	c.proxy_callback= &proxy_update_list_save;
	c.proxy_callback_ptr= &pul;

	if (pcp_commit(pcp, nei, &c))
	{
		proxy_update_list_free(&pul);
		saveerror(pcp, &c.errcode);
		pcp_destroy_eventid(pcp, nei);
		return (-1);
	}
	pcp_destroy_eventid(pcp, nei);
	unlink(CACHE);	/* Have it rebuilt */
	proxy_notify_email(fp, &pul, c.event_times, c.n_event_times);
	proxy_update_list_free(&pul);
	refreshcache(pcp);
	return (0);
}

/* -------------- Daily stuff --------------- */

void sqpcp_todays_date()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	char buf[100];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end)
	    || pcp_fmttime(buf, sizeof(buf), start, FMTTIME_DATE))
		return;

	print_safe(buf);
}

void sqpcp_todays_date_verbose()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	char buf[500];
	struct tm *tmptr;

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end)
	    || (tmptr=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);
}

void sqpcp_weeklylink()
{
	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%s", cgi("date"));
}

void sqpcp_monthlylink()
{
	const char *p=cgi("date");

	if (!*p)
		p=cgi("weekof");
	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%s", p);
}

#define VIEW_DAILY	0
#define	VIEW_WEEKLY	1
#define VIEW_MONTHLY	2

static void do_daily_view(struct cacherecord *, unsigned, int,
			  time_t *, time_t *);

static void show_pcp_errmsg(const char *p)
{
	printf("<pre class=\"error\">");
	output_attrencoded_oknl_fp(p, stdout);
	printf("</pre>");
}

void sqpcp_daily_view()
{
	unsigned y, m, d;
	time_t start;
	time_t end;

	struct PCP *pcp;
	struct cacherecord *recs;
	unsigned n_recs;

	if (*cgi("clearcache"))
		unlink(CACHE);

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	if ((pcp=sqpcp_calendar()) == NULL)
	{
		printf("<span class=\"error\">%s</span>", strerror(errno));
		return;
	}

	if (createcache(pcp, &recs, &n_recs, start, end))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	(void)do_daily_view(recs, n_recs, VIEW_DAILY, NULL, NULL);
	destroycache(recs, n_recs);
}

static void print_event_link_url(const char *id, const char *extra)
{
	output_scriptptrget();
	printf("%s&amp;form=eventshow&amp;eventid=", extra);
	output_urlencoded(id);
	if (*cgi("date"))
		printf("&amp;date=%s", cgi("date"));
	if (*cgi("weekof"))
		printf("&amp;weekof=%s", cgi("weekof"));
	if (*cgi("monthof"))
		printf("&amp;monthof=%s", cgi("monthof"));
}

static void print_event_link(const char *id, const char *extra,
			     const char *extra2)
{
	printf("<a href=\"");
	print_event_link_url(id, extra);
	printf("\" %s >", extra2);
}

static void do_daily_view(struct cacherecord *recs, unsigned n_recs,
			  int viewtype, time_t *start_ptr, time_t *end_ptr)
{
	unsigned i;
	int printed=0;

	printf("<table width=\"100%%\">");

	for (i=0; i<n_recs; i++)
	{
		char date1[256];
		char date2[256];

		char time1[128];
		char time2[128];

		time_t start=recs[i].start;
		time_t end=recs[i].end;

		if (start_ptr && *start_ptr >= end)
			continue;

		if (end_ptr && *end_ptr <= start)
			continue;

		if ( start_ptr && *start_ptr > start)
			start= *start_ptr;

		if ( end_ptr && *end_ptr < end)
			end= *end_ptr;

		if (pcp_fmttime(date1, sizeof(date1),
				start, FMTTIME_DATE))
			continue;
		if (pcp_fmttime(date2, sizeof(date2),
				end, FMTTIME_DATE))
				continue;

		printed=1;

		if (strcmp(date1, date2) && viewtype == VIEW_DAILY)
		{
			char timerange[512];

			if (pcp_fmttimerange(timerange, sizeof(timerange),
					     start, end))
				continue;

			printf("<tr><td align=\"left\">");
			print_event_link(recs[i].eventid, "", "class=\"dailyeventtimes\"");
			print_safe(timerange);
		}
		else
		{
			if (pcp_fmttime(time1, sizeof(time1),
					start,
					FMTTIME_TIME))
				continue;

			if (pcp_fmttime(time2,
					sizeof(time2),
					end,
					FMTTIME_TIME))
				continue;

			printf("<tr><td align=\"left\">");
			print_event_link(recs[i].eventid, "", "class=\"dailyeventtimes\"");
			print_safe(time1);
			printf("&nbsp;-&nbsp;");
			print_safe(time2);
		}
		printf("</a>");

		if (viewtype == VIEW_DAILY)
		{
			printf("</td><td width=\"100%%\">");
		}
		else
			printf("<br />");

		printf("&nbsp;&nbsp;");
		print_event_link(recs[i].eventid, "", "class=\"dailyeventsubject\"");
		print_event_subject(recs[i].flags, recs[i].subject,
				    viewtype == VIEW_DAILY ? 80:15);
		printf("</a>");
		if (viewtype != VIEW_DAILY)
			printf("<br />&nbsp;");

		printf("</td></tr>\n");
	}

	if (!printed)
	{
		printf("<tr><td align=\"center\">%s</td></tr>",
		       getarg("NOEVENTS"));
	}

	printf("</table>\n");
}

void sqpcp_prevday()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tm;
	char buf[256];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	start -= 12 * 60 * 60;

	if ((tm=localtime(&start)) == NULL)
		return;

	y=tm->tm_year + 1900;
	m=tm->tm_mon + 1;
	d=tm->tm_mday;

	if (pcp_parse_ymd(y, m, d, &start, &end)
	    || (tm=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("PREVDAY"), tm) == 0)
		return;

	printf("<a class=\"dailynextprev\" href=\"");
	output_scriptptrget();
	printf("&amp;form=eventdaily&amp;date=%04d%02d%02d\">%s</a>", y, m, d, buf);
}

void sqpcp_nextday()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tm;
	char buf[256];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	start=end;

	if ((tm=localtime(&start)) == NULL)
		return;

	y=tm->tm_year + 1900;
	m=tm->tm_mon + 1;
	d=tm->tm_mday;

	if (pcp_parse_ymd(y, m, d, &start, &end)
	    || (tm=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("NEXTDAY"), tm) == 0)
		return;

	printf("<a class=\"dailynextprev\" href=\"");
	output_scriptptrget();
	printf("&amp;form=eventdaily&amp;date=%04d%02d%02d\">%s</a>", y, m, d, buf);
}

/* -------------- Display event stuff --------------- */

struct display_retr_time_list {
	struct display_retr_time_list *next;
	time_t start;
	time_t end;
} ;

struct display_retr_participant_list {
	struct display_retr_participant_list *next;
	char *participant;
} ;

struct display_retr {
	FILE *f;
	
	struct display_retr_time_list *time_list;
	struct display_retr_participant_list *participant_list;

} ;

static void free_display_retr(struct display_retr *r)
{
}


static int save_displayed_event(struct PCP_retr *, const char *, int, void *);

static int set_status(struct PCP_retr *, int, void *);

void sqpcp_displayeventinit()
{
	struct PCP *pcp;
	const char *event_id_list[2];
	struct PCP_retr r;

	showerror();

	pcp=sqpcp_calendar();

	if (!pcp)
		return;

	event_id_list[0]=cgi("eventid");
	event_id_list[1]=0;

	init_save_conflict();
	if (*cgi("docancel"))
	{
		if (pcp_cancel(pcp, event_id_list[0], NULL))
			show_pcp_errmsg(pcp_errmsg(pcp));
		unlink(CACHE);
	}

	if (*cgi("douncancel"))
	{
		struct PCP_uncancel u;
		int flags=0;

		memset(&u, 0, sizeof(u));

		if (*cgi("okconflict"))
			flags |= PCP_OK_CONFLICT;

		u.uncancel_conflict_callback=save_conflict;
		if (pcp_uncancel(pcp, event_id_list[0], flags, &u))
		{
			saveerror(pcp, &u.errcode);
			showerror();
			if (u.errcode == PCP_ERR_CONFLICT)
				cgi_put("okconflict", "1");
		}
		unlink(CACHE);
	}

	memset(&r, 0, sizeof(r));
	r.event_id_list=event_id_list;
	r.callback_retr_status=set_status;

	if (pcp_retr(pcp, &r))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

}

static int set_status(struct PCP_retr *pcp, int status, void *dummy)
{
	cgi_put("event_cancelled", status & LIST_CANCELLED ? "1":"");
	cgi_put("event_booked", status & LIST_BOOKED ? "1":"");
	cgi_put("event_proxy", status & LIST_PROXY ? "1":"");
	return (0);
}

static int save_displayed_date(struct PCP_retr *r, time_t start, time_t end,
			       void *vp)
{
	struct display_retr *dr=(struct display_retr *)vp;

	struct display_retr_time_list **ptr, *p;

	for (ptr= &dr->time_list; *ptr; ptr=&(*ptr)->next)
		if ((*ptr)->start > start)
			break;

	if ((p=malloc(sizeof(struct display_retr_time_list))) == NULL)
		return (-1);

	p->next= *ptr;
	*ptr=p;
	p->start=start;
	p->end=end;

	return (0);
}

static int save_displayed_participants(struct PCP_retr *r, const char *address,
				       const char *dummy, void *vp)
{
	struct display_retr *dr=(struct display_retr *)vp;

	struct display_retr_participant_list **ptr, *p;

	for (ptr= &dr->participant_list; *ptr; ptr=&(*ptr)->next)
		if (strcasecmp((*ptr)->participant, address) > 0)
			break;

	if ((p=malloc(sizeof(struct display_retr_participant_list))) == NULL)
		return (-1);

	if ((p->participant=strdup(address)) == NULL)
	{
		free(p);
		return (-1);
	}
	p->next= *ptr;
	*ptr=p;
	return (0);
}

void sqpcp_displayevent()
{
	struct PCP *pcp=sqpcp_calendar();
	const char *event_id_list[2];
	struct PCP_retr r;
	struct display_retr dr;
	struct maildir_tmpcreate_info createInfo;
	struct display_retr_time_list *tl;
	struct display_retr_participant_list *pl;

	if (!pcp)
		return;

	event_id_list[0]=cgi("eventid");
	event_id_list[1]=0;

	memset(&r, 0, sizeof(r));
	memset(&dr, 0, sizeof(dr));
	r.event_id_list=event_id_list;
	r.callback_arg=&dr;
	r.callback_rfc822_func=save_displayed_event;
	r.callback_retr_date=save_displayed_date;
	r.callback_retr_participants=save_displayed_participants;

	maildir_purgemimegpg(); /* Delete previous :calendar: file */

	maildir_tmpcreate_init(&createInfo);
	createInfo.uniq=":calendar:";
	createInfo.doordie=1;

	if ((dr.f=maildir_tmpcreate_fp(&createInfo)) == NULL)
	{
		error(strerror(errno));
	}

	cgi_put(MIMEGPGFILENAME, strrchr(createInfo.tmpname, '/')+1);

	if (pcp_retr(pcp, &r))
	{
		free_display_retr(&dr);
		fclose(dr.f);
		cgi_put(MIMEGPGFILENAME, "");
		unlink(createInfo.tmpname);
		maildir_tmpcreate_free(&createInfo);
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}
	fclose(dr.f);

	printf("<table class=\"calendarevent\" align=\"center\" border=\"0\"><tr valign=\"top\"><th align=\"left\">%s</th><td>",
	       getarg("EVENT"));

	for (tl=dr.time_list; tl; tl=tl->next)
	{
		char buffer[512];

		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     tl->start, tl->end))
			continue;

		printf("<span class=\"tt\">");
		print_safe(buffer);
		printf("</span><br />\n");
	}
	printf("</td></tr>\n");

	if (dr.participant_list)
	{
		printf("<tr valign=\"top\"><th align=\"left\">%s</th><td>",
		       getarg("PARTICIPANTS"));
		for (pl=dr.participant_list; pl; pl=pl->next)
		{
			printf("<span class=\"tt\">&lt;");
			print_safe(pl->participant);
			printf("&gt;</span><br />\n");
		}
		printf("</td></tr>");
	}
	printf("</table>\n");
	folder_showmsg(INBOX "." DRAFTS, 0);
	free_display_retr(&dr);
	cgi_put(MIMEGPGFILENAME, "");
	maildir_tmpcreate_free(&createInfo);
}

static int save_displayed_event(struct PCP_retr *r,
				const char *buf, int cnt,
				void *vp)
{
	if (fwrite( buf, cnt, 1, ((struct display_retr *)vp)->f) != 1)
		return (-1);

	return (0);
}


static void back_to_summary()
{
	const char *p;

	if (*(p=cgi("monthof")) != 0)
	{
		printf("&amp;form=eventmonthly&amp;monthof=%s", p);
	}
	else if (*(p=cgi("weekof")) != 0)
	{
		printf("&amp;form=eventweekly&amp;weekof=%s", p);
	}
	else
	{
		p=cgi("date");
		printf("&amp;form=eventdaily&amp;date=%s", p);
	}
}

void sqpcp_eventbacklink()
{
	output_scriptptrget();
	back_to_summary();
}

void sqpcp_eventeditlink()
{
	const char *p=cgi("date");

	output_scriptptrget();
	printf("&amp;form=event-edit&amp;date=%s&amp;eventid=", p);
	output_urlencoded(cgi("eventid"));
}

void sqpcp_eventdeletelink()
{
	const char *p=cgi("date");

	output_scriptptrget();
	printf("&amp;eventid=");

	output_urlencoded(cgi("eventid"));
	printf("&amp;form=eventdelete&amp;date=%s", p);
}

void sqpcp_eventcanceluncancellink()
{
	print_event_link_url(cgi("eventid"), *cgi("event_cancelled")
			 ? (*cgi("okconflict") ?
			    "&amp;okconflict=1&amp;douncancel=1"
			    :"&amp;douncancel=1")
			     :"&amp;docancel=1");
}

void sqpcp_eventcanceluncancelimage()
{
	printf("%s", getarg(*cgi("event_cancelled")
			    ? "UNCANCELIMAGE":"CANCELIMAGE"));
}

void sqpcp_eventcanceluncanceltext()
{
	printf("%s", getarg(*cgi("event_cancelled")
			    ? "UNCANCELTEXT":"CANCELTEXT"));
}

void sqpcp_deleteeventinit()
{
	sqpcp_displayeventinit();
	if (*cgi("event_proxy"))
		printf("%s", getarg("PROXYWARN"));
}

static int save_orig_headers(struct PCP_retr *pcp,
			     const char *h, const char *v, void *vp)
{
	FILE *fp=(FILE *)vp;

	if (strcasecmp(h, "Date"))
		fprintf(fp, "%s: %s\n", h, v);
	return (0);
}


void sqpcp_dodelete()
{
	struct PCP *pcp=sqpcp_calendar();
	struct PCP_retr r;
	const char *event_list_ary[2];
	struct PCP_delete del;
	struct proxy_update_list pul;
	FILE *tmpfp;

	if (!pcp)
		return;

	memset(&del, 0, sizeof(del));
	del.id=cgi("eventid");

	memset(&r, 0, sizeof(r));
	event_list_ary[0]=del.id;
	event_list_ary[1]=NULL;
	r.event_id_list=event_list_ary;
	r.callback_headers_func=save_orig_headers;

	tmpfp=tmpfile();
	if (!tmpfp)
		enomem();
	r.callback_arg=tmpfp;
	pcp_retr(pcp, &r);

	{
		time_t t;

		time(&t);

		fprintf(tmpfp, "Date: %s\n\n", rfc822_mkdate(t));
	}

	memset(&pul, 0, sizeof(pul));
	del.proxy_callback=&proxy_update_list_save;
	del.proxy_callback_ptr=&pul;

	if (pcp_delete(pcp, &del))
	{
		saveerror(pcp, &del.errcode);
		output_form("eventdelete.html");
	}
	else
	{
		proxy_notify_email(tmpfp, &pul, NULL, 0);
	}
	fclose(tmpfp);
	proxy_update_list_free(&pul);
	unlink(CACHE);
	output_form("eventdaily.html");
}

/* ------------- Bring in an event to edit -------------------------- */

static int doeventedit(struct PCP *, int);

int sqpcp_eventedit()
{
	struct PCP *pcp=sqpcp_calendar();
	int newdraftfd;
	char *draftfilename;

	if (!pcp)
		return (-1);

	newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, &draftfilename);
	if (doeventedit(pcp, newdraftfd))
	{
		maildir_deletenewmsg(newdraftfd, INBOX "." DRAFTS, draftfilename);
	}
	else if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 1, 0))
	{
		cgi_put("error", "quota");
	}
	else
	{
		static char *filenamebuf=0;

		if (filenamebuf)
			free(filenamebuf);

		filenamebuf=draftfilename;
		cgi_put("draftmessage", filenamebuf);
		return (0);
	}
	free(draftfilename);
	return (-1);
}

struct getedit_info {
	int fd;
	int flag;

	int in_headers;
	int sol;
	int skiph;

} ;

static int get_date(struct PCP_retr *r, time_t start, time_t end, void *vp)
{
	char buf[NUMBUFSIZE];
	struct getedit_info *ge=(struct getedit_info *)vp;

	maildir_writemsgstr(ge->fd, "X-Event-Time: ");
	maildir_writemsgstr(ge->fd, libmail_str_time_t(start, buf));
	maildir_writemsgstr(ge->fd, " ");
	maildir_writemsgstr(ge->fd, libmail_str_time_t(end, buf));
	maildir_writemsgstr(ge->fd, "\n");

	return (0);
}

static int get_participants(struct PCP_retr *r, const char *address,
			    const char *dummy, void *vp)
{
	struct getedit_info *ge=(struct getedit_info *)vp;

	maildir_writemsgstr(ge->fd, "X-Event-Participant: ");
	maildir_writemsgstr(ge->fd, address);
	maildir_writemsgstr(ge->fd, "\n");

	return (0);
}

static int get_msgtext(struct PCP_retr *r, const char *ptr, int n, void *vp)
{
	struct getedit_info *ge=(struct getedit_info *)vp;

	/* We want to drop all X headers when we read in this event */
	/* Also, drop CRs */

	ge->flag=1;

	while (n)
	{
		int i;

		if (!ge->in_headers)	/* Write out msg body */
		{
			while (n)
			{
				if (*ptr == '\r')
				{
					++ptr;
					--n;
					continue;
				}

				for (i=0; i<n; i++)
					if (ptr[i] == '\r')
						break;

				maildir_writemsg(ge->fd, ptr, i);
				ptr += i;
				n -= i;
			}
			break;
		}

		if (*ptr == '\r')
		{
			++ptr;
			--n;
			continue;
		}

		if (*ptr == '\n')
		{
			if (!ge->skiph)
				maildir_writemsgstr(ge->fd, "\n");

			ge->skiph=0;
			if (ge->sol)
				ge->in_headers=0;	/* End of headers */
			ge->sol=1;
			++ptr;
			--n;
			continue;
		}

		if (ge->sol && (*ptr == 'x' || *ptr == 'X'))
			ge->skiph=1;

		for (i=0; i<n; i++)
			if (ptr[i] == '\r' || ptr[i] == '\n')
				break;

		if (!ge->skiph)	/* Skip X- header */
			maildir_writemsg(ge->fd, ptr, i);
		ptr += i;
		n -= i;
	}
	return (0);
}

static int doeventedit(struct PCP *pcp, int fd)
{
	const char *p=cgi("eventid");
	struct PCP_retr r;
	struct getedit_info ge;

	const char *eventid[2];

	if (!p || !*p)
		return (-1);

	maildir_writemsgstr(fd, "X-Event: 1\nX-Old-EventId: ");
	maildir_writemsgstr(fd, p);
	maildir_writemsgstr(fd, "\n");

	memset(&r, 0, sizeof(r));
	eventid[0]=p;
	eventid[1]=NULL;

	r.event_id_list=eventid;

	ge.fd=fd;
	ge.flag=0;
	r.callback_arg=&ge;

	r.callback_retr_date=get_date;
	r.callback_retr_participants=get_participants;

	if (pcp_retr(pcp, &r))
	{
		saveerror(pcp, &r.errcode);
		return (-1);
	}

	memset(&r, 0, sizeof(r));
	eventid[0]=p;
	eventid[1]=NULL;

	r.event_id_list=eventid;

	ge.fd=fd;
	ge.flag=0;
	r.callback_arg=&ge;

	ge.in_headers=1;
	ge.sol=1;
	ge.skiph=0;

	r.callback_rfc822_func=get_msgtext;
	if (pcp_retr(pcp, &r))
	{
		saveerror(pcp, &r.errcode);
		return (-1);
	}

	if (!ge.flag)
	{
		cgi_put("error", "notfound");
		return (-1);
	}

	return (0);
}

/* ------ Weekly stuff ------- */

static void prevday(time_t *tm)
{
	struct tm *tmptr;
	time_t t= *tm - 12 * 60 * 60;

	if ((tmptr=localtime(&t)) == NULL)
	{
		enomem();
		return;
	}

	if (pcp_parse_ymd(tmptr->tm_year + 1900, tmptr->tm_mon + 1,
			  tmptr->tm_mday, tm, &t))
	{
		enomem();
	}
}

static void nextday(time_t *tm)
{
	struct tm *tmptr;
	time_t t= *tm + 36 * 60 * 60;

	if ((tmptr=localtime(&t)) == NULL)
	{
		enomem();
		return;
	}

	if (pcp_parse_ymd(tmptr->tm_year + 1900, tmptr->tm_mon + 1,
			  tmptr->tm_mday, tm, &t))
	{
		enomem();
	}
}

static time_t get_start_of_week()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tmptr;
	int i;

	if (sscanf(cgi("weekof"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
	{
		time(&start);
		if ((tmptr=localtime(&start)) == NULL)
		{
			enomem();
			return (0);
		}
		y=tmptr->tm_year + 1900;
		m=tmptr->tm_mon + 1;
		d=tmptr->tm_mday;

		if (pcp_parse_ymd(y, m, d, &start, &end))
		{
			enomem();
			return (0);
		}
	}

	for (i=0; i<7; i++)
	{
		tmptr=localtime(&start);
		if (!tmptr)
			enomem();

		if (tmptr->tm_wday == pref_startofweek)
			break;

		prevday(&start);
	}

	return (start);
}

void sqpcp_show_cal_week()
{
	time_t start=get_start_of_week();
	struct tm *tmptr;
	char buf[512];

	if ((tmptr=localtime(&start)) == NULL)
		return;

	if (strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);
}

void sqpcp_show_cal_nextweek()
{
	time_t start=get_start_of_week();
	int i;
	struct tm *tmptr;

	for (i=0; i<7; i++)
		nextday(&start);

	if ((tmptr=localtime(&start)) == NULL)
		return;

	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%04d%02d%02d",
	       tmptr->tm_year + 1900, tmptr->tm_mon+1, tmptr->tm_mday);

}

void sqpcp_show_cal_prevweek()
{
	time_t start=get_start_of_week();
	int i;
	struct tm *tmptr;

	for (i=0; i<7; i++)
		prevday(&start);

	if ((tmptr=localtime(&start)) == NULL)
		return;

	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%04d%02d%02d",
	       tmptr->tm_year + 1900, tmptr->tm_mon+1, tmptr->tm_mday);

}

void sqpcp_displayweek()
{
	int i;
	time_t start=get_start_of_week(), save_start;
	time_t end;
	struct PCP *pcp=sqpcp_calendar();
	struct cacherecord *recs;
	unsigned n_recs;

	if (!pcp)
		return;

	save_start=start;

	for (i=0; i<7; i++)
	{
		nextday(&start);
	}

	if (createcache(pcp, &recs, &n_recs, save_start, start))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	printf("<table align=\"center\" border=\"0\" class=\"weekly-border\""
	       " cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\">"
	       "<tr><td>\n");
	printf("<table border=\"1\" class=\"weekly-bg\" cellspacing=\"0\" cellpadding=\"10\" width=\"100%%\">"
	       "<tr valign=\"top\">");

	start=save_start;
	for (i=0; i<7; i++)
	{
		const char *p;
		struct tm *tmptr;

		printf("<td width=\"14%%\">");
		p=pcp_wdayname((i + pref_startofweek) % 7);
		printf("<div align=\"center\" class=\"weekly-day\">");
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=eventdaily&amp;date=");

		tmptr=localtime(&start);

		if (tmptr)
		{
			printf("%04d%02d%02d", tmptr->tm_year + 1900,
			       tmptr->tm_mon+1,
			       tmptr->tm_mday);
		}
		printf("\">");
		print_safe(p);
		printf("</a><hr width=\"70%%\" />");
		printf("</div>\n");

		end=start;
		nextday(&end);


		do_daily_view(recs, n_recs, VIEW_WEEKLY, &start, &end);

		start=end;
		printf("</td>");
	}
	destroycache(recs, n_recs);
	printf("</tr></table>\n");
	printf("</td></tr></table>\n");
}

/* ---------------- Monthly view ---------------- */

static time_t get_start_of_month()
{
	unsigned y, m;
	time_t start;
	time_t end;
	struct tm *tmptr;

	if (sscanf(cgi("monthof"), "%4u%2u", &y, &m) != 2
	    || pcp_parse_ymd(y, m, 1, &start, &end))
	{
		time(&start);
		if ((tmptr=localtime(&start)) == NULL)
		{
			enomem();
			return (0);
		}
		y=tmptr->tm_year + 1900;
		m=tmptr->tm_mon + 1;

		if (pcp_parse_ymd(y, m, 1, &start, &end))
		{
			enomem();
			return (0);
		}
	}

	return (start);
}

void sqpcp_show_cal_month()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	char buf[512];

	if ((tmptr=localtime(&start)) == NULL)
		return;

	if (strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);

}

void sqpcp_show_cal_nextmonth()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	int m, y;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	++m;
	if (m > 12)
	{
		m=1;
		++y;
	}

	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%04d%02d01", y, m);
}

void sqpcp_show_cal_prevmonth()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	int m, y;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	--m;
	if (m <= 0)
	{
		m=12;
		--y;
	}

	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%04d%02d01", y, m);
}

void sqpcp_displaymonth()
{
	int i, y, m;
	time_t start=get_start_of_month(), save_start;
	time_t end;
	struct PCP *pcp=sqpcp_calendar();
	struct cacherecord *recs;
	unsigned n_recs;
	struct tm *tmptr;

	if (!pcp)
		return;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	if (++m > 12)
	{
		m=1;
		++y;
	}

	if (pcp_parse_ymd(y, m, 1, &end, &save_start))
		return;

	if (createcache(pcp, &recs, &n_recs, start, end))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	printf("<table align=\"center\" border=\"0\" class=\"monthly-border\""
	       " cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\">"
	       "<tr><td>\n");
	printf("<table border=\"1\" class=\"monthly-bg\" cellspacing=\"0\" cellpadding=\"10\" width=\"100%%\">"
	       "<tr valign=\"top\">");

	for (i=0; i<7; i++)
	{
		printf("<td width=\"14%%\" align=\"center\" class=\"monthly-day\">");
		print_safe(pcp_wdayname((i + pref_startofweek) % 7));
		printf("</td>");
	}
	printf("</tr>\n");

	while (start < end)
	{
		printf("<tr valign=\"top\">\n");

		for (i=0; i<7; i++)
		{
			struct tm *tmptr;
			time_t next_day;

			tmptr=localtime(&start);

			if (tmptr->tm_wday != (i + pref_startofweek) % 7
			    || start >= end)
			{
				printf("<td width=\"14%%\" class=\"monthly-bg-othermonth\">&nbsp;</td>");
				continue;
			}

			printf("<td width=\"14%%\">");


			printf("<a href=\"");
			output_scriptptrget();
			printf("&amp;form=eventdaily&amp;date=");

			if (tmptr)
			{
				printf("%04d%02d%02d", tmptr->tm_year + 1900,
				       tmptr->tm_mon+1,
				       tmptr->tm_mday);
			}
			printf("\" class=\"monthly-day\">");

			printf("%2d", tmptr->tm_mday);
			printf("</a><div align=\"center\"><hr width=\"70%%\" /></div>\n");

			next_day=start;
			nextday(&next_day);

			do_daily_view(recs, n_recs, VIEW_MONTHLY, &start,
				      &next_day);

			start=next_day;
			printf("</td>\n");
		}
		printf("</tr>\n");
	}
	destroycache(recs, n_recs);
	printf("</table>\n");
	printf("</td></tr></table>\n");
}

/* -------------------------------------------------------------------- */
/* Access control lists */

static void addacl(const char *);

struct acl_list {
	struct acl_list *next;
	char *addr;
	char *name;
	int flags;
} ;

static int listacl(const char *a, int f, void *vp)
{
	struct acl_list **p=(struct acl_list **)vp, *q;

	if ((q=malloc(sizeof(struct acl_list))) == NULL)
		return (-1);
	memset(q, 0, sizeof(*q));
	if ((q->addr=strdup(a)) == NULL)
	{
		free(q);
		return (-1);
	}

	q->flags=f;

	while (*p)
	{
		if (strcasecmp( (*p)->addr, a) > 0)
			break;
		p= &(*p)->next;
	}

	q->next= *p;
	*p=q;
	return (0);
}

static int save_listacl_names(const char *addr, const char *name,
			      void *vp)
{
	struct acl_list *p=(struct acl_list *)vp;

	for ( ; name && p; p=p->next)
		if (strcasecmp(p->addr, addr) == 0 && p->name == 0)
			p->name=strdup(name);
	return (0);
}

void sqpcp_eventacl()
{
	const char *p;
	struct acl_list *acl_list=NULL;
	struct PCP *pcp;
	struct acl_list *pp;

	if (!sqpcp_has_groupware())
		return;

	p=cgi("addemail");

	while (*p && isspace((int)(unsigned char)*p))
		++p;

	if (!*p)
		p=cgi("addressbookname");

	if (*p)
		addacl(p);

	pcp=sqpcp_calendar();

	if (!pcp)
		return;

	if (*(p=cgi("remove")))
	{
		if (pcp_acl(pcp, p, 0))
		{
			saveerror(pcp, NULL);
			showerror();
		}
	}

	if (pcp_list_acl(pcp, listacl, &acl_list))
	{
		saveerror(pcp, NULL);
		showerror();
	}
	else if (ab_get_nameaddr(save_listacl_names, acl_list))
	{
		int dummy=PCP_ERR_SYSERR;

		saveerror(NULL, &dummy);
		showerror();
	}
	else if (acl_list)
	{
		printf("<table align=\"center\">\n");

		for (pp=acl_list; pp; pp=pp->next)
		{
			printf("<tr><td align=\"right\"><span class=\"tt\">");
			if (pp->addr)
				ab_nameaddr_show(pp->name, pp->addr);

			printf("</span></td><td>-");
			if (pp->flags & PCP_ACL_MODIFY)
				printf("&nbsp;%s", getarg("MODIFY"));
			if (pp->flags & PCP_ACL_CONFLICT)
				printf("&nbsp;%s", getarg("CONFLICT"));
			printf("</td><td><a href=\"");
			output_scriptptrget();
			printf("&amp;form=eventacl&amp;remove=");
			output_urlencoded(pp->addr);
			printf("\">%s</a></td></tr>\n", getarg("REMOVE"));
		}
		printf("</table>\n");
	}

	while ((pp=acl_list) != NULL)
	{
		acl_list=pp->next;
		if (pp->addr)
			free(pp->addr);
		if (pp->name)
			free(pp->name);
		free(pp);
	}
}

static void addacl(const char *p)
{
	int flags=0;
	struct PCP *pcp;

	if (strchr(p, '@') == NULL)
	{
		const char *mhn=myhostname();
		char *q=malloc(strlen(p)+strlen(mhn)+2);

		if (!q)
			enomem();

		strcat(strcat(strcpy(q, p), "@"), mhn);
		addacl(q);
		free(q);
		return;
	}

	if (*cgi("aclMODIFY"))
		flags |= PCP_ACL_MODIFY;

	if (*cgi("aclCONFLICT"))
		flags |= PCP_ACL_CONFLICT;

	if (!flags)
		return;	/* Noop */

	pcp=sqpcp_calendar();

	if (!pcp)
	{
		int xerror=PCP_ERR_SYSERR;

		saveerror(NULL, &xerror);
		showerror();
		return;
	}

	if (!pcp_has_acl(pcp))
	{
		printf("%s\n", getarg("NOACL"));
		return;
	}

	if (pcp_acl(pcp, p, flags))
	{
		saveerror(pcp, NULL);
		showerror();
		return;
	}
}
