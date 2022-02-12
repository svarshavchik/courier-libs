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
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdexcept>
#include <functional>

// Prepend current working directory to a relative pathname (if it is relative)

static std::string prepend_wd(const std::string &maildir)
{
	char wd[PATH_MAX];

	if (getcwd(wd, sizeof(wd)-1) == NULL)
		throw std::runtime_error("getcwd() failed");

	if (maildir[0] == '/')
		wd[0]=0;
	else
		strcat(wd, "/");

	return std::string{wd}+maildir;
}

maildir::watch::watch(const std::string &maildir_arg)
	: maildir{prepend_wd(maildir_arg.empty() ? ".":maildir_arg)}
{

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
	inotify_fd=inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
#else
	notify_fd=inotify_init();

	if (inotify_fd >= 0 &&
	    (fcntl(inotify_fd, F_SETFL, O_NONBLOCK) < 0 ||
	     fcntl(inotify_fd, F_SETFD, FD_CLOEXEC)))
	{
		close(inotify_fd);
		inotify_fd=-1;
	}
#endif

	if (inotify_fd < 0)
	{
		throw std::runtime_error("inotity_init() failed");
	}
#endif
}

maildir::watch::~watch()
{
#if HAVE_INOTIFY_INIT
	if (inotify_fd >= 0)
	{
		close(inotify_fd);
	}
#endif
}

struct maildirwatch *maildirwatch_alloc(const char *maildir)
{
	if (!maildir)
		maildir="";

	return new maildirwatch{maildir};
}

void maildirwatch_free(struct maildirwatch *w)
{
	delete w;
}

void maildirwatch_cleanup()
{
}

#if HAVE_INOTIFY_INIT


/*
** Poll the inotify file descriptor. Returns false on timeout, true if
** the inotify file descriptor becomes ready before the timeout expires.
*/

bool maildir::watch::poll_inotify()
{
	time_t now2;

	int rc;

	struct pollfd pfd;

	while (now < timeout)
	{
		pfd.fd=inotify_fd;
		pfd.events=POLLIN;

		rc=poll(&pfd, 1, (timeout - now)*1000);

		now2=time(NULL);

		if (now2 < now)
			return true; /* System clock changed */

		now=now2;

		if (rc && pfd.revents & POLLIN)
			return true;
	}

	return false;
}

/*
** read() inotify_events from the inotify handler.
*/

static void read_inotify_events(
	int fd,
	const std::function<void(const inotify_event &ie)> &callback)
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
		return;
	}

	iecp=inotify_buffer;

	while (iecp < inotify_buffer+l)
	{
		struct inotify_event *ie=
			reinterpret_cast<struct inotify_event *>(iecp);

		iecp += sizeof(struct inotify_event)+ie->len;

		callback(*ie);
	}
}

#endif

bool maildir::watch::unlock(int nseconds)
{
	std::string p=maildir + "/" WATCHDOTLOCK;

#if HAVE_INOTIFY_INIT
	bool cancelled=false;

	auto handle=inotify_add_watch(inotify_fd, p.c_str(), IN_DELETE_SELF);
	bool removed=false;
	bool deleted=false;

	if (handle < 0)
	{
		if (errno == ENOENT)
		{
			/* Doesn't exist anymore, that's ok */
			return true;
		}

		fprintf(stderr, "ERR: %s: %s\n", p.c_str(), strerror(errno));
		return false;
	}

	if (nseconds < 0)
		nseconds=0;

	time(&now);

	timeout=now + nseconds;

	do
	{
		errno=ETIMEDOUT;

		if (!poll_inotify())
		{
			if (!cancelled)
			{
				/*
				** Timeout on the lock, cancel the inotify.
				*/
				timeout=now+15;
				cancelled=true;
				inotify_rm_watch(inotify_fd, handle);
				continue;
			}

			fprintf(stderr, "ERR:inotify timeout: %s\n",
				strerror(errno));

			break;
		}

		read_inotify_events(
			inotify_fd,
			[&]
			(const inotify_event &ie)
			{
				if (ie.wd == handle)
				{
					if (ie.mask & IN_DELETE_SELF)
						removed=true;

					if (ie.mask & IN_IGNORED)
						deleted=true;
				}
			});

		if (removed && !cancelled)
		{
			timeout=now+15;
			cancelled=true;
			inotify_rm_watch(inotify_fd, handle);
		}

		/* We don't terminate the loop until we get IN_IGNORED */

	} while (!deleted);

	return removed;
#else

	int n;

	for (n=0; n<nseconds; ++n)
	{
		if (access(p.c_str(), 0))
			return true;
		sleep(1);
	}
	return false;
#endif
}

int maildirwatch_unlock(struct maildirwatch *w, int nseconds)
{
	return w->unlock(nseconds) ? 0:-1;
}

int maildirwatch_start(struct maildirwatch *w,
		       struct maildirwatch_contents *mc)
{
	mc->w=w;

	return w->start(mc->handles) ? 1:0;
}

maildirwatch::contents::contents(watch &w)
	: w{w}, fallback{w.start(*this)}
{
}

maildirwatch::contents::~contents()
{
	w.end(*this);
}

bool maildir::watch::start(maildirwatch_contents_filehandles &handles)
{
	time(&now);
	timeout = now + 60;

#if HAVE_INOTIFY_INIT

	std::string s;

	s.reserve(maildir.size() + sizeof(KEYWORDDIR));

	s=maildir;
	s += "/new";

	handles.handles[0]=inotify_add_watch(inotify_fd, s.c_str(),
					     IN_CREATE |
					     IN_DELETE |
					     IN_MOVED_FROM |
					     IN_MOVED_TO);

	s=maildir;
	s += "/cur";
	handles.handles[1]=inotify_add_watch(inotify_fd, s.c_str(),
					     IN_CREATE |
					     IN_DELETE |
					     IN_MOVED_FROM |
					     IN_MOVED_TO);

	s=maildir;
	s += "/" KEYWORDDIR;

	handles.handles[2]=inotify_add_watch(inotify_fd, s.c_str(),
					     IN_CREATE |
					     IN_DELETE |
					     IN_MOVED_FROM |
					     IN_MOVED_TO);
	if (handles.handles[2] < 0)
	{
		// Maybe it's time to create it.

		create_keyword_dir(maildir, s);

		handles.handles[2]=inotify_add_watch(inotify_fd, s.c_str(),
						     IN_CREATE |
						     IN_DELETE |
						     IN_MOVED_FROM |
						     IN_MOVED_TO);
	}

	return false;
#else
	return true;
#endif
}

void maildir::create_keyword_dir(const std::string &maildir)
{
	create_keyword_dir(maildir, maildir + "/" KEYWORDDIR);
}

void maildir::create_keyword_dir(const std::string &maildir,
				 const std::string &keyworddir)
{
	struct stat stat_buf;

	mkdir(keyworddir.c_str(), 0700);

	/* Give it same mode as the maildir */

	if (stat(maildir.c_str(), &stat_buf) == 0)
		chmod(keyworddir.c_str(),
		      stat_buf.st_mode & 0777);
}

int maildirwatch_started(struct maildirwatch_contents *mc,
			 int *fdret)
{
	return mc->w->started(mc->handles, *fdret) ? 1:-1;
}

bool maildir::watch::contents::started(int &fd) const
{
	return w.started(*this, fd);
}

bool maildir::watch::started(const maildirwatch_contents_filehandles &handles,
			     int &fd) const
{
	fd= -1;

#if HAVE_INOTIFY_INIT

	for (auto h:handles.handles)
	{
		if (h < 0)
			return false;
	}

	fd=inotify_fd;

	return true;

#else
	fd= -1;

	return true;
#endif
}

bool maildir::watch::contents::check(int &fd, int &timeout) const
{
	return w.check(*this, fd, timeout);
}

int maildirwatch_check(struct maildirwatch_contents *mc,
		       int *changed,
		       int *fdret,
		       int *timeout)
{
	*changed=mc->w->check(mc->handles, *fdret, *timeout);
	return 0;
}

bool maildir::watch::check(const maildirwatch_contents_filehandles &handles,
			   int &fd, int &timeout_ret)
{
	time_t curTime;

	bool changed=false;

	fd=-1;

	curTime=time(NULL);

	if (curTime < now)
		timeout=curTime; /* System clock changed */
	now=curTime;

	timeout_ret=60;

#if HAVE_INOTIFY_INIT
	if (started(handles, fd))
	{
		timeout_ret=60 * 60;

		bool handled=true;

		while (handled)
		{
			handled=false;
			read_inotify_events(
				inotify_fd,
				[&]
				(const inotify_event &ie)
				{
					handled=true;

					for (auto h:handles.handles)
					{
						if (ie.wd == h)
							changed=true;
					}
				});
		}
	}
#endif
	if (now >= timeout)
	{
                timeout = now + timeout_ret;
		changed=true;
	}
	return changed;
}

#if HAVE_INOTIFY_INIT
bool maildir::watch::end_unwatch(maildirwatch_contents_filehandles &handles)
{
	for (auto h:handles.handles)
	{
		if (h >= 0)
		{
			inotify_rm_watch(inotify_fd, h);
			return true;
		}
	}
	return false;
}
#endif

void maildirwatch_end(struct maildirwatch_contents *mc)
{
	mc->w->end(mc->handles);
}

void maildir::watch::end(maildirwatch_contents_filehandles &handles)
{
#if HAVE_INOTIFY_INIT

	time(&now);
	timeout=now + 15;

	if (end_unwatch(handles)) /* Send the first inotify_rm_watch */
	{
		while (1)
		{
			if (!poll_inotify())
			{
				fprintf(stderr, "ERR:inotify timeout: %s\n",
					strerror(errno));
				break;
			}

			bool unwatched=false;

			read_inotify_events(
				inotify_fd,
				[&]
				(const inotify_event &ie)
				{
					for (auto &h:handles.handles)
					{
						if (ie.wd == h &&
						    ie.mask & IN_IGNORED)
						{
							h= -1;
							unwatched=true;
						}
					}
				});

			if (unwatched)
			{
				/* Send the next inotify_rm_watch? */
				if (!end_unwatch(handles))
					break; /* Nope, all done! */
			}
		}
	}
#endif
}
