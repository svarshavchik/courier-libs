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
#include	<unistd.h>

int ldapabook_del(const char *filename, const char *tempname,
		const char *delname)
{
/* This is cheating, but we won't really have many abooks, come on... */
struct ldapabook *a=ldapabook_read(filename), *b;

FILE	*fp;

	if (!a)	return (0);

	if ((fp=fopen(tempname, "w")) == 0)
	{
		ldapabook_free(a);
		return (-1);
	}

	for (b=a; b; b=b->next)
	{
		if (strcmp(b->name, delname) == 0)	continue;

		ldapabook_writerec(b, fp);
	}
	ldapabook_free(a);

	if (fflush(fp) || fclose(fp))
	{
		fclose(fp);
		unlink(tempname);
		return (-1);
	}

	if (rename(tempname, filename))
	{
		unlink(tempname);
		return (-1);
	}

	return (0);
}
