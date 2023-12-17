/*
** Copyright 2023 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "general_categorytab.h"

unicode_general_category_t unicode_general_category_lookup(char32_t ch)
{
	return unicode_tab_lookup(ch,
				  unicode_starting_indextab,
				  unicode_starting_pagetab,
				  sizeof(unicode_starting_indextab)/
				  sizeof(unicode_starting_indextab[0]),
				  unicode_rangetab,
				  sizeof(unicode_rangetab)/
				  sizeof(unicode_rangetab[0]),
				  unicode_classtab,
				  UNICODE_GENERAL_CATEGORY_Cn);
}
