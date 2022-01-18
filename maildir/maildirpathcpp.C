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

#include	"maildirmisc.h"

std::string maildir::name2dir(const std::string &maildir,
			      const std::string &foldername) /* INBOX.name */
{
	if (maildir.empty())
		return name2dir(".", foldername);
	const auto ll=strlen(INBOX);

	if (strncasecmp(foldername.c_str(), INBOX, ll) == 0 &&
	    foldername.find('/') == foldername.npos)
	{
		if (foldername[ll] == 0)
			return maildir; /* INBOX: main maildir inbox */

		if (foldername[ll] == '.')
		{
			char prev_ch=0;

			for (auto c:foldername)
			{
				if (prev_ch == '.' && c == '.')
				{
					errno=EINVAL;
					return "";
				}
				prev_ch=c;
			}

			if (prev_ch == '.')
			{
				return "";
			}

			return maildir + "/" + foldername.substr(ll);
		}
	}

	errno=EINVAL;
	return "";
}

extern "C"
char *maildir_name2dir(const char *maildir,	/* DIR location */
		       const char *foldername) /* INBOX.name */
{
	auto ret=maildir::name2dir(maildir ? maildir:"",
				   foldername ? foldername:"");

	if (ret.empty())
	{
		errno=EINVAL;
		return NULL;
	}

	return strdup(ret.c_str());
}

std::string maildir::location(const std::string &homedir,
			     const std::string &maildir)
{
	if (*maildir.c_str() == '/')
		return maildir;

	std::string ret;

	ret.reserve(homedir.size()+maildir.size()+1);

	ret += homedir;
	ret += "/";
	ret += maildir;

	return ret;
}

extern "C"
char *maildir_location(const char *homedir,
		       const char *maildir)
{
	auto ret=maildir::location(homedir, maildir);

	return strdup(ret.c_str());
}

std::string maildir::folderdir(const std::string &maildir,
			       const std::string &foldername)
{
	auto path=foldername.empty() || foldername == INBOX
		? name2dir(maildir, INBOX)
		: name2dir(maildir, std::string{INBOX "."} + foldername);

	if (path.substr(0, 2) == "./")
	{
		path.erase(0, 2);
	}

	return path;
}

extern "C"
char *maildir_folderdir(const char *maildir, const char *foldername)
{
	auto dir=maildir::folderdir( maildir ? maildir:"",
				     foldername ? foldername:"" );

	if (dir.empty())
		return NULL;

	return strdup(dir.c_str());
}
