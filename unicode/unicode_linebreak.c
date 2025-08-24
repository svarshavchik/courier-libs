/*
** Copyright 2011-2013 Double Precision, Inc.
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

#include "linebreaktab_internal.h"

#include "linebreaktab.h"

#define UNICODE_LB_SOT	0xFF

struct state_t {
	char32_t ch;
	uint8_t lb;
	uint8_t ew;

#define EASTASIA(uclass)	(					\
	(uclass).ew == UNICODE_EASTASIA_F ||				\
	(uclass).ew == UNICODE_EASTASIA_W ||				\
	(uclass).ew == UNICODE_EASTASIA_H				\
									)


	uint8_t emoji_extended_pictographic;
	unicode_general_category_t category;
};

typedef struct state_t state_t;

typedef struct def_common_state {

	state_t prevclass_min1;
	state_t prevclass;
	state_t prevclass_nsp;

	char lb15_state;

#define LB15_NONE	0
#define LB15_SEENQUPI	1

	char lb25_state;
#define LB25_NONE			0
#define LB25_SEEN_NU			1
#define LB25_SEEN_NU_SYISMAYBE_CLCP	2

	char lb28_state;
#define LB28_NONE		0
#define LB28_SEENRULE3VI	1

} def_common_state_t;

struct unicode_lb_info {
	int (*cb_func)(int, void *);
	void *cb_arg;

	int opts;

	size_t savedcmcnt;

	state_t savedclass, savedclass2;

	def_common_state_t def_common_state,
		savedstate;

	/* Flag -- seen a pair of RIs */
	char nolb30a;

	int (*next_handler)(struct unicode_lb_info *, state_t);
	int (*end_handler)(struct unicode_lb_info *);
};


/* http://www.unicode.org/reports/tr14/#Algorithm */

static int next_def(unicode_lb_info_t, state_t);
static int end_def(unicode_lb_info_t);

static void state_t_init(state_t *s,
			 char32_t ch,
			 uint8_t lb, uint8_t ew,
			 uint8_t emoji_extended_pictographic,
			 unicode_general_category_t category)
{
	s->ch=ch;
	s->lb=lb;
	s->ew=ew;
	s->emoji_extended_pictographic=emoji_extended_pictographic;
	s->category=category;
}

static void unicode_lb_reset(unicode_lb_info_t i)
{
	state_t_init(&i->def_common_state.prevclass, 0,
		     UNICODE_LB_SOT,
		     UNICODE_EASTASIA_N,
		     0,
		     UNICODE_GENERAL_CATEGORY_Cn);

	i->def_common_state.prevclass_min1=
		i->def_common_state.prevclass_nsp=i->def_common_state.prevclass;

	i->next_handler=next_def;
	i->end_handler=end_def;
}

/* #define DEBUG_LB */

unicode_lb_info_t unicode_lb_init(int (*cb_func)(int, void *),
				  void *cb_arg)
{
	unicode_lb_info_t i=calloc(1, sizeof(struct unicode_lb_info));

	if (!i)
		abort();

	i->cb_func=cb_func;
	i->cb_arg=cb_arg;

#ifdef DEBUG_LB
	printf("===\n");
#endif
	unicode_lb_reset(i);
	return i;
}

int unicode_lb_end(unicode_lb_info_t i)
{
	int rc=(*i->end_handler)(i);

	free(i);
	return rc;
}

void unicode_lb_set_opts(unicode_lb_info_t i, int opts)
{
	i->opts=opts;
}

/* Default end handler has nothing to do */

static int end_def(unicode_lb_info_t i)
{
	/* LB3 N/A */
	return 0;
}

#ifdef DEBUG_LB
static void debug_lb(const char *str)
{
	printf("%s\n", str);
}

#define RULE(x) (debug_lb(x))
#else
#define RULE(x) ( (void)0 )
#endif

#define RESULT(x, msg) (RULE(msg),*i->cb_func)((x), i->cb_arg)

int unicode_lb_next_cnt(unicode_lb_info_t i,
			const char32_t *chars,
			size_t cnt)
{
	while (cnt)
	{
		int rc=unicode_lb_next(i, *chars);

		if (rc)
			return rc;

		++chars;
		--cnt;
	}
	return 0;
}

int unicode_lb_lookup(char32_t ch)
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
				  UNICODE_LB_AL /* XX, LB1 */);
}

int unicode_lb_next(unicode_lb_info_t i,
		    char32_t ch)
{
	state_t c;

	state_t_init(&c, ch,
		     unicode_lb_lookup(ch),
		     unicode_eastasia(ch),
		     unicode_emoji_extended_pictographic(ch),
		     unicode_general_category_lookup(ch));


	if ((i->opts & UNICODE_LB_OPT_DASHWJ) &&
	    (ch == 0x2012 || ch == 0x2013))
	{
		c.lb=UNICODE_LB_WJ;
	}

	return (*i->next_handler)(i, c);
}

static int next_def_common(unicode_lb_info_t i,
			   state_t uclass);

/*
** Reset state for next_def_common.
*/

static void next_def_reset_common(unicode_lb_info_t i)
{
	i->nolb30a=0;
}

/*
** Default logic for next unicode char.
*/
static int next_def(unicode_lb_info_t i,
		    state_t uclass)
{
	next_def_reset_common(i);
	return next_def_common(i, uclass);
}

static int next_def_seen_lb30a(unicode_lb_info_t i,
			       state_t uclass)
{
	i->next_handler=next_def;
	next_def_reset_common(i);
	i->nolb30a=1;
	return next_def_common(i, uclass);
}

static int next_def_15b(unicode_lb_info_t i,
			state_t uclass);
static int no_next_def_15b(unicode_lb_info_t i);
static int next_def_15c(unicode_lb_info_t i,
			state_t uclass);
static int no_next_def_15c(unicode_lb_info_t i);
static int next_def_no15b(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state);

static int next_def_lb19a(struct unicode_lb_info *, state_t);

static int no_next_def_lb19a(struct unicode_lb_info *);

static int next_def_lb28a_rule4(struct unicode_lb_info *, state_t);

static int no_next_def_lb28a_rule4(struct unicode_lb_info *);

static int no_lb28a_rule4(struct unicode_lb_info *i, state_t uclass,
			  def_common_state_t prevstate);

static int next_def_lb25_popr_op(struct unicode_lb_info *, state_t);

static int no_next_def_lb25_popr_op(struct unicode_lb_info *);

static int next_def_lb25_popr_op_is(struct unicode_lb_info *, state_t);

static int no_next_def_lb25_popr_op_is(struct unicode_lb_info *);

static int next_def_common(unicode_lb_info_t i,
			   state_t uclass)
{
	/* Retrieve the previous unicode character's linebreak class. */

	def_common_state_t prev_state=i->def_common_state;

	/* Save this unicode char's linebreak class, for the next goaround */
	i->def_common_state.prevclass_min1=i->def_common_state.prevclass;
	i->def_common_state.prevclass=uclass;

	i->def_common_state.lb15_state=LB15_NONE;
	i->def_common_state.lb25_state=LB25_NONE;
	i->def_common_state.lb28_state=LB28_NONE;

	unicode_general_category_t c=uclass.category;

	if (uclass.lb == UNICODE_LB_NU)
	{
		i->def_common_state.lb25_state=LB25_SEEN_NU;
	}
	else if (prev_state.lb25_state == LB25_SEEN_NU)
	{
		switch (uclass.lb) {
		case UNICODE_LB_SY:
		case UNICODE_LB_IS:
			i->def_common_state.lb25_state=LB25_SEEN_NU;
			break;

		case UNICODE_LB_CL:
		case UNICODE_LB_CP:
			i->def_common_state.lb25_state=
				LB25_SEEN_NU_SYISMAYBE_CLCP;
			break;
		}
	}
	int lb15a_possible=
		(uclass.lb == UNICODE_LB_QU &&
		 c == UNICODE_GENERAL_CATEGORY_Pi);

	if (uclass.lb != UNICODE_LB_SP)
		i->def_common_state.prevclass_nsp=uclass;

	if (prev_state.prevclass.lb == UNICODE_LB_SOT)
	{
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;

		return RESULT(UNICODE_LB_NONE, "LB2");
	}

	if (prev_state.prevclass.lb == UNICODE_LB_BK)
	{
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		return RESULT(UNICODE_LB_MANDATORY, "LB4");
	}

	if (prev_state.prevclass.lb == UNICODE_LB_CR &&
	    uclass.lb == UNICODE_LB_LF)
		return RESULT(UNICODE_LB_NONE, "LB5");


	switch (prev_state.prevclass.lb) {
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		return RESULT(UNICODE_LB_MANDATORY, "LB5");
	}

	switch (uclass.lb) {
	case UNICODE_LB_SP:
		/* LB7: */
		if (prev_state.lb15_state == LB15_SEENQUPI)
		{
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		}
		/* FALLTHROUGH */
	case UNICODE_LB_ZW:
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		/* FALLTHROUGH */
	case UNICODE_LB_BK:
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:
		/* LB6: */

		return RESULT(UNICODE_LB_NONE, "LB6, LB7");
	default:
		break;
	}

	if (prev_state.prevclass_nsp.lb == UNICODE_LB_ZW)
	{
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		return RESULT(UNICODE_LB_ALLOWED, "LB8");
	}

	if (prev_state.prevclass.lb == UNICODE_LB_ZWJ)
		return RESULT(UNICODE_LB_NONE, "LB8a");

	switch (prev_state.prevclass.lb) {
	case UNICODE_LB_BK:
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:
	case UNICODE_LB_SP:
	case UNICODE_LB_ZW:
		break;
	default:

		if (uclass.lb == UNICODE_LB_CM || uclass.lb == UNICODE_LB_ZWJ)
		{
			i->def_common_state=prev_state;
			return RESULT(UNICODE_LB_NONE, "LB9");
		}
	}

	if (uclass.lb == UNICODE_LB_CM || uclass.lb == UNICODE_LB_ZWJ)
	{
		uclass.lb=UNICODE_LB_AL;
		RULE("LB10");
	}
	if (prev_state.prevclass.lb == UNICODE_LB_CM || prev_state.prevclass.lb == UNICODE_LB_ZWJ)
	{
		prev_state.prevclass.lb=UNICODE_LB_AL;
		RULE("LB10");
	}

	if (prev_state.prevclass.lb == UNICODE_LB_WJ || uclass.lb == UNICODE_LB_WJ)
		return RESULT(UNICODE_LB_NONE, "LB11");

	if (prev_state.prevclass.lb == UNICODE_LB_GL)
	{
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		return RESULT(UNICODE_LB_NONE, "LB12");
	}
	if (uclass.lb == UNICODE_LB_GL &&
	    prev_state.prevclass.lb != UNICODE_LB_SP &&
	    prev_state.prevclass.lb != UNICODE_LB_BA &&
	    prev_state.prevclass.lb != UNICODE_LB_HY)
		return RESULT(UNICODE_LB_NONE, "LB12a");

	switch (uclass.lb) {
	case UNICODE_LB_CL:
	case UNICODE_LB_CP:
	case UNICODE_LB_EX:
	case UNICODE_LB_SY:
		return RESULT(UNICODE_LB_NONE, "LB13");
	default:
		break;
	}

	if (prev_state.prevclass_nsp.lb == UNICODE_LB_OP)
	{
		if (lb15a_possible)
			i->def_common_state.lb15_state=LB15_SEENQUPI;
		return RESULT(UNICODE_LB_NONE, "LB14");
	}

	if (lb15a_possible)
	{
		switch (prev_state.prevclass.lb) {
		case UNICODE_LB_SP:
		case UNICODE_LB_QU:
		case UNICODE_LB_ZW:
			i->def_common_state.lb15_state=LB15_SEENQUPI;
			break;
		}
	}
	switch (prev_state.lb15_state) {
		case LB15_SEENQUPI:
			return RESULT(UNICODE_LB_NONE, "LB15a");
		case LB15_NONE:
			if (uclass.lb == UNICODE_LB_QU &&
			    c == UNICODE_GENERAL_CATEGORY_Pf)
			{
				i->next_handler=next_def_15b;
				i->end_handler=no_next_def_15b;
				i->savedclass=uclass;
				i->savedstate=prev_state;
				return 0;
			}
		}

	return next_def_no15b(i, uclass, prev_state);
}

static int next_def_15b(unicode_lb_info_t i,
			state_t uclass)
{
	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_SP ||
	    uclass.lb == UNICODE_LB_GL ||
	    uclass.lb == UNICODE_LB_WJ ||
	    uclass.lb == UNICODE_LB_CL ||
	    uclass.lb == UNICODE_LB_QU ||
	    uclass.lb == UNICODE_LB_CP ||
	    uclass.lb == UNICODE_LB_EX ||
	    uclass.lb == UNICODE_LB_IS ||
	    uclass.lb == UNICODE_LB_SY ||
	    uclass.lb == UNICODE_LB_BK ||
	    uclass.lb == UNICODE_LB_CR ||
	    uclass.lb == UNICODE_LB_LF ||
	    uclass.lb == UNICODE_LB_NL ||
	    uclass.lb == UNICODE_LB_ZW)
	{
		RESULT(UNICODE_LB_NONE, "LB15b");
		return next_def(i, uclass);
	}

	next_def_no15b(i, i->savedclass, i->savedstate);

	return (*i->next_handler)(i, uclass);
}

static int no_next_def_15b(unicode_lb_info_t i)
{
	return RESULT(UNICODE_LB_NONE, "LB15b");
}

static int next_def_no15c(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state);

static int next_def_no15b(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state)
{
	if (prev_state.prevclass.lb == UNICODE_LB_SP &&
	    uclass.lb == UNICODE_LB_IS)
	{
		i->next_handler=next_def_15c;
		i->end_handler=no_next_def_15c;
		i->savedclass=uclass;
		i->savedstate=prev_state;
		return 0;
	}

	return next_def_no15c(i, uclass, prev_state);
}

static int next_def_15c(unicode_lb_info_t i,
			state_t uclass)
{
	int rc;

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_NU)
	{
		rc=RESULT(UNICODE_LB_ALLOWED, "LB15c");

		if (rc == 0)
			rc=next_def(i, uclass);
		return rc;
	}

	rc=next_def_no15c(i, i->savedclass, i->savedstate);

	if (rc == 0)
		rc=(*i->next_handler)(i, uclass);
	return rc;
}

static int no_next_def_15c(unicode_lb_info_t i)
{
	return next_def_no15c(i, i->savedclass, i->savedstate);
}

static int next_def_nolb25(unicode_lb_info_t i,
			   state_t uclass,
			   def_common_state_t prev_state);

static int next_def_no19a(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state);

static int next_def_no15c(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state)
{
	if (uclass.lb == UNICODE_LB_IS)
		return RESULT(UNICODE_LB_NONE, "LB15d");

	if ((prev_state.prevclass_nsp.lb == UNICODE_LB_CL || prev_state.prevclass_nsp.lb == UNICODE_LB_CP)
	    && uclass.lb == UNICODE_LB_NS)
		return RESULT(UNICODE_LB_NONE, "LB16");

	if (prev_state.prevclass_nsp.lb == UNICODE_LB_B2 && uclass.lb == UNICODE_LB_B2)
		return RESULT(UNICODE_LB_NONE, "LB17");

	if (prev_state.prevclass.lb == UNICODE_LB_SP)
		return RESULT(UNICODE_LB_ALLOWED, "LB18");

	if (uclass.lb == UNICODE_LB_QU &&
	    uclass.category != UNICODE_GENERAL_CATEGORY_Pi)
		return RESULT(UNICODE_LB_NONE, "LB19");

	if (prev_state.prevclass.lb == UNICODE_LB_QU &&
	    prev_state.prevclass.category != UNICODE_GENERAL_CATEGORY_Pf)
		return RESULT(UNICODE_LB_NONE, "LB19");

	if (!EASTASIA(prev_state.prevclass) &&
	    uclass.lb == UNICODE_LB_QU)
		return RESULT(UNICODE_LB_NONE, "LB19a");

	if (!EASTASIA(prev_state.prevclass) &&
	    uclass.lb == UNICODE_LB_QU)
		return RESULT(UNICODE_LB_NONE, "LB19a");

	if (prev_state.prevclass.lb == UNICODE_LB_QU &&
	    !EASTASIA(uclass))
		return RESULT(UNICODE_LB_NONE, "LB19a");

	if (prev_state.prevclass.lb == UNICODE_LB_QU &&
	    (
	     prev_state.prevclass_min1.lb == UNICODE_LB_SOT ||
	     !EASTASIA(prev_state.prevclass_min1)))
	{
		return RESULT(UNICODE_LB_NONE, "LB19a");
	}

	if (uclass.lb == UNICODE_LB_QU)
	{
		i->next_handler=next_def_lb19a;
		i->end_handler=no_next_def_lb19a;
		i->savedclass=uclass;
		i->savedstate=prev_state;
		return 0;
	}

	return next_def_no19a(i, uclass, prev_state);
}

static int next_def_lb19a(struct unicode_lb_info *i, state_t uclass)
{
	int rc=0;

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (!EASTASIA(uclass))
	{
		rc=RESULT(UNICODE_LB_NONE, "LB19a");
		if (rc == 0)
			rc=next_def(i, uclass);
		return rc;
	}

	rc=next_def_no19a(i, i->savedclass, i->savedstate);

	if (rc == 0)
		rc=(*i->next_handler)(i, uclass);

	return rc;
}

static int no_next_def_lb19a(struct unicode_lb_info *i)
{
	return RESULT(UNICODE_LB_NONE, "lb19a");
}

static int next_def_no19a(unicode_lb_info_t i,
			  state_t uclass,
			  def_common_state_t prev_state)
{
	if (uclass.lb == UNICODE_LB_CB || prev_state.prevclass.lb == UNICODE_LB_CB)
		return RESULT(UNICODE_LB_ALLOWED, "LB20");

	if (uclass.lb == UNICODE_LB_AL &&
	    (prev_state.prevclass.lb == UNICODE_LB_HY ||
	     prev_state.prevclass.ch == 0x2010) &&

	    (prev_state.prevclass_min1.lb == UNICODE_LB_SOT ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_BK ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_CR ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_LF ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_NL ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_SP ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_ZW ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_CB ||
	     prev_state.prevclass_min1.lb == UNICODE_LB_GL))
	{
		return RESULT(UNICODE_LB_NONE, "LB20a");
	}

	switch (uclass.lb) {
	case UNICODE_LB_BA:
	case UNICODE_LB_HY:
	case UNICODE_LB_NS:
		return RESULT(UNICODE_LB_NONE, "LB21");
	default:
		break;
	}

	if (prev_state.prevclass.lb == UNICODE_LB_BB)
		return RESULT(UNICODE_LB_NONE, "LB21");

	if (prev_state.prevclass_min1.lb == UNICODE_LB_HL &&
	    (prev_state.prevclass.lb == UNICODE_LB_HY ||
	     (
	      prev_state.prevclass.lb == UNICODE_LB_BA &&
	      !EASTASIA(prev_state.prevclass)
	      )) &&
	    uclass.lb != UNICODE_LB_HL)
		return RESULT(UNICODE_LB_NONE, "LB21a");

	if (prev_state.prevclass.lb == UNICODE_LB_SY && uclass.lb == UNICODE_LB_HL)
		return RESULT(UNICODE_LB_NONE, "LB21b");

	if (uclass.lb == UNICODE_LB_IN)
		return RESULT(UNICODE_LB_NONE, "LB22");

	if (prev_state.prevclass.lb == UNICODE_LB_AL && uclass.lb == UNICODE_LB_NU)
		return RESULT(UNICODE_LB_NONE, "LB23");
	if (prev_state.prevclass.lb == UNICODE_LB_HL && uclass.lb == UNICODE_LB_NU)
		return RESULT(UNICODE_LB_NONE, "LB23");

	if (prev_state.prevclass.lb == UNICODE_LB_NU && uclass.lb == UNICODE_LB_AL)
		return RESULT(UNICODE_LB_NONE, "LB23");
	if (prev_state.prevclass.lb == UNICODE_LB_NU && uclass.lb == UNICODE_LB_HL)
		return RESULT(UNICODE_LB_NONE, "LB23");


	if (prev_state.prevclass.lb == UNICODE_LB_PR &&
	    (uclass.lb == UNICODE_LB_ID || uclass.lb == UNICODE_LB_EB ||
	     uclass.lb == UNICODE_LB_EM))
		return RESULT(UNICODE_LB_NONE, "LB23a");

	if ((prev_state.prevclass.lb == UNICODE_LB_ID || prev_state.prevclass.lb == UNICODE_LB_EB ||
	     prev_state.prevclass.lb == UNICODE_LB_EM) &&
	    uclass.lb == UNICODE_LB_PO)
		return RESULT(UNICODE_LB_NONE, "LB23a");

	if ((prev_state.prevclass.lb == UNICODE_LB_PR || prev_state.prevclass.lb == UNICODE_LB_PO) &&
	    (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB24");

	if ((prev_state.prevclass.lb == UNICODE_LB_AL || prev_state.prevclass.lb == UNICODE_LB_HL) &&
	    (uclass.lb == UNICODE_LB_PR || uclass.lb == UNICODE_LB_PO))
		return RESULT(UNICODE_LB_NONE, "LB24");

	if ((i->opts & UNICODE_LB_OPT_PRBREAK) && uclass.lb == UNICODE_LB_PR)
		switch (prev_state.prevclass.lb) {
		case UNICODE_LB_PR:
		case UNICODE_LB_AL:
		case UNICODE_LB_ID:
			return RESULT(UNICODE_LB_NONE, "LB24 (tailored)");
		}

	/*****/

	if (prev_state.lb25_state)
		switch (uclass.lb) {
		case UNICODE_LB_PO:
		case UNICODE_LB_PR:
			return RESULT(UNICODE_LB_NONE, "LB25");
		}

	switch (prev_state.prevclass.lb) {
	case UNICODE_LB_PO:
	case UNICODE_LB_PR:
		if (uclass.lb == UNICODE_LB_OP)
		{
			i->next_handler=next_def_lb25_popr_op;
			i->end_handler=no_next_def_lb25_popr_op;
			i->savedclass=uclass;
			i->savedstate=prev_state;
			return 0;
		}

		/* FALLTHRU */

	case UNICODE_LB_HY:
	case UNICODE_LB_IS:
		if (uclass.lb == UNICODE_LB_NU)
			return RESULT(UNICODE_LB_NONE, "LB25");
	}

	if (prev_state.lb25_state == LB25_SEEN_NU &&
	    uclass.lb == UNICODE_LB_NU)
	{
		return RESULT(UNICODE_LB_NONE, "LB25");
	}

	return next_def_nolb25(i, uclass, prev_state);
}

static int next_def_lb25_popr_op(struct unicode_lb_info *i, state_t uclass)
{
	int rc;

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_NU)
	{
		rc=RESULT(UNICODE_LB_NONE, "LB25");

		if (rc == 0)
			rc=next_def(i, uclass);

		return rc;
	}

	if (uclass.lb == UNICODE_LB_IS)
	{
		i->savedclass2=uclass;
		i->next_handler=next_def_lb25_popr_op_is;
		i->end_handler=no_next_def_lb25_popr_op_is;
		return 0;
	}

	rc=next_def_nolb25(i, i->savedclass, i->savedstate);

	if (rc == 0)
		rc=(*i->next_handler)(i, uclass);

	return rc;
}

static int no_next_def_lb25_popr_op(struct unicode_lb_info *i)
{
	return next_def_nolb25(i, i->savedclass, i->savedstate);
}

static int next_def_lb25_popr_op_is(struct unicode_lb_info *i, state_t uclass)
{
	int rc;
	state_t savedclass2=i->savedclass2;

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_NU)
	{
		rc=RESULT(UNICODE_LB_NONE, "LB25");

		if (rc == 0)
			rc=next_def(i, savedclass2);
		if (rc == 0)
			rc=(*i->next_handler)(i, uclass);

		return rc;
	}

	rc=next_def_nolb25(i, i->savedclass, i->savedstate);

	if (rc == 0)
		rc=(*i->next_handler)(i, savedclass2);
	if (rc == 0)
		rc=(*i->next_handler)(i, uclass);

	return rc;
}

static int no_next_def_lb25_popr_op_is(struct unicode_lb_info *i)
{
	state_t savedclass2=i->savedclass2;

	i->next_handler=next_def;
	i->end_handler=end_def;

	int rc=next_def_nolb25(i, i->savedclass, i->savedstate);

	if (rc == 0)
		rc=(*i->next_handler)(i, savedclass2);

	return rc;
}

static int next_def_nolb25(unicode_lb_info_t i,
			   state_t uclass,
			   def_common_state_t prev_state)
{
	if (prev_state.prevclass.lb == UNICODE_LB_JL)
		switch (uclass.lb) {
		case UNICODE_LB_JL:
		case UNICODE_LB_JV:
		case UNICODE_LB_H2:
		case UNICODE_LB_H3:
			return RESULT(UNICODE_LB_NONE, "LB26");
		default:
			break;
		}

	if ((prev_state.prevclass.lb == UNICODE_LB_JV ||
	     prev_state.prevclass.lb == UNICODE_LB_H2) &&
	    (uclass.lb == UNICODE_LB_JV ||
	     uclass.lb == UNICODE_LB_JT))
		return RESULT(UNICODE_LB_NONE, "LB26");

	if ((prev_state.prevclass.lb == UNICODE_LB_JT ||
	     prev_state.prevclass.lb == UNICODE_LB_H3) &&
	    uclass.lb == UNICODE_LB_JT)
		return RESULT(UNICODE_LB_NONE, "LB26");


	switch (prev_state.prevclass.lb) {
	case UNICODE_LB_JL:
	case UNICODE_LB_JV:
	case UNICODE_LB_JT:
	case UNICODE_LB_H2:
	case UNICODE_LB_H3:
		if (uclass.lb == UNICODE_LB_PO)
			return RESULT(UNICODE_LB_NONE, "LB27");
	default:
		break;
	}

	switch (uclass.lb) {
	case UNICODE_LB_JL:
	case UNICODE_LB_JV:
	case UNICODE_LB_JT:
	case UNICODE_LB_H2:
	case UNICODE_LB_H3:
		if (prev_state.prevclass.lb == UNICODE_LB_PR)
			return RESULT(UNICODE_LB_NONE, "LB27");
	default:
		break;
	}

	if ((prev_state.prevclass.lb == UNICODE_LB_AL || prev_state.prevclass.lb == UNICODE_LB_HL)
	    && (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB28");

	if ((prev_state.prevclass.lb == UNICODE_LB_AP)
	    && (uclass.lb == UNICODE_LB_AK ||
		uclass.ch == 0x25cc || uclass.lb == UNICODE_LB_AS))
		return RESULT(UNICODE_LB_NONE, "LB28a");

	if (prev_state.prevclass.lb == UNICODE_LB_AK ||
	     prev_state.prevclass.ch == 0x25cc ||
	    prev_state.prevclass.lb == UNICODE_LB_AS)
	{
		if (uclass.lb == UNICODE_LB_VF)
		{
			return RESULT(UNICODE_LB_NONE, "LB28a");
		}
		if (uclass.lb == UNICODE_LB_VI)
		{
			i->def_common_state.lb28_state=LB28_SEENRULE3VI;
			return RESULT(UNICODE_LB_NONE, "LB28a");
		}
	}

	if (prev_state.lb28_state == LB28_SEENRULE3VI)
	{
		if (uclass.lb == UNICODE_LB_AK || uclass.ch == 0x25cc)
			return RESULT(UNICODE_LB_NONE, "LB28a");
	}

	if ((prev_state.prevclass.lb == UNICODE_LB_AK ||
	     prev_state.prevclass.ch == 0x25cc ||
	     prev_state.prevclass.lb == UNICODE_LB_AS) &&
	    (uclass.lb == UNICODE_LB_AK ||
	     uclass.ch == 0x25cc ||
	     uclass.lb == UNICODE_LB_AS))
	{
		i->next_handler=next_def_lb28a_rule4;
		i->end_handler=no_next_def_lb28a_rule4;
		i->savedclass=uclass;
		i->savedstate=prev_state;
		RULE("LB28a (AK | ◌ | AS) x (AK | ◌ | AS) ...");
		return 0;
	}

	return no_lb28a_rule4(i, uclass, prev_state);
}

static int next_def_lb28a_rule4(struct unicode_lb_info *i, state_t uclass)
{
	int rc;

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_VF)
	{
		rc=RESULT(UNICODE_LB_NONE,
			  "LB28a (AK | ◌ | AS) x (AK | ◌ | AS) VF");
	}
	else
	{
		rc=no_lb28a_rule4(i, i->savedclass, i->savedstate);
	}

	if (rc)
		return rc;

	return next_def(i, uclass);
}

static int no_next_def_lb28a_rule4(struct unicode_lb_info *i)
{
	return no_lb28a_rule4(i, i->savedclass, i->savedstate);
}

static int no_lb28a_rule4(struct unicode_lb_info *i, state_t uclass,
			  def_common_state_t prev_state)
{
	if (prev_state.prevclass.lb == UNICODE_LB_IS &&
	    (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB29");

	if ((prev_state.prevclass.lb == UNICODE_LB_AL || prev_state.prevclass.lb == UNICODE_LB_HL
	     || prev_state.prevclass.lb == UNICODE_LB_NU) &&
	    (uclass.lb == UNICODE_LB_OP && !EASTASIA(uclass)
	     ))
		return RESULT(UNICODE_LB_NONE, "LB30");

	if ((uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL
	     || uclass.lb == UNICODE_LB_NU) &&
	    (prev_state.prevclass.lb == UNICODE_LB_CP
	     && !EASTASIA(prev_state.prevclass)))
		return RESULT(UNICODE_LB_NONE, "LB30");

	if (uclass.lb == UNICODE_LB_RI && prev_state.prevclass.lb == UNICODE_LB_RI &&
	    !i->nolb30a)
	{
		i->next_handler=next_def_seen_lb30a;
		return RESULT(UNICODE_LB_NONE, "LB30a");
	}

	if (prev_state.prevclass.lb == UNICODE_LB_EB && uclass.lb == UNICODE_LB_EM)
		return RESULT(UNICODE_LB_NONE, "LB30b");

	if (prev_state.prevclass.emoji_extended_pictographic &&
	    prev_state.prevclass.category == UNICODE_GENERAL_CATEGORY_Cn &&
	    uclass.lb == UNICODE_LB_EM)
		return RESULT(UNICODE_LB_NONE, "LB30b");

	return RESULT(UNICODE_LB_ALLOWED, "LB31");
}


/******************/

struct unicode_lbc_info {
	unicode_lb_info_t handle;

	struct unicode_buf buf;

	size_t buf_ptr;

	int (*cb_func)(int, char32_t, void *);
	void *cb_arg;
};

static int unicode_lbc_callback(int value, void *ptr)
{
	unicode_lbc_info_t h=(unicode_lbc_info_t)ptr;

	if (h->buf_ptr >= unicode_buf_len(&h->buf))
	{
		errno=EINVAL;
		return -1; /* Shouldn't happen */
	}

	return (*h->cb_func)(value, unicode_buf_ptr(&h->buf)[h->buf_ptr++],
			     h->cb_arg);
}

unicode_lbc_info_t unicode_lbc_init(int (*cb_func)(int, char32_t, void *),
				    void *cb_arg)
{
	unicode_lbc_info_t h=
		(unicode_lbc_info_t)calloc(1, sizeof(struct unicode_lbc_info));

	if (!h)
		return NULL;

	h->cb_func=cb_func;
	h->cb_arg=cb_arg;

	if ((h->handle=unicode_lb_init(unicode_lbc_callback, h)) == NULL)
	{
		free(h);
		return NULL;
	}
	unicode_buf_init(&h->buf, (size_t)-1);
	return h;
}

void unicode_lbc_set_opts(unicode_lbc_info_t i, int opts)
{
	unicode_lb_set_opts(i->handle, opts);
}

int unicode_lbc_next_cnt(unicode_lbc_info_t i,
			 const char32_t *chars,
			 size_t cnt)
{
	while (cnt)
	{
		int n=unicode_lbc_next(i, *chars);

		--cnt;
		++chars;

		if (n)
			return n;
	}
	return 0;
}

int unicode_lbc_next(unicode_lbc_info_t i, char32_t ch)
{
	if (i->buf_ptr >= unicode_buf_len(&i->buf))
	{
		i->buf_ptr=0;
		unicode_buf_clear(&i->buf);
	}

	unicode_buf_append(&i->buf, &ch, 1);
	return unicode_lb_next(i->handle, ch);
}

int unicode_lbc_end(unicode_lbc_info_t i)
{
	int rc=unicode_lb_end(i->handle);

	unicode_buf_deinit(&i->buf);
	free(i);
	return rc;
}
