/*
** Copyright 2004 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "gpg.h"

char *libmail_gpg_options(const char *p)
{
	if (!p || !*p)
		p=getenv("GNUPGHOME");

	if (p && *p)
	{
		char *s=malloc(strlen(p)+sizeof("/options"));

		if (s)
			return (strcat(strcpy(s, p), "/options"));
	}
	else
	{
		p=getenv("HOME");

		if (p && *p)
		{
			char *s=malloc(strlen(p)+sizeof("/.gnupg/options"));

			if (s)
				return (strcat(strcpy(s, p),
					       "/.gnupg/options"));
		}
	}
	return NULL;
}
