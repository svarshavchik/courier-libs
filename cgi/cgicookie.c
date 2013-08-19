/*
** Copyright 2007 Double Precision, Inc.
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

static void enomem()
{
	error("Out of memory.");
}

char	*cgi_cookie(const char *p)
{
size_t	pl=strlen(p);
const char *c=getenv("HTTP_COOKIE");
char	*buf;

	while (c && *c)
	{
	size_t	i;

		for (i=0; c[i] && c[i] != '='; i++)
			;
		if (i == pl && strncmp(p, c, i) == 0)
		{
			c += i;
			if (*c)	++c;	/* skip over = */
			for (i=0; c[i] && c[i] != ';'; i++)
				;

			buf=malloc(i+1);
			if (!buf)	enomem();
			memcpy(buf, c, i);
			buf[i]=0;
			cgiurldecode(buf);
			return (buf);
		}
		c=strchr(c, ';');
		if (c)
			do
			{
				++c;
			} while (isspace((int)(unsigned char)*c));
	}
	buf=malloc(1);
	if (!buf)	enomem();
	*buf=0;
	return (buf);
}

void cgi_setcookie(const char *name, const char *value)
{
char	*p;
const	char *sn;

	p=cgiurlencode(value);
	sn=getenv("SCRIPT_NAME");
	if (!sn || !*sn)
		sn="/";
	printf("Set-Cookie: %s=%s; path=%s\n", name, value, sn);
	free(p);
}
