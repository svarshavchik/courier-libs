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


int maildir_acl_disabled=0;

/* ---------------------------------------------------------------------- */

int maildir_acl_delete(const char *maildir,
		       const char *path)
{
	char *p, *q;

#if 0
	if (strcmp(path, SHARED) == 0)
		return 0;

	if (strncmp(path, SHARED ".", sizeof(SHARED)) == 0)
		return 0;
#endif
	if (!maildir || !*maildir)
		maildir=".";
	if (!path || !*path)
		path=".";

	if (strchr(path, '/') || *path != '.')
	{
		errno=EINVAL;
		return -1;
	}

	p=malloc(strlen(maildir)+strlen(path)+2);

	if (!p)
		return -1;

	strcat(strcat(strcpy(p, maildir), "/"), path);

	q=malloc(strlen(p)+sizeof("/" ACLFILE));
	if (!q)
	{
		free(p);
		return -1;
	}

	unlink(strcat(strcpy(q, p), "/" ACLFILE));
	free(p);
	free(q);

	if (strcmp(path, ".") == 0)
	{
		/* INBOX ACL default */

		return 0;
	}

	q=malloc(strlen(maildir)+sizeof("/" ACLHIERDIR "/") +
		 strlen(path));
	if (!q)
	{
		return -1;
	}
	strcat(strcat(strcpy(q, maildir), "/" ACLHIERDIR "/"),
	       path+1);

	unlink(q);
	free(q);
	return 0;
}

/* -------------------------------------------------------------------- */

int maildir_acl_canlistrights(const char *myrights)
{
	return (strchr(myrights, ACL_LOOKUP[0]) ||
		strchr(myrights, ACL_READ[0]) ||
		strchr(myrights, ACL_INSERT[0]) ||
		strchr(myrights, ACL_CREATE[0]) ||
		strchr(myrights, ACL_DELETEFOLDER[0]) ||
		strchr(myrights, ACL_EXPUNGE[0]) ||
		strchr(myrights, ACL_ADMINISTER[0]));
}
