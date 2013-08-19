/*
** Copyright 1998 - 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include	<stdio.h>

void cginocache()
{
	if (cgi_useragent("MSIE"))
	{
		printf("Cache-Control: private\n");
		printf("Pragma: private\n");
	}
	else
	{
		printf("Cache-Control: no-store\n");
		printf("Pragma: no-cache\n");
	}
}
