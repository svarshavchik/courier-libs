/*
** Copyright 2003-2012 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildiraclt.h"
#include	"maildirmisc.h"
#include	"maildircreate.h"
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#include	<string.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<assert.h>

struct maildir_aclt_impl {
	char *ptr;
};

static int compar_aclt(const void *a, const void *b)
{
	char ca=*(const char *)a;
	char cb=*(const char *)b;

	return (int)(unsigned char)ca - (int)(unsigned char)cb;
}

/* Post-op fixup of an aclt: collate, remove dupes. */

static void fixup(maildir_aclt *aclt)
{
	char *a, *b;

	qsort((*aclt)->ptr, strlen((*aclt)->ptr), 1, compar_aclt);

	for (a=b=(*aclt)->ptr; *a; a++)
	{
		if (*a == a[1])
			continue;
		if ((int)(unsigned char)*a <= ' ')
			continue; /* Silently drop bad access rights */

		*b++= *a;
	}
	*b=0;
}

static int validacl(const char *p)
{
	while (*p)
	{
		if ((int)(unsigned char)*p <= ' ')
		{
			errno=EINVAL;
			return -1;
		}
		++p;
	}

	return 0;
}

int maildir_aclt_init(maildir_aclt *aclt,
		      const char *initvalue_cstr,
		      const maildir_aclt *initvalue_cpy)
{
	if (initvalue_cpy)
		initvalue_cstr= (*initvalue_cpy)->ptr;

	*aclt=NULL;

	if (!initvalue_cstr || !*initvalue_cstr)
		return 0;

	if (validacl(initvalue_cstr) < 0)
		return -1;

	if (!(*aclt=(maildir_aclt_impl *)malloc(sizeof(maildir_aclt_impl))))
	{
		return -1;
	}

	if ( ((*aclt)->ptr=strdup(initvalue_cstr)) == NULL)
	{
		free(*aclt);
		*aclt=NULL;
		return -1;
	}

	fixup(aclt);
	return 0;
}

/* Destroy an aclt after it is no longer used. */

void maildir_aclt_destroy(maildir_aclt *aclt)
{
	if (!*aclt)
		return;

	free((*aclt)->ptr);
	free(*aclt);
	*aclt=NULL;
}


/* Add or remove access chars. */

int maildir_aclt_add(maildir_aclt *aclt,
		     const char *add_strs,
		     const maildir_aclt *add_aclt)
{
	if (add_aclt)
		add_strs= (*add_aclt)->ptr;

	if (!add_strs || !*add_strs)
		return 0;

	if (validacl(add_strs) < 0)
		return -1;

	if (*aclt)
	{
		char *p=(char *)realloc((*aclt)->ptr, strlen((*aclt)->ptr)+strlen(add_strs)+1);

		if (!p)
			return -1;
		strcat(p, add_strs);
		(*aclt)->ptr=p;

	}
	else
	{
		return maildir_aclt_init(aclt, add_strs, NULL);
	}

	fixup(aclt);
	return 0;
}

int maildir_aclt_del(maildir_aclt *aclt,
		     const char *del_strs,
		     const maildir_aclt *del_aclt)
{
	char *a, *b;

	if (del_aclt)
		del_strs= (*del_aclt)->ptr;

	if (!del_strs)
		return 0;

	if (!*aclt)
		return 0;

	for (a=b=(*aclt)->ptr; *a; a++)
	{
		if (strchr(del_strs, *a))
			continue;
		*b++= *a;
	}
	*b=0;

	if (*(*aclt)->ptr == 0)
	{
		maildir_aclt_destroy(aclt);
	}
	return 0;
}

const char *maildir_aclt_ascstr(const maildir_aclt *aclt)
{
	if (!aclt || !*aclt)
		return "";

	return (*aclt)->ptr;
}
