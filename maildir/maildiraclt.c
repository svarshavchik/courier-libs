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

struct maildir_acl_resetList {
	struct maildir_acl_resetList *next;
	char *mbox;
};

/*
** When a maildir is opened check for stale entries in Maildir/ACLHIERDIR.
**
** Maildir/ACLHIERDIR/folder.subfolder should be removed unless there exists
** Maildir/.folder.subfolder.subsubfolder
**
**
** acl_check_cb is the callback function for maildir_list, which receives
** INBOX.folder.subfolder.subsubfolder.  It goes through the link list with
** Maildir/ACLHIERDIR's contents, and removes folder.subfolder if its found.
**
** After maildir_list is done, anything that's left in the list can be safely
** removed.
*/

static void acl_check_cb(const char *mbox, void *voidarg)
{
	struct maildir_acl_resetList **l=
		(struct maildir_acl_resetList **)voidarg;

	if (strncmp(mbox, INBOX ".", sizeof(INBOX ".")-1))
		return; /* Huh? */

	mbox += sizeof(INBOX ".")-1;

	while (*l)
	{
		int cl= strlen( (*l)->mbox );

		if (strncmp(mbox, (*l)->mbox, cl) == 0 &&
		    mbox[cl] == '.')
		{
			struct maildir_acl_resetList *p= *l;

			*l= p->next;
			free(p->mbox);
			free(p);
			continue;
		}

		l= &(*l)->next;
	}
}

int maildir_acl_reset(const char *maildir)
{
	DIR *dirp;
	struct dirent *de;
	char *p;
	struct maildir_acl_resetList *rl=NULL;
	struct maildir_acl_resetList *r;
	time_t now;
	struct stat stat_buf;

	p=malloc(strlen(maildir) + sizeof("/" ACLHIERDIR));
	if (!p)
		return -1;

	strcat(strcpy(p, maildir), "/" ACLHIERDIR);

	dirp=opendir(p);

	if (!dirp)
	{
		mkdir(p, 0755);
		dirp=opendir(p);
	}
	free(p);

	while (dirp && (de=readdir(dirp)) != NULL)
	{
		if (de->d_name[0] == '.')
			continue;

		if ((r=malloc(sizeof(struct maildir_acl_resetList))) == NULL
		    || (r->mbox=strdup(de->d_name)) == NULL)
		{
			if (r)
				free(r);

			while (rl)
			{
				r=rl;
				rl=r->next;
				free(r->mbox);
				free(r);
			}
			closedir(dirp);
			return -1;
		}

		r->next=rl;
		rl=r;
	}
	if (dirp) closedir(dirp);

	maildir_list(maildir, acl_check_cb, &rl);

	time(&now);

	while (rl)
	{
		r=rl;
		rl=r->next;

		p=malloc(strlen(maildir)+strlen(r->mbox) +
			 sizeof("/" ACLHIERDIR "/"));
		if (p)
		{
			strcat(strcat(strcpy(p, maildir),
				      "/" ACLHIERDIR "/"), r->mbox);

			/* Only unlink stale entries after 1 hour (race) */

			if (stat(p, &stat_buf) == 0 &&
			    stat_buf.st_mtime < now - 60*60)
				unlink(p);
			free(p);
		}
		free(r->mbox);
		free(r);
	}
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
