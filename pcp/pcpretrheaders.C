/*
** Copyright 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcpdir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "rfc822/rfc822.h"
#include "rfc2045/rfc2045.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int retrheaders(struct PCPdir *pd, struct PCP_retr *ri,
		const char *filename)
{
	rfc822::fdstreambuf fp{open(filename, O_RDONLY)};

	if (fp.error())
		return (errno == ENOENT ? 0:-1);

	rfc2045::entity::line_iter<false>::headers headers{fp};

	do
	{
		const auto &[name, content] = headers.name_content();

		if (name.empty() && content.empty())
			continue;

		std::string header{name.begin(), name.end()},
			value{content.begin(), content.end()};

		if ((*ri->callback_headers_func)(ri, header.c_str(),
						      value.c_str(),
						      ri->callback_arg) != 0)
			break;
	} while (headers.next());

	return (0);
}
