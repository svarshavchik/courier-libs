/*
** Copyright 2015 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	"categoriestab.h"
#include	"linebreaktab_internal.h"

uint32_t unicode_category_lookup(unicode_char ch)
{
	return unicode_tab32_lookup(ch,
				    unicode_indextab,
				    sizeof(unicode_indextab)
				    / sizeof(unicode_indextab[0]),
				    unicode_rangetab,
				    unicode_classtab,
				    0);
}

int unicode_isspace(unicode_char ch)
{
	switch (unicode_lb_lookup(ch)) {
	case UNICODE_LB_BK:
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:
	case UNICODE_LB_SP:
		return 1;
	}

	return unicode_isblank(ch);
}

int unicode_isblank(unicode_char ch)
{
	if (ch == 9)
		return 1;

	if ((unicode_category_lookup(ch) & UNICODE_CATEGORY_2) ==
	    UNICODE_CATEGORY_2_SPACE)
		return 1;
	return 0;
}

int unicode_isalpha(unicode_char ch)
{
	return (unicode_category_lookup(ch) & UNICODE_CATEGORY_1) ==
		UNICODE_CATEGORY_1_LETTER;
}

int unicode_isdigit(unicode_char ch)
{
	return unicode_category_lookup(ch) ==
		(UNICODE_CATEGORY_1_NUMBER | UNICODE_CATEGORY_2_DIGIT);
}

int unicode_isalnum(unicode_char ch)
{
	return unicode_is_alpha(ch) || unicode_is_digit(ch);
}

int unicode_isgraph(unicode_char ch)
{
	return (ch >= ' ' && !unicode_isspace(ch));
}

int unicode_ispunct(unicode_char ch)
{
	return (unicode_category_lookup(ch) & UNICODE_CATEGORY_1) ==
		UNICODE_CATEGORY_1_PUNCTUATION;
}

int unicode_islower(unicode_char ch)
{
	return unicode_isalpha(ch) && ch == unicode_lc(ch);
}

int unicode_isupper(unicode_char ch)
{
	return unicode_isalpha(ch) && ch == unicode_uc(ch);
}
