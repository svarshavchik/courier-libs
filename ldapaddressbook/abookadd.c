/*
** Copyright 2006, Double Precision Inc.
**
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"ldapaddressbook.h"

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

int ldapabook_add(const char *f, const struct ldapabook *a)
{
FILE	*fp=fopen(f, "a");

	if (!fp)	return (-1);

	ldapabook_writerec(a, fp);

	if (fflush(fp) || fclose(fp))
	{
		fclose(fp);
		return (-1);
	}
	return (0);
}
