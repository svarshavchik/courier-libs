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

std::tuple<std::string, std::string> maildir::shared_fparse(std::string_view s)
{
	std::string name, dir;

	auto i=s.find('#');
	if (i != s.npos)
		s=s.substr(0, i);

	i=s.find_first_not_of(" \t\r\n");
	if (i == s.npos)
		return {};

	s=s.substr(i);
	i=s.find_first_of(" \t\r\n");
	if (i == s.npos)
		return {};

	name=std::string{s.data(), s.data()+i};

	s=s.substr(i);

	i=s.find_first_not_of(" \t\r\n");
	if (i == s.npos)
		return {};

	s=s.substr(i);

	i=s.find_first_of(" \t\r\n");
	if (i == s.npos)
		i=s.size();
	dir=std::string{s.data(), s.data()+i};

	return {name, dir};
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
