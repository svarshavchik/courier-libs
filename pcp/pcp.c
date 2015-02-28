/*
** Copyright 2001-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <langinfo.h>
#if HAVE_TERMIOS_H
#include <termios.h>
#endif
#include <fcntl.h>
#include <locale.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pwd.h>
#include <rfc822/rfc822.h>
#include <rfc822/rfc2047.h>
#include <rfc822/rfc822hdr.h>
#include <rfc2045/rfc2045.h>
#include <rfc2045/rfc2045charset.h>
#include <courier-unicode.h>
#include <numlib/numlib.h>

#define PCP_ERRMSG(s) gettext(s)

#include "pcp.h"
#include "calendardir.h"

#define FLAG_LIST_EVENT_ID	1

static const char *charset=RFC2045CHARSET;

void rfc2045_enomem()
{
	fprintf(stderr, "Out of memory.\n");
	exit(1);
}

PCP_STRERROR

static time_t parse_datetime(int *argn, int argc, char **argv)
{
	struct pcp_parse_datetime_info pdi;

	memset(&pdi, 0, sizeof(pdi));

	pdi.today_name=gettext("today");
	pdi.tomorrow_name=gettext("tomorrow");

	return (pcp_parse_datetime(argn, argc, argv, &pdi));
}

static const char *from_s()
{
	return (gettext("from"));
}

static const char *to_s()
{
	return (gettext("to"));
}

static const char *event_s()
{
	return (gettext("event"));
}

static const char *on_s()
{
	return (gettext("on"));
}

static void error(struct PCP *pcp, int n, const char *s)
{
	const char *p;

	p=pcp_strerror(n);

	if (p)
		fprintf(stderr, "%s: %s\n", s, p);
	else
		fprintf(stderr, "%s: %s\n", s, pcp_errmsg(pcp));
}

struct event_time_list {
	struct event_time_list *next;
	struct PCP_event_time thetime;
} ;

static int save_time(time_t start, time_t end, void *vp)
{
	struct event_time_list **ptr=(struct event_time_list **)vp;
	struct event_time_list *etl=
		(struct event_time_list *)
		malloc(sizeof(struct event_time_list));

	if (!etl)
	{
		perror("malloc");
		exit(1);
	}

	for (; *ptr; ptr=&(*ptr)->next)
				;
	*ptr=etl;
	etl->next=NULL;

	etl->thetime.start=start;
	etl->thetime.end=end;
	return (0);
}


struct participant_list {
	struct participant_list *next;
	struct PCP_event_participant participant;
} ;

static void usage();

static struct passwd *do_getpw()
{
	struct passwd *pw=getpwuid(getuid());

	if (!pw)
	{
		perror("getpwuid");
		exit(1);
	}
	return(pw);
}

struct PCP *open_calendar()
{
	struct PCP *pcp;
	struct passwd *pw=do_getpw();
	char *p;
	const char *cp;
	FILE *fp;
	char authtoken[1024];

	p=malloc(strlen(pw->pw_dir)+sizeof("/.pcplogin"));
	if (!p)
	{
		perror("malloc");
		exit(1);
	}

	strcat(strcpy(p, pw->pw_dir), "/.pcplogin");

	if ((fp=fopen(p, "r")) != NULL)
	{
		if (fgets(authtoken, sizeof(authtoken)-2, fp) != NULL)
		{
			char *q=authtoken+strlen(authtoken);

			if (fgets(q, sizeof(authtoken)- (q-authtoken), fp)
			    != NULL)
			{
				char *userid=strtok(authtoken, "\n");
				const char *cp;

				if (userid)
				{
					char *password=strtok(NULL, "\n");
					char *errmsg;

					fclose(fp);

					pcp=pcp_reopen_server(userid,
							      password,
							      &errmsg);

					if (!pcp)
					{
						printf(gettext("LOGIN ERROR:\n%s\n"),
						       errmsg ?
						       errmsg:strerror(errno));
						if (errmsg)
							free(errmsg);
						exit(1);
					}

					cp=pcp_authtoken(pcp);

					if (!cp)
					{
						fprintf(stderr, gettext("ERROR: Unable to obtain authentication token from the server.\n"));
						exit(1);
					}

					umask(077);

					if ((fp=fopen(p, "w")) == NULL)
					{
						perror(p);
						exit(1);
					}

					fprintf(fp, "%s\n%s\n",
						userid,
						cp);
					if (fflush(fp) || ferror(fp)
					    || fclose(fp))
					{
						perror(p);
						unlink(p);
						exit(1);
					}
					free(p);
					return (pcp);
				}
			}
		}
		fclose(fp);
		unlink(p);
	}

	if ((cp=getenv("PCPDIR")) != NULL && *cp)
	{
		free(p);
		p=strdup(cp);
		if (!p)
		{
			perror("strdup");
			exit(1);
		}
	}
	else
	{
		strcat(strcpy(p, pw->pw_dir), "/.pcp");
	}

	if (mkdir(p, 0700) == 0)
	{
		fprintf(stderr, "pcp: created %s\n", p);
	}

	pcp=pcp_open_dir(p, pw->pw_name);
	free(p);

	if (!pcp)
	{
		perror("pcp_open_dir");
		exit(1);
	}

	if (pcp_cleanup(pcp))
	{
		perror("pcp_cleanup");
		pcp_close(pcp);
		return (NULL);
	}
	return (pcp);
}

/**** Add stuff to the calendar ****/

struct add_info {
	int (*add_func)(struct add_info *);
	const char *add_charset;
	char *add_subject;

	char *bufptr;
	int bufleft;
	char buffer[BUFSIZ];

} ;

static int add_info_callback(char *, int, void *);

static int show_conflict(const char *event_id,
			 time_t from,
			 time_t to,
			 const char *addr,
			 void *dummy)
{
	char buf[500];

	if (pcp_fmttimerange(buf, sizeof(buf), from, to) < 0)
		buf[0]=0;

	fprintf(stderr, gettext("Conflict: %s\n    (%s)\n"),
		buf, event_id);
	return (0);
}

static void add(int argn, int argc, char **argv, int flags, const char *old,
		struct add_info *add_info)
{
	struct event_time_list *list=NULL;
	struct participant_list *book=NULL;

	const char *subject=NULL;
	struct PCP *pcp;
	struct PCP_new_eventid *nei;
	struct PCP_commit commit_info;
	struct PCP_save_event add_event_info;

	while (argn < argc)
	{
		if (strcmp(argv[argn], gettext("with")) == 0)
		{
			struct participant_list **ptr;

			++argn;
			if (argn >= argc)
				usage();

			for (ptr= &book; *ptr; ptr= &(*ptr)->next)
				;

			if ((*ptr=malloc(sizeof(struct participant_list)))
			    == NULL)
			{
				perror("malloc");
				exit(1);
			}
			(*ptr)->participant.address=argv[argn];
			(*ptr)->next=NULL;
			++argn;
			continue;
		}

		if (strcasecmp(argv[argn], from_s()) == 0)
		{
			time_t from_time, to_time;

			++argn;
			if ((from_time=parse_datetime(&argn, argc, argv))
			    == 0)
				usage();

			if (argn < argc && strcasecmp(argv[argn], to_s()) == 0)
			{
				++argn;
				if ((to_time=parse_datetime(&argn, argc,
							    argv)) == 0)
					usage();

				if (from_time > to_time)
					usage();
			}
			else
			{
				to_time=from_time + 60 * 60;
			}

			if (argn < argc && strcmp(argv[argn],
						  gettext("until")) == 0)
			{
				++argn;

				if (pcp_parse_datetime_until(from_time,
							     to_time,
							     &argn,
							     argc,
							     argv,

							     PCP_RECURRING_WEEKLY,
							     &save_time,
							     &list))
					usage();
			}
			else
			{
				save_time(from_time, to_time, &list);
			}
			continue;
		}

		if (strcmp(argv[argn], "subject") == 0)
		{
			if (subject || ++argn >= argc)
				usage();
			subject=argv[argn++];
			continue;
		}
		usage();
	}

	memset(&commit_info, 0, sizeof(commit_info));
	commit_info.flags=flags;

	{
		struct event_time_list *p;
		struct PCP_event_time *q=0;

		for (p=list; p; p=p->next)
			++commit_info.n_event_times;

		if (!commit_info.n_event_times)
			usage();

		if ((commit_info.event_times=q=
		     calloc(commit_info.n_event_times,
			    sizeof(*commit_info.event_times))) == NULL)
		{
			perror("malloc");
			exit(1);
		}
		commit_info.n_event_times=0;
		for (p=list; p; p=p->next)
			q[commit_info.n_event_times++]=p->thetime;
	}

	memset(&add_event_info, 0, sizeof(add_event_info));
	{
		struct participant_list *p;
		struct PCP_event_participant *q=0;

		for (p=book; p; p=p->next)
			++add_event_info.n_event_participants;

		if (add_event_info.n_event_participants &&
		    (add_event_info.event_participants=q=
		     calloc(add_event_info.n_event_participants,
			    sizeof(*add_event_info.event_participants)))
		    == NULL)
		{
			perror("malloc");
			exit(1);
			return;
		}
		add_event_info.n_event_participants=0;

		for (p=book; p; p=p->next)
			q[add_event_info.n_event_participants++]
				=p->participant;
	}

	add_event_info.write_event_func=add_info_callback;
	add_event_info.write_event_func_misc_ptr=add_info;

	pcp=open_calendar();

	nei=pcp_new_eventid(pcp, old, &add_event_info);

	if (!nei)
	{
		pcp_close(pcp);
		perror("pcp_new_event_id");
		exit(1);
	}

	commit_info.add_conflict_callback= &show_conflict;
	if (pcp_commit(pcp, nei, &commit_info))
	{
		error(pcp, commit_info.errcode, "pcp_commit");
		pcp_destroy_eventid(pcp, nei);
		pcp_close(pcp);
		exit(1);
	}

	printf(gettext("Created event %s\n"), nei->eventid);
	pcp_destroy_eventid(pcp, nei);
	pcp_close(pcp);
	exit (0);
}

static int add_info_callback(char *buf, int cnt, void *p)
{
	struct add_info *ai=(struct add_info *)p;
	int n;

	if (!ai->bufleft)
	{
		n= (*ai->add_func)(ai);

		if (n < 0)
			return (n);
	}
	n=0;
	while (cnt && ai->bufleft)
	{
		*buf++ = *ai->bufptr++;
		--ai->bufleft;
		--cnt;
		++n;
	}
	return (n);
}

static int add_read_stdin(struct add_info *p)
{
	int n=read(0, p->buffer, sizeof(p->buffer));

	p->bufptr=p->buffer;
	if (n < 0)
		return (n);
	p->bufleft=n;
	return (0);
}

static int add_read_stdin_prompt(struct add_info *p)
{
	p->add_func=add_read_stdin;

	printf(gettext("\nEnter event information, terminate with EOF (usually CTRL-D)\n\n"));
	return (add_read_stdin(p));
}

static int add_read_mime(struct add_info *p);

static int add_read_subject(struct add_info *p)
{
	strcpy(p->buffer, "Subject: ");
	strncat(p->buffer, p->add_subject, sizeof(p->buffer)-100);
	strcat(p->buffer, "\n");
	p->add_func= &add_read_mime;
	p->bufptr=p->buffer;
	p->bufleft=strlen(p->buffer);
	return (0);
}

static int add_read_mime(struct add_info *p)
{
	strcpy(p->buffer, "Mime-Version: 1.0\n"
	       "Content-Type: text/plain; charset=\"");
	strcat(p->buffer, p->add_charset);
	strcat(p->buffer, "\"\nContent-Transfer-Encoding: 8bit\n\n");
	p->add_func= add_read_stdin_prompt;
	p->bufptr=p->buffer;
	p->bufleft=strlen(p->buffer);
	return (0);
}

/**** List calendar ****/

static int list_callback_saveindex(struct PCP_list_all *, void *);

struct listinfo {
	time_t list_from;
	time_t list_to;
	const char *list_event_id;
	int cnt;
	struct listinfo_index *index_list;
	unsigned i_cnt;
} ;

struct listinfo_index {
	struct listinfo_index *next;
	size_t from;
	size_t to;
	char *subject;
	char *eventid;
	int status;
} ;

static int indexcmp(const void *a, const void *b)
{
	struct listinfo_index *ap=*(struct listinfo_index * const *)a;
	struct listinfo_index *bp=*(struct listinfo_index * const *)b;

	return ( ap->from < bp->from ? -1:
		 ap->from > bp->from ? 1:
		 ap->to < bp->to ? -1:
		 ap->to > bp->to ? 1:0);
}

static void doretr(const char *);

static int save_retr_headers(struct PCP_retr *, const char *,
			     const char *, void *);
static int save_retr_status(struct PCP_retr *, int, void *);

static void dump_rfc822_hdr(const char *ptr, size_t cnt,
			    void *dummy)
{
	fwrite(ptr, cnt, 1, stdout);
}


static void list(int argn, int argc, char **argv, int flags)
{
	struct listinfo listinfo;
	int all_flag=0;
	struct PCP *pcp;
	struct PCP_list_all list_all;

	memset(&listinfo, 0, sizeof(listinfo));
	memset(&list_all, 0, sizeof(list_all));

	while (argn < argc)
	{
		if (strcasecmp(argv[argn], gettext("all")) == 0)
		{
			all_flag=1;
			++argn;
			continue;
		}

		if (strcasecmp(argv[argn], on_s()) == 0)
		{
			++argn;
			if (listinfo.list_from ||
			    listinfo.list_to ||
			    (listinfo.list_from=
			     listinfo.list_to=
			     parse_datetime(&argn, argc, argv)) == 0)
				usage();
			continue;
		}

		if (strcasecmp(argv[argn], from_s()) == 0)
		{
			++argn;
			if (listinfo.list_from != 0 ||
			    (listinfo.list_from=
			     parse_datetime(&argn, argc, argv)) == 0)
				usage();
			continue;
		}

		if (strcasecmp(argv[argn], to_s()) == 0)
		{
			++argn;
			if (listinfo.list_to != 0 ||
			    (listinfo.list_to=
			     parse_datetime(&argn, argc, argv)) == 0)
				usage();
			continue;
		}

		if (strcasecmp(argv[argn], event_s()) == 0)
		{
			++argn;
			if (argn >= argc || listinfo.list_event_id)
				usage();
			listinfo.list_event_id=argv[argn++];
			continue;
		}
		usage();
	}

	/* If neither start-end, nor "all" is specified, list events
	   for today */

	if (!all_flag && listinfo.list_from == 0 &&
	    listinfo.list_to == 0 && !listinfo.list_event_id)
	{
		time_t t;
		struct tm *tmptr;

		time(&t);
		tmptr=localtime(&t);

		pcp_parse_ymd(tmptr->tm_year + 1900,
			      tmptr->tm_mon + 1,
			      tmptr->tm_mday,
			      &listinfo.list_from,
			      &listinfo.list_to);
	}

	if ((listinfo.list_from || listinfo.list_to)
	    && listinfo.list_event_id)
	usage();

	if (listinfo.list_event_id)
	{
		doretr(listinfo.list_event_id);
		return;
	}

	pcp=open_calendar();

	list_all.callback_func=list_callback_saveindex;
	list_all.callback_arg= &listinfo;
	list_all.list_from=listinfo.list_from;
	list_all.list_to=listinfo.list_to;
	listinfo.i_cnt=0;
	if (pcp_list_all(pcp, &list_all))
	{
		perror("pcp_xlist");
		pcp_close(pcp);
		exit(1);
	}

	/* Show event index */

	if (listinfo.index_list)
	{
		unsigned cnt=0, i, maxl;
		struct listinfo_index *p, **ary;
		const char **event_id_list;
		struct PCP_retr r;

		for (p=listinfo.index_list; p; p=p->next)
			++cnt;

		event_id_list=(const char **)
			malloc(sizeof(const char *)*(cnt+1));
		if (!event_id_list)
		{
			perror("malloc");
			exit(1);
		}
		cnt=0;

		for (p=listinfo.index_list; p; p=p->next)
			event_id_list[cnt++]=p->eventid;
		event_id_list[cnt]=0;
		memset(&r, 0, sizeof(r));
		r.callback_arg=&listinfo;

		r.callback_retr_status=save_retr_status;
		r.callback_arg=&listinfo;
		r.event_id_list=event_id_list;

		if (pcp_retr(pcp, &r))
		{
			error(pcp, r.errcode, "pcp_retr");
			pcp_close(pcp);
			exit(1);
		}

		r.callback_headers_func=save_retr_headers;

		if (pcp_retr(pcp, &r))
		{
			error(pcp, r.errcode, "pcp_retr");
			pcp_close(pcp);
			exit(1);
		}

		free(event_id_list);

		ary=(struct listinfo_index **)
			malloc(sizeof(struct listinfo_index *)
			       * listinfo.i_cnt);

		if (!ary)
		{
			perror("malloc");
			pcp_close(pcp);
			exit(1);
		}

		cnt=0;
		for (p=listinfo.index_list; p; p=p->next)
			ary[cnt++]=p;
		qsort(ary, cnt, sizeof(ary[0]), indexcmp);

		maxl=20;

		for (i=0; i<cnt; i++)
		{
			char fromto[500];

			if (pcp_fmttimerange(fromto, sizeof(fromto),
					     ary[i]->from,
					     ary[i]->to) == 0 &&
			    strlen(fromto) > maxl)
				maxl=strlen(fromto);
		}

		for (i=0; i<cnt; i++)
		{
			char fromto[500];

			if (pcp_fmttimerange(fromto, sizeof(fromto),
					     ary[i]->from,
					     ary[i]->to) < 0)
				strcpy(fromto, "******");
			printf("%-*s %s%s", (int)maxl, fromto,
			       ary[i]->status & LIST_CANCELLED
			       ? gettext("CANCELLED: "):"",
			       ary[i]->status & LIST_BOOKED
			       ? gettext("(event not yet commited) "):"");

			if (rfc822_display_hdrvalue("subject",
						    ary[i]->subject
						    ? ary[i]->subject:"",
						    charset,
						    dump_rfc822_hdr,
						    NULL, NULL) < 0)
			{
				printf("%s", 
				       ary[i]->subject
				       ? ary[i]->subject:"");
			}
			printf("\n");

			if (flags & FLAG_LIST_EVENT_ID)
				printf("%-*s(%s)\n", (int)maxl, "",
				       ary[i]->eventid);
		}
		free(ary);
		listinfo.cnt=cnt;
	}

	printf(gettext("%d events found.\n"), listinfo.cnt);

	while (listinfo.index_list)
	{
		struct listinfo_index *p=listinfo.index_list;

		listinfo.index_list=p->next;
		free(p->eventid);
		if (p->subject)
			free(p->subject);
		free(p);
	}
	pcp_close(pcp);
}

static int list_callback_saveindex(struct PCP_list_all *xl, void *vp)
{
	struct listinfo *li=(struct listinfo *)vp;
	struct listinfo_index *i;

	li->cnt++;

	if ((i=(struct listinfo_index *)
	     malloc(sizeof(struct listinfo_index))) == NULL)
	{
		perror("malloc");
		exit(1);
	}
	memset(i, 0, sizeof(*i));

	if ((i->eventid=strdup(xl->event_id)) == NULL)
	{
		free(i);
		perror("malloc");
		exit(1);
	}

	i->next=li->index_list;
	li->index_list=i;
	++li->i_cnt;
	i->from=xl->event_from;
	i->to=xl->event_to;
	i->subject=NULL;
	return (0);
}

static int save_retr_status(struct PCP_retr *r, int status, void *vp)
{
	struct listinfo *l=(struct listinfo *)vp;
	struct listinfo_index *i;

	for (i=l->index_list; i; i=i->next)
		if (strcmp(i->eventid, r->event_id) == 0)
		{
			i->status=status;
		}
	return (0);
}

static int save_retr_headers(struct PCP_retr *ri, const char *h,
			     const char *v, void *vp)
{
	struct listinfo *l=(struct listinfo *)vp;
	struct listinfo_index *i;
	char *p, *q;

	if (strcasecmp(h, "subject"))
		return (0);

	for (i=l->index_list; i; i=i->next)
		if (strcmp(i->eventid, ri->event_id) == 0)
		{
			if (!i->subject)
			{
				i->subject=strdup(v);
				if (!i->subject)
					return (-1);
			}

			for (p=q=i->subject; *p; )
			{
				if (*p == '\n')
				{
					while (*p && isspace((int)
							     (unsigned char)
							     *p))
						++p;
					*q++=' ';
					continue;
				}
				*q++ = *p++;
			}
			*q=0;
		}
	return (0);
}

static int doretr_begin(struct PCP_retr *r, void *vp);
static int doretr_save(struct PCP_retr *, const char *, int, void *);
static int do_show_retr(struct PCP_retr *, void *);

struct xretrinfo {
	FILE *tmpfile;
	int status;
	struct xretr_participant_list *participant_list;
	struct xretr_time_list *time_list;

} ;

struct xretr_participant_list {
	struct xretr_participant_list *next;
	char *participant;
} ;

struct xretr_time_list {
	struct xretr_time_list *next;
	time_t from;
	time_t to;
} ;

static int doretr_status(struct PCP_retr *p, int status, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;

	xr->status=status;
	return (0);
}

static int doretr_date(struct PCP_retr *p, time_t from, time_t to, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;
	struct xretr_time_list *t=malloc(sizeof(struct xretr_time_list));

	if (!t)
		return (-1);

	t->next=xr->time_list;
	xr->time_list=t;
	t->from=from;
	t->to=to;
	return (0);
}

static int doretr_participants(struct PCP_retr *p, const char *n,
			       const char *id, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;
	char *s=strdup(n);
	struct xretr_participant_list *pa;

	if (!s)
		return (-1);

	if ((pa=malloc(sizeof(struct xretr_participant_list))) == NULL)
	{
		free(s);
		return (-1);
	}
	pa->participant=s;
	pa->next=xr->participant_list;
	xr->participant_list=pa;
	return (0);
}

static void doretr(const char *eventid)
{
	struct PCP *pcp;
	struct PCP_retr r;
	struct xretrinfo xr;
	const char *event_id_array[2];
	struct xretr_time_list *tl;
	struct xretr_participant_list *pl;

	pcp=open_calendar();

	memset(&r, 0, sizeof(r));
	memset(&xr, 0, sizeof(xr));

	r.callback_arg= &xr;
	r.callback_retr_status=doretr_status;
	r.callback_retr_date=doretr_date;
	r.callback_retr_participants=doretr_participants;

	event_id_array[0]=eventid;
	event_id_array[1]=NULL;

	r.event_id_list=event_id_array;

	if (pcp_retr(pcp, &r) == 0)
	{
		r.callback_retr_status=NULL;
		r.callback_retr_date=NULL;
		r.callback_retr_participants=NULL;

		r.callback_begin_func=doretr_begin;
		r.callback_rfc822_func=doretr_save;
		r.callback_end_func=do_show_retr;
		if (pcp_retr(pcp, &r) == 0)
		{
			pcp_close(pcp);
		}
		else
		{
			error(pcp, r.errcode, "pcp_retr");
			pcp_close(pcp);
		}
	}
	else
	{
		error(pcp, r.errcode, "pcp_retr");
		pcp_close(pcp);
	}

	while ((pl=xr.participant_list) != NULL)
	{
		xr.participant_list=pl->next;
		free(pl->participant);
		free(pl);
	}

	while ((tl=xr.time_list) != NULL)
	{
		xr.time_list=tl->next;
		free(tl);
	}
}

static int doretr_begin(struct PCP_retr *r, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;

	if ((xr->tmpfile=tmpfile()) == NULL)
		return (-1);
	return (0);
}

static int doretr_save(struct PCP_retr *r, const char *p, int n, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;

	if (fwrite(p, n, 1, xr->tmpfile) != 1)
		return (-1);
	return (0);
}

static int tcmp(const void *a, const void *b)
{
	struct xretr_time_list *ap=*(struct xretr_time_list **)a;
	struct xretr_time_list *bp=*(struct xretr_time_list **)b;

	return ( ap->from < bp->from ? -1:
		 ap->from > bp->from ? 1:
		 ap->to < bp->to ? -1:
		 ap->to > bp->to ? 1:0);
}

static int list_msg_rfc822(struct rfc2045 *, FILE *);

static int do_show_retr(struct PCP_retr *r, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;
	struct xretr_participant_list *p;
	struct xretr_time_list *t, **tt;
	unsigned cnt, i;
	struct rfc2045 *rfcp;
	int rc;

	if (fseek(xr->tmpfile, 0L, SEEK_SET) < 0
	    || lseek(fileno(xr->tmpfile), 0L, SEEK_SET) < 0)
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	if (xr->time_list == NULL)
	{
		fclose(xr->tmpfile);
		return (0);
	}

	printf(gettext("Event: %s\n"), r->event_id);

	for (cnt=0, t=xr->time_list; t; t=t->next)
		++cnt;

	tt=(struct xretr_time_list **)malloc(cnt * sizeof(*t));
	if (!tt)
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	for (cnt=0, t=xr->time_list; t; t=t->next)
		tt[cnt++]=t;

	qsort(tt, cnt, sizeof(*tt), tcmp);

	for (i=0; i<cnt; i++)
	{
		char fromto[500];

		if (pcp_fmttimerange(fromto, sizeof(fromto),
				     tt[i]->from, tt[i]->to) < 0)
			strcpy(fromto, "******");
		printf(gettext("       %s\n"), fromto);
	}
	free(tt);

	if (xr->status & LIST_CANCELLED)
		printf(gettext("    **** CANCELLED ****\n"));
	if (xr->status & LIST_BOOKED)
		printf(gettext("    **** EVENT NOT YET COMMITED ****\n"));

	for (p=xr->participant_list; p; p=p->next)
		printf(gettext("    Participant: %s\n"), p->participant);


	rfcp=rfc2045_fromfp(xr->tmpfile);
	if (!rfcp)
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	rc=list_msg_rfc822(rfcp, xr->tmpfile);
	rfc2045_free(rfcp);
	fclose(xr->tmpfile);
	return (rc);
}

static int list_msg_mime(struct rfc2045 *, FILE *);

static int list_msg_rfc822(struct rfc2045 *rfc, FILE *fp)
{
	off_t   start_pos, end_pos, start_body;
	off_t	dummy, pos;
	struct rfc822hdr h;

        rfc2045_mimepos(rfc, &start_pos, &end_pos, &start_body,
			&dummy, &dummy);
        if (fseek(fp, start_pos, SEEK_SET) < 0)
		return (-1);

	pos=start_pos;
	rfc822hdr_init(&h, 8192);

	while (rfc822hdr_read(&h, fp, &pos, start_body) == 0)
	{
		printf("%s: ", h.header);

		if (rfc822_display_hdrvalue(h.header, h.value, charset,
					    dump_rfc822_hdr, NULL, NULL) < 0)
		{
			printf("%s", h.value);
		}

		printf("\n");
	}
	rfc822hdr_free(&h);
	printf("\n");
	return (list_msg_mime(rfc, fp));
}

static int list_msg_rfc822_part(struct rfc2045 *rfc, FILE *fp)
{
	struct rfc2045 *q;

	for (q=rfc->firstpart; q; q=q->next)
	{
		if (q->isdummy) continue;
		return (list_msg_rfc822(q, fp));
	}
	return (0);
}


static int list_msg_mime_multipart(struct rfc2045 *, FILE *);
static int list_msg_mime_multipart_alternative(struct rfc2045 *, FILE *);
static int list_msg_textplain(struct rfc2045 *, FILE *);

static int (*mime_handler(struct rfc2045 *rfc))(struct rfc2045 *, FILE *)
{
	const char      *content_type, *dummy;

        rfc2045_mimeinfo(rfc, &content_type, &dummy, &dummy);
        if (strcmp(content_type, "multipart/alternative") == 0)
		return ( &list_msg_mime_multipart_alternative);
        if (strncmp(content_type, "multipart/", 10) == 0)
		return ( &list_msg_mime_multipart);

        if (strcmp(content_type, "message/rfc822") == 0)
		return ( &list_msg_rfc822_part );

        if (strcmp(content_type, "text/plain") == 0
	    || strcmp(content_type, "text/rfc822-headers") == 0
	    || strcmp(content_type, "message/delivery-status") == 0)
		return ( &list_msg_textplain);
	return (NULL);
}


static int list_msg_mime(struct rfc2045 *rfc, FILE *fp)
{
	int (*handler)(struct rfc2045 *, FILE *)=
		mime_handler(rfc);
	const char      *content_type, *dummy;


	char *disposition_name;
	char *disposition_filename;
	char *content_name;

	const char *disposition_filename_s;

	off_t start_pos, end_pos, start_body;
	off_t dummy2;
	char buffer[NUMBUFSIZE+10];

	if (handler)
		return ( (*handler)(rfc, fp));

        rfc2045_mimeinfo(rfc, &content_type, &dummy, &dummy);

	if (rfc2231_udecodeDisposition(rfc, "name", NULL, &disposition_name)<0)
		disposition_name=NULL;

	if (rfc2231_udecodeDisposition(rfc, "filename", NULL,
				       &disposition_filename) < 0)
		disposition_filename=NULL;

	if (rfc2231_udecodeType(rfc, "name", NULL, &content_name) < 0)
		content_name=NULL;

        rfc2045_mimepos(rfc, &start_pos, &end_pos, &start_body,
                        &dummy2, &dummy2);

	disposition_filename_s=disposition_filename;

	if (!disposition_filename_s || !*disposition_filename_s)
		disposition_filename_s=disposition_name;
	if (!disposition_filename_s || !*disposition_filename_s)
		disposition_filename_s=content_name;

	printf(gettext("Attachment: %s (%s)\n"), content_type,
	       libmail_str_sizekb(end_pos - start_body, buffer));
	if (disposition_filename_s && *disposition_filename_s)
		printf("    %s\n", disposition_filename_s);
	printf("\n");

	if (content_name)		free(content_name);
	if (disposition_name)		free(disposition_name);
	if (disposition_filename)	free(disposition_filename);

	return (0);
}

static int list_msg_mime_multipart(struct rfc2045 *rfc, FILE *fp)
{
	struct rfc2045 *q;
	int first=1;
	int rc;

	for (q=rfc->firstpart; q; q=q->next)
	{
		if (q->isdummy) continue;

		if (!first)
			printf("\n    ------------------------------\n\n");
		first=0;

		rc=list_msg_mime(q, fp);
		if (rc)
			return (rc);
	}
	return (0);
}

static int list_msg_mime_multipart_alternative(struct rfc2045 *rfc, FILE *fp)
{
	struct rfc2045 *q, *first=NULL, *last=NULL;

	for (q=rfc->firstpart; q; q=q->next)
	{
		if (q->isdummy) continue;
		if (!first)
			first=q;
		if ( mime_handler(q) != NULL)
			last=q;
	}

	return (last ? list_msg_mime(last, fp):
		first ? list_msg_mime(first, fp):0);
}

static int textplain_output(const char *ptr, size_t cnt, void *voidptr)
{
	while (cnt)
	{
		putchar(*ptr);
		++ptr;
		--cnt;
	}
	return (0);
}

static int list_msg_textplain(struct rfc2045 *rfc, FILE *fp)
{
	const char *mime_charset, *dummy;
	int rc;
	struct rfc2045src *src;

        rfc2045_mimeinfo(rfc, &dummy, &dummy, &mime_charset);

        if (strcasecmp(mime_charset, charset))
        {
		printf(gettext("    (The following text was converted from %s)\n\n"),
		       mime_charset);
	}

	src=rfc2045src_init_fd(fileno(fp));

	if (src == NULL)
		return (-1);

	rc=rfc2045_decodetextmimesection(src,
					 rfc,
					 charset,
					 NULL,
					 textplain_output, NULL);

	printf("\n");
	rfc2045src_deinit(src);
	return (rc);
}

/*** CANCEL/UNCANCEL/DELETE ***/

static void docancel(const char *id, int flags)
{
	struct PCP *pcp=open_calendar();
	int errcode;

	if (pcp_cancel(pcp, id, &errcode))
	{
		error(pcp, errcode, "pcp_cancel");
		exit(1);
	}
	pcp_close(pcp);
}

static void dodelete(const char *id, int flags)
{
	struct PCP *pcp=open_calendar();
	struct PCP_delete del;

	memset(&del, 0, sizeof(del));
	del.id=id;

	if (pcp_delete(pcp, &del))
	{
		error(pcp, del.errcode, "pcp_delete");
		exit(1);
	}
	pcp_close(pcp);
}

static void douncancel(const char *id, int flags)
{
	struct PCP *pcp=open_calendar();
	struct PCP_uncancel uncancel_info;

	memset(&uncancel_info, 0, sizeof(uncancel_info));
	uncancel_info.uncancel_conflict_callback= &show_conflict;
	uncancel_info.uncancel_conflict_callback_ptr=NULL;

	if (pcp_uncancel(pcp, id, flags, &uncancel_info))
	{
		error(pcp, uncancel_info.errcode, "pcp_uncancel");
		exit(1);
	}
	pcp_close(pcp);
}

/* Initialize */

static void init(const char *shell)
{
	struct passwd *pw=do_getpw();

	if (chdir(pw->pw_dir))
	{
		perror(pw->pw_dir);
		exit(1);
	}
	unlink(".pcplogin");
}

/* Login to a server */

static void login(const char *userid)
{
	char *errmsg;
	char password[1024];
	struct PCP *pcp;
	char *p;

#if HAVE_TCGETATTR
	struct termios tios;
	int tios_rc=tcgetattr(0, &tios);

	if (tios_rc >= 0)
	{
		tios.c_lflag &= ~ECHO;
		tcsetattr(0, TCSANOW, &tios);
	}
#endif

	printf(gettext("Password: "));

	if (fgets(password, sizeof(password), stdin) == NULL)
		password[0]=0;

#if HAVE_TCGETATTR
	if (tios_rc >= 0)
	{
		tios.c_lflag |= ECHO;
		tcsetattr(0, TCSANOW, &tios);
		printf("\n");
	}
#endif

	if ((p=strchr(password, '\n')) != 0)
		*p=0;

	pcp=pcp_open_server(userid, password, &errmsg);

	if (pcp)
	{
		struct passwd *pw=do_getpw();
		FILE *fp;
		const char *p=pcp_authtoken(pcp);

		if (!p)
		{
			fprintf(stderr, gettext("ERROR: Unable to obtain authentication token from the server.\n"));
			exit(1);
		}

		if (chdir(pw->pw_dir) < 0)
		{
			perror(pw->pw_dir);
			exit(1);
		}
		umask(077);
		fp=fopen(".pcplogin", "w");

		if (!fp)
		{
			perror("$HOME/.pcplogin");
			exit(1);
		}

		fprintf(fp, "%s\n%s\n", userid, p);
		if (fflush(fp) < 0 || ferror(fp) || fclose(fp))
		{
			perror("$HOME/.pcplogin");
			unlink(".pcplogin");
			exit(1);
		}
		pcp_close(pcp);
		return;
	}

	printf(gettext("ERROR:\n%s\n"), errmsg ? errmsg:strerror(errno));
	if (errmsg)
		free(errmsg);
}

/* setacl */

static void setacl(const char *who, int flags)
{
	struct PCP *pcp=open_calendar();

	if (pcp_has_acl(pcp))
	{
		if (pcp_acl(pcp, who, flags))
		{
			error(pcp, 0, "pcp_acl");
			exit(1);
		}
	}
	else
	{
		fprintf(stderr, gettext("ERROR: ACLs not supported.\n"));
	}
	pcp_close(pcp);
}

static int do_list_acl(const char *who, int flags, void *dummy)
{
	char buf[1024];

	buf[0]=0;
	pcp_acl_name(flags, buf);

	printf("%-30s\t%s\n", who, buf);
	return (0);
}

static void listacls()
{
	struct PCP *pcp=open_calendar();

	if (pcp_has_acl(pcp))
	{
		if (pcp_list_acl(pcp, do_list_acl, NULL))
		{
			error(pcp, 0, "pcp_list_acl");
			exit(1);
		}
	}
	else
	{
		fprintf(stderr, gettext("ERROR: ACLs not supported.\n"));
	}
	pcp_close(pcp);
}

/* Connect to a PCP server */

static int doconnect(const char *pathname)
{
        int     fd=socket(PF_UNIX, SOCK_STREAM, 0);
        struct  sockaddr_un skun;

	skun.sun_family=AF_UNIX;
        strcpy(skun.sun_path, pathname);

	if (fd >= 0 && fcntl(fd, F_SETFL, O_NONBLOCK) >= 0)
	{
		if (connect(fd, (struct sockaddr *)&skun, sizeof(skun)) == 0)
			return (fd);

		if (errno == EINPROGRESS || errno == EWOULDBLOCK)
		{
			struct timeval tv;
			fd_set fds;
			int rc;

			tv.tv_sec=10;
			tv.tv_usec=0;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);

			rc=select(fd+1, NULL, &fds, NULL, &tv);

			if (rc > 1 && FD_ISSET(fd, &fds))
			{
				if (connect(fd, (struct sockaddr *)&skun,
					    sizeof(skun)) == 0)
					return (fd);
				if (errno == EISCONN)
					return (fd);
			}

			if (rc >= 0)
				errno=ETIMEDOUT;
		}
	}
	perror(pathname);
	exit(1);
	return (0);
}

static void doconnectwrite(int, const char *, int);

static void doconnectloop(int fd)
{
	char buf[BUFSIZ];
	fd_set rfd;

	for (;;)
	{
		FD_ZERO(&rfd);
		FD_SET(0, &rfd);
		FD_SET(fd, &rfd);

		if (select(fd+1, &rfd, NULL, NULL, NULL) <= 0)
		{
			perror("select");
			continue;
		}

		if (FD_ISSET(fd, &rfd))
		{
			int n=read(fd, buf, sizeof(buf));

			if (n < 0)
				perror("read");
			if (n <= 0)
				break;
			doconnectwrite(1, buf, n);
		}

		if (FD_ISSET(0, &rfd))
		{
			int n=read(0, buf, sizeof(buf));

			if (n < 0)
				perror("read");
			if (n <= 0)
				break;
			doconnectwrite(fd, buf, n);
		}
	}
	exit(0);
}

static void doconnectwrite(int fd, const char *p, int cnt)
{
	while (cnt > 0)
	{
		int n=write(fd, p, cnt);

		if (n <= 0)
			exit(0);

		p += n;
		cnt -= n;
	}
}

static void usage()
{
	const char *charset=unicode_default_chset();

	fprintf(stderr,
		gettext("Usage: pcp [options] [command]\n"
			"\n"
			"Options:\n"
			"   -c           - add/uncancel event that conflicts with an existing event\n"
			"   -s subject   - specify event subject\n"
			"   -C charset   - specify your local charset (default %s)\n"
			"   -m           - standard input is already a MIME-formatted message\n"
			"   -e           - list event ids\n"
			"\n"), charset);

	fprintf(stderr, "%s",
		gettext(
			"Commands:\n"
			"   init\n"
			"   login USERID\n"
			"   logout\n"
			"   add from FROM to TO [ from FROM to TO...]\n"
			"   update ID from FROM to TO [ from FROM to TO...]\n"
			"   list [all] [from FROM] [to TO] [event ID]\n"
			"   cancel ID\n"
			"   uncancel ID\n"
			"   delete ID\n"
			"   connect [/pathname]\n"
			"   sconnect [/pathname]\n"
			"   setacl [MODIFY|CONFLICT|LIST|RETR|NONE]*\n"
			"   listacl\n"
			));
	exit(1);
}

static char *read_subject()
{
	char buf[BUFSIZ];
	char *p;

	printf("Subject: ");

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		exit(0);

	p=strchr(buf, '\n');
	if (p)
		*p=0;

	p=strdup(buf);

	if (!p)
	{
		perror("malloc");
		exit(1);
	}
	return (p);
}

static char *mimeify(const char *subject, const char *charset)
{
	char *p=rfc2047_encode_str(subject, charset,
				   rfc2047_qp_allow_any);

	if (!p)
	{
		perror("rfc2047_encode_str");
		exit(1);
	}
	return (p);
}

int main(int argc, char **argv)
{
	int flags=0;
	int list_flags=0;
	int optchar;
	const char *subject=0;
	int ismime=0;

	setlocale(LC_ALL, "");
	textdomain("pcp");

	charset=unicode_default_chset();

	while ((optchar=getopt(argc, argv, "emcs:C:")) >= 0)
	{
		switch (optchar) {
		case 'c':
			flags |= PCP_OK_CONFLICT;
			break;
		case 's':
			subject=optarg;
			break;
		case 'C':
			charset=optarg;
			break;
		case 'm':
			ismime=1;
			break;
		case 'e':
			list_flags |= FLAG_LIST_EVENT_ID;
			break;
		default:
			usage();
		}
	}

	if (optind < argc)
	{
		const char *addstr=gettext("add");
		const char *updatestr=gettext("update");

		if (strcmp(argv[optind], gettext("init")) == 0)
		{
			++optind;
			init(optind < argc ? argv[optind]:NULL);
			exit(0);
		}
		else if (strcmp(argv[optind], gettext("login")) == 0)
		{
			++optind;
			if (optind < argc)
			{
				login(argv[optind]);
				exit (0);
			}
		}
		else if (strcmp(argv[optind], gettext("connect")) == 0)
		{
			int fd;
			const char *n;

			++optind;
			n=optind < argc ? argv[optind] : PUBDIR "/50PCPDLOCAL";
			fd=doconnect(n);

			if (fcntl(fd, F_SETFL, 0) < 0)
			{
				perror(argv[optind]);
				exit (0);
			}
			printf("Connected to %s...\n", n);
			doconnectloop(fd);
			exit (0);
		}
		else if (strcmp(argv[optind], gettext("sconnect")) == 0)
		{
			int fd;
			const char *n;

			++optind;
			n=optind < argc ? argv[optind]
				: PRIVDIR "/50PCPDLOCAL";
			fd=doconnect(n);

			if (fcntl(fd, F_SETFL, 0) < 0)
			{
				perror(argv[optind]);
				exit (0);
			}
			printf("Connected to %s...\n", n);
			doconnectloop(fd);
			exit (0);
		}
		else if (strcmp(argv[optind], gettext("cancel")) == 0)
		{
			++optind;
			if (optind < argc)
			{
				docancel(argv[optind], flags);
				exit (0);
			}
		}
		else if (strcmp(argv[optind], gettext("delete")) == 0)
		{
			++optind;
			if (optind < argc)
			{
				dodelete(argv[optind], flags);
				exit (0);
			}
		}
		else if (strcmp(argv[optind], gettext("uncancel")) == 0)
		{
			++optind;
			if (optind < argc)
			{
				douncancel(argv[optind], flags);
				exit (0);
			}
		}
		else if (strcmp(argv[optind], addstr) == 0 ||
			 strcmp(argv[optind], updatestr) == 0)
		{
			struct add_info info;
			const char *oldeventid=0;

			++optind;

			if (strcmp(argv[optind-1], updatestr) == 0)
			{
				if (optind >= argc)
					usage();
				oldeventid=argv[optind++];
			}

			memset(&info, 0, sizeof(info));

			if (ismime)
				info.add_func=add_read_stdin;
			else
			{
				info.add_func=add_read_subject;
				info.add_charset=charset;
				if (subject)
					info.add_subject=mimeify(subject,
								 charset);
				else
				{
					char *p;

					if (!isatty(0))
					{
						fprintf(stderr,
							gettext("Error: -s is required\n"));
						exit(1);
					}

					p=read_subject();

					info.add_subject=mimeify(p, charset);
					free(p);
				}
			}
			add(optind, argc, argv, flags, oldeventid, &info);
			exit (0);
		}
		else if (strcmp(argv[optind], gettext("list")) == 0)
		{
			list(optind+1, argc, argv, list_flags);
			exit (0);
		}
		else if (strcmp(argv[optind], gettext("setacl")) == 0)
		{
			int flags=0;
			const char *acl;

			if (++optind < argc)
			{
				acl=argv[optind];

				while (++optind < argc)
					flags |= pcp_acl_num(argv[optind]);

				setacl(acl, flags);
			}
			exit (0);
		}
		else if (strcmp(argv[optind], gettext("listacl")) == 0)
		{
			listacls();
			exit(0);
		}
	}

	usage();
	return (0);
}
