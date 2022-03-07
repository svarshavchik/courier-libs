/*
** Copyright 1998 - 2008 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<string>

extern "C" const char *externalauth()
{
	const char *p=getenv("TLS_EXTERNAL");

	if (!p || !*p)
		return NULL;

	std::string q="TLS_SUBJECT_";

	q += p;

	for (char &r:q)
		if (r >= 'a' && r <= 'z')
			r -= 'a' - 'A';

	p=getenv(q.c_str());

	if (p && *p)
		return p;
	return 0;
}
