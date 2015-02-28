/*
** Copyright 2000-2007 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"threadlib.h"

#include	<pthread.h>
#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<unistd.h>

typedef struct {
	unsigned	me;
	pthread_t	pt;

	pthread_cond_t	gocond;
	pthread_mutex_t	gomutex;
	int		goflag;

	struct		cthreadinfo *myinfo;

	void		*metadata;
	} cthread_t;

struct cthreadinfo {
	unsigned nthreads;
	void (*workfunc)(void *);
	void (*cleanupfunc)(void *);
	cthread_t *threads;
	char *metadata_buf;
	int *newbuf;
	int newcnt;

	pthread_cond_t	newtask_cond;
	pthread_mutex_t	newtask_mutex;

	pthread_mutex_t	cleanup_mutex;
	} ;

typedef struct cthreadinfo cthreadinfo_t;

static void mutexcleanup(void *vp)
{
pthread_mutex_t *mp=(pthread_mutex_t *)vp;

	pthread_mutex_unlock( mp );
}

#define PTHREAD_CHK(x) (errno=(x))

/*
**  This is the thread function wrapper.  It waits for its conditional
**  signal, runs the workhorse function, then puts its task id onto the ready
**  queue, then goes back to waiting for another go signal.
*/

static void *threadfunc(void *p)
{
cthread_t	*c= (cthread_t *)p;
cthreadinfo_t	*i= c->myinfo;

	for (;;)
	{
		while (PTHREAD_CHK(pthread_mutex_lock(&c->gomutex)))
		{
			perror("pthread_mutex_lock");
			sleep(3);
		}

		pthread_cleanup_push(&mutexcleanup, (void *)&c->gomutex);

		while (!c->goflag)
		{
			if (PTHREAD_CHK(pthread_cond_wait(&c->gocond,
							  &c->gomutex)))
			{
				perror("pthread_cond_wait");
				sleep(3);
			}
		}
		c->goflag=0;
		if (PTHREAD_CHK(pthread_mutex_unlock( &c->gomutex )))
			perror("pthread_mutex_unlock");

		pthread_cleanup_pop(0);

		(*i->workfunc)(c->metadata);

		while (PTHREAD_CHK(pthread_mutex_lock(&i->cleanup_mutex)))
		{
			perror("pthread_mutex_lock");
			sleep(3);
		}

		pthread_cleanup_push(&mutexcleanup, (void *)&i->cleanup_mutex);

		(*i->cleanupfunc)(c->metadata);

		if (PTHREAD_CHK(pthread_mutex_unlock( &i->cleanup_mutex )))
			perror("pthread_mutex_unlock");

		pthread_cleanup_pop(0);

		while (PTHREAD_CHK(pthread_mutex_lock(&i->newtask_mutex)))
		{
			perror("pthread_mutex_lock");
			sleep(3);
		}

		i->newbuf[i->newcnt++] = c->me;

		if (PTHREAD_CHK(pthread_mutex_unlock( &i->newtask_mutex )))
			perror("pthread_mutex_unlock");

		if (PTHREAD_CHK(pthread_cond_signal( &i->newtask_cond )))
			perror("pthread_cond_signal");
	}
	return 0;
}

static int initcondmutex(pthread_cond_t *c, pthread_mutex_t *m)
{
pthread_condattr_t cattr;
pthread_mutexattr_t mattr;

	if (c)
	{
		if ( PTHREAD_CHK(pthread_condattr_init(&cattr))) return (-1);
		if ( PTHREAD_CHK(pthread_cond_init(c, &cattr)))
		{
			pthread_condattr_destroy(&cattr);
			return (-1);
		}
		pthread_condattr_destroy(&cattr);
	}

	if ( PTHREAD_CHK(pthread_mutexattr_init(&mattr)))
	{
		if (c) pthread_cond_destroy(c);
		return (-1);
	}

	if ( PTHREAD_CHK(pthread_mutex_init(m, &mattr)))
	{
		pthread_mutexattr_destroy(&mattr);

		if (c) pthread_cond_destroy(c);
		return (-1);
	}
	pthread_mutexattr_destroy(&mattr);
	return (0);
}

cthreadinfo_t *cthread_init(unsigned nthreads_, unsigned metasize,
	void (*workfunc_)(void *),
	void (*cleanupfunc_)(void *))
{
unsigned i;
pthread_attr_t	pat;
cthreadinfo_t *cit;

	if ((cit=(cthreadinfo_t *)malloc(sizeof(cthreadinfo_t))) == 0)
		return (0);

	cit->nthreads=nthreads_;
	cit->cleanupfunc=cleanupfunc_;
	cit->workfunc=workfunc_;
	if ( (cit->threads=(cthread_t *)malloc(cit->nthreads * sizeof(cthread_t))) == 0)
	{
		free((char *)cit);
		return (0);
	}
	if ( (cit->metadata_buf=malloc(metasize * cit->nthreads)) == 0)
	{
		free( (char *)cit->threads);
		free( (char *)cit );
		return (0);
	}

	cit->newcnt=cit->nthreads;
	if ( (cit->newbuf=(int *)malloc(cit->nthreads * sizeof(int))) == 0)
	{
		free(cit->metadata_buf);
		free( (char *)cit->threads);
		free( (char *)cit );
		return (0);
	}
	for (i=0; i<cit->nthreads; i++)
	{
		cit->newbuf[i]=i;
	}

	if (initcondmutex(&cit->newtask_cond, &cit->newtask_mutex))
	{
		free(cit->newbuf);
		free(cit->metadata_buf);
		free( (char *)cit->threads);
		free( (char *)cit );
		return (0);
	}

	if (initcondmutex(0, &cit->cleanup_mutex))
	{
		pthread_cond_destroy(&cit->newtask_cond);
		pthread_mutex_destroy(&cit->newtask_mutex);

		free(cit->newbuf);
		free(cit->metadata_buf);
		free( (char *)cit->threads);
		free( (char *)cit );
		return (0);
	}

	if (PTHREAD_CHK(pthread_attr_init(&pat)))
	{
		pthread_mutex_destroy(&cit->cleanup_mutex);
		pthread_cond_destroy(&cit->newtask_cond);
		pthread_mutex_destroy(&cit->newtask_mutex);
		free(cit->newbuf);
		free(cit->metadata_buf);
		free( (char *)cit->threads);
		free( (char *)cit );
		return (0);
	}

	for (i=0; i<cit->nthreads; i++)
	{
		cit->threads[i].me=i;
		cit->threads[i].metadata=(void *) (cit->metadata_buf+i*metasize);
		cit->threads[i].myinfo=cit;

		if (initcondmutex(&cit->threads[i].gocond,
			&cit->threads[i].gomutex))
			break;

		cit->threads[i].goflag=0;

		if (PTHREAD_CHK(pthread_create(&cit->threads[i].pt, &pat,
					       &threadfunc,
					       (void *)&cit->threads[i])))
		{
			pthread_cond_destroy(&cit->threads[i].gocond);
			pthread_mutex_destroy(&cit->threads[i].gomutex);
			break;
		}
	}

	if ( i >= cit->nthreads)
	{
		pthread_attr_destroy(&pat);
		return (cit);
	}

	while (i)
	{
		--i;
		if (PTHREAD_CHK(pthread_cancel(cit->threads[i].pt)))
			perror("pthread_cancel");

		if (PTHREAD_CHK(pthread_join(cit->threads[i].pt, NULL)))
			perror("pthread_join");

		pthread_cond_destroy(&cit->threads[i].gocond);
		pthread_mutex_destroy(&cit->threads[i].gomutex);
	}

	pthread_attr_destroy(&pat);
	pthread_mutex_destroy(&cit->cleanup_mutex);
	pthread_cond_destroy(&cit->newtask_cond);
	pthread_mutex_destroy(&cit->newtask_mutex);
	free(cit->newbuf);
	free(cit->metadata_buf);
	free( (char *)cit->threads);
	free( (char *)cit );
	return (0);
}

void cthread_wait(cthreadinfo_t *cit)
{
	unsigned i;

	if (PTHREAD_CHK(pthread_mutex_lock(&cit->newtask_mutex)))
	{
		perror("pthread_mutex_lock");
		return;
	}

	pthread_cleanup_push(&mutexcleanup, (void *)&cit->newtask_mutex);

	while (cit->newcnt < cit->nthreads)
	{
		if (PTHREAD_CHK(pthread_cond_wait(&cit->newtask_cond,
						  &cit->newtask_mutex)))
		{
			perror("pthread_cond_wait");
			sleep(3);
		}
	}

	if (PTHREAD_CHK(pthread_mutex_unlock(&cit->newtask_mutex)))
		perror("pthread_mutex_unlock");

	pthread_cleanup_pop(0);

	for (i=0; i<cit->nthreads; i++)
	{
		if (PTHREAD_CHK(pthread_cancel(cit->threads[i].pt) ))
			perror("pthread_cancel");

		if (PTHREAD_CHK(pthread_join(cit->threads[i].pt, NULL)))
			perror("pthread_join");

		if (PTHREAD_CHK(pthread_cond_destroy(&cit->threads[i].gocond)))
			perror("pthread_cond_destroy(gocond)");

		if (PTHREAD_CHK(pthread_mutex_destroy(&cit->threads[i]
						      .gomutex)))
			perror("pthread_mutex_destroy");
	}

	if (PTHREAD_CHK(pthread_mutex_destroy(&cit->cleanup_mutex)))
		perror("pthread_mutex_destroy");

	if (PTHREAD_CHK(pthread_cond_destroy(&cit->newtask_cond)))
		perror("pthread_cond_destroy(newtask_cond)");

	if (PTHREAD_CHK(pthread_mutex_destroy(&cit->newtask_mutex)))
		perror("pthread_mutex_destroy");
	free(cit->newbuf);
	free(cit->metadata_buf);
	free( (char *)cit->threads);
	free( (char *)cit);
}

int cthread_go( cthreadinfo_t *cit, void (*gofunc)(void *, void *), void *arg)
{
	int	n=0;
	int	err=0;

	if (PTHREAD_CHK(pthread_mutex_lock(&cit->newtask_mutex)))
		return (-1);

	pthread_cleanup_push(&mutexcleanup, (void *)&cit->newtask_mutex);

	while (cit->newcnt == 0)
	{
		if (PTHREAD_CHK(pthread_cond_wait(&cit->newtask_cond,
						  &cit->newtask_mutex)))
		{
			err=1;
			break;
		}
	}

	if (!err)
		n=cit->newbuf[ --cit->newcnt ];

	if (PTHREAD_CHK(pthread_mutex_unlock(&cit->newtask_mutex)))
		err=1;

	pthread_cleanup_pop(0);
	if (err)	return (-1);

	(*gofunc)( cit->threads[n].metadata, arg);

	if (PTHREAD_CHK(pthread_mutex_lock(&cit->threads[n].gomutex)))
		return (-1);
	cit->threads[n].goflag=1;
	if (PTHREAD_CHK(pthread_mutex_unlock(&cit->threads[n].gomutex)))
		return (-1);
	if (PTHREAD_CHK(pthread_cond_signal( &cit->threads[n].gocond )))
		return (-1);
	return (0);
}

struct cthreadlock {
	pthread_mutex_t	mutex;
	} ;

struct cthreadlock *cthread_lockcreate(void)
{
struct cthreadlock *p= (struct cthreadlock *)malloc(sizeof(struct cthreadlock));
pthread_mutexattr_t mattr;

	if (!p)	return (0);

	if (PTHREAD_CHK(pthread_mutexattr_init(&mattr)))
	{
		errno=EIO;
		free( (char *)p );
		return (0);
	}

	if (PTHREAD_CHK(pthread_mutex_init(&p->mutex, &mattr)))
	{
		pthread_mutexattr_destroy(&mattr);
		errno=EIO;
		free( (char *)p );
		return (0);
	}
	pthread_mutexattr_destroy(&mattr);
	return (p);
}

void cthread_lockdestroy(struct cthreadlock *p)
{
	pthread_mutex_destroy(&p->mutex);
	free( (char *)p );
}

int cthread_lock(struct cthreadlock *p, int (*func)(void *), void *arg)
{
int	rc;

	while (pthread_mutex_lock(&p->mutex))
	{
		perror("pthread_mutex_lock");
		sleep(3);
	}

	pthread_cleanup_push(&mutexcleanup, (void *)&p->mutex);

	rc= (*func)(arg);

	if (PTHREAD_CHK(pthread_mutex_unlock( &p->mutex )))
		perror("pthread_mutex_unlock");
	pthread_cleanup_pop(0);
	return (rc);
}
