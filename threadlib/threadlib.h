#ifndef	threadlib_h
#define	threadlib_h

/*
** Copyright 2000 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"

struct cthreadinfo;
struct cthreadlock;

#if	HAVE_PTHREADS
#include	"havepthread.h"
#else
#include	"nopthread.h"
#endif

#endif
