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

extern "C"
char *maildir_folderdir(const char *maildir, const char *foldername)
{
char	*p;
const char *r;
size_t	l;

	if (!maildir)	maildir=".";
	l=strlen(maildir);

	if (foldername == 0 ||
		strcasecmp(foldername, INBOX) == 0)
	{
		if ((p=(char *)malloc(l+1)) == 0)	return (0);
		strcpy(p, maildir);
		return(p);
	}

	/* Rules: no leading/trailing periods, no /s */
	if (*foldername == '.' || strchr(foldername, '/'))
	{
		errno=EINVAL;
		return (0);
	}

	for (r=foldername; *r; r++)
	{
		if (*r != '.')	continue;
		if (r[1] == 0 || r[1] == '.')
		{
			errno=EINVAL;
			return (0);
		}
	}

	if ((p=(char *)malloc(l+strlen(foldername)+3)) == 0)	return (0);
	*p=0;
	if (strcmp(maildir, "."))
		strcat(strcpy(p, maildir), "/");

	return (strcat(strcat(p, "."), foldername));
}
