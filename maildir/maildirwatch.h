#ifndef maildirwatch_h
#define maildirwatch_h
/*
** Copyright 2002-2021 S. Varshavchik.
** See COPYING for distribution information.
*/


#ifdef  __cplusplus
extern "C" {
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

/*
** These function use inotify to watch for maildir changes.
*/

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

struct maildirwatch {
	char *maildir;

#if HAVE_INOTIFY_INIT
	int inotify_fd;
#endif
	time_t now;
	time_t timeout;

};

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

struct maildirwatch_contents {
	struct maildirwatch *w;

#if HAVE_INOTIFY_INIT
	int handles[3];
#endif
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
	** A NULL return with tryAnyway != 0 means that the lock failed
	** probably as a result of misconfigured FAM, or something.
	**
	*/
char *maildir_lock(const char *maildir,
		   struct maildirwatch *w, /* If NULL, we sleep() */
		   int *tryAnyway);

#ifdef  __cplusplus
}
#endif

#endif
