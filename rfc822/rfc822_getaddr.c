/*
** Copyright 1998 - 2008 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/
#include	"rfc822.h"
#include	<stdlib.h>

static void cntlen(const char *c, size_t n, void *p)
{
	while (n)
	{
		if (*c != '\n')
			++ *(size_t *)p;
		++c;
		--n;
	}
}

static void saveaddr(const char *c, size_t n, void *p)
{
	while (n)
	{
		if (*c != '\n')
		{
			char **cp=(char **)p;

			*(*cp)++=*c;
		}
		++c;
		--n;
	}
}

char *rfc822_getaddr(const struct rfc822a *rfc, int n)
{
	return rfc822_display_addr_tobuf(rfc, n, NULL);
}

char *rfc822_gettok(const struct rfc822token *t)
{
size_t	addrbuflen=0;
char	*addrbuf, *ptr;

	rfc822tok_print(t, &cntlen, &addrbuflen);

	if (!(addrbuf=malloc(addrbuflen+1)))
		return (0);

	ptr=addrbuf;
	rfc822tok_print(t, &saveaddr, &ptr);
	addrbuf[addrbuflen]=0;
	return (addrbuf);
}
