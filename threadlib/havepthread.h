#ifndef	havepthread_h
#define	havepthread_h

/*
** Copyright 2000 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif

/*

This library encapsulates Posix threads in such a fashion as to be
able to transparently emulate the encapsulation if Posix threads are
not available.

That is, the caller simply implements the documented interface, and if Posix
threads are not available, this library will provide stub functions that will
implement a functional equivalent (although probably a far less efficient
one).

The interface implement by this library is as follows:

* A function is defined which will be executed by multiple threads in parallel.

* When this function returns, another 'cleanup' function will be called, except
  that this cleanup function will be single-threaded.

  In fact, the cleanup function may be executed by any thread.  All this is
  is when the main function, the work function, completes, a mutex is locked
  for the duration of the cleanup function call.

* The work function, and the cleanup function, can make use of some amount of
  metadata that can be initialized before starting the threaded function.

The general concept is that you have some section of code that can benefit
from being threaded.  So, it is threaded, and the execution resumes in
single-step function once the threaded section of the code completes.

*/

struct cthreadinfo *cthread_init(
	unsigned,		/* Number of threads */
	unsigned,		/* Size of per-thread metadata */
	void (*)(void *),	/* The work function */
	void (*)(void *));	/* The cleanup function */

/*

cthread_init is used to initialize and start all threads.  cthread_init returns
NULL if there was an error in setting up threading.  The first argument
specifies the number of threads to start.  The second argument specifies how
many bytes are there in per-thread metadata.  cthread_init will automatically
allocate nthreads*metasize bytes internally.  The third argument is a pointer
to the threaded function.  The fourth argument is a pointer to the cleanup
function.

cthread_init returns a handle pointer if threading has been succesfully
set up.

*/

void cthread_wait(struct cthreadinfo *);

/*

cthread_wait pauses until all current active threads have finished.  If there
are any threads which are currently busy executed the work function or the
cleanup function, cthread_wait pauses until they're done.  Then, cthread_wait
kills all the threads, and deallocates all allocated resources for the
handle

*/

int cthread_go( struct cthreadinfo *,		/* Handle */

	void (*)(void *, void *),	/* Initialization function */
	void *);	/* Second arg to the initialization func */

/*

cthread_go finds an unused thread, and has it execute the work function, then
the cleanup function.

Both the work function and the cleanup function receive a pointer to the
thread-specific metadata.  The second argument to cthread_go points to a
function that will be used to initialize the thread-specific metadata before
running the work function.  The first argument to this initialization function
will be a pointer to the thread-specific metadata, which is stored internally
by this library (associated with the handle).  When the initialization
function returns, the work/cleanup functions are called with the same pointer.
The third argument to cthread_go is passed as the second argument to the
initialization function.

If there are no available threads, cthread_go pauses until one becomes
available.  cthread_go returns immediately after starting the thread, so
upon return from cthread_go the work and cleanup functions are NOT guaranteed
to have been called already.  cthread_go merely starts the thread, which will
execute the work and the cleanup functions concurrently.

When Posix threads are not available, cthread_go is a stub.  It calls the
initialization function, then the work function, then the cleanup function,
and then returns to the caller.  Hopefully, these semantics will be sufficient
to carry out the necessary deed if threading is not available.
*/

/*

cthreadlock structure is used to implement locks.  Create a lock by calling
cthread_lockcreate.  Destroy a lock by calling cthread_lockdelete.

Calling cthread_lock obtains the lock specified by the first argument, calls
the function specified by the second argument.  When the function returns,
the lock is released.  The third argument is passed as the argument to the
function.

*/

struct cthreadlock *cthread_lockcreate(void);
void cthread_lockdestroy(struct cthreadlock *);
int cthread_lock(struct cthreadlock *, int (*)(void *), void *);

#endif
