/*
** Copyright 2002-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
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
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<stdio.h>
#include	<ctype.h>
#include	<errno.h>
#include	<fcntl.h>

#include	"maildirmisc.h"

void maildir::list(const std::string &maildir,
		   const std::function<void (const std::string &)> &callback)
{
	DIR *dirp=opendir(maildir.c_str());
	struct dirent *de;

	while (dirp && (de=readdir(dirp)) != NULL)
	{
		if (strcmp(de->d_name, "..") == 0)
			continue;

		if (de->d_name[0] != '.')
			continue;

		std::string p;

		p.reserve(maildir.size()+strlen(de->d_name)+20);

		p=maildir;
		p+="/";
		p+=de->d_name;
		p+="/cur/.";

		if (access(p.c_str(), X_OK))
		{
			continue;
		}

		p=INBOX;

		if (strcmp(de->d_name, "."))
			p += de->d_name;

		callback(p);
	}
	if (dirp)
		closedir(dirp);
}

void maildir_list(const char *maildir,
		  void (*func)(const char *, void *),
		  void *voidp)
{
	maildir::list(maildir,
		      [&]
		      (const std::string &s)
		      {
			      (*func)(s.c_str(), voidp);
		      });
}
