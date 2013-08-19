/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"config.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

struct arglist {
	struct arglist *next;
	char *argbuf;
} ;

static struct arglist *arglist=0;

void freeargs()
{
	struct arglist *a;

	while ((a=arglist) != NULL)
	{
		arglist=a->next;
		free(a->argbuf);
		free(a);
	}
}

void addarg(const char *p)
{
	char *s=strdup(p);
	struct arglist *a;

	if (!s)
		enomem();

	a=(struct arglist *)malloc(sizeof(struct arglist));

	if (!a)
	{
		free(s);
		enomem();
	}

	a->next=arglist;
	arglist=a;
	a->argbuf=s;
}

const char *getarg(const char *n)
{
	size_t l=strlen(n);
	struct arglist *a;

	for (a=arglist; a; a=a->next)
		if (strncmp(a->argbuf, n, l) == 0 &&
		    a->argbuf[l] == '=')
			return (a->argbuf+l+1);
	return ("");
}
