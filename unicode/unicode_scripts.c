/*
** Copyright 2015 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include "scriptstab.h"

unicode_script_t unicode_script(char32_t a)
{
	return unicode_tab_lookup(a,
				  unicode_starting_indextab,
				  unicode_starting_pagetab,
				  sizeof(unicode_starting_indextab)/
				  sizeof(unicode_starting_indextab[0]),
				  unicode_rangetab,
				  sizeof(unicode_rangetab)/
				  sizeof(unicode_rangetab[0]),
				  unicode_classtab,
				  unicode_script_unknown);
}
