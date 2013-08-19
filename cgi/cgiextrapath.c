/*
** Copyright 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include <stdlib.h>
#include <unistd.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

const char *cgiextrapath()
{
	const char *pi=getenv("PATH_INFO");

	if (!pi) pi="";
	return pi;
}
