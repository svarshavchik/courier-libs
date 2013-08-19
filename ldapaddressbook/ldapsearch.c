/*
** Copyright 2006, Double Precision Inc.
**
** See COPYING for distribution information.
*/

#include "libldapsearch.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static int cb(const char *utf8_name,
	      const char *address,
	      void *callback_arg)
{
	if (strchr(utf8_name, '\n') == NULL &&
	    strchr(address, '\n') == NULL)
		/* filter out if it looks funny */

		printf("%s\n%s\n", utf8_name, address);
	return 0;
}

int main(int argc, char **argv)
{
	const char *host, *port, *suffix, *search;
	int port_n;
	struct ldapsearch *s;

	if (argc < 5)
	{
		fprintf(stderr, "INTERNAL ERROR: Invalid # of parameters to ldapsearch\n");
		exit(1);
	}

	host=argv[1];
	port=argv[2];
	suffix=argv[3];
	search=argv[4];

	port_n=atoi(port);

	if (port_n <= 0)
		port_n=LDAP_PORT;

	s=l_search_alloc(host, port_n, argc > 5 ? argv[5]:NULL,
			 argc > 6 ?argv[6]:NULL, suffix);

	if (!s)
	{
		perror("l_search_alloc");
		exit(1);
	}

	if (l_search_do(s, search, cb, NULL))
	{
		perror("l_search_do");
		exit(1);
	}
	l_search_free(s);
	exit(0);
}
