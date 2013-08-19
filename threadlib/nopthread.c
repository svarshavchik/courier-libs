/*
** Copyright 2000-2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"threadlib.h"

#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<unistd.h>

typedef struct cthreadinfo cthreadinfo_t;


cthreadinfo_t *cthread_init(unsigned nthreads_, unsigned metasize,
	void (*workfunc_)(void *),
	void (*cleanupfunc_)(void *))
{
cthreadinfo_t *cit;

	if ((cit=(cthreadinfo_t *)malloc(sizeof(cthreadinfo_t))) == 0)
		return (0);

	cit->cleanupfunc=cleanupfunc_;
	cit->workfunc=workfunc_;

	if ( (cit->metadata_buf=malloc(metasize)) == 0)
	{
		free( (char *)cit );
		return (0);
	}
	return (cit);
}

void cthread_wait(cthreadinfo_t *cit)
{
	free(cit->metadata_buf);
	free( (char *)cit);
}


struct cthreadlock {
	int dummy;
	} ;

struct cthreadlock cthread_dummy;
