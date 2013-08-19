/*
** Copyright 2000 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<math.h>
#include	<errno.h>
#include	<time.h>
#include	"threadlib.h"

static double buf[100];
static int bufcnt=0;
static unsigned ndelay=0;

static void workfunc(void *p)
{
double *dp= (double *)p;
time_t	t1, t2;

	*dp=sqrt( *dp );

	time(&t1);
	while ( time(&t2) < t1 + ndelay)
		;
}

static void cleanupfunc(void *p)
{
double *dp= (double *)p;

	buf[bufcnt++]= *dp;
}

static void getwork(void *p, void *q)
{
double *dp= (double *)p;
double *qp= (double *)q;

	*dp= *qp;
}

static int cmpdbl (const void *a, const void *b)
{
const double *pa=(const double *)a;
const double *pb=(const double *)b;

	return (*pa < *pb ? -1: *pa > *pb ? 1:0);
}

int main(int argc, char **argv)
{
double n;
struct cthreadinfo *cit;
int	i, j;

	if (argc < 2)	return (1);

	j=atoi(argv[1]);
	if (argc > 2)
		ndelay=atoi(argv[2]);

	if (j <= 0 || j > sizeof(buf)/sizeof(buf[0]))	return (1);

	if ( (cit=cthread_init(2, sizeof(double), workfunc, cleanupfunc)) == 0)
	{
		perror("cthread_init");
		return (1);
	}

	for (i=0; i<j; i++)
	{
		n= i+1;

		if (cthread_go(cit, getwork, &n))
		{
			perror("cthread_go");
			return (1);
		}
		printf ("Started %d\n", i+1);
	}
	cthread_wait(cit);

	qsort(buf, bufcnt, sizeof(buf[0]), &cmpdbl);

	for (i=0; i<bufcnt; i++)
	{
		printf("%6.2f\n", buf[i]);
	}
	return (0);
}
