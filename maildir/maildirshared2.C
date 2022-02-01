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
#include	<algorithm>

namespace maildir {
	std::string shared_filename(const std::string &maildir)
	{
		std::string m;

		m.reserve(maildir.size() + sizeof("shared-maildirs"));

		m=maildir;
		m += "/shared-maildirs";

		return m;
	}
}

extern "C"
FILE *maildir_shared_fopen(const char *maildir, const char *mode)
{
	return fopen(maildir::shared_filename(maildir).c_str(), mode);
}

void maildir::shared_fparse(char *b, char *e,
			    char * &nameb, char * &namee,
			    char * &dirb, char * &dire)
{
	nameb=namee=dirb=dire=e;

	e=std::find(b, e, '\n');
	e=std::find(b, e, '#');

	nameb=b;

	while (b != e)
	{
		if (isspace((int)(unsigned char)*b))	break;
		++b;
	}
	if (b == e)
	{
		nameb=namee;
		return;
	}

	namee=b;

	while (b != e && isspace((int)(unsigned char)*b))
		++b;

	if (b != e)
	{
		dirb=b;
		dire=e;
		return;
	}

	nameb=namee=dirb=dire=e;
}

extern "C"
void maildir_shared_fparse(char *p, char **name, char **dir)
{
	char *nameb, *namee, *dirb, *dire;

	maildir::shared_fparse(p, p+strlen(p), nameb, namee, dirb, dire);

	if (nameb != namee)
	{
		*name=nameb;
		*namee=0;
		*dir=dirb;
		*dire=0;
	}
	else
	{
		*name=*dir=0;
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
