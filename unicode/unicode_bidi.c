/*
** Copyright 2011-2020 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<unistd.h>
#include	<stdint.h>
#include	<string.h>
#include	<stdlib.h>

static const char32_t bidi_brackets[][2]={
#include "bidi_brackets.h"
};

static const unicode_bidi_bracket_type_t bidi_brackets_v[]={
#include "bidi_brackets_v.h"
};

static const char32_t bidi_mirroring[][2]={
#include "bidi_mirroring.h"
};

static int bidi_lookup(const char32_t (*p)[2], size_t e, char32_t c,
		       size_t *ret)
{
	size_t b=0;

	while (b < e)
	{
		size_t n=b + (e-b)/2;

		if (c > p[n][0])
		{
			b=n+1;
		}
		else if (c < p[n][0])
		{
			e=n;
		}
		else
		{
			*ret=n;
			return 1;
		}
	}

	return 0;
}

char32_t unicode_bidi_mirror(char32_t c)
{
	size_t ret;

	if (bidi_lookup(bidi_mirroring,
			sizeof(bidi_mirroring)/sizeof(bidi_mirroring[0]),
			c,
			&ret))
	{
		return bidi_mirroring[ret][1];
	}

	return c;
}

char32_t unicode_bidi_bracket_type(char32_t c,
				   unicode_bidi_bracket_type_t *type_ret)
{
	size_t ret;

	if (bidi_lookup(bidi_brackets,
			sizeof(bidi_brackets)/sizeof(bidi_brackets[0]),
			c,
			&ret))
	{
		if (type_ret)
			*type_ret=bidi_brackets_v[ret];

		return bidi_brackets[ret][1];
	}

	if (type_ret)
		*type_ret=UNICODE_BIDI_n;
	return c;
}

typedef enum {
	      UNICODE_BIDI_CLASS_AL,
	      UNICODE_BIDI_CLASS_AN,
	      UNICODE_BIDI_CLASS_B,
	      UNICODE_BIDI_CLASS_BN,
	      UNICODE_BIDI_CLASS_CS,
	      UNICODE_BIDI_CLASS_EN,
	      UNICODE_BIDI_CLASS_ES,
	      UNICODE_BIDI_CLASS_ET,
	      UNICODE_BIDI_CLASS_FSI,
	      UNICODE_BIDI_CLASS_L,
	      UNICODE_BIDI_CLASS_LRE,
	      UNICODE_BIDI_CLASS_LRI,
	      UNICODE_BIDI_CLASS_LRO,
	      UNICODE_BIDI_CLASS_NSM,
	      UNICODE_BIDI_CLASS_ON,
	      UNICODE_BIDI_CLASS_PDF,
	      UNICODE_BIDI_CLASS_PDI,
	      UNICODE_BIDI_CLASS_R,
	      UNICODE_BIDI_CLASS_RLE,
	      UNICODE_BIDI_CLASS_RLI,
	      UNICODE_BIDI_CLASS_RLO,
	      UNICODE_BIDI_CLASS_S,
	      UNICODE_BIDI_CLASS_WS,
} enum_bidi_class_t;

#include "bidi_class.h"

enum_bidi_class_t unicode_bidi_class(char32_t c)
{
	return unicode_tab_lookup(c,
				  unicode_indextab,
                                  sizeof(unicode_indextab)
                                  /sizeof(unicode_indextab[0]),
                                  unicode_rangetab,
                                  unicode_classtab,
				  UNICODE_BIDI_CLASS_L);
}
