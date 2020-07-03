/*
** Copyright 2011 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	"emojitab.h"

static int emoji_lookup(const char32_t (*p)[2], size_t e, char32_t c)
{
	size_t b=0;

	while (b < e)
	{
		size_t n=b + (e-b)/2;

		if (c >= p[n][0])
		{
			if (c <= p[n][1])
			{
				return 1;
			}
			b=n+1;
		}
		else
		{
			e=n;
		}
	}

	return 0;
}

#define LOOKUP(t) emoji_lookup(t, sizeof(t)/sizeof(t[0]), c)

int unicode_emoji(char32_t c)
{
	return LOOKUP(unicode_emoji_emoji_lookup);
}

int unicode_emoji_presentation(char32_t c)
{
	return LOOKUP(unicode_emoji_emoji_presentation_lookup);
}

int unicode_emoji_modifier(char32_t c)
{
	return LOOKUP(unicode_emoji_emoji_modifier_lookup);
}

int unicode_emoji_modifier_base(char32_t c)
{
	return LOOKUP(unicode_emoji_emoji_modifier_base_lookup);
}

int unicode_emoji_component(char32_t c)
{
	return LOOKUP(unicode_emoji_emoji_component_lookup);
}

int unicode_emoji_extended_pictographic(char32_t c)
{
	return LOOKUP(unicode_emoji_extended_pictographic_lookup);
}

static const struct {
	int (*lookup_func)(char32_t);
	unicode_emoji_t flag;
} lookups[]={
	     { unicode_emoji, UNICODE_EMOJI},
	     { unicode_emoji_presentation, UNICODE_EMOJI_PRESENTATION},
	     { unicode_emoji_modifier, UNICODE_EMOJI_MODIFIER},
	     { unicode_emoji_modifier_base, UNICODE_EMOJI_MODIFIER_BASE},
	     { unicode_emoji_component, UNICODE_EMOJI_COMPONENT},
	     { unicode_emoji_extended_pictographic,
	       UNICODE_EMOJI_EXTENDED_PICTOGRAPHIC},
};

extern unicode_emoji_t unicode_emoji_lookup(char32_t c)
{
	unicode_emoji_t v=UNICODE_EMOJI_NONE;

	for (size_t i=0; i<sizeof(lookups)/sizeof(lookups[0]); ++i)
		if ( (*lookups[i].lookup_func)(c) )
			v |= lookups[i].flag;

	return v;
}
