/*
** Copyright 2000-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"maildirmisc.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<errno.h>
#include	<string>

extern "C"
FILE *maildir_shared_fopen(const char *maildir, const char *mode)
{
	FILE	*fp;

	std::string m;

	m.reserve(strlen(maildir) + sizeof("/shared-maildirs"));

	m=maildir;
	m += "/shared-maildirs";

	fp=fopen(m.c_str(), mode);
	return (fp);
}

extern "C"
void maildir_shared_fparse(char *p, char **name, char **dir)
{
char	*q;

	*name=0;
	*dir=0;

	if ((q=strchr(p, '\n')) != 0)	*q=0;
	if ((q=strchr(p, '#')) != 0)	*q=0;

	for (q=p; *q; q++)
		if (isspace((int)(unsigned char)*q))	break;
	if (!*q)	return;
	*q++=0;
	while (*q && isspace((int)(unsigned char)*q))
		++q;
	if (*q)
	{
		*name=p;
		*dir=q;
	}
}

char *maildir_shareddir(const char *maildir, const char *sharedname)
{
	if (!maildir)   maildir=".";

	auto path=maildir::shareddir(maildir, sharedname);

	if (path.empty())
		return NULL;

	return strdup(path.c_str());
}

std::string maildir::shareddir(const std::string &maildir,
			       const std::string &sharedname)
{

	if (sharedname.find('.') == sharedname.npos ||
	    sharedname.size() == 0 ||
	    sharedname[0] == '.' ||
	    sharedname.find('/') != sharedname.npos)
	{
		errno=EINVAL;
		return "";
	}

	char last_char=0;
	for (auto c:sharedname)
	{
		if (c == '.' && last_char == '.')
		{
			errno=EINVAL;
			return "";
		}

		last_char=c;
	}

	if (last_char == '.')
	{
		errno=EINVAL;
		return "";
	}


	std::string p;

	p.reserve(maildir.size()+sizeof("/" SHAREDSUBDIR)+sharedname.size());

	if (maildir != ".")
	{
		p=maildir;
		p+= "/";
	}
	p += SHAREDSUBDIR "/";
	size_t l=p.size();
	p += sharedname;
	p[p.find('.', l)]='/';

	return p;
}
