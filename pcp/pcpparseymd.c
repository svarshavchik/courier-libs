/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "pcp.h"

/*
** Map yyyymmdd into time_t by reverse-engineering mktime().
*/

static time_t sod(struct tm *tmptr, unsigned y, unsigned m, unsigned d)
{
	struct tm mytm;
	time_t tt;

	/* Ok, first try */

	mytm= *tmptr;
	mytm.tm_year=y-1900;
	mytm.tm_mon=m-1;
	mytm.tm_mday=d;
	mytm.tm_hour=0;
	mytm.tm_min=0;
	mytm.tm_sec=0;

	tt=mktime(&mytm);

	if (tt == (time_t)-1 ||
	    (tmptr=localtime(&tt)) == NULL)
		return (0);

	/* Do it one more time, due to timezone changes. */

	mytm= *tmptr;
	mytm.tm_year=y-1900;
	mytm.tm_mon=m-1;
	mytm.tm_mday=d;
	mytm.tm_hour=0;
	mytm.tm_min=0;
	mytm.tm_sec=0;

	tt=mktime(&mytm);

	if (tt == (time_t)-1)
		return (0);
	return (tt);
}

int pcp_parse_ymd(unsigned y, unsigned m, unsigned d, time_t *s, time_t *e)
{
	time_t tt;
	struct tm *tmptr;

	time(&tt);

	tmptr=localtime(&tt);

	if (!tmptr)
		return (-1);

	if ((tt=sod(tmptr, y, m, d)) == 0)
		return (-1);

	*s=tt;

	tt += 36 * 60 * 60;

	tmptr=localtime(&tt);

	if (!tmptr)
		return (-1);

	if ((tt=sod(tmptr, tmptr->tm_year + 1900, tmptr->tm_mon+1,
		    tmptr->tm_mday)) == 0)
		return (-1);

	*e=tt;
	return (0);
}

