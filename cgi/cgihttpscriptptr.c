/*
** Copyright 1998 - 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>

static char *scriptptr=0;

extern void error(const char *);

void cgihttpscriptptr_init()
{
	/* Reinitialisation required when running as fastcgi */
	if (scriptptr) {
		free(scriptptr);
		scriptptr=0;
	}
}

const char *cgihttpscriptptr()
{
	if (!scriptptr)
	{
	char	*p=getenv("SCRIPT_NAME");
	char	*h=getenv("HTTP_HOST");
	char	*q;

		if (!h)	h="";
		if (!p)	p="";

		q=malloc(strlen(p)+strlen(h)+sizeof("http://"));
		if (!q)	error("Out of memory.");
		sprintf(q, "http:%s%s%s", (*h ? "//":""), h, p);
		scriptptr=q;
	}
	return (scriptptr);
}
