/*
** Copyright 2000-2022 S. Varshavchik.
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

#include	"maildirmisc.h"


/*
** char *maildir_filename(const char *maildir, const char *folder,
**   const char *filename)
**	- find a message in a maildir
**
** Return the full path to the indicated message.  If the message flags
** in filename have changed, we search the maildir for this message.
*/

char *maildir_filename(const char *maildir,
		       const char *folder, const char *filename)
{
	std::string s=maildir::filename(maildir ? maildir:"",
					folder ? folder:"",
					filename);

	if (s.empty())
		return nullptr;

	return strdup(s.c_str());
}

std::string maildir::filename(const std::string &maildir,
			      const std::string &folder,
			      const std::string &filename)
{
	struct stat stat_buf;
	struct dirent *de;

	if (filename.find('/') != filename.npos ||
	    *filename.c_str() == '.')
	{
		errno=ENOENT;
		return "";
	}

	auto dir=folderdir(maildir, folder);

	if (dir.empty())
		return "";

	std::string p;

	p.reserve(dir.size()+filename.size() + sizeof("/cur/")-1);

	p=dir;
	p += "/cur/";
	p += filename;

	if (stat(p.c_str(), &stat_buf) == 0)
	{
		return (p);
	}

	/* Oh, a wise guy... */

	auto dirp=opendir(p.substr(0, p.rfind('/')).c_str());

	if (!dirp)
		return (p);

	/* Compare filenames, ignore filename size if set by maildirquota */

	while ((de=readdir(dirp)) != NULL)
	{
		const char *a=filename.c_str();
		const char *b=de->d_name;

		for (;;)
		{
			if ( a[0] == ',' && a[1] == 'S' && a[2] == '=')
			{
				/* File size - quota shortcut - skip */
				a += 3;
				while (*a && isdigit((int)(unsigned char)*a))
					++a;
			}

			if ( b[0] == ',' && b[1] == 'S' && b[2] == '=')
			{
				/* File size - quota shortcut - skip */
				b += 3;
				while (*b && isdigit((int)(unsigned char)*b))
					++b;
			}

			if ( (*a == 0 || *a == MDIRSEP[0]) &&
			     (*b == 0 || *b == MDIRSEP[0]))
			{
				p.clear();
				p.reserve(dir.size()+strlen(de->d_name)+
					  sizeof("/cur/")-1);

				p=dir;
				p += "/cur/";
				p += de->d_name;

				closedir(dirp);
				return (p);
			}
			if ( *a == 0 || *a == MDIRSEP[0] ||
			     *b == 0 || *b == MDIRSEP[0] ||
			     *a != *b)
				break;

			++a;
			++b;
		}
	}
	closedir(dirp);
	return (p);
}
