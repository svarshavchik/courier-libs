/*
** Copyright 1998 - 1999 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/

#include	"rfc822.h"

#include	<sys/types.h>
#include	<time.h>
#include	<stdio.h>
#include	<string.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

static const char months[][4]={
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec"};

static const char wdays[][4]={
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat"};

std::string rfc822::mkdate(time_t t)
{
	char buf[50];
	struct	tm *p;
	int	offset;

#if	USE_TIME_ALTZONE

	p=localtime(&t);
	offset= -(int)timezone;

	if (p->tm_isdst > 0)
		offset= -(int)altzone;

	if (offset % 60)
	{
		offset=0;
		p=gmtime(&t);
	}
	offset /= 60;
#else
#if	USE_TIME_DAYLIGHT

	p=localtime(&t);
	offset= -(int)timezone;

	if (p->tm_isdst > 0)
		offset += 60*60;
	if (offset % 60)
	{
		offset=0;
		p=gmtime(&t);
	}
	offset /= 60;
#else
#if	USE_TIME_GMTOFF
	p=localtime(&t);
	offset= p->tm_gmtoff;

	if (offset % 60)
	{
		offset=0;
		p=gmtime(&t);
	}
	offset /= 60;
#else
	p=gmtime(&t);
	offset=0;
#endif
#endif
#endif

	offset = (offset % 60) + offset / 60 * 100;

	sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d %+05d",
		wdays[p->tm_wday],
		p->tm_mday,
		months[p->tm_mon],
		p->tm_year+1900,
		p->tm_hour,
		p->tm_min,
		p->tm_sec,
		offset);

	return buf;
}
