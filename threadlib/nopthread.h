#ifndef	nopthread_h
#define	nopthread_h

/*
** Copyright 2000 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif

struct cthreadinfo {
	void (*workfunc)(void *);
	void (*cleanupfunc)(void *);
	char *metadata_buf;
	} ;

struct cthreadinfo *cthread_init(
	unsigned,		/* Number of threads */
	unsigned,		/* Size of per-thread metadata */
	void (*)(void *),	/* The work function */
	void (*)(void *));	/* The cleanup function */

void cthread_wait(struct cthreadinfo *);

#define	cthread_go(cit, gofunc, arg)	\
	( (*gofunc)(cit->metadata_buf, arg),	\
		(*cit->workfunc)(cit->metadata_buf), \
		(*cit->cleanupfunc)(cit->metadata_buf), 0)

struct cthreadlock *cthread_lockcreate(void);
void cthread_lockdestroy(struct cthreadlock *);
int cthread_lock(struct cthreadlock *, int (*)(void *), void *);

extern struct cthreadlock cthread_dummy;

#define	cthread_lockcreate()	( &cthread_dummy )
#define	cthread_lockdestroy(p)
#define	cthread_lock(p,func,arg)	( (*func)(arg))

#endif
