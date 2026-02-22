/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"cgi.h"

std::vector<std::string> cgi_multiple(const char *arg)
{
	struct cgi_arglist *argp;
	size_t	l=0;

	for (argp=cgi_arglist; argp; argp=argp->next)
		if (strcmp(argp->argname, arg) == 0)
			++l;

	std::vector<std::string> args;

	args.reserve(l);

	/*
	** Because the cgi list is build from the tail end up, we go backwards
	** now, so that we return options in the same order they were selected.
	*/

	argp=cgi_arglist;
	while (argp && argp->next)
		argp=argp->next;

	for (; argp; argp=argp->prev)
		if (strcmp(argp->argname, arg) == 0)
		{
			args.emplace_back(argp->argvalue);
		}
	return args;;
}
