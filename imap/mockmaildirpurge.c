/*
** Copyright 20212 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	"maildir/maildirmisc.h"
#include	<stdio.h>

void maildir_purgetmp(const char *dir)
{
	printf("maildir_purgetmp: %s\n", dir);
}

void maildir_purge(const char *dir,
		   unsigned n)
{
	printf("maildir_purge: %s (%u)\n", dir, n);
}
