/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"cgi.h"

std::vector<std::string> cgi_multiple(const char *arg)
{
	auto iter=cgi_arglist.find(arg);
	if (iter != cgi_arglist.end())
		return (iter->second);
	return {};
}
