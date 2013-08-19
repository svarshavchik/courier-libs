/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include "pcp.h"

#define leap(y) ( \
                        ((y) % 400) == 0 || \
                        (((y) % 4) == 0 && (y) % 100) )

static unsigned mlength[]={31,28,31,30,31,30,31,31,30,31,30,31};
#define mdays(m,y)      ( (m) != 2 ? mlength[(m)-1] : leap(y) ? 29:28)


static int tmcmp(int y1, int m1, int d1,
		 int y2, int m2, int d2)
{
	if (y1 < y2)
		return (-1);
	if (y1 > y2)
		return (1);
	if (m1 < m2)
		return (-1);
	if (m1 > m2)
		return (1);
	if (d1 < d2)
		return (-1);
	if (d1 > d2)
		return (1);
	return (0);
}

time_t pcp_gmtime(int y, int m, int d, int hh, int mm, int ss)
{
	time_t t;
	struct tm *tmptr;
	struct tm mytm;
	int n;

	if (y < 1970 || y > 3000 || m < 1 || m > 12 || d < 1 ||
	    d > mdays(m, y) || hh < 0 || mm < 0 || ss < 0 || hh > 23 ||
	    mm > 59 || ss > 59)
		return (0);

	time(&t);

	tmptr=localtime(&t);
	if (!tmptr)
		return (0);

	mytm= *tmptr;
	mytm.tm_year = y - 1900;
	mytm.tm_mon = m - 1;
	mytm.tm_mday = d;
	mytm.tm_hour=0;
	mytm.tm_min=0;
	mytm.tm_sec=0;

	t=mktime(&mytm);
	if (t == (time_t)-1)
		return (0);

	/* Do it again, due to a potential timezone change */

	tmptr=localtime(&t);
	mytm= *tmptr;
	mytm.tm_year = y - 1900;
	mytm.tm_mon = m - 1;
	mytm.tm_mday = d;
	mytm.tm_hour=0;
	mytm.tm_min=0;
	mytm.tm_sec=0;

	t=mktime(&mytm);
	if (t == (time_t)-1)
		return (0);

	/* Sanity check */

	tmptr=localtime(&t);
	if (tmptr->tm_hour || tmptr->tm_min || tmptr->tm_sec)
	{
		return (0);
	}

	tmptr=gmtime(&t);

	n=tmcmp(tmptr->tm_year + 1900,
		tmptr->tm_mon + 1,
		tmptr->tm_mday,
		y, m, d);
	if (n < 0)
	{
		while (n < 0)
		{
			t += 24 * 60 * 60;
			tmptr=gmtime(&t);
			n=tmcmp(tmptr->tm_year + 1900,
				tmptr->tm_mon + 1,
				tmptr->tm_mday,
				y, m, d);
		}
	}
	else while (n > 0)
	{
		t -= 24 * 60 * 60;
		tmptr=gmtime(&t);
		n=tmcmp(tmptr->tm_year + 1900,
			tmptr->tm_mon + 1,
			tmptr->tm_mday,
			y, m, d);
	}

	t -= tmptr->tm_hour * 60 * 60 + tmptr->tm_min * 60 + tmptr->tm_sec;

	return (t + hh * 60 * 60 + mm * 60 + ss);
}

time_t pcp_gmtime_s(const char *p)
{
	int y, m, d, hh, mm, ss;

	if (sscanf(p, "%4d%2d%2d%2d%2d%2d", &y, &m, &d, &hh, &mm, &ss)
	    != 6)
		return (0);
	return (pcp_gmtime(y, m, d, hh, mm, ss));
}

void pcp_gmtimestr(time_t t, char *buf)
{
	struct tm *tmptr=gmtime(&t);

	sprintf(buf, "%04d%02d%02d%02d%02d%02d",
		tmptr->tm_year + 1900,
		tmptr->tm_mon+1,
		tmptr->tm_mday,
		tmptr->tm_hour,
		tmptr->tm_min,
		tmptr->tm_sec);
}
