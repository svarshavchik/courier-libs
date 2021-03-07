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

#define UNICODE_GRAPHEMEBREAK_ANY		0x00
#define UNICODE_GRAPHEMEBREAK_CR		0x01
#define UNICODE_GRAPHEMEBREAK_LF		0x02
#define UNICODE_GRAPHEMEBREAK_Control		0x03
#define UNICODE_GRAPHEMEBREAK_Extend		0x04
#define UNICODE_GRAPHEMEBREAK_Prepend		0x05
#define UNICODE_GRAPHEMEBREAK_SpacingMark	0x06
#define UNICODE_GRAPHEMEBREAK_L			0x07
#define UNICODE_GRAPHEMEBREAK_V			0x08
#define UNICODE_GRAPHEMEBREAK_T			0x09
#define UNICODE_GRAPHEMEBREAK_LV		0x0A
#define UNICODE_GRAPHEMEBREAK_LVT		0x0B
#define UNICODE_GRAPHEMEBREAK_Regional_Indicator 0x0C

#define UNICODE_GRAPHEMEBREAK_ZWJ		0x0D

#define UNICODE_GRAPHEMEBREAK_SOT		0xFF

#include "graphemebreaktab.h"

struct unicode_grapheme_break_info_s {
	uint8_t prev_class;
	unsigned prev_count;
};

unicode_grapheme_break_info_t unicode_grapheme_break_init()
{
	unicode_grapheme_break_info_t t=(unicode_grapheme_break_info_t)
		calloc(1, sizeof(struct unicode_grapheme_break_info_s));

	if (!t)
		abort();

	t->prev_class=UNICODE_GRAPHEMEBREAK_SOT;

	return t;
}

void unicode_grapheme_break_deinit(unicode_grapheme_break_info_t t)
{
	free(t);
}

int unicode_grapheme_break(char32_t a, char32_t b)
{
	struct unicode_grapheme_break_info_s s;

	memset((char *)&s, 0, sizeof(s));

	(void)unicode_grapheme_break_next(&s, a);

	return unicode_grapheme_break_next(&s, b);
}

int unicode_grapheme_break_next(unicode_grapheme_break_info_t t, char32_t b)
{
	uint8_t ac=t->prev_class;
	uint8_t bc=unicode_tab_lookup(b,
				      unicode_starting_indextab,
				      unicode_starting_pagetab,
				      sizeof(unicode_starting_indextab)/
				      sizeof(unicode_starting_indextab[0]),
				      unicode_rangetab,
				      sizeof(unicode_rangetab)/
				      sizeof(unicode_rangetab[0]),
				      unicode_classtab,
				      UNICODE_GRAPHEMEBREAK_ANY);

	if (ac != bc)
		t->prev_count=0;
	++t->prev_count;

	t->prev_class=bc;

	if (ac == UNICODE_GRAPHEMEBREAK_SOT)
		return 1; /* GB1, GB2 is implied */

	if (ac == UNICODE_GRAPHEMEBREAK_CR && bc == UNICODE_GRAPHEMEBREAK_LF)
		return 0; /* GB3 */


	switch (ac) {
	case UNICODE_GRAPHEMEBREAK_CR:
	case UNICODE_GRAPHEMEBREAK_LF:
	case UNICODE_GRAPHEMEBREAK_Control:
		return 1; /* GB4 */
	default:
		break;
	}

	switch (bc) {
	case UNICODE_GRAPHEMEBREAK_CR:
	case UNICODE_GRAPHEMEBREAK_LF:
	case UNICODE_GRAPHEMEBREAK_Control:
		return 1; /* GB5 */
	default:
		break;
	}

	if (ac == UNICODE_GRAPHEMEBREAK_L)
		switch (bc) {
		case UNICODE_GRAPHEMEBREAK_L:
		case UNICODE_GRAPHEMEBREAK_V:
		case UNICODE_GRAPHEMEBREAK_LV:
		case UNICODE_GRAPHEMEBREAK_LVT:
			return 0; /* GB6 */
		}

	if ((ac == UNICODE_GRAPHEMEBREAK_LV ||
	     ac == UNICODE_GRAPHEMEBREAK_V) &&
	    (bc == UNICODE_GRAPHEMEBREAK_V ||
	     bc == UNICODE_GRAPHEMEBREAK_T))
		return 0; /* GB7 */

	if ((ac == UNICODE_GRAPHEMEBREAK_LVT ||
	     ac == UNICODE_GRAPHEMEBREAK_T) &&
	    bc == UNICODE_GRAPHEMEBREAK_T)
		return 0; /* GB8 */

	if (bc == UNICODE_GRAPHEMEBREAK_Extend ||
	    bc == UNICODE_GRAPHEMEBREAK_ZWJ)
		return 0; /* GB9 */

	if (bc == UNICODE_GRAPHEMEBREAK_SpacingMark)
		return 0; /* GB9a */

	if (ac == UNICODE_GRAPHEMEBREAK_Prepend)
		return 0; /* GB9b */

	if (ac == UNICODE_GRAPHEMEBREAK_Extend ||
	    ac == UNICODE_GRAPHEMEBREAK_ZWJ)
		return 0; /* GB11? */

	if (ac == UNICODE_GRAPHEMEBREAK_Regional_Indicator &&
	    bc == UNICODE_GRAPHEMEBREAK_Regional_Indicator &&
	    (t->prev_count % 2) == 0)
		return 0; /* GB12, GB13 */

	return 1; /* GB999 */
}
