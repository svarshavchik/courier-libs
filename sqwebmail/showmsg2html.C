/*
** Copyright 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"msg2html.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>

void error(const char *p)
{
	fprintf(stderr, "%s\n", p);
	exit(1);
}

void fake_exit(int rc)
{
	exit(rc);
}

int main(int argc, char **argv)
{
	struct msg2html_info *info;

	if (argc < 2)
		return 0;

	rfc822::fdstreambuf fd{open(argv[1], O_RDONLY)};

	if (fd.error())
	{
		perror(argv[1]);
		exit(1);
	}

	rfc2045::entity message;
	{
		std::istreambuf_iterator<char> b{&fd}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	info=msg2html_alloc("utf-8");
	info->showhtml=1;
	msg2html(fd, message, info);
	msg2html_free(info);
	return (0);
}
