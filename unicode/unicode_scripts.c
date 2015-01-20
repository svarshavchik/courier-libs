/*
** Copyright 2015 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"unicode.h"
#include "scriptstab.h"

const char *unicode_script(unicode_char a)
{
	uint8_t n=unicode_tab_lookup(a, unicode_indextab,
				     sizeof(unicode_indextab)
				     /sizeof(unicode_indextab[0]),
				     unicode_rangetab,
				     unicode_classtab,
				     sizeof(scripts)/sizeof(scripts[0])-1);

	return scripts[n];
}
