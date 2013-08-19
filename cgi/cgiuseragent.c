/*
** Copyright 2000 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/

#include	"cgi.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>

extern void error(const char *);

int cgi_useragent(const char *p)
{
	const char *c=getenv("HTTP_USER_AGENT");

	for ( ; c && *c; c++)
	{
		size_t i;

		if (isalpha((int)(unsigned char)*c))
			continue;

		for (i=0; p[i]; i++)
		{
			int a,b;

			a=(unsigned char)p[i];
			b=(unsigned char)c[i+1];
			if (!b)
				break;

			a=toupper(a);
			b=toupper(b);
			if (a != b)
				break;
		}

		if (p[i] == 0)
		{
			int b=(unsigned char)c[i+2];

			if (b == 0 || !isalpha(b))
				return (1);
		}
	}
	return (0);
}
