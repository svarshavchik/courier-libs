/*
** Copyright 1998 - 2025 S. Varshavchik.  See COPYING for
** distribution information.
*/
#if    HAVE_CONFIG_H
#include       "rfc2045_config.h"
#endif
#include "rfc2045.h"
#include "rfc2045charset.h"
#include <stdlib.h>
#include <string.h>

static char	*rfc2045_defcharset=0;

const char *rfc2045_getdefaultcharset()
{
const char *p=rfc2045_defcharset;

	if (!p)	p=RFC2045CHARSET;
	return (p);
}

void rfc2045_setdefaultcharset(const char *charset)
{
char	*p=strdup(charset);

	if (!p)
	{
		perror("malloc");
		abort();
		return;
	}

	if (rfc2045_defcharset)	free(rfc2045_defcharset);
	rfc2045_defcharset=p;
}
