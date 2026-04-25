/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/

#include	"cgi.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>

std::string cgi_cookie(std::string_view p)
{
	const char *c=getenv("HTTP_COOKIE");
	std::string buf;

	while (c && *c)
	{
	size_t	i;

		for (i=0; c[i] && c[i] != '='; i++)
			;
		if (i == p.size() && strncmp(p.data(), c, i) == 0)
		{
			c += i;
			if (*c)	++c;	/* skip over = */
			for (i=0; c[i] && c[i] != ';'; i++)
				;

			buf=std::string{c, i};
			buf.resize(cgiurldecode(buf.data()));
			return (buf);
		}
		c=strchr(c, ';');
		if (c)
			do
			{
				++c;
			} while (isspace((int)(unsigned char)*c));
	}
	return (buf);
}

void cgi_setcookie(const char *name, const char *value)
{
	const	char *sn;
	std::string p;

	p.reserve(cgi_encode::estimate(value));
	cgi_encode::encode(std::back_inserter(p), value);

	sn=getenv("SCRIPT_NAME");
	if (!sn || !*sn)
		sn="/";
	printf("Set-Cookie: %s=%s; path=%s\n", name, p.c_str(), sn);
}
