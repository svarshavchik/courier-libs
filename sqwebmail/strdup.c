#include "config.h"
/*
** Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	<stdlib.h>
#include	<string.h>

char *strdup(const char *p)
{
char *s;

	if ((s=malloc(strlen(p)+1)) != 0)	strcpy(s, p);
	return (s);
}
