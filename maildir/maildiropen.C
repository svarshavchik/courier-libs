/*
** Copyright 2000 S. Varshavchik.
** See COPYING for distribution information.
*/

#if HAVE_CONFIG_H
#include "config.h"
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

#include	<vector>

char *maildir_getlink(const char *filename)
{
	auto l=maildir::getlink(filename);

	return l.empty() ? NULL:strdup(l.c_str());
}

std::string maildir::getlink(const std::string &filename)
{
#if     HAVE_READLINK
	size_t	bufsiz;
	char	*buf;

	bufsiz=0;
	buf=0;

	for (;;)
	{
		int	n;

		if (buf)	delete[] buf;

		bufsiz += 256;

		if ((buf=new char[bufsiz]) == 0)
		{
			perror("malloc");
			return "";
		}
		if ((n=readlink(filename.c_str(), buf, bufsiz)) < 0)
		{
			delete[] buf;
			return "";
		}
		if ((size_t)n < bufsiz)
		{
			std::string s{buf, buf+n};

			delete[] buf;

			return s;
		}
	}
#else
	return "";
#endif
}

int maildir_semisafeopen(const char *path, int mode, int perm)
{
	return maildir::semisafeopen(path, mode, perm);
}

int maildir::semisafeopen(const std::string &path, int mode, int perm)
{
#if	HAVE_READLINK

	std::string l=getlink(path);

	if (!l.empty())
	{
		if (*l.c_str() != '/')
		{

			std::string q;

			q.reserve(path.size()+l.size()+1);

			q=path;

			auto p=q.rfind('/');

			if (p != q.npos)
				q.resize(++p);

			q += l;
			l=q;
		}

		return safeopen(l, mode, perm);
	}
#endif

	return (safeopen(path, mode, perm));
}

int maildir_safeopen(const char *path, int mode, int perm)
{
	return maildir::safeopen(path, mode, perm);
}

int maildir::safeopen(const std::string &path, int mode, int perm)
{
	return safeopen_stat(path, mode, perm, NULL);
}

int maildir_safeopen_stat(const char *path, int mode, int perm,
			  struct stat *stat1)
{
	return maildir::safeopen_stat(path, mode, perm, stat1);
}

int maildir::safeopen_stat(const std::string &path, int mode, int perm,
			   struct stat *stat1)
{
	struct	stat	stat2, statt;
	char *p;

	int	fd=open(path.c_str(), mode
#ifdef	O_NONBLOCK
			| O_NONBLOCK
#else
			| O_NDELAY
#endif
			, perm);

	if (fd < 0)	return (fd);

	if (fcntl(fd, F_SETFL, (mode & O_APPEND)) || (stat1 && fstat(fd, stat1)))
	{
		close(fd);
		return (-1);
	}

	p = getenv("MAILDIR_SKIP_SYMLINK_CHECKS");
	if (p && atoi(p)) return (fd);

	if (!stat1)
	{
		stat1 = &statt;
		if (fstat(fd, stat1))
		{
			close(fd);
			return (-1);
		}
	}

	errno = 0;
	if (lstat(path.c_str(), &stat2) || stat1->st_dev != stat2.st_dev || stat1->st_ino != stat2.st_ino)
	{
		close(fd);
		if (!errno) errno=ENOENT;
		return (-1);
	}

	return (fd);
}
