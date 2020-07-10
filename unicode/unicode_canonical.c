/*
** Copyright 2020 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include <string.h>

struct canon_map_table {
	char32_t lookup_char;
	unsigned char fmt_flag_v;
	unsigned char n_chars;
	unsigned short offset;
};

#include "canonicalmappings.h"

unicode_canonical_t unicode_canonical(char32_t c)
{
	size_t i=canon_map_hash[c % HASH_SIZE]
		+
		/* Compile-time sanity check */
		sizeof(char[ sizeof(canon_map_hash)/
			     sizeof(canon_map_hash[0]) == HASH_SIZE
			     ? 1:-1])*0;

	while (i < sizeof(canon_map_lookup)/sizeof(canon_map_lookup[0]))
	{
		if (canon_map_lookup[i].lookup_char == c)
		{
			unicode_canonical_t ret;

			ret.canonical_chars=
				canon_map_values+canon_map_lookup[i].offset;
			ret.n_canonical_chars=
				canon_map_lookup[i].n_chars;
			ret.format=
				(unicode_canonical_fmt_t)
				canon_map_lookup[i].fmt_flag_v;

			return ret;
		}

		if ((canon_map_lookup[i].lookup_char % HASH_SIZE) !=
		    (c % HASH_SIZE))
			break;
		++i;
	}

	unicode_canonical_t ret;

	memset(&ret, 0, sizeof(ret));

	return ret;
}
