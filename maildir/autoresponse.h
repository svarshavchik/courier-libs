#ifndef	maildir_autoresponse_h
#define	maildir_autoresponse_h

/*
** Copyright 2001-2003 S. Varshavchik.
** See COPYING for distribution information.
*/


#include	"config.h"

#include	<vector>
#include	<string>
#include	<string_view>
#include	<fstream>
#include	<functional>

namespace mail {
	namespace autoresponse {
#if 0
	}
}
#endif

/* Return a list of available autoresponses, NULL if error */

std::vector<std::string> list(std::string_view maildirpath);

/* Validate the autoresponse name */

bool validate(std::string_view maildirpath, std::string_view name);

/* Delete/Create/Open autoresponse text */

void remove(std::string_view maildirpath, std::string_view name);

bool create(std::string_view maildirpath, std::string_view name,
	    std::function<void (std::ostream &)> creator);

void open(std::ifstream &i,
	  std::string_view maildirpath, std::string_view name);

#if 0
{
	{
#endif
	}
}

#endif
