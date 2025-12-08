/*
** Copyright 1998 - 2009 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/
#include	"rfc822.h"
#include	<stdlib.h>
#include	<string.h>

static void cntlen(const char *c, size_t l, void *p)
{
	c=c;
	*(size_t *)p += l;
}

static void cntlensep(const char *p, void *ptr)
{
	cntlen(p, strlen(p), ptr);
}

static void saveaddr(const char *c, size_t l, void *ptr)
{
	char **p=(char **)ptr;

	memcpy(*p, c, l);

	*p += l;
}

static void saveaddrsep(const char *p, void *ptr)
{
	saveaddr(p, strlen(p), ptr);
}

char *rfc822_getaddrs(const struct rfc822a *rfc)
{
	size_t	addrbuflen=0;
	char	*addrbuf, *ptr;

	if (rfc822_print(rfc, &cntlen, &cntlensep, &addrbuflen) < 0)
		return NULL;

	if (!(addrbuf=malloc(addrbuflen+1)))
		return (0);

	ptr=addrbuf;
	if (rfc822_print(rfc, &saveaddr, &saveaddrsep, &ptr) < 0)
	{
		free(addrbuf);
		return NULL;
	}

	addrbuf[addrbuflen]=0;
	return (addrbuf);
}

static void saveaddrsep_wrap(const char *p, void *ptr)
{
	size_t i=0, n=strlen(p);

	while (i < n)
	{
		size_t j;

		for (j=i; j<n; j++)
			if (p[j] == ' ')
				break;

		if (j == i)
		{
			saveaddr("\n", 1, ptr);
			++i;
			continue;
		}

		saveaddr(p+i, j-i, ptr);
		i=j;
	}
}

char *rfc822_getaddrs_wrap(const struct rfc822a *rfc, int w)
{
	size_t	addrbuflen=0;
	char	*addrbuf, *ptr, *start, *lastnl;

	if (rfc822_print(rfc, &cntlen, &cntlensep, &addrbuflen) < 0)
		return NULL;

	if (!(addrbuf=malloc(addrbuflen+1)))
		return (0);

	ptr=addrbuf;

	if (rfc822_print(rfc, &saveaddr, &saveaddrsep_wrap, &ptr) < 0)
	{
		free(addrbuf);
		return NULL;
	}

	addrbuf[addrbuflen]=0;

	for (lastnl=0, start=ptr=addrbuf; *ptr; )
	{
		while (*ptr && *ptr != '\n')	ptr++;
		if (ptr-start < w)
		{
			if (lastnl)	*lastnl=' ';
			lastnl=ptr;
			if (*ptr)	++ptr;
		}
		else
		{
			if (lastnl)
				start=lastnl+1;
			else
			{
				start=ptr+1;
				if (*ptr)	++ptr;
			}
			lastnl=0;
		}
	}
	return (addrbuf);
}
