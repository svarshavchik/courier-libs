/*
** Copyright 2011 Double Precision, Inc.  See COPYING for
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

void rfc2045_error(const char *p)
{
	fprintf(stderr, "%s\n", p);
	exit(1);
}

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
	FILE *fp;
	struct rfc2045 *rfc;
	struct msg2html_info *info;

	if (argc < 2)
		return 0;

	if ((fp=fopen(argv[1], "r")) == NULL)
	{
		perror(argv[1]);
		exit(1);
	}

	rfc=rfc2045_fromfp(fp);

	info=msg2html_alloc("utf-8");
	info->showhtml=1;
	msg2html(fp, rfc, info);
	fclose(fp);
	msg2html_free(info);
	rfc2045_free(rfc);
	return (0);
}
