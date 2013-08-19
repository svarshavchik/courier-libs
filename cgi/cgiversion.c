/*
** Copyright 1998 - 1999 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include	<stdlib.h>
#include	<ctype.h>

void cgiversion(unsigned *major, unsigned *minor)
{
const char *p=getenv("SERVER_PROTOCOL");

	*major=0;
	*minor=0;
	if (!p)	return;
	if ( toupper(*p++) != 'H' ||
		toupper(*p++) != 'T' ||
		toupper(*p++) != 'T' ||
		toupper(*p++) != 'P' ||
		*p++ != '/')	return;

	while (isdigit(*p))
		*major= *major * 10 + (*p++ - '0');
	if (*p++ == '.')
	{
		while (isdigit(*p))
			*minor= *minor * 10 + (*p++ - '0');
	}
}
