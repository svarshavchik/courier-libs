/*
** Copyright 2002-2021 S. Varshavchik.
** See COPYING for distribution information.
*/

#include "config.h"
#include "maildirwatch.h"

#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/signal.h>
#if HAVE_INOTIFY_INIT
#include <sys/inotify.h>
#include <poll.h>
#include <fcntl.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct maildirwatch *maildirwatch_alloc(const char *maildir)
{
	char wd[PATH_MAX];
	struct maildirwatch *w;

	if (maildir == 0 || *maildir == 0)
		maildir=".";

	if (getcwd(wd, sizeof(wd)-1) == NULL)
		return NULL;

	if (*maildir == '/')
		wd[0]=0;
	else
		strcat(wd, "/");

	if ((w=malloc(sizeof(struct maildirwatch))) == NULL)
		return NULL;

	if ((w->maildir=malloc(strlen(wd)+strlen(maildir)+1)) == NULL)
	{
		free(w);
		return NULL;
	}

	strcat(strcpy(w->maildir, wd), maildir);

#if HAVE_INOTIFY_INIT
#if HAVE_INOTIFY_INIT1
#ifdef IN_CLOEXEC
#else
#undef HAVE_INOTIFY_INIT1
#endif
#ifdef IN_NONBLOCK
#else
#undef HAVE_INOTIFY_INIT1
#endif
#endif

#if HAVE_INOTIFY_INIT1
	w->inotify_fd=inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
#else
	w->inotify_fd=inotify_init();

	if (w->inotify_fd >= 0 &&
	    (fcntl(w->inotify_fd, F_SETFL, O_NONBLOCK) < 0 ||
	     fcntl(w->inotify_fd, F_SETFD, FD_CLOEXEC)))
	{
		close(w->inotify_fd);
		w->inotify_fd=-1;
	}
#endif

	if (w->inotify_fd < 0)
	{
		maildirwatch_free(w);
		w=NULL;
	}
#endif
	return w;
}

void maildirwatch_free(struct maildirwatch *w)
{
#if HAVE_INOTIFY_INIT
	if (w->inotify_fd >= 0)
	{
		close(w->inotify_fd);
	}
#endif

	free(w->maildir);
	free(w);
}

void maildirwatch_cleanup()
{
}

#if HAVE_INOTIFY_INIT

/*
** Poll the inotify file descriptor. Returns 0 on timeout, non-0 if
** the inotify file descriptor becomes ready before the timeout expires.
*/

static int poll_inotify(struct maildirwatch *w)
{
	time_t now2;

	int rc;

	struct pollfd pfd;

	while (w->now < w->timeout)
	{
		pfd.fd=w->inotify_fd;
		pfd.events=POLLIN;

		rc=poll(&pfd, 1, (w->timeout - w->now)*1000);

		now2=time(NULL);

		if (now2 < w->now)
			return 1; /* System clock changed */

		w->now=now2;

		if (rc && pfd.revents & POLLIN)
			return 1;
	}

	return 0;
}

/*
** read() inotify_events from the inotify handler.
*/

static int read_inotify_events(int fd,
			       void (*callback)(struct inotify_event *ie,
						void *arg),
			       void *arg)
{
	char inotify_buffer[sizeof(struct inotify_event)+NAME_MAX+1];
	int l;
	char *iecp;

	l=read(fd, inotify_buffer, sizeof(inotify_buffer));

	if (l < 0 &&
	    (errno == EAGAIN || errno == EWOULDBLOCK))
		l=0; /* Non-blocking socket timeout */

	if (l < 0)
	{
		fprintf(stderr, "ERR:inotify read: %s\n", strerror(errno));
		return -1;
	}

	iecp=inotify_buffer;

	while (iecp < inotify_buffer+l)
	{
		struct inotify_event *ie=
			(struct inotify_event *)iecp;

		iecp += sizeof(struct inotify_event)+ie->len;

		(*callback)(ie, arg);
	}
	return 0;
}

struct unlock_info {
	int handle;
	int removed;
	int deleted;
};

static void unlock_handler(struct inotify_event *ie,
			   void *arg)
{
	struct unlock_info *ui=(struct unlock_info *)arg;

	if (ie->wd == ui->handle)
	{
		if (ie->mask & IN_DELETE_SELF)
			ui->removed=1;

		if (ie->mask & IN_IGNORED)
			ui->deleted=1;
	}
}
#endif

static int do_maildirwatch_unlock(struct maildirwatch *w, int nseconds,
				  const char *p)
{
#if HAVE_INOTIFY_INIT
	int cancelled=0;

	struct unlock_info ui;

	ui.handle=inotify_add_watch(w->inotify_fd, p, IN_DELETE_SELF);
	ui.removed=0;
	ui.deleted=0;

	if (ui.handle < 0)
	{
		if (errno == ENOENT)
		{
			/* Doesn't exist anymore, that's ok */
			return 0;
		}

		fprintf(stderr, "ERR: %s: %s\n", p, strerror(errno));
		return -1;
	}

	if (nseconds < 0)
		nseconds=0;

	time(&w->now);

	w->timeout=w->now + nseconds;

	do
	{
		errno=ETIMEDOUT;

		if (!poll_inotify(w))
		{
			if (!cancelled)
			{
				/*
				** Timeout on the lock, cancel the inotify.
				*/
				w->timeout=w->now+15;
				cancelled=1;
				inotify_rm_watch(w->inotify_fd, ui.handle);
				continue;
			}

			fprintf(stderr, "ERR:inotify timeout: %s\n",
				strerror(errno));

			break;
		}

		read_inotify_events(w->inotify_fd, unlock_handler, &ui);

		if (ui.removed && !cancelled)
		{
			w->timeout=w->now+15;
			cancelled=1;
			inotify_rm_watch(w->inotify_fd, ui.handle);
		}

		/* We don't terminate the loop until we get IN_IGNORED */

	} while (!ui.deleted);

	return ui.removed;
#else

	int n;

	for (n=0; n<nseconds; ++n)
	{
		if (access(p, 0))
			return 1;
		sleep(1);
	}
	return 0;
#endif
}

int maildirwatch_unlock(struct maildirwatch *w, int nseconds)
{
	char *p;
	int rc;

	p=malloc(strlen(w->maildir)+ sizeof("/" WATCHDOTLOCK));

	if (!p)
		return -1;

	strcat(strcpy(p, w->maildir), "/" WATCHDOTLOCK);

	rc=do_maildirwatch_unlock(w, nseconds, p);

	free(p);
	return rc;
}

int maildirwatch_start(struct maildirwatch *w,
		       struct maildirwatch_contents *mc)
{
	mc->w=w;

	time(&w->now);
	w->timeout = w->now + 60;

#if HAVE_INOTIFY_INIT

	{
		char *s=malloc(strlen(w->maildir)
			       +sizeof("/" KEYWORDDIR));

		if (!s)
			return (-1);

		strcat(strcpy(s, w->maildir), "/new");

		mc->handles[0]=inotify_add_watch(w->inotify_fd, s,
						 IN_CREATE |
						 IN_DELETE |
						 IN_MOVED_FROM |
						 IN_MOVED_TO);


		strcat(strcpy(s, w->maildir), "/cur");

		mc->handles[1]=inotify_add_watch(w->inotify_fd, s,
						 IN_CREATE |
						 IN_DELETE |
						 IN_MOVED_FROM |
						 IN_MOVED_TO);

		strcat(strcpy(s, w->maildir), "/" KEYWORDDIR);

		mc->handles[2]=inotify_add_watch(w->inotify_fd, s,
						 IN_CREATE |
						 IN_DELETE |
						 IN_MOVED_FROM |
						 IN_MOVED_TO);
		free(s);
	}

	return 0;
#else
	return 1;
#endif
}

int maildirwatch_started(struct maildirwatch_contents *mc,
			 int *fdret)
{
#if HAVE_INOTIFY_INIT
	int n;
#endif

	*fdret= -1;

#if HAVE_INOTIFY_INIT

	for (n=0; n<sizeof(mc->handles)/sizeof(mc->handles[0]); ++n)
	{
		if (mc->handles[n] < 0)
			return -1;
	}

	*fdret=mc->w->inotify_fd;

	return 1;

#else
	*fdret= -1;

	return 1;
#endif
}

#if HAVE_INOTIFY_INIT

struct check_info {
	struct maildirwatch_contents *mc;
	int *changed;
	int handled;
};

static void check_handler(struct inotify_event *ie,
			  void *arg)
{
	struct check_info *ci=(struct check_info *)arg;
	int n;

	ci->handled=1;

	for (n=0; n<sizeof(ci->mc->handles)/sizeof(ci->mc->handles[0]); ++n)
	{
		if (ie->wd == ci->mc->handles[n])
			*ci->changed=1;
	}

}
#endif

int maildirwatch_check(struct maildirwatch_contents *mc,
		       int *changed,
		       int *fdret,
		       int *timeout)
{
	struct maildirwatch *w=mc->w;
	time_t curTime;
#if HAVE_INOTIFY_INIT
	struct check_info ci;

	ci.mc=mc;
	ci.changed=changed;
#endif

	*changed=0;
	*fdret=-1;

	curTime=time(NULL);

	if (curTime < w->now)
		w->timeout=curTime; /* System clock changed */
	w->now=curTime;

	*timeout=60;

#if HAVE_INOTIFY_INIT
	if (maildirwatch_started(mc, fdret) > 0)
	{
		*timeout=60 * 60;

		*fdret=w->inotify_fd;

		ci.handled=1;

		while (ci.handled)
		{
			ci.handled=0;
			read_inotify_events(w->inotify_fd, check_handler, &ci);
		}
	}
#endif
	if (w->now >= w->timeout)
	{
                w->timeout = w->now + *timeout;
		*changed=1;
	}
	return 0;
}

#if HAVE_INOTIFY_INIT

struct end_info {
	struct maildirwatch_contents *mc;
	int unwatched;
};

static void end_handler(struct inotify_event *ie,
			void *arg)
{
	struct end_info *ei=(struct end_info *)arg;
	int n;

	for (n=0; n<sizeof(ei->mc->handles)/sizeof(ei->mc->handles[0]); ++n)
	{
		if (ie->wd == ei->mc->handles[n] &&
		    ie->mask & IN_IGNORED)
		{
			ei->mc->handles[n]=-1;
			ei->unwatched=1;
		}
	}
}

static int maildir_end_unwatch(struct maildirwatch_contents *mc)
{
	int n;

	for (n=0; n<sizeof(mc->handles)/sizeof(mc->handles[0]); ++n)
	{
		if (mc->handles[n] >= 0)
		{
			inotify_rm_watch(mc->w->inotify_fd,
					 mc->handles[n]);
			return 1;
		}
	}
	return 0;
}
#endif

void maildirwatch_end(struct maildirwatch_contents *mc)
{
#if HAVE_INOTIFY_INIT
	struct end_info ei;

	time(&mc->w->now);
	mc->w->timeout=mc->w->now + 15;

	if (maildir_end_unwatch(mc)) /* Send the first inotify_rm_watch */
	{
		while (1)
		{
			if (poll_inotify(mc->w) != 1)
			{
				fprintf(stderr, "ERR:inotify timeout: %s\n",
					strerror(errno));
				break;
			}

			ei.mc=mc;
			ei.unwatched=0;
			read_inotify_events(mc->w->inotify_fd,
						end_handler, &ei);

			if (ei.unwatched)
			{
				/* Send the next inotify_rm_watch? */
				if (!maildir_end_unwatch(mc))
					break; /* Nope, all done! */
			}
		}
	}
#endif
}
