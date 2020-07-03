/*
** Copyright 2011 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<errno.h>


int main(int argc, char **argv)
{
	if (argc >= 3)
	{
		unicode_grapheme_break_info_t t=unicode_grapheme_break_init();
		int n=0;
		int i;
		for (i=1; i<argc; ++i)
		{
			n=unicode_grapheme_break_next(t,
						      strtol(argv[i], NULL, 0));
		}
		unicode_grapheme_break_deinit(t);

		printf("%d\n", n);
	}
	return (0);
}
