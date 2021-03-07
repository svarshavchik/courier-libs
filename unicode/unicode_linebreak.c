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
	uint8_t lb;
	uint8_t ew;
};

typedef struct state_t state_t;

struct unicode_lb_info {
	int (*cb_func)(int, void *);
	void *cb_arg;

	int opts;

	state_t savedclass;
	size_t savedcmcnt;

	state_t prevclass_min1;
	state_t prevclass;
	state_t prevclass_nsp;

	/* Flag -- recursively invoked after discarding LB25 */
	char nolb25;

	/* Flag -- seen a pair of RIs */
	char nolb30a;

	int (*next_handler)(struct unicode_lb_info *, state_t);
	int (*end_handler)(struct unicode_lb_info *);
};


/* http://www.unicode.org/reports/tr14/#Algorithm */

static int next_def(unicode_lb_info_t, state_t);
static int end_def(unicode_lb_info_t);

static int next_lb25_seenophy(unicode_lb_info_t, state_t);
static int end_lb25_seenophy(unicode_lb_info_t);

static int next_lb25_seennu(unicode_lb_info_t, state_t);

static int next_lb25_seennuclcp(unicode_lb_info_t, state_t);

static void unicode_lb_reset(unicode_lb_info_t i)
{
	i->prevclass.lb=UNICODE_LB_SOT;
	i->prevclass.ew=UNICODE_EASTASIA_N;

	i->prevclass_min1=i->prevclass_nsp=i->prevclass;
	i->next_handler=next_def;
	i->end_handler=end_def;
}

unicode_lb_info_t unicode_lb_init(int (*cb_func)(int, void *),
				  void *cb_arg)
{
	unicode_lb_info_t i=calloc(1, sizeof(struct unicode_lb_info));

	i->cb_func=cb_func;
	i->cb_arg=cb_arg;

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

/* #define DEBUG_LB */

#ifdef DEBUG_LB
#define RULE(x) ( (void)printf("%s\n", x))
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

	c.lb=unicode_lb_lookup(ch);
	c.ew=unicode_eastasia(ch);

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
	i->nolb25=0;
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


static int next_def_common(unicode_lb_info_t i,
			   state_t uclass)
{

	/* Retrieve the previous unicode character's linebreak class. */

	state_t prevclass_min1=i->prevclass_min1;
	state_t prevclass=i->prevclass;
	state_t prevclass_nsp=i->prevclass_nsp;

#define RESTORE (i->prevclass_min1=prevclass_min1,			\
		 i->prevclass=prevclass,				\
		 i->prevclass_nsp=prevclass_nsp)			\
	/* Save this unicode char's linebreak class, for the next goaround */
	i->prevclass_min1=i->prevclass;
	i->prevclass=uclass;

	if (uclass.lb != UNICODE_LB_SP)
		i->prevclass_nsp=uclass;

	if (uclass.lb == UNICODE_LB_NU)
		i->next_handler=next_lb25_seennu; /* LB25 */

	if (prevclass.lb == UNICODE_LB_SOT)
	{
		return RESULT(UNICODE_LB_NONE, "LB2");
	}

	if (prevclass.lb == UNICODE_LB_BK)
		return RESULT(UNICODE_LB_MANDATORY, "LB4");

	if (prevclass.lb == UNICODE_LB_CR && uclass.lb == UNICODE_LB_LF)
		return RESULT(UNICODE_LB_NONE, "LB5");


	switch (prevclass.lb) {
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:
		return RESULT(UNICODE_LB_MANDATORY, "LB5");
	}


	switch (uclass.lb) {
		/* LB6: */
	case UNICODE_LB_BK:
	case UNICODE_LB_CR:
	case UNICODE_LB_LF:
	case UNICODE_LB_NL:

		/* LB7: */
	case UNICODE_LB_SP:
	case UNICODE_LB_ZW:

		return RESULT(UNICODE_LB_NONE, "LB6, LB7");
	default:
		break;
	}

	if (prevclass_nsp.lb == UNICODE_LB_ZW)
		return RESULT(UNICODE_LB_ALLOWED, "LB8");


	if (prevclass.lb == UNICODE_LB_ZWJ)
		return RESULT(UNICODE_LB_NONE, "LB8a");

	switch (prevclass.lb) {
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
			RESTORE;
			return RESULT(UNICODE_LB_NONE, "LB9");
		}
	}

	if (uclass.lb == UNICODE_LB_CM || uclass.lb == UNICODE_LB_ZWJ)
	{
		uclass.lb=UNICODE_LB_AL;
		RULE("LB10");
	}
	if (prevclass.lb == UNICODE_LB_CM || prevclass.lb == UNICODE_LB_ZWJ)
	{
		prevclass.lb=UNICODE_LB_AL;
		RULE("LB10");
	}

	if (prevclass.lb == UNICODE_LB_WJ || uclass.lb == UNICODE_LB_WJ)
		return RESULT(UNICODE_LB_NONE, "LB11");

	if (prevclass.lb == UNICODE_LB_GL)
		return RESULT(UNICODE_LB_NONE, "LB12");

	if (uclass.lb == UNICODE_LB_GL &&
	    prevclass.lb != UNICODE_LB_SP &&
	    prevclass.lb != UNICODE_LB_BA &&
	    prevclass.lb != UNICODE_LB_HY)
		return RESULT(UNICODE_LB_NONE, "LB12a");


	if (uclass.lb == UNICODE_LB_SY &&
	    i->opts & UNICODE_LB_OPT_SYBREAK)
	{
		if (prevclass.lb == UNICODE_LB_SP)
			return RESULT(UNICODE_LB_ALLOWED, "LB13 (tailored)");
	}

	if (prevclass.lb != UNICODE_LB_NU) {
		switch (uclass.lb) {
		case UNICODE_LB_CL:
		case UNICODE_LB_CP:
		case UNICODE_LB_IS:
		case UNICODE_LB_SY:
			return RESULT(UNICODE_LB_NONE, "LB13");
		default:
			break;
		}
	}

	if (uclass.lb == UNICODE_LB_EX)
		return RESULT(UNICODE_LB_NONE, "LB13");

	if ((i->opts & UNICODE_LB_OPT_SYBREAK) && prevclass.lb == UNICODE_LB_SY)
		switch (uclass.lb) {
		case UNICODE_LB_EX:
		case UNICODE_LB_AL:
		case UNICODE_LB_ID:
			return RESULT(UNICODE_LB_NONE, "LB13");
		}

	if (prevclass_nsp.lb == UNICODE_LB_OP)
		return RESULT(UNICODE_LB_NONE, "LB14");

	if (prevclass_nsp.lb == UNICODE_LB_QU && uclass.lb == UNICODE_LB_OP)
		return RESULT(UNICODE_LB_NONE, "LB15");

	if ((prevclass_nsp.lb == UNICODE_LB_CL || prevclass_nsp.lb == UNICODE_LB_CP)
	    && uclass.lb == UNICODE_LB_NS)
		return RESULT(UNICODE_LB_NONE, "LB16");

	if (prevclass_nsp.lb == UNICODE_LB_B2 && uclass.lb == UNICODE_LB_B2)
		return RESULT(UNICODE_LB_NONE, "LB17");

	if (prevclass.lb == UNICODE_LB_SP)
		return RESULT(UNICODE_LB_ALLOWED, "LB18");

	if (uclass.lb == UNICODE_LB_QU || prevclass.lb == UNICODE_LB_QU)
		return RESULT(UNICODE_LB_NONE, "LB19");

	if (uclass.lb == UNICODE_LB_CB || prevclass.lb == UNICODE_LB_CB)
		return RESULT(UNICODE_LB_ALLOWED, "LB20");

	switch (uclass.lb) {
	case UNICODE_LB_BA:
	case UNICODE_LB_HY:
	case UNICODE_LB_NS:
		return RESULT(UNICODE_LB_NONE, "LB21");
	default:
		break;
	}

	if (prevclass.lb == UNICODE_LB_BB)
		return RESULT(UNICODE_LB_NONE, "LB21");

	if (prevclass_min1.lb == UNICODE_LB_HL &&
	    (prevclass.lb == UNICODE_LB_HY || prevclass.lb == UNICODE_LB_BA))
		return RESULT(UNICODE_LB_NONE, "LB21a");

	if (prevclass.lb == UNICODE_LB_SY && uclass.lb == UNICODE_LB_HL)
		return RESULT(UNICODE_LB_NONE, "LB21b");

	if (uclass.lb == UNICODE_LB_IN)
		return RESULT(UNICODE_LB_NONE, "LB22");

	if (prevclass.lb == UNICODE_LB_AL && uclass.lb == UNICODE_LB_NU)
		return RESULT(UNICODE_LB_NONE, "LB23");
	if (prevclass.lb == UNICODE_LB_HL && uclass.lb == UNICODE_LB_NU)
		return RESULT(UNICODE_LB_NONE, "LB23");

	if (prevclass.lb == UNICODE_LB_NU && uclass.lb == UNICODE_LB_AL)
		return RESULT(UNICODE_LB_NONE, "LB23");
	if (prevclass.lb == UNICODE_LB_NU && uclass.lb == UNICODE_LB_HL)
		return RESULT(UNICODE_LB_NONE, "LB23");


	if (prevclass.lb == UNICODE_LB_PR &&
	    (uclass.lb == UNICODE_LB_ID || uclass.lb == UNICODE_LB_EB ||
	     uclass.lb == UNICODE_LB_EM))
		return RESULT(UNICODE_LB_NONE, "LB23a");

	if ((prevclass.lb == UNICODE_LB_ID || prevclass.lb == UNICODE_LB_EB ||
	     prevclass.lb == UNICODE_LB_EM) &&
	    uclass.lb == UNICODE_LB_PO)
		return RESULT(UNICODE_LB_NONE, "LB23a");

	if ((prevclass.lb == UNICODE_LB_PR || prevclass.lb == UNICODE_LB_PO) &&
	    (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB24");

	if ((prevclass.lb == UNICODE_LB_AL || prevclass.lb == UNICODE_LB_HL) &&
	    (uclass.lb == UNICODE_LB_PR || uclass.lb == UNICODE_LB_PO))
		return RESULT(UNICODE_LB_NONE, "LB24");

	if ((i->opts & UNICODE_LB_OPT_PRBREAK) && uclass.lb == UNICODE_LB_PR)
		switch (prevclass.lb) {
		case UNICODE_LB_PR:
		case UNICODE_LB_AL:
		case UNICODE_LB_ID:
			return RESULT(UNICODE_LB_NONE, "LB24 (tailored)");
		}

	if (!i->nolb25 &&
	    (prevclass.lb == UNICODE_LB_PR || prevclass.lb == UNICODE_LB_PO))
	{
		if (uclass.lb == UNICODE_LB_NU)
			return RESULT(UNICODE_LB_NONE, "LB25");

		if (uclass.lb == UNICODE_LB_OP || uclass.lb == UNICODE_LB_HY)
		{
			RESTORE;
			RULE("LB25 (start)");
			i->savedclass=uclass;
			i->savedcmcnt=0;
			i->next_handler=next_lb25_seenophy;
			i->end_handler=end_lb25_seenophy;
			return 0;
		}
	}

	if ((prevclass.lb == UNICODE_LB_OP || prevclass.lb == UNICODE_LB_HY) &&
	    uclass.lb == UNICODE_LB_NU)
		return RESULT(UNICODE_LB_NONE, "LB25");

	/*****/

	if (prevclass.lb == UNICODE_LB_JL)
		switch (uclass.lb) {
		case UNICODE_LB_JL:
		case UNICODE_LB_JV:
		case UNICODE_LB_H2:
		case UNICODE_LB_H3:
			return RESULT(UNICODE_LB_NONE, "LB26");
		default:
			break;
		}

	if ((prevclass.lb == UNICODE_LB_JV ||
	     prevclass.lb == UNICODE_LB_H2) &&
	    (uclass.lb == UNICODE_LB_JV ||
	     uclass.lb == UNICODE_LB_JT))
		return RESULT(UNICODE_LB_NONE, "LB26");

	if ((prevclass.lb == UNICODE_LB_JT ||
	     prevclass.lb == UNICODE_LB_H3) &&
	    uclass.lb == UNICODE_LB_JT)
		return RESULT(UNICODE_LB_NONE, "LB26");


	switch (prevclass.lb) {
	case UNICODE_LB_JL:
	case UNICODE_LB_JV:
	case UNICODE_LB_JT:
	case UNICODE_LB_H2:
	case UNICODE_LB_H3:
		if (uclass.lb == UNICODE_LB_IN || uclass.lb == UNICODE_LB_PO)
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
		if (prevclass.lb == UNICODE_LB_PR)
			return RESULT(UNICODE_LB_NONE, "LB27");
	default:
		break;
	}

	if ((prevclass.lb == UNICODE_LB_AL || prevclass.lb == UNICODE_LB_HL)
	    && (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB28");

	if (prevclass.lb == UNICODE_LB_IS &&
	    (uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL))
		return RESULT(UNICODE_LB_NONE, "LB29");

	if ((prevclass.lb == UNICODE_LB_AL || prevclass.lb == UNICODE_LB_HL
	     || prevclass.lb == UNICODE_LB_NU) &&
	    (uclass.lb == UNICODE_LB_OP && uclass.ew != UNICODE_EASTASIA_F
	     && uclass.ew != UNICODE_EASTASIA_W
	     && uclass.ew != UNICODE_EASTASIA_H))
		return RESULT(UNICODE_LB_NONE, "LB30");

	if ((uclass.lb == UNICODE_LB_AL || uclass.lb == UNICODE_LB_HL
	     || uclass.lb == UNICODE_LB_NU) &&
	    (prevclass.lb == UNICODE_LB_CP
	     && prevclass.ew != UNICODE_EASTASIA_F
	     && prevclass.ew != UNICODE_EASTASIA_W
	     && prevclass.ew != UNICODE_EASTASIA_H))
		return RESULT(UNICODE_LB_NONE, "LB30");

	if (uclass.lb == UNICODE_LB_RI && prevclass.lb == UNICODE_LB_RI &&
	    !i->nolb30a)
	{
		i->next_handler=next_def_seen_lb30a;
		return RESULT(UNICODE_LB_NONE, "LB30a");
	}

	if (prevclass.lb == UNICODE_LB_EB && uclass.lb == UNICODE_LB_EM)
		return RESULT(UNICODE_LB_NONE, "LB30b");

	return RESULT(UNICODE_LB_ALLOWED, "LB31");
}

/*
** Seen (PR|PO)(OP|HY), without returning the linebreak property for the second
** character, but NU did not follow. Backtrack.
*/

static int unwind_lb25_seenophy(unicode_lb_info_t i)
{
	int rc;

	/*state_t class=i->savedclass;*/
	int nolb25_flag=1;

	i->next_handler=next_def;
	i->end_handler=end_def;

	do
	{
		next_def_reset_common(i);
		i->nolb25=nolb25_flag;
		rc=next_def_common(i, i->savedclass);

		if (rc)
			return rc;

		/*class=UNICODE_LB_CM;*/
		nolb25_flag=0;
	} while (i->savedcmcnt--);
	return 0;
}

/*
** Seen (PR|PO)(OP|HY), without returning the linebreak property for the second
** character. If there's now a NU, we found the modified LB25 regexp.
*/

static int next_lb25_seenophy(unicode_lb_info_t i,
			      state_t uclass)
{
	int rc;

	if (uclass.lb == UNICODE_LB_CM)
	{
		++i->savedcmcnt; /* Keep track of CMs, and try again */
		return 0;
	}

	if (uclass.lb != UNICODE_LB_NU)
	{
		rc=unwind_lb25_seenophy(i);

		if (rc)
			return rc;

		return next_def(i, uclass);
	}

	do
	{
		rc=RESULT(UNICODE_LB_NONE, "LB25 (OP|HY)"); /* (OP|HY) feedback */

		if (rc)
			return rc;
	} while (i->savedcmcnt--);

	i->next_handler=next_lb25_seennu;
	i->end_handler=end_def;
	i->prevclass=i->prevclass_nsp=uclass;
	return RESULT(UNICODE_LB_NONE, "LB25");
}

/*
** Seen (PR|PO)(OP|HY), and now The End. Unwind, and give up.
*/

static int end_lb25_seenophy(unicode_lb_info_t i)
{
	int rc=unwind_lb25_seenophy(i);

	if (rc == 0)
		rc=end_def(i);
	return rc;
}

/*
** Seen an NU, modified LB25 regexp.
*/
static int next_lb25_seennu(unicode_lb_info_t i, state_t uclass)
{
	if (uclass.lb == UNICODE_LB_NU || uclass.lb == UNICODE_LB_SY ||
	    uclass.lb == UNICODE_LB_IS)
	{
		i->prevclass=i->prevclass_nsp=uclass;
		return RESULT(UNICODE_LB_NONE, "LB25");
	}

	if (uclass.lb == UNICODE_LB_CM || uclass.lb == UNICODE_LB_ZWJ)
		return RESULT(UNICODE_LB_NONE, "LB9 (LB25)");

	if (uclass.lb == UNICODE_LB_CL || uclass.lb == UNICODE_LB_CP)
	{
		i->prevclass=i->prevclass_nsp=uclass;
		i->next_handler=next_lb25_seennuclcp;
		i->end_handler=end_def;
		return RESULT(UNICODE_LB_NONE, "LB25");
	}

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_PR || uclass.lb == UNICODE_LB_PO)
	{
		i->prevclass=i->prevclass_nsp=uclass;
		return RESULT(UNICODE_LB_NONE, "LB25");
	}

	return next_def(i, uclass); /* Not a prefix, process normally */
}

/*
** Seen CL|CP, in the modified LB25 regexp.
*/
static int next_lb25_seennuclcp(unicode_lb_info_t i, state_t uclass)
{
	if (uclass.lb == UNICODE_LB_CM || uclass.lb == UNICODE_LB_ZWJ)
		return RESULT(UNICODE_LB_NONE, "LB9 (LB25)");

	i->next_handler=next_def;
	i->end_handler=end_def;

	if (uclass.lb == UNICODE_LB_PR || uclass.lb == UNICODE_LB_PO)
	{
		i->prevclass=i->prevclass_nsp=uclass;

		return RESULT(UNICODE_LB_NONE, "LB25");
	}

	return next_def(i, uclass);
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
