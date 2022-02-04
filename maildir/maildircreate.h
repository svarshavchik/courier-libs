#ifndef	maildircreate_h
#define	maildircreate_h

/*
** Copyright 1998 - 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdio.h>

#ifdef  __cplusplus
extern "C" {
#endif
#if 0
}
#endif

/* Create messages in maildirs */

struct maildir_tmpcreate_info {
	const char *maildir;
	unsigned long msgsize;  /* If known, 0 otherwise (must use requota later)*/
	const char *uniq;	/* You need when creating multiple msgs */
	const char *hostname;	/* If known, NULL otherwise */
	int openmode;		/* Default open mode */
	int doordie;		/* Loop until we get it right. */
	char *tmpname;	/* On exit, filename in tmp */
	char *newname; /* On exit, filename in new */
	char *curname; /* On exit, filename in cur */
};

#define maildir_tmpcreate_init(i) \
	do \
	{ \
		memset( (i), 0, sizeof(*(i))); \
		(i)->openmode=0644; \
	} while(0)

int maildir_tmpcreate_fd(struct maildir_tmpcreate_info *);
FILE *maildir_tmpcreate_fp(struct maildir_tmpcreate_info *);
void maildir_tmpcreate_free(struct maildir_tmpcreate_info *);

	/* Move created message from tmp to new */
int maildir_movetmpnew(const char *tmpname, const char *newname);

#ifdef  __cplusplus
#if 0
{
#endif
}

#include <string>

namespace maildir {
#if 0
}
#endif

/* Create messages in maildirs */

struct tmpcreate_info {
	std::string maildir;
	unsigned long msgsize=0;  /* If known, 0 otherwise (must use requota later)*/
	std::string uniq;	/* You need when creating multiple msgs */
	std::string hostname;	/* If known, empty string otherwise */
	int openmode=0644;     	/* Default open mode */
	bool doordie=true;    	/* Loop until we get it right. */
	std::string tmpname;	/* On exit, filename in tmp */
	std::string newname; /* On exit, filename in new */
	std::string curname; /* On exit, filename in cur */

	// Create a file descript
	int fd();

	// Open a FILE
	FILE *fp();
};

int movetmpnew(const std::string &tmpname, const std::string &newname);

#if 0
{
#endif
}

#endif

#endif
