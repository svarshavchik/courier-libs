/*
** Copyright 2001-2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <rfc822/rfc822hdr.h>
#include "pcp.h"

#if HAVE_DIRENT_H
#include <dirent.h>
#else
#define dirent direct
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif

/* PCP driver for filesystem-based calendar */

#define HOSTNAMELEN 256

struct PCPdir {
	struct PCP pcp;
	char *username;
	char *dirname;
	char *indexname;
	char *newindexname;
	char hostname_buf[HOSTNAMELEN];
	char unique_filename_buf[256];
	unsigned uniq_cnt;
} ;

struct PCPdir_event_participant {
	char *address;
	char *eventid;
} ;

struct PCPdir_new_eventid {
	struct PCP_new_eventid eventid;
	char *newfile;
	char *tmpfile;

	char *oldeventid;

	struct PCPdir_event_participant *event_participants;
	unsigned n_event_participants;

	int booked;
} ;

static void pcp_close_dir(struct PCPdir *);
static int cleanup(struct PCPdir *);

static struct PCPdir_new_eventid *neweventid(struct PCPdir *,
					     const char *,
					     struct PCP_save_event *);
static void destroyeventid(struct PCPdir *, struct PCPdir_new_eventid *);

static int saveevent(struct PCPdir *, struct PCPdir_new_eventid *,
		     struct PCP_save_event *);

static int commitevent(struct PCPdir *, struct PCPdir_new_eventid *,
		       struct PCP_commit *);
static int bookevent(struct PCPdir *, struct PCPdir_new_eventid *,
		     struct PCP_commit *);

static int listallevents(struct PCPdir *, struct PCP_list_all *);

static int cancelevent(struct PCPdir *, const char *, int *);
static int uncancelevent(struct PCPdir *, const char *,
			 int, struct PCP_uncancel *);
static int deleteevent(struct PCPdir *, struct PCP_delete *);
static int retrevent(struct PCPdir *, struct PCP_retr *);
static int setacl(struct PCPdir *, const char *, int);
static int listacl(struct PCPdir *,
		   int (*)(const char *, int, void *),
		   void *);

static const char *errmsg(struct PCP *pcp)
{
	return (strerror(errno));
}

static void noop(struct PCP *pcp)
{
}

static const char *getauthtoken(struct PCP *pcp)
{
	return (NULL);
}

struct PCP *pcp_open_dir(const char *dirname, const char *username)
{
	struct PCPdir *pd=(struct PCPdir *)malloc(sizeof(struct PCPdir));

	if (!pd)
		return (NULL);

	memset(pd, 0, sizeof(*pd));

	pd->username=strdup(username);
	if (!pd->username)
	{
		free(pd);
		return (NULL);
	}

	pd->dirname=strdup(dirname);

	if (!pd->dirname)
	{
		free(pd);
		return (NULL);
	}

	pd->indexname=malloc(strlen(dirname)+sizeof("/index"));
	if (!pd->indexname)
	{
		free(pd->username);
		free(pd->dirname);
		free(pd);
		return (NULL);
	}
	strcat(strcpy(pd->indexname, dirname), "/index");

	pd->newindexname=malloc(strlen(dirname)+sizeof("/.index.tmp"));
	if (!pd->newindexname)
	{
		free(pd->username);
		free(pd->indexname);
		free(pd->dirname);
		free(pd);
		return (NULL);
	}
	strcat(strcpy(pd->newindexname, dirname), "/.index.tmp");

	pd->hostname_buf[sizeof(pd->hostname_buf)-1]=0;
	pd->hostname_buf[0]=0;
	if (gethostname(pd->hostname_buf, sizeof(pd->hostname_buf)-1) < 0
	    || pd->hostname_buf[0] == 0)
		strcpy(pd->hostname_buf, "localhost");


	pd->pcp.authtoken_func=getauthtoken;
	pd->pcp.close_func= (void (*)(struct PCP *)) pcp_close_dir;
	pd->pcp.cleanup_func= (int (*)(struct PCP *)) cleanup;

	pd->pcp.create_new_eventid_func=
		(struct PCP_new_eventid *(*)(struct PCP *, const char *,
					     struct PCP_save_event *))
		neweventid;

	pd->pcp.destroy_new_eventid_func=
		(void (*)(struct PCP *, struct PCP_new_eventid *))
		destroyeventid;

	pd->pcp.commit_func=
		(int (*)(struct PCP *, struct PCP_new_eventid *,
			 struct PCP_commit *))
		commitevent;

	pd->pcp.book_func=
		(int (*)(struct PCP *, struct PCP_new_eventid *,
			 struct PCP_commit *))
		bookevent;

	pd->pcp.list_all_func=
		(int (*)(struct PCP *, struct PCP_list_all *))
		listallevents;

	pd->pcp.cancel_func=
		(int (*)(struct PCP *, const char *, int *))
		cancelevent;

	pd->pcp.uncancel_func=
		(int (*)(struct PCP *, const char *, int,
			 struct PCP_uncancel *))
		uncancelevent;

	pd->pcp.delete_func=
		(int (*)(struct PCP *, struct PCP_delete *))
		deleteevent;

	pd->pcp.retr_func=
		(int (*)(struct PCP *, struct PCP_retr *))
		retrevent;

	pd->pcp.errmsg_func=errmsg;

	pd->pcp.noop_func=noop;

	pd->pcp.acl_func=
		(int (*)(struct PCP *, const char *, int))setacl;
	pd->pcp.listacl_func=
		(int (*)(struct PCP *, int (*)(const char *, int, void *),
			 void *))listacl;

	return ((struct PCP *)pd);
}

static void pcp_close_dir(struct PCPdir *pd)
{
	free(pd->newindexname);
	free(pd->indexname);
	free(pd->dirname);
	free(pd->username);
	free(pd);
}


static void mkunique(struct PCPdir *pd)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	sprintf(pd->unique_filename_buf, "%lu.%lu.%lu_%u.",
		(unsigned long)tv.tv_sec,
		(unsigned long)tv.tv_usec,
		(unsigned long)getpid(),
		++pd->uniq_cnt);

	strncat(pd->unique_filename_buf, pd->hostname_buf,
		sizeof(pd->unique_filename_buf)-1
		-strlen(pd->unique_filename_buf));
}

static char *dotlockname(struct PCPdir *pd)
{
	char *p=malloc(strlen(pd->dirname)+sizeof("/.lock"));

	if (!p)
		return (NULL);

	return (strcat(strcpy(p, pd->dirname), "/.lock"));
}

/*
** Mark calendar as updated
*/

static char *changedname(struct PCPdir *pd)
{
	char *p=malloc(strlen(pd->dirname)+sizeof("/changed"));

	if (!p)
	{
		fprintf(stderr, "CRIT: out of memory.\n");
		return (NULL);
	}

	return (strcat(strcpy(p, pd->dirname), "/changed"));
}

static void markchanged(struct PCPdir *pd)
{
	char *p=changedname(pd);
	int n;

	if (!p) return;

	n=open(p, O_RDWR|O_CREAT, 0600);
	if (n >= 0)
		close(n);
}

/*
** Attempt to get rid of a stale dot-lock file.
**
** Return code: 0 - no dot-lock exists
**              1 - transient race condition, try again later
**              2 - valid dot-lock exists
**             <0 - permanent error.
*/

#define TRY_SLEEP 3
#define TRY_MAX 3

static int kill_stale_lock(struct PCPdir *pd, const char *stale_lock,
			   time_t *tm)
{
	int fd;
	char buffer[HOSTNAMELEN+256];
	int n;
	struct stat stat_buf;

	fd=open(stale_lock, O_RDONLY);
	if (fd < 0)
	{
		if (errno == ENOENT)
			return (0);
		return (-1);
	}

	n=read(fd, buffer, sizeof(buffer)-1);

	if (n < 0 || fstat(fd, &stat_buf) < 0)
	{
		close(fd);
		return (-1);
	}

	*tm=stat_buf.st_mtime;
	close(fd);

	if (n > 0 && buffer[n-1] == '\n')
	{
		char *p;

		buffer[n-1]=0;

		p=strchr(buffer, ' ');
		if (p)
		{
			*p++=0;
			if (strcmp(p, pd->hostname_buf) == 0)
				/* Dot-lock created on this server */
			{
				pid_t pn=atol(buffer);

				if (pn > 1)
				{
					if (kill(pn, 0) == 0 ||
					    errno != ESRCH)
					{
						return (2);
					}
					/* Process doesn't exist no more */

					if (unlink(stale_lock) == 0
					    || errno == ENOENT)
					{
						return (1);
					}
					return (-1);
				}

				fprintf(stderr, "INFO: "
					"Corrupted dot-lock - removing %s\n",
					stale_lock);

				if (unlink(stale_lock) == 0
				    || errno == ENOENT)
				{
					return (1);
				}
				return (-1);
			}

			return (1);
		}
	}

	fprintf(stderr, "INFO: Corrupted dot-lock - removing %s\n", stale_lock);

	if (unlink(stale_lock) == 0
	    || errno == ENOENT)
	{
		return (1);
	}

	return (-1);
}

static char *acquire_dotlock(struct PCPdir *pd)
{
	char *n=dotlockname(pd);
	char *tmpname;
	FILE *fp;
	unsigned try_cnt;
	time_t last_time;

	if (!n)
		return (NULL);

	mkunique(pd);

	tmpname=malloc(strlen(pd->dirname)+strlen(pd->unique_filename_buf)
		       +20);

	if (!tmpname)
	{
		free(n);
		fprintf(stderr, "ALERT: Failed to create dotlock: %s - %s\n",
			n, strerror(errno));
		return (NULL);
	}
	strcat(strcat(strcat(strcpy(tmpname, pd->dirname), "/."),
		      pd->unique_filename_buf), ".lock");

	fp=fopen(tmpname, "w");

	if (!fp)
	{
		free(tmpname);
		free(n);
		fprintf(stderr, "ALERT: Failed to create dotlock: %s - %s\n",
			n, strerror(errno));
		return (NULL);
	}

	if (fprintf(fp, "%lu %s\n", (unsigned long)getpid(), pd->hostname_buf)
	    < 0 || fflush(fp))
	{
		fclose(fp);
		unlink(tmpname);
		free(tmpname);
		free(n);
		fprintf(stderr, "ALERT: Failed to create dotlock: %s - %s\n",
			n, strerror(errno));
		return (NULL);
	}

	if (fclose(fp) < 0)
	{
		unlink(tmpname);
		free(tmpname);
		free(n);
		fprintf(stderr, "ALERT: Failed to create dotlock: %s - %s\n",
			n, strerror(errno));
		return (NULL);
	}

	try_cnt=0;
	last_time=0;
	for (;;)
	{
		time_t timestamp;
		int rc=kill_stale_lock(pd, n, &timestamp);

		if (rc > 0)
		{
			++try_cnt;
			if (last_time == 0)
				last_time=timestamp;

			if (timestamp != last_time)
				try_cnt=0;	/* Dot-lock was modified,
						** reset.
						*/

			if (try_cnt == TRY_MAX)	/* Time to kill dot lock */
			{
				if (rc > 1 || (unlink(n) < 0
					       && errno != ENOENT))
				{
					fprintf(stderr, "ALERT: "
						"Failed to obtain dotlock: %s\n",
						n);

					unlink(tmpname);
					free(tmpname);
					free(n);
					return (NULL);
				}
				fprintf(stderr, "ALERT: Removed stale dotlock: %s\n",
					n);

			}
			else if (try_cnt > TRY_MAX)
			{
				fprintf(stderr, "ALERT: "
					"Failed to obtain dotlock: %s\n",
					n);

				unlink(tmpname);
				free(tmpname);
				free(n);
				return (NULL);
			}
			last_time=timestamp;
			sleep(TRY_SLEEP);
			continue;
		}

		if (rc < 0)
		{
			fprintf(stderr, "ALERT: Failed to obtain dotlock: %s\n",
				n);
			unlink(tmpname);
			free(tmpname);
			free(n);
			return (NULL);
		}

		if (link(tmpname, n) == 0)
			break;

		if (try_cnt > TRY_MAX)
		{
			fprintf(stderr, "ALERT: Failed to obtain dotlock: %s\n",
				n);

			unlink(tmpname);
			free(tmpname);
			free(n);
			return (NULL);
		}
		++try_cnt;
		sleep(TRY_SLEEP);
	}

	unlink(tmpname);
	free(tmpname);
	return (n);
}

#if 0
static void renew_dotlock(struct PCPdir *pd, char *n)
{
	FILE *fp;
	char buf[HOSTNAMELEN*2];

	fp=fopen(n, "r");

	if (fp && fgets(buf, sizeof(buf), fp) != NULL)
	{
		char *p=strchr(buf, '\n');

		if (p)
			*p=0;

		p=strchr(buf, ' ');
		if (p)
		{
			*p++=0;

			if (atol(buf) == getpid() &&
			    strcmp(p, pd->hostname_buf) == 0)
			{
				fclose(fp);
				fp=fopen(n, "r+");
				if (fp)
				{
					if (fseek(fp, -1, SEEK_END) >= 0)
						putc('\n', fp);
					fflush(fp);
					fclose(fp);
				}
				return;
			}
		}
	}
	if (fp)
		fclose(fp);
}
#endif

static void release_dotlock(struct PCPdir *pd, char *n)
{
	FILE *fp;
	char buf[HOSTNAMELEN*2];

	fp=fopen(n, "r");

	if (fp && fgets(buf, sizeof(buf), fp) != NULL)
	{
		char *p=strchr(buf, '\n');

		if (p)
			*p=0;

		p=strchr(buf, ' ');
		if (p)
		{
			*p++=0;

			if (atol(buf) == getpid() &&
			    strcmp(p, pd->hostname_buf) == 0)
			{
				fclose(fp);
				unlink(n);
				free(n);
				return;
			}
		}
	}
	if (fp)
		fclose(fp);
	fprintf(stderr, "ALERT: Dotlock unexpectedly gone: %s\n", n);
	free(n);
}

static struct PCPdir_new_eventid *neweventid(struct PCPdir *pd,
					     const char *oldeventid_arg,
					     struct PCP_save_event *se)
{
	unsigned ut;

	struct PCPdir_new_eventid *p=(struct PCPdir_new_eventid *)
		malloc(sizeof(struct PCPdir_new_eventid));

	if (!p)
		return (NULL);

	mkunique(pd);

	p->eventid.eventid=malloc(strlen(pd->unique_filename_buf)+
				  strlen(pd->username)+2);
	if (!p->eventid.eventid)
	{
		free(p);
		return (NULL);
	}

	strcat(strcat(strcpy(p->eventid.eventid, pd->unique_filename_buf),
		      "@"), pd->username);

	p->tmpfile=malloc(strlen(pd->unique_filename_buf)
			  +strlen(pd->dirname)+10);
	if (!p->tmpfile)
	{
		free(p->eventid.eventid);
		free(p);
		return (NULL);
	}
	strcat(strcat(strcat(strcpy(p->tmpfile, pd->dirname),
			     "/."), pd->unique_filename_buf), ".tmp");

	p->newfile=malloc(strlen(pd->unique_filename_buf)
			  +strlen(pd->dirname)+2);
	if (!p->newfile)
	{
		free(p->tmpfile);
		free(p->eventid.eventid);
		free(p);
		return (NULL);
	}
	strcat(strcat(strcpy(p->newfile, pd->dirname),
		      "/"), pd->unique_filename_buf);

	if (oldeventid_arg)
	{
		if ((p->oldeventid=strdup(oldeventid_arg)) == NULL)
		{
			free(p->newfile);
			free(p->tmpfile);
			free(p->eventid.eventid);
			free(p);
			return (NULL);
		}
	}
	else
		p->oldeventid=0;
	p->booked=0;

	p->event_participants=NULL;
	p->n_event_participants=0;

	for (ut=0; ut<se->n_event_participants; ut++)
	{
		const char *q;

		for (q=se->event_participants[ut].address; *q; q++)
			if (isspace((int)(unsigned char)*q))
			{
				errno=EINVAL;
				destroyeventid(pd, p);
				return (NULL);
			}

		for (q=se->event_participants[ut].eventid; q && *q; q++)
			if (isspace((int)(unsigned char)*q))
			{
				errno=EINVAL;
				destroyeventid(pd, p);
				return (NULL);
			}
	}

	if (se->n_event_participants > 0)
	{
		if ((p->event_participants=
		     malloc( sizeof (*p->event_participants)
			     * se->n_event_participants)) == 0)
		{
			destroyeventid(pd, p);
			return (NULL);
		}

		for ( ; p->n_event_participants < se->n_event_participants;
		      ++p->n_event_participants)
		{
			p->event_participants[p->n_event_participants]
				.eventid=NULL;

			if (se->event_participants[p->n_event_participants]
			    .eventid
			    && (p->event_participants[p->n_event_participants]
				.eventid=strdup(se->event_participants
						[p->n_event_participants]
						.eventid))
			     == NULL)
			{
				destroyeventid(pd, p);
				return (NULL);
			}

			if ( (p->event_participants[p->n_event_participants]
			      .address=strdup(se->event_participants
					      [p->n_event_participants]
					      .address))
			     == NULL)
			{
				if (p->event_participants
				    [p->n_event_participants].eventid)
					free(p->event_participants
					     [p->n_event_participants]
					     .eventid);
				destroyeventid(pd, p);
				return (NULL);
			}
		}
	}

	if (saveevent(pd, p, se))
	{
		destroyeventid(pd, p);
		p=NULL;
	}

	return (p);
}

static void destroyeventid(struct PCPdir *pd, struct PCPdir_new_eventid *p)
{
	unsigned i;

	for (i=0; i<p->n_event_participants; i++)
	{
		if (p->event_participants[i].eventid)
			free(p->event_participants[i].eventid);
		if (p->event_participants[i].address)
			free(p->event_participants[i].address);
	}
	if (p->event_participants)
		free(p->event_participants);

	if (p->booked && p->eventid.eventid)
	{
		struct PCP_delete del;

		memset(&del, 0, sizeof(del));
		del.id=p->eventid.eventid;

		deleteevent(pd, &del);
	}

	if (p->oldeventid)
		free(p->oldeventid);
	free(p->newfile);

	if (p->tmpfile)
	{
		unlink(p->tmpfile);
		free(p->tmpfile);
		p->tmpfile=NULL;
	}

	free(p->eventid.eventid);
	free(p);
}		

static int saveevent(struct PCPdir *pd, struct PCPdir_new_eventid *ne,
		     struct PCP_save_event *si)
{
	char buf[BUFSIZ];
	int n;

	FILE *nf=fopen(ne->tmpfile, "w");

	if (!nf)
		return (-1);

	while ((n=pcp_read_saveevent(si, buf, sizeof(buf))) > 0)
	{
		if (fwrite(buf, n, 1, nf) != 1)
		{
			fclose(nf);
			return (-1);
		}
	}

	if (fflush(nf) || ferror(nf))
		n= -1;
	if (fclose(nf))
		n= -1;

	if (n < 0)
		return (-1);

	return (0);
}

/*
** Structure of the index file:
**
** eventid<TAB>filename<TAB>field<TAB>field<TAB>field...
**
** field is name=value
**
** Fields:
*/

#define TIME_FIELD	"t"		/* start,end */
#define CANCELLED_FIELD "c"		/* This event has been cancelled */
#define BOOKED_FIELD	"b"		/* This is a booked event */
#define PENDEL_FIELD	"d"		/* This event is pending to be deleted */
#define PROXY_FIELD	"P"		/* This event was placed by proxy */
#define PARTICIPANT_FIELD "p"		/* event participant */

/* The line_buffer struct is a line's worth of a buffer */

struct line_buffer {
	int bufsiz;
	char *buffer;	/* Also the eventid */

	char *next_field;	/* Iterator through the fields */
} ;

static int lb_init(struct line_buffer *pi)
{
	pi->bufsiz=BUFSIZ;
	pi->buffer=malloc(pi->bufsiz);
	if (!pi->buffer)
		return (-1);
	return (0);
}

static void lb_destroy(struct line_buffer *pi)
{
	free(pi->buffer);
}

static int lb_read(FILE *fp, struct line_buffer *pi)
{
	int n=0;
	int c;

	for (;;)
	{
		if (n >= pi->bufsiz)
		{
			int news=pi->bufsiz + BUFSIZ;
			char *newp=realloc(pi->buffer, news);

			if (!newp)
				return (-1);

			pi->buffer=newp;
			pi->bufsiz=news;
		}

		c=getc(fp);
		if (c == EOF)
			return (-1);

		if (c == '\n')
		{
			pi->buffer[n]=0;
			break;
		}
		pi->buffer[n++]=c;
	}

	return (0);
}

static int lb_is_eventid(struct line_buffer *pi, const char *ei)
{
	int l=strlen(ei);
	const char *p=pi->buffer;

	return (p && strncmp(p, ei, l) == 0 && p[l] == '\t');
}

static char *lb_event_id(struct line_buffer *pi)
{
	char *p=strchr(pi->buffer, '\t');
	char *q;

	if (!p)
		return (strdup(""));	/* Corrupted index file */

	q=malloc(p-pi->buffer+1);

	if (!q)
		return (NULL);

	memcpy(q, pi->buffer, p-pi->buffer);
	q[p-pi->buffer]=0;
	return (q);
}

static char *lb_filename(struct PCPdir *pd, struct line_buffer *pi)
{
	char *p=strchr(pi->buffer, '\t');
	char *q, *r;

	if (!p)
		return (strdup(""));	/* Corrupted index file */

	++p;
	r=strchr(p, '\t');
	if (!r)
		r=p+strlen(p);

	q=malloc(strlen(pd->dirname)+2+ (r-p));

	if (!q)
		return (NULL);

	strncat(strcat(strcpy(q, pd->dirname), "/"), p, r-p);
	return (q);
}

static char *lb_filename_nodir(struct line_buffer *pi)
{
	char *p=strchr(pi->buffer, '\t');
	char *q, *r;

	if (!p)
		return (strdup(""));	/* Corrupted index file */

	++p;
	r=strchr(p, '\t');
	if (!r)
		r=p+strlen(p);

	q=malloc(1+(r-p));

	if (!q)
		return (NULL);

	*q=0;
	strncat(q, p, r-p);
	return (q);
}

static const char *lb_first_field(struct line_buffer *pi)
{
	char *p;

	p=pi->buffer ? strchr(pi->buffer, '\t'):NULL;

	if (p)
	{
		++p;	/* p now is the filename */
		p=strchr(p, '\t');
		if (p)
			++p;
	}

	return (pi->next_field=p);
}

static const char *lb_next_field(struct line_buffer *pi)
{
	char *p=pi->next_field ? strchr(pi->next_field, '\t'):NULL;

	if (p)
		++p;
	return (pi->next_field=p);
}

static int lb_is_field(const char *p, const char *n)
{
	if (p)
	{
		int l=strlen(n);

		if (memcmp(p, n, l) == 0 &&
		    (p[l] == '=' || p[l] == '\t' || p[l] == 0))
			return (1);
	}
	return (0);
}

static int lb_is_cancelled(struct line_buffer *pi)
{
	const char *p;

	for (p=lb_first_field(pi); p; p=lb_next_field(pi))
		if (lb_is_field(p, CANCELLED_FIELD))
			return (1);
	return (0);
}

static int lb_is_proxy(struct line_buffer *pi)
{
	const char *p;

	for (p=lb_first_field(pi); p; p=lb_next_field(pi))
		if (lb_is_field(p, PROXY_FIELD))
			return (1);
	return (0);
}

static const char *lb_field_value(const char *p)
{
	while (p && *p && *p != '\t')
		if ( *p++ == '=')
			return (p);
	return (NULL);
}

static int lb_is_booked(struct line_buffer *pi, time_t *t)
{
	const char *p;

	for (p=lb_first_field(pi); p; p=lb_next_field(pi))
		if (lb_is_field(p, BOOKED_FIELD))
		{
			const char *q=lb_field_value(p);
			unsigned long ul;

			if (q && sscanf(q, "%lu", &ul) == 1)
			{
				if (t)
					*t= (time_t)ul;
				return (1);
			}
		}
	return (0);
}

static char *lb_field_value_buf(const char *p)
{
	const char *q;
	char *r;

	while (p && *p && *p != '\t')
		if ( *p++ == '=')
			break;

	if (!p)
		return (strdup(""));

	q=strchr(p, '\t');
	if (!q)
		return (strdup(p));

	r=malloc(q-p+1);

	if (!r)
		return (NULL);

	memcpy(r, p, q-p);
	r[q-p]=0;
	return (r);
}

static const char *lb_is_pendel(struct line_buffer *pi)
{
	const char *p;

	for (p=lb_first_field(pi); p; p=lb_next_field(pi))
		if (lb_is_field(p, PENDEL_FIELD))
			return (p);
	return (0);
}

static int lb_remove_field(struct line_buffer *pi,
			   FILE *nfp,
			   const char *fieldname,
			   const char *newfield,
			   const char *newvalue)
{
	char *q;
	const char *cp;

	q=lb_event_id(pi);
	if (!q)
		return (-1);

	fprintf(nfp, "%s\t", q);
	free(q);
	q=lb_filename_nodir(pi);
	if (!q)
		return (-1);
	fprintf(nfp, "%s", q);
	free(q);

	for (cp=lb_first_field(pi); cp; cp=lb_next_field(pi))
	{
		const char *cc;

		if (lb_is_field(cp, fieldname))
		    continue;

		cc=strchr(cp, '\t');
		if (!cc) cc=cp+strlen(cp);

		putc('\t', nfp);
		if (fwrite(cp, cc-cp, 1, nfp) != 1)
			break;
	}
	if (newfield)
	{
		fprintf(nfp, "\t%s", newfield);
		if (newvalue)
			fprintf(nfp, "=%s", newvalue);
	}
	putc('\n', nfp);
	return (0);
}

static int docommitevent(struct PCPdir *, struct PCPdir_new_eventid *,
			 int,
			 struct PCP_commit *);

static int commitevent(struct PCPdir *pd, struct PCPdir_new_eventid *ae,
		       struct PCP_commit *ci)
{
	return (docommitevent(pd, ae, 0, ci));
}

static int bookevent(struct PCPdir *pd, struct PCPdir_new_eventid *ae,
		     struct PCP_commit *ci)
{
	return (docommitevent(pd, ae, 1, ci));
}

static int pcp_is_conflict(struct line_buffer *,
			   const struct PCP_event_time **, unsigned,
			   struct PCP_commit *,
			   struct PCP_uncancel *);

static int eventexists(struct PCPdir *pd, const char *e, int *flag)
{
	FILE *fp=fopen(pd->indexname, "r");
	struct line_buffer pi;

	if (!fp)
		return (0);

	if (fseek(fp, 0L, SEEK_SET) < 0)
		return (-1);


	if (lb_init(&pi))
	{
		fclose(fp);
		return (-1);
	}

	*flag=0;

	while (lb_read(fp, &pi) == 0)
	{
		if (lb_is_eventid(&pi, e))
		{
			*flag=1;
			break;
		}
	}
	lb_destroy(&pi);
	if (ferror(fp))
	{
		fclose(fp);
		return (-1);
	}

	if (fclose(fp))
		return (-1);
	return (0);
}


static int docommitevent(struct PCPdir *pd, struct PCPdir_new_eventid *ae,
			 int bookmode,
			 struct PCP_commit *ci)
{
	const struct PCP_event_time **times;
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	unsigned ut;

	char *deleted_event=0;	/* Filename of deleted event */
	int is_cancelled=0;
	int is_booked=0;
	int was_booked=0;

	ci->errcode=0;


	if (ae->tmpfile == NULL)	/* Already commited */
	{
		ci->errcode=PCP_ERR_EVENTNOTFOUND;
		return (-1);
	}

	if (ci->n_event_times <= 0)
	{
		ci->errcode=PCP_ERR_CONFLICT;
		return (-1);
	}

	/* Sort event times in chronological order */

	times=pcp_add_sort_times(ci->event_times,
				     ci->n_event_times);
	if (!times)
		return (-1);

	/* Use a dotlock to protect: reading old index + checking for
	** conflicts, writing a new index, updating everything */

	dotlock=acquire_dotlock(pd);

	if (!dotlock)
	{
		ci->errcode=PCP_ERR_LOCK;
		free(times);
		return (-1);
	}

	rc= -1;
	fp=fopen(pd->indexname, "a+");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		nfp=fopen(pd->newindexname, "w");
		if (nfp)
		{
			struct line_buffer pi;

			if (lb_init(&pi) == 0)
			{
				rc= 0;
				while (lb_read(fp, &pi) == 0)
				{
					if (ae->booked &&
					    lb_is_eventid(&pi,
							  ae->eventid.eventid))
					{
						was_booked=1;
						if (bookmode)
							continue;
						/* Drop old booking */

						if (lb_remove_field(&pi, nfp,
								    BOOKED_FIELD,
								    NULL,
								    NULL))
							rc= -1;
						is_booked=1;
						continue;
					}
					if (ae->oldeventid
					    && lb_is_eventid(&pi,
							     ae->oldeventid))
					{
						if (lb_is_cancelled(&pi))
							is_cancelled=1;
						if (deleted_event) /* ??? */
							free(deleted_event);
						deleted_event=lb_filename
							(pd, &pi);
						if (!deleted_event)
							rc= -1;
						if (bookmode)
						{
							const char *pendel=
								lb_is_pendel(&pi);
							if (pendel)
							{
								/* Event is locked by another book/update? */

								char *delid=lb_field_value_buf(pendel);

								if (!delid)
									rc= -1;
								else if (strcmp(delid, ae->eventid.eventid))
								{
									int flag=0;
									/*
									** Possibly, but make sure the locking
									** event actually exists.
									*/
									if (eventexists(pd, delid, &flag))
										rc= -1;
									else if (flag)
									{
										rc= -1;
										ci->errcode=PCP_ERR_EVENTLOCKED;
									}
								}
								fprintf(nfp, "%s\n",
									pi.buffer);
								free(delid);
							}
							else
							{
								lb_remove_field(&pi,
										nfp,
										PENDEL_FIELD,
										PENDEL_FIELD,
										ae->eventid.eventid);
							}
							/* Dont del just yet */
						}
						continue;
					}
					fprintf(nfp, "%s\n", pi.buffer);

					/* Check for conflicts UNLESS */

					if ((ci->flags & PCP_OK_CONFLICT)
					    == 0 &&
					    /* Conflics are OK */

					    (!ae->booked || bookmode) &&
					    /*
					    ** We're not committing a booked
					    ** event (check for conflicts at
					    ** time of booking).
					    */

					    !lb_is_cancelled(&pi) &&
					    /* This event is cancelled */

					    pcp_is_conflict(&pi,
							    times,
							    ci->n_event_times,
							    ci,
							    NULL)
					    )
					{
						rc= -1;
					}
				}

				if (rc == 0 && !ferror(fp) && !ferror(nfp)
				    && !is_booked)
				{
					/* Add a new index record */

					const char *p=
						strrchr(ae->newfile, '/');

					if (p)
						++p;
					else
						p=ae->newfile;

					fprintf(nfp, "%s\t%s",
						ae->eventid.eventid, p);

					/*
					** If older event is cancelled, so
					** is this event.
					*/

					if (is_cancelled)
					{
						fprintf(nfp, "\t"
							CANCELLED_FIELD);
					}

					if (ci->flags & PCP_BYPROXY)
						fprintf(nfp, "\t"
							PROXY_FIELD);

					if (bookmode)
					{
						time_t book_time;

						time(&book_time);
						fprintf(nfp, "\t" BOOKED_FIELD
							"=%lu",
							(unsigned long)
							book_time);
					}

					for (ut=0; ut<ci->n_event_times; ut++)
						fprintf(nfp, "\t"
							TIME_FIELD
							"=%lu,%lu",
							(unsigned long)
							times[ut]->start,
							(unsigned long)
							times[ut]->end);


					for (ut=0; ut<ae->n_event_participants;
					     ut++)
					{
						const char *p=
							ae->event_participants
							[ut].address;
						const char *q=
							ae->event_participants
							[ut].eventid;

						if (!p)
							continue;

						fprintf(nfp,
							"\t"
							PARTICIPANT_FIELD
							"=%s%s%s", p,
							q ? " ":"", q ? q:"");

						/* Sanity check: */

						while (*p)
						{
							if ((int)
							    (unsigned char)*p
							    <= ' ')
							{
								rc= -1;
								errno=EINVAL;
							}
							++p;
						}
					}

					fprintf(nfp, "\n");
				}
				if (ae->booked && !was_booked)
				{
					rc= -1;
					ci->errcode=PCP_ERR_EVENTNOTFOUND;
					ae->booked=0;
					/* Booked event disappeared */
				}

				if (rc == 0 && 
				    (fflush(nfp) < 0 || ferror(nfp)))
					rc= -1;

				lb_destroy(&pi);
			}
			if (fclose(nfp))
				rc= -1;
		}
		if (fclose(fp))
			rc= -1;

		if (rc == 0 && !bookmode)
		{
			if (rename(ae->tmpfile, ae->newfile))
				rc= -1;
		}

		if (rc == 0)
		{
			if (rename(pd->newindexname, pd->indexname))
				rc=-1;
		}

		if (rc == 0 && !bookmode)
		{
			if (deleted_event)
				unlink(deleted_event);
			free(ae->tmpfile);
			ae->tmpfile=0;
			ae->booked=0;
		}
		if (rc == 0 && bookmode)
			ae->booked=1;
	}
	else if (fp)
		fclose(fp);

	if (deleted_event)
		free(deleted_event);
	markchanged(pd);
	release_dotlock(pd, dotlock);
	free(times);
	return (rc);
}

/*
** Spring cleaning.
*/

struct cleanup_filename_list {
	struct cleanup_filename_list *next;
	char *filename;
} ;

static int event_expired(struct line_buffer *, time_t);

static int cleanup(struct PCPdir *pd)
{
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	struct stat stat_buf;
	struct cleanup_filename_list *list=NULL;
	time_t now;

	dotlock=acquire_dotlock(pd);

	if (!dotlock)
		return (-1);

	/*
	** Read the current index.
	** Remove entries for events whose files don't exist.
	**
	** Save list of all files that are indexed.
	*/

	time(&now);
	rc= -1;
	fp=fopen(pd->indexname, "a+");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		nfp=fopen(pd->newindexname, "w");
		if (nfp)
		{
			struct line_buffer pi;

			if (lb_init(&pi) == 0)
			{
				rc= 0;
				while (lb_read(fp, &pi) == 0)
				{
					char *filename=lb_filename(pd, &pi);
					struct cleanup_filename_list *l;
					time_t booked_time;

					if (!filename)
					{
						rc= -1;
						break;
					}

					if (!*filename)
					{
						free(filename);
						continue;
					}

					if (lb_is_booked(&pi, &booked_time))
					{
						/* Expire books after 1 hr */

						if (booked_time < now - 60*60)
						{
							free(filename);
							continue;
						}
					}
					else if (stat(filename, &stat_buf))
					{
						if (errno != ENOENT)
						{
							free(filename);
							rc= -1;
							break;
						}
						free(filename);
						continue;
					}
					else if ( event_expired(&pi,
								now -
								CALENDARPURGE *
								60 * 60 * 24))
					{
						free(filename);
						continue;
						/*
						** cleanup loop below will
						** take care of the event file
						*/
					}

					l=malloc(sizeof(*list));
					l->next=list;
					l->filename=filename;
					list=l;

					fprintf(nfp, "%s\n", pi.buffer);
				}
				lb_destroy(&pi);
			}

			if (rc == 0 && (ferror(fp) || ferror(nfp)
					|| fflush(nfp)))
				rc= -1;

			if (fclose(nfp))
				rc= -1;
		}
		if (fclose(fp))
			rc= -1;

		if (rename(pd->newindexname, pd->indexname))
			rc= -1;
	}
	else if (fp)
		fclose(fp);

	if (rc == 0)	/* Time to scan the directory */
	{
		DIR *dirp=opendir(pd->dirname);
		struct dirent *de;
		const char *p;
		time_t now;

		time(&now);

		while (dirp && (de=readdir(dirp)) != NULL)
		{
			char *filename=malloc(strlen(pd->dirname)+2+
					      strlen(de->d_name));
			if (!filename)
			{
				rc= -1;
				break;
			}
			strcat(strcat(strcpy(filename, pd->dirname),
				      "/"), de->d_name);
			if (isdigit((int)(unsigned char)de->d_name[0]))
			{
				struct cleanup_filename_list *l;
				for (l=list; l; l=l->next)
					if (strcmp(l->filename, filename) == 0)
						break;

				if (!l)
				{
					unlink(filename);
				}
			}
			else
			{
				p=strrchr(filename, '.');
				if (p && (strcmp(p, ".tmp") == 0 ||
					  strcmp(p, ".lock") == 0) &&
				    stat(filename, &stat_buf) == 0 &&
				    stat_buf.st_mtime < now - 36 * 60 * 60)
					unlink(filename);
			}
			free(filename);
		}
		if (dirp)
			closedir(dirp);
	}
	release_dotlock(pd, dotlock);

	while (list)
	{
		struct cleanup_filename_list *l=list;

		list=l->next;
		free(l->filename);
		free(l);
	}
	return (rc);
}

static int parse_time_field(const char *, time_t *, time_t *);

static int event_expired(struct line_buffer *pi, time_t when)
{
	const char *p;

	for (p=lb_first_field(pi); p; p=lb_next_field(pi))
	{
		time_t start_time;
		time_t end_time;

		if (!lb_is_field(p, TIME_FIELD))
			continue;

		if (parse_time_field(lb_field_value(p),
				     &start_time, &end_time))
			continue;
		if (end_time > when)
			return (0);
	}
	return (1);
}

/* Check if existing event conflicts with new event */

static int parse_time_field(const char *p, time_t *start_time,
			    time_t *end_time)
{
	if (!p)
		return (-1);

	*start_time=0;
	*end_time=0;

	while (*p && isdigit((int)(unsigned char)*p))
	{
		*start_time=*start_time * 10 + (*p-'0');
		++p;
	}
	if (*p != ',')
		return (-1);

	++p;
	while (*p && isdigit((int)(unsigned char)*p))
	{
		*end_time=*end_time * 10 + (*p-'0');
		++p;
	}
	return (0);
}

static int pcp_is_conflict(struct line_buffer *lb,
			   const struct PCP_event_time **t, unsigned n,
			   struct PCP_commit *ae,
			   struct PCP_uncancel *un)
{
	unsigned i;
	const char *p;
	int rc=0;

	char *event_id=lb_event_id(lb);

	if (!event_id)
		return (-1);

	for (p=lb_first_field(lb); p; p=lb_next_field(lb))
	{
		time_t start_time;
		time_t end_time;

		if (!lb_is_field(p, TIME_FIELD))
			continue;

		if (parse_time_field(lb_field_value(p),
				     &start_time, &end_time))
			continue;

		for (i=0; i<n; i++)
		{
			time_t com_start;
			time_t com_end;

			if (start_time >= t[i]->end)
				continue;
			if (t[i]->start >= end_time)
				continue;

			com_start=start_time;
			com_end=end_time;
			if (t[i]->start > com_start)
				com_start=t[i]->start;
			if (t[i]->end < com_end)
				com_end=t[i]->end;

			rc= -1;
			if (ae)
			{
				ae->errcode=PCP_ERR_CONFLICT;
				if (ae->add_conflict_callback)
				{
					rc=(*ae->add_conflict_callback)
						(event_id, com_start,
						 com_end,
						 "@",
						 ae->add_conflict_callback_ptr
						 );
					if (rc)
					{
						ae->errcode=0;
						break;
					}
					rc= -1;
				}
			}

			if (un)
			{
				un->errcode=PCP_ERR_CONFLICT;
				if (un->uncancel_conflict_callback)
				{
					rc=(*un->uncancel_conflict_callback)
						(event_id, com_start,
						 com_end, "@",
						 un->uncancel_conflict_callback_ptr
						 );
					if (rc)
					{
						un->errcode=0;
						break;
					}
					rc= -1;
				}
			}
		}

	}
	free(event_id);
	return (rc);
}

static int listallevents(struct PCPdir *pd, struct PCP_list_all *li)
{
	FILE *fp;
	struct line_buffer lb;
	int rc=0;
	const char *p;

	if ((fp=fopen(pd->indexname, "r")) == NULL)
		return (0);	/* Empty calendar */

	if (lb_init(&lb) == 0)
	{
		while (lb_read(fp, &lb) == 0)
		{
			char *event_id=lb_event_id(&lb);

			if (!event_id)
			{
				rc= -1;
				break;
			}

			li->event_id=event_id;

			for (p=lb_first_field(&lb); p; p=lb_next_field(&lb))
			{
				if (lb_is_field(p, TIME_FIELD))
				{
					time_t start_time;
					time_t end_time;

					if (parse_time_field(lb_field_value(p),
							     &start_time,
							     &end_time))
						continue;

					if (li->list_from
					    && li->list_from == li->list_to)
					{
						if (li->list_from < start_time
						    || li->list_to >= end_time)
							continue;
					}
					else
					{
						if (li->list_from &&
						    end_time <= li->list_from)
							continue;

						if (li->list_to &&
						    start_time >= li->list_to)
							continue;
					}
					li->event_from=start_time;
					li->event_to=end_time;

					if ((rc= (*li->callback_func)
					     (li, li->callback_arg)) != 0)
						break;
				}
				if (rc) break;

			}

			free(event_id);
			if (rc)
				break;
		}
		lb_destroy(&lb);
	}
	fclose(fp);
	return (rc);
}

static int read_event_times(struct line_buffer *pi,
			    struct PCP_event_time **time_ret,
			    unsigned *time_n_ret)
{
	const char *c;
	unsigned n;
	time_t start_time, end_time;

	*time_n_ret=0;
	*time_ret=NULL;

	for (c=lb_first_field(pi); c; c=lb_next_field(pi))
		if (lb_is_field(c, TIME_FIELD) &&
		    parse_time_field(lb_field_value(c),
				     &start_time, &end_time) == 0)
			++ *time_n_ret;

	if (!*time_n_ret)
		return (0);

	if ( (*time_ret=(struct PCP_event_time *)malloc
	      (sizeof(struct PCP_event_time)* *time_n_ret)) == NULL)
		return (-1);

	n=0;
	for (c=lb_first_field(pi); c; c=lb_next_field(pi))
		if (lb_is_field(c, TIME_FIELD) &&
		    parse_time_field(lb_field_value(c),
				     &start_time, &end_time) == 0)
		{
			(*time_ret)[n].start=start_time;
			(*time_ret)[n].end=end_time;
			++n;
		}
	return (0);
}

static int cancelevent(struct PCPdir *pd, const char *event, int *errcode)
{
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	int found=0;

	if (errcode) *errcode=0;
	dotlock=acquire_dotlock(pd);

	if (!dotlock)
		return (-1);

	rc= -1;
	fp=fopen(pd->indexname, "a+");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		nfp=fopen(pd->newindexname, "w");
		if (nfp)
		{
			struct line_buffer pi;

			if (lb_init(&pi) == 0)
			{
				rc= 0;
				while (lb_read(fp, &pi) == 0)
				{
					if (!lb_is_eventid(&pi, event))
					{
						fprintf(nfp, "%s\n",
							pi.buffer);
						continue;
					}
					found=1;
					if (lb_is_cancelled(&pi))
					{
						/* Already canned */
						fprintf(nfp, "%s\n",
							pi.buffer);
						continue;
					}

					fprintf(nfp, "%s\t"
						CANCELLED_FIELD
						"\n", pi.buffer);
				}
				lb_destroy(&pi);
				if (!found)
				{
					if (errcode)
						*errcode=PCP_ERR_EVENTNOTFOUND;
					errno=ENOENT;
					rc= -1;
				}
			}
			if (ferror(nfp) || fflush(nfp))
				rc= -1;
			if (fclose(nfp))
				rc= -1;
		}
		fclose(fp);

		if (rc == 0 && rename(pd->newindexname, pd->indexname))
			rc= -1;
	}
	else if (fp)
		fclose(fp);
	release_dotlock(pd, dotlock);
	return (rc);
}

static int uncancelevent(struct PCPdir *pd, const char *event, int flags,
			 struct PCP_uncancel *info)
{
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	int found=0;
	struct PCP_event_time *old_times=NULL;
	const struct PCP_event_time **old_times_sorted=NULL;
	unsigned n_old_times=0;

	if (info) info->errcode=0;
	dotlock=acquire_dotlock(pd);

	if (!dotlock)
		return (-1);

	rc= -1;
	fp=fopen(pd->indexname, "a+");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		struct line_buffer pi;

		/* First, find the existing booked times */

		if (lb_init(&pi) == 0)
		{
			if (info)
				info->errcode=PCP_ERR_EVENTNOTFOUND;
			while (lb_read(fp, &pi) == 0)
			{
				if (!lb_is_eventid(&pi, event))
					continue;
				if (read_event_times(&pi, &old_times,
						     &n_old_times) == 0)
				{
					if (fseek(fp, 0L, SEEK_SET) >= 0)
						rc=0;
					break;
				}
			}
			lb_destroy(&pi);

			if (rc == 0 && info)
				info->errcode=0;
		}

		if (rc == 0 && n_old_times &&
		    (old_times_sorted=
		     pcp_add_sort_times(old_times, n_old_times)) == NULL)
			rc= -1;

		nfp=rc == 0 ? fopen(pd->newindexname, "w"):NULL;
		if (nfp)
		{
			struct line_buffer pi;

			if (lb_init(&pi) == 0)
			{
				rc= 0;
				while (lb_read(fp, &pi) == 0)
				{
					char *id=lb_event_id(&pi);

					if (!id)
					{
						rc= -1;
						break;
					}

					if (!lb_is_eventid(&pi, event))
					{
						if ((flags & PCP_OK_CONFLICT)
						    == 0 &&
						    !lb_is_cancelled(&pi))
						{
							rc= pcp_is_conflict
								(&pi,
								 old_times_sorted,
								 n_old_times,
								 NULL,
								 info);
						}
						fprintf(nfp, "%s\n",
							pi.buffer);
						continue;
					}
					found=1;
					if (!lb_is_cancelled(&pi))
					{
						/* Already uncanned */
						fprintf(nfp, "%s\n",
							pi.buffer);
						continue;
					}

					if (lb_remove_field(&pi, nfp,
							    CANCELLED_FIELD,
							    NULL,
							    NULL))
						rc= -1;
				}
				lb_destroy(&pi);
				if (!found)
				{
					if (info)
						info->errcode=PCP_ERR_EVENTNOTFOUND;
					errno=ENOENT;
					rc= -1;
				}
			}
			if (ferror(nfp) || fflush(nfp))
				rc= -1;
			if (fclose(nfp))
				rc= -1;
		}
		fclose(fp);

		if (rc == 0 && rename(pd->newindexname, pd->indexname))
			rc= -1;
	}
	else if (fp)
		fclose(fp);
	if (old_times_sorted)
		free (old_times_sorted);
	if (old_times)
		free (old_times);
	release_dotlock(pd, dotlock);
	return (rc);
}

static int deleteevent(struct PCPdir *pd, struct PCP_delete *del)
{
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	char *deleted_event=0;

	del->errcode=0;
	dotlock=acquire_dotlock(pd);

	if (!dotlock)
		return (-1);

	rc= -1;
	fp=fopen(pd->indexname, "a+");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		nfp=fopen(pd->newindexname, "w");
		if (nfp)
		{
			struct line_buffer pi;

			if (lb_init(&pi) == 0)
			{
				rc= 0;
				while (lb_read(fp, &pi) == 0)
				{
					if (!lb_is_eventid(&pi, del->id))
					{
						fprintf(nfp, "%s\n",
							pi.buffer);
						continue;
					}

					if (deleted_event) /* ??? */
						free(deleted_event);
					deleted_event=lb_filename(pd, &pi);
					if (!deleted_event)
						rc= -1;
				}
				lb_destroy(&pi);
				if (!deleted_event)
				{
					del->errcode=PCP_ERR_EVENTNOTFOUND;
					errno=ENOENT;
					rc= -1;
				}
			}
			if (ferror(nfp) || fflush(nfp))
				rc= -1;
			if (fclose(nfp))
				rc= -1;
		}
		fclose(fp);

		if (rc == 0 && rename(pd->newindexname, pd->indexname))
			rc= -1;
	}
	else if (fp)
		fclose(fp);
	if (deleted_event && rc == 0)
		unlink(deleted_event);
	if (deleted_event)
		free(deleted_event);
	markchanged(pd);
	release_dotlock(pd, dotlock);
	return (rc);
}

static int retrheaders(struct PCPdir *, struct PCP_retr *, const char *);
static int retrrfc822(struct PCPdir *, struct PCP_retr *, const char *);

static int retrevent(struct PCPdir *pd, struct PCP_retr *ri)
{
	FILE *fp;
	int rc;

	rc= -1;

	fp=fopen(pd->indexname, "r");
	if (fp && fseek(fp, 0L, SEEK_SET) >= 0)
	{
		struct line_buffer pi;

		if (lb_init(&pi) == 0)
		{
			rc= 0;
			while (lb_read(fp, &pi) == 0)
			{
				const char *c;
				char *filename;
				unsigned x;
				int status;

				for (x=0; ri->event_id_list[x]; x++)
					if (lb_is_eventid(&pi,
							  ri->event_id_list[x])
					    )
						break;
				if (!(ri->event_id=ri->event_id_list[x]))
					continue;

				filename=lb_filename(pd, &pi);
				if (!filename)
				{
					rc= -1;
					break;
				}
				if (!*filename)
				{
					free(filename);
					continue;
				}

				status=0;
				if (lb_is_cancelled(&pi))
					status |= LIST_CANCELLED;
				if (lb_is_booked(&pi, NULL))
					status |= LIST_BOOKED;
				if (lb_is_proxy(&pi))
					status |= LIST_PROXY;

				if (ri->callback_retr_status)
				{
					rc= (*ri->callback_retr_status)
						(ri, status,
						 ri->callback_arg);
					if (rc)
						break;
				}

				for (c=lb_first_field(&pi); c;
				     c=lb_next_field(&pi))
				{
					if (lb_is_field(c, TIME_FIELD))
					{
						time_t from, to;

						if (!ri->callback_retr_date)
							continue;

						if (parse_time_field
						    (lb_field_value(c),
						     &from, &to))
							continue;

						rc=(*ri->
						    callback_retr_date)
							(ri, from, to,
							 ri->callback_arg);
						if (rc)
							break;
						continue;
					}

					if (lb_is_field(c, PARTICIPANT_FIELD))
					{
						char *pp, *qq;;
						if (ri->
						    callback_retr_participants
						    == NULL)
							continue;

						pp=lb_field_value_buf(c);

						if (!pp)
						{
							rc= -1;
							break;
						}

						if ((qq=strchr(pp, ' ')) != 0)
							*qq++=0;

						rc=(*ri->
						    callback_retr_participants)
							(ri, pp, qq,
							 ri->callback_arg);
						free(pp);
						if (rc)
							break;
						continue;
					}
				}

				if (rc)
				{
					free(filename);
					break;
				}

				if (ri->callback_headers_func == NULL &&
				    ri->callback_rfc822_func == NULL)
				{
					free(filename);
					continue;
				}

				if (ri->callback_begin_func)
				{
					rc= (*ri->callback_begin_func)
						(ri, ri->callback_arg);
					if (rc)
					{
						free(filename);
						break;
					}
				}

				if (ri->callback_rfc822_func)
					rc=retrrfc822(pd, ri, filename);
				else if (ri->callback_headers_func)
					rc=retrheaders(pd, ri, filename);

				if (rc == 0 && ri->callback_end_func)
					rc= (*ri->callback_end_func)
						(ri, ri->callback_arg);

				free(filename);
				if (rc)
					break;
			}
			lb_destroy(&pi);
		}
		fclose(fp);
	}
	else if (fp)
		fclose(fp);
	return (rc);
}

static int retrheaders(struct PCPdir *pd, struct PCP_retr *ri,
		       const char *filename)
{
	FILE *fp=fopen(filename, "r");
	struct rfc822hdr h;
	int rc=0;

	if (!fp)
		return (errno == ENOENT ? 0:-1);

	rfc822hdr_init(&h, 8192);

	while (rfc822hdr_read(&h, fp, NULL, 0) == 0)
	{
		if ((rc= (*ri->callback_headers_func)(ri, h.header,
						      h.value,
						      ri->callback_arg)) != 0)
			break;
	}
	if (rc == 0 && ferror(fp))
		rc= -1;
	rfc822hdr_free(&h);
	fclose(fp);
	return (0);
}

static int retrrfc822(struct PCPdir *pd, struct PCP_retr *ri,
		      const char *filename)
{
	int fd=open(filename, O_RDONLY);
	char buf[BUFSIZ];
	int n;
	int rc=0;

	if (fd < 0)
		return (errno == ENOENT ? 0:-1);

	while ((n=read(fd, buf, sizeof(buf))) > 0)
		if ((rc= (*ri->callback_rfc822_func)(ri, buf, n,
						     ri->callback_arg)) != 0)
			break;
	if (rc == 0 && n < 0)
		rc= -1;
	close(fd);
	return (rc);
}

static int setacl(struct PCPdir *pd, const char *who, int flags)
{
	char *dotlock;
	FILE *fp;
	FILE *nfp;
	int rc;
	char *aclname;
	char *newaclname;
	char buf[1024];

	if (strchr(who, '\r') || strchr(who, '\n') || strlen(who) > 512)
	{
		errno=EINVAL;
		return (-1);
	}

	aclname=malloc(strlen(pd->dirname)+sizeof("/acl"));

	if (!aclname)
		return (-1);

	newaclname=malloc(strlen(pd->dirname)+sizeof("/acl.new"));

	if (!newaclname)
	{
		free(aclname);
		return (-1);
	}

	dotlock=acquire_dotlock(pd);

	if (!dotlock)
	{
		free(newaclname);
		free(aclname);
		return (-1);
	}

	strcat(strcpy(aclname, pd->dirname), "/acl");
	strcat(strcpy(newaclname, pd->dirname), "/acl.new");

	rc= -1;
	nfp=fopen(newaclname, "w");
	if (nfp)
	{
		int l=strlen(who);

		fp=fopen(aclname, "r");
		if (fp)
		{
			while (fgets(buf, sizeof(buf), fp))
			{
				if (strncmp(buf, who, l) == 0 &&
				    isspace((int)(unsigned char)buf[l]))
					continue;
				fprintf(nfp, "%s", buf);
			}
			fclose(fp);
		}
		if (flags)
			fprintf(nfp, "%s\t%d\n", who, flags);
		rc=0;
		if (fflush(nfp) || ferror(nfp))
			rc= -1;
		if (fclose(nfp) || rename(newaclname, aclname))
			rc= -1;
	}
	release_dotlock(pd, dotlock);
	free(newaclname);
	free(aclname);
	return (rc);
}

static int listacl(struct PCPdir *pd, int (*func)(const char *, int, void *),
		   void *arg)
{
	char *aclname;
	char buf[1024];
	FILE *fp;

	aclname=malloc(strlen(pd->dirname)+sizeof("/acl"));

	if (!aclname)
		return (-1);

	strcat(strcpy(aclname, pd->dirname), "/acl");

	fp=fopen(aclname, "r");
	free(aclname);

	if (fp)
	{
		while (fgets(buf, sizeof(buf), fp))
		{
			char *p;
			int rc;

			for (p=buf; *p; p++)
				if (isspace((int)(unsigned char)*p))
				{
					*p++=0;
					break;
				}

			rc= (*func)(buf, atoi(p), arg);
			if (rc)
			{
				fclose(fp);
				return (rc);
			}
		}
		fclose(fp);
	}
	return (0);
}
