/*
** Copyright 1998 - 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include	<stdio.h>

void cgiredirect(const char *buf)
{
	if (cgihasversion(1,1))
		printf("Status: 303 Moved\n");
				/* Alas, Communicator can't handle it */
	cginocache();
	printf("Location: %s\n\n", buf);
	printf("URI: %s\n", buf);
}
