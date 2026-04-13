/*
** Copyright 2002-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<filesystem>
#include	"maildirmisc.h"

void maildir::list(std::string_view maildir,
		   const std::function<void (const std::string &)> &callback)
{
	std::string name;

	callback(INBOX);
	for (auto &e: std::filesystem::directory_iterator(maildir))
	{
		auto p=e.path();
		std::string fn=p.filename().string();
		if (fn.empty() || *fn.c_str() != '.')
			continue;

		std::string p2=p.string();
		p2+="/cur/.";
		if (access(p2.c_str(), X_OK))
			continue;
		name=INBOX;
		name += fn;
		callback(name);
	}
}
