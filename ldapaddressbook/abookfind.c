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

const struct ldapabook *ldapabook_find(const struct ldapabook *l,
					const char *n)
{
	while (l)
	{
		if (strcmp(l->name, n) == 0)	return (l);

		l=l->next;
	}
	return (0);
}
