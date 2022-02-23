#ifndef maildirwatch_h
#define maildirwatch_h
/*
** Copyright 2002-2021 S. Varshavchik.
** See COPYING for distribution information.
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif

#ifdef __cplusplus

extern "C" {
#if 0
}
#endif
#endif

// Internal helper object
struct maildirwatch_contents_filehandles {
#if HAVE_INOTIFY_INIT
	int handles[3];
#endif
};

#ifdef __cplusplus
#if 0
{
#endif
}

#include <string>

namespace maildir {
#if 0
}
#endif

/////////////////////////////////////////////////////////////////////////////
//
// Wait for changes to a maildir.
//
// The constructor initializes a watcher for the maildir path specified
// by the constructor:
//
// maildir::watch watcher{maildir};
//
// Lock this maildir. The destructor unlocks it. An exception gets thrown
// if there was an internal error of some kind.
//
// maildir::watch::lock locked{watcher};
//
// Wait for up to this many seconds for a process that used lock()
// to unlock the maildir. Return immediately if there is no lock. Returns
// false in case of a timeout:
//
// bool flag=watcher.unlock(30);
//
// Start monitoring the maildir:
//
// watch::contents wait4{watcher};
//
// started() is a sanity check to verify that the maildir has successfully
// being monitored. false gets returned if the maildir was corrupted (missing
// cur, new, courierimapkeywords).
//
// if (wait4.started())
//
// check() immediately returns true or false, indicating whether or not
// the maildir has changed. A false return indicates that the caller should
// wait for up to "timeout" seconds for the "fd" file descriptor to become
// readable, then call check() again.
//
// int fd;
// int timeout;
// if (wait4.check(fd, timeout))
//
// The destructor stops watching the maildir.

class watch {
	std::string maildir;

#if HAVE_INOTIFY_INIT
	int inotify_fd;
#endif
	time_t now;
	time_t timeout;

	int fd();

	bool poll_inotify();

 public:
	watch(const std::string &);
	~watch();

	watch(const watch &)=delete;
	watch &operator=(const watch &)=delete;

	watch(watch &&w);
	watch &operator=(watch &&w);

	class lock {
		std::string lockname;

	public:

		lock(watch &w);
		lock(watch &&w);

		~lock();

		lock(const lock &)=delete;
		lock &operator=(const lock &)=delete;
	};

	bool unlock(int nseconds);

	class contents : maildirwatch_contents_filehandles {

		watch &w;

	public:
		const bool fallback;

		contents(watch &);
		~contents();

		bool started() const;

		bool check(int &fd, int &timeout) const;

		contents(const contents &)=delete;
		contents &operator=(const contents &)=delete;
	};

	// TODO: make the following private after the C API is removed.
	bool start(maildirwatch_contents_filehandles &handles);
	bool started(const maildirwatch_contents_filehandles &handles,
		     int &fd) const;

	bool check(const maildirwatch_contents_filehandles &handles,
		   int &fd, int &timeout);

	void end(maildirwatch_contents_filehandles &handles);
	bool end_unwatch(maildirwatch_contents_filehandles &handles);
};

// Internal utility functions:

void create_keyword_dir(const std::string &maildir);

void create_keyword_dir(const std::string &maildir,
			const std::string &keyworddir);

#if 0
{
#endif
}

extern "C" {
#endif

	/* C API */

struct maildirwatch;

#ifdef __cplusplus
struct maildirwatch : maildir::watch {

	using watch::watch;
};
#endif

#define WATCHDOTLOCK	"tmp/courier.lock"

#define KEYWORDDIR "courierimapkeywords"

struct maildirwatch *maildirwatch_alloc(const char *maildir);

void maildirwatch_free(struct maildirwatch *w);
	/*
	** Wait for WATCHDOTLOCK to go away
	*/

void maildirwatch_cleanup();
	/* Final cleanup before prog terminates */

int maildirwatch_unlock(struct maildirwatch *w, int nseconds);

	/*********** Wait for changes to new and cur subdirs ************/

	/* Caller must allocate the follownig structure: */

struct maildirwatch_contents  {
	struct maildirwatch *w;

	struct  maildirwatch_contents_filehandles handles;
};

/*
** maildirwatch_start() initiates the process of monitoring the maildir.
**
** Returns: 0 - monitoring started.
**          1 - inotify not available, will fall back to 60 second polls.
**         -1 - Fatal error.
*/

int maildirwatch_start(struct maildirwatch *p,
		       struct maildirwatch_contents *w);

/*
** Check the status of inotify monitoring.
**
** Returns: 1 - Monitoring has started, or we're in fallback mode.
**          0 - Not yet, *fdret is initialized to file descriptor to wait on.
**              (not used at this time).
**         -1 - A fatal error occurred, fall back to polling mode.
**
** maildirwatch_started() returns right away, without blocking.
*/

int maildirwatch_started(struct maildirwatch_contents *w,
			 int *fdret);

/*
** Check if maildir's contents have changed.
**
** Returns: 0 - Monitoring in progress.  *changed set to non-zero if maildir
**              was changed.
**         -1 - Fatal error.
**
** *fdret and *timeout get initialized to the file descriptor to wait on,
** and the requested timeout.  *fdret may be negative in polling mode, this
** should be interpreted as: if *changed is not set, sleep for this period of
** time.
*/

int maildirwatch_check(struct maildirwatch_contents *w,
		       int *changed,
		       int *fdret,
		       int *timeout);

	/*
	** Clean everything up.
	*/
void maildirwatch_end(struct maildirwatch_contents *w);


	/*
	** Courier-IMAP compatible maildir lock.
	**
	** Returns a non-NULL filename on success.  To unlock:
	**
	** unlink(filename); free(filename);
	**
	** tryAnyway is now ignored, if passed in it will get set to 0.
	**
	*/
char *maildir_lock(const char *maildir,
		   struct maildirwatch *w, /* If NULL, we sleep() */
		   int *tryAnyway);

#ifdef  __cplusplus
}
#endif

#endif
