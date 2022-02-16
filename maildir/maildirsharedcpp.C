/*
** Copyright 2000-2007 Double Precision, Inc.
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
#include	"maildircreate.h"
#include	"maildirsharedrc.h"

#include	<fstream>

/* Prerequisited for shared folder support */

#if	HAVE_READLINK
#if	HAVE_SYMLINK
#if	HAVE_DBOBJ

#define	YES_WE_CAN_DO_SHARED	1

#endif
#endif
#endif

#if	YES_WE_CAN_DO_SHARED

#include	"dbobj.h"

extern "C" void maildir_shared_fparse(char *, char **, char **);

namespace maildir {
#if 0
}
#endif

static void list_sharable(const std::string &, const std::string &,
			  const std::function<void (const std::string &)> &);

void list_sharable(const std::string &maildir,
		   const std::function<void (const std::string &)> &cb)
{
	int pass;

	for (pass=0; pass<2; pass++)
	{
		std::ifstream fp{
			pass ? shared_filename(maildir.empty() ? ".":maildir)
			: MAILDIRSHAREDRC
		};

		if (!fp)	continue;

		std::string line;

		while (std::getline(fp, line))
		{
			if (line.empty())
				continue;

			char *b=&line[0];
			char *e=b+line.size();

			char *nameb, *namee, *dirb, *dire;

			shared_fparse(b, e, nameb, namee,
				      dirb, dire);

			if (nameb != namee)
				list_sharable({nameb, namee},
					      {dirb, dire}, cb);
		}
	}
}

static void list_sharable(const std::string &pfix,
			  const std::string &path,
			  const std::function<void (const std::string &)> &cb)
{
	DIR	*dirp;
	struct	dirent *de;
	struct	stat	stat_buf;

	dirp=opendir(path.c_str());
	while (dirp && (de=readdir(dirp)) != 0)
	{
		if (de->d_name[0] != '.')	continue;
		if (strcmp(de->d_name, ".") == 0 ||
			strcmp(de->d_name, "..") == 0)	continue;

		std::string z;

		z.reserve(path.size()+strlen(de->d_name)+12);

		z=path;

		z += "/";
		z += de->d_name;
		z += "/cur/.";

		if (stat(z.c_str(), &stat_buf))
		{
			continue;
		}

		z.reserve(pfix.size()+strlen(de->d_name)+1);

		z=pfix;
		z += de->d_name;
		cb(z);
	}
	if (dirp)	closedir(dirp);
}

void list_shared(const std::string &maildir,
		 const std::function<void (const std::string &)> &func)
{
	DIR	*dirp;
	struct	dirent *de;

	std::string sh;

	sh.reserve(maildir.size()+sizeof("/" SHAREDSUBDIR));

	sh=maildir;

	if (sh.empty())
		sh=".";

	sh += "/" SHAREDSUBDIR;

	dirp=opendir(sh.c_str());

	while (dirp && (de=readdir(dirp)) != 0)
	{
		DIR	*dirp2;
		struct	dirent *de2;

		if (de->d_name[0] == '.')	continue;

		std::string z;

		z.reserve(sh.size()+strlen(de->d_name)+1);

		z=sh;
		z+="/";
		z+=de->d_name;
		dirp2=opendir(z.c_str());

		while (dirp2 && (de2=readdir(dirp2)) != 0)
		{

			if (de2->d_name[0] == '.')	continue;

			std::string s;

			s.reserve(strlen(de->d_name)+strlen(de2->d_name)+1);

			s=de->d_name;
			s+=".";
			s+=de2->d_name;
			func(s);
		}
		if (dirp2)	closedir(dirp2);
	}
	if (dirp)	closedir(dirp);
}

#if 0
{
#endif
}


#else

/* We cannot implement sharing */

void maildir::list_sharable(const std::string &maildir,
			    const std::function<void (const std::string &)> &)
{
}

void maildir::list_shared(const std::string &maildir,
			  const std::function<void (const std::string &)> &)
{
}

#endif

void maildir_list_sharable(const char *maildir,
			   void (*func)(const char *, void *),
			   void *voidp)
{
	if (!maildir)
		maildir="";

	maildir::list_sharable(maildir,
			       [&]
			       (const std::string &s)
			       {
				       (*func)(s.c_str(), voidp);
			       });
}


void maildir_list_shared(const char *maildir,
	void (*func)(const char *, void *),
	void *voidp)
{
	if (!maildir)
		maildir="";

	maildir::list_shared(maildir,
			     [&]
			     (const std::string &s)
			     {
				     (*func)(s.c_str(), voidp);
			     });
}
