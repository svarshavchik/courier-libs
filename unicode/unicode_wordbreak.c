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

#include "wordbreaktab_internal.h"
#include "wordbreaktab.h"

/*
** We need to keep track of the original character, in addition
** to the wordbreaking class, to check WB3.
*/

typedef struct {
	uint8_t cl;
	char32_t ch;
} wb_info_t;

/*
** Internal object.
*/
struct unicode_wb_info {
	int (*cb_func)(int, void *);
	void *cb_arg;

	/* Previous character seen. */
	wb_info_t prevclass;

	/*
	** For some rules we peek an extra character, and so need to
	** stash away the 2nd previous character seen, when we're looking at
	** it.
	*/
	wb_info_t prev2class;

	/*
	** How many (Extend | Format | ZWJ) were processed, so far,
	** for WB4's sake.
	*/
	size_t wb4_cnt;

	/*
	** Most recently processed WB4 character.
	*/
	wb_info_t wb4_last;

	/*
	** Each character received by unicode_wb_next is forwarded to
	** this handler.
	*/
	int (*next_handler)(unicode_wb_info_t, wb_info_t);

	/*
	** unicode_wb_end() calls this. If we were in a middle of a
	** multi-char rule, this wraps things up.
	*/
	int (*end_handler)(unicode_wb_info_t);
};

/* Forward declarations */

static int sot(unicode_wb_info_t i, wb_info_t cl);
static int wb1and2_done(unicode_wb_info_t i, wb_info_t cl);

static int seen_wb67_handler(unicode_wb_info_t i, wb_info_t cl);
static int seen_wb67_end_handler(unicode_wb_info_t i);
static int wb67_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl);

static int seen_wb7bc_handler(unicode_wb_info_t i, wb_info_t cl);
static int seen_wb7bc_end_handler(unicode_wb_info_t i);
static int wb7bc_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl);

static int seen_wb1112_handler(unicode_wb_info_t i, wb_info_t cl);
static int seen_wb1112_end_handler(unicode_wb_info_t i);
static int wb1112_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl);

static int seen_wb1516_handler(unicode_wb_info_t i, wb_info_t cl);
static int wb1516_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl);

unicode_wb_info_t unicode_wb_init(int (*cb_func)(int, void *),
				  void *cb_arg)
{
	unicode_wb_info_t i=calloc(1, sizeof(struct unicode_wb_info));

	if (!i)
		return NULL;

	i->next_handler=sot;
	i->cb_func=cb_func;
	i->cb_arg=cb_arg;
	i->wb4_cnt=0;
	return i;
}

int unicode_wb_end(unicode_wb_info_t i)
{
	int rc=0;

	if (i->end_handler)
		rc=(*i->end_handler)(i);

	free(i);
	return rc;
}

int unicode_wb_next_cnt(unicode_wb_info_t i,
			const char32_t *chars,
			size_t cnt)
{
	int rc;

	while (cnt)
	{
		rc=unicode_wb_next(i, *chars++);
		--cnt;
		if (rc)
			return rc;
	}
	return 0;
}

int unicode_wb_next(unicode_wb_info_t i, char32_t ch)
{
	wb_info_t info;

	info.ch=ch;

	info.cl=unicode_tab_lookup(ch,
				   unicode_starting_indextab,
				   unicode_starting_pagetab,
				   sizeof(unicode_starting_indextab)/
				   sizeof(unicode_starting_indextab[0]),
				   unicode_rangetab,
				   sizeof(unicode_rangetab)/
				   sizeof(unicode_rangetab[0]),
				   unicode_classtab,
				   UNICODE_WB_OTHER);

	return (*i->next_handler)(i, info);
}

#if 0

static int result(unicode_wb_info_t i, int flag)
{
	return (*i->cb_func)(flag, i->cb_arg);
}
#else
#define result(i,flag) ( (*(i)->cb_func)( (flag), (i)->cb_arg))
#endif

/*
** Check for WB3C
*/

#define WB3C_APPLIES(prevclass,uclass)			\
	((prevclass).cl == UNICODE_WB_ZWJ &&		\
	 unicode_emoji_extended_pictographic((uclass).ch))

/*
** Finished WB4 processing. Emit the equivalent number of non-break
** indications.
*/
static int wb4(unicode_wb_info_t i)
{
	int rc=0;

	while (i->wb4_cnt > 0)
	{
		--i->wb4_cnt;

		if (rc == 0)
			rc=result(i, 0);
	}
	return rc;
}

#define SET_HANDLER(next,end) (i->next_handler=next, i->end_handler=end)

static int sot(unicode_wb_info_t i, wb_info_t cl)
{
	i->prevclass=cl;
	SET_HANDLER(wb1and2_done, NULL);

	return result(i, 1);	/* WB1 */
}

static int wb4_handled(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl);

static int wb1and2_done(unicode_wb_info_t i, wb_info_t cl)
{
	wb_info_t prevclass=i->prevclass;

	i->prevclass=cl;

	if (prevclass.cl == UNICODE_WB_CR && cl.cl == UNICODE_WB_LF)
		return result(i, 0); /* WB3 */

	switch (prevclass.cl) {
	case UNICODE_WB_CR:
	case UNICODE_WB_LF:
	case UNICODE_WB_Newline:
		return result(i, 1); /* WB3a */
	}

	switch (cl.cl) {
	case UNICODE_WB_CR:
	case UNICODE_WB_LF:
	case UNICODE_WB_Newline:
		return result(i, 1); /* WB3b */
	}

	if (WB3C_APPLIES(prevclass, cl))
		return result(i, 0); /* WB3c */

	if (prevclass.cl == UNICODE_WB_WSegSpace &&
	    cl.cl == UNICODE_WB_WSegSpace)
		return result(i, 0); /* WB3d */

	return wb4_handled(i, prevclass, cl);
}

/*
** Macros, as defined in the TR
*/
#define AHLetter(c) (c.cl == UNICODE_WB_ALetter || \
		     c.cl == UNICODE_WB_Hebrew_Letter)
#define MidNumLetQ(c) (c.cl == UNICODE_WB_MidNumLet || \
		       c.cl == UNICODE_WB_Single_Quote)

/*
** Whether the character is applicable to the WB4 rule.
*/

#define WB4(C)	((C).cl == UNICODE_WB_Extend || (C).cl == UNICODE_WB_Format ||\
		 (C).cl == UNICODE_WB_ZWJ)

/*
** Check if the current character invokes the WB4 rule, if so return s0,
** doing nothing, here, after performing some record keeping.
*/

#define WB4_APPLY(i,cl)				\
	do {					\
		if (WB4(cl))			\
		{				\
			++(i)->wb4_cnt;		\
			(i)->wb4_last=(cl);	\
			return 0;		\
		}				\
	} while (0)

/*
** After processing WB4, check if the last WB4-processed character
** will invoke WB3C for the next character.
**
** This is invoked after WB4_APPLY. The return value must be stored in an
** int.
**
** This must be followed by WB4_END. Then, after WB4_END, if this returned
** non 0, WB3C applies, returning a non-break indication.
*/

#define WB3C_APPLIES_AFTER_WB4(i,cl)			\
	( (i)->wb4_cnt > 0 &&				\
	  WB3C_APPLIES( (i)->wb4_last, (cl)))

/*
** Wrapper for invoking wb4() after detecting that it no longer applies. This
** gets invoked:
**
** - After WB4_APPLY
**
** - After WB3C_APPLIES_AFTER_WB4
*/

#define WB4_END(i)				\
	do {					\
						\
		int rc=wb4(i);			\
						\
		if (rc)				\
			return rc;		\
	} while (0)


static int resume_wb4(unicode_wb_info_t i, wb_info_t cl)
{
	if (!WB4(cl))
	{
		SET_HANDLER(wb1and2_done, NULL);

		if (WB3C_APPLIES(i->wb4_last, cl))
		{
			i->prevclass=cl;
			return result(i, 0);
		}

		wb_info_t prevclass=i->prevclass;

		i->prevclass=cl;

		return wb4_handled(i, prevclass, cl);
	}
	i->wb4_last=cl;
	return result(i, 0);
}


static int wb4_handled(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl)
{
	if (WB4(cl))
	{
		i->prevclass=prevclass;
		i->wb4_last=cl;
		SET_HANDLER(resume_wb4, 0);
		return result(i, 0); /* WB4 */
	}

	if (AHLetter(prevclass) && AHLetter(cl))
	{
		return result(i, 0); /* WB5 */
	}

	if (AHLetter(prevclass) &&
	    (MidNumLetQ(cl) || cl.cl == UNICODE_WB_MidLetter))
	{
		i->prev2class=prevclass;
		SET_HANDLER(seen_wb67_handler, seen_wb67_end_handler);
		return 0;
	}

	return wb67_done(i, prevclass, cl);
}

/*
** AHLetter (MidLetter | MidNumLetQ) seen, is this followed by AHLetter?
*/

static int seen_wb67_handler(unicode_wb_info_t i, wb_info_t cl)
{
	int rc;

	WB4_APPLY(i, cl);

	SET_HANDLER(wb1and2_done, NULL);

	if (AHLetter(cl))
	{
		i->prevclass=cl;

		rc=result(i, 0);  /* WB6 */

		WB4_END(i);
		if (rc == 0)
			rc=result(i, 0); /* WB7 */

		return rc;
	}

	int wb3c_applies=WB3C_APPLIES_AFTER_WB4(i, cl);

	rc=seen_wb67_end_handler(i);

	if (wb3c_applies)
		return result(i, 0);

	if (rc == 0)
		rc=(*i->next_handler)(i, cl);

	return rc;
}

/*
** AHLetter (MidLetter | MidNumLetQ) seen, with the second
** character's status not returned yet, and now either sot, or something
** else.
*/

static int seen_wb67_end_handler(unicode_wb_info_t i)
{
	int rc=wb67_done(i, i->prev2class, i->prevclass);

	if (rc)
		return rc;
	WB4_END(i);
	return 0;
}

static int wb67_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl)
{
	if (prevclass.cl == UNICODE_WB_Hebrew_Letter &&
	    cl.cl == UNICODE_WB_Single_Quote)
		return result(i, 0); /* WB7a */

	if (prevclass.cl == UNICODE_WB_Hebrew_Letter &&
	    cl.cl == UNICODE_WB_Double_Quote)
	{
		i->prev2class=prevclass;
		SET_HANDLER(seen_wb7bc_handler, seen_wb7bc_end_handler);
		return 0;
	}

	return wb7bc_done(i, prevclass, cl);
}

/*
** Hebrew_Letter Double_Quote       ?
**
**               prevclass          cl
**
** Seen Hebrew_Letter Double_Quote, with the second character's status
** not returned yet.
*/

static int seen_wb7bc_handler(unicode_wb_info_t i, wb_info_t cl)
{
	int rc;

	WB4_APPLY(i, cl);

	SET_HANDLER(wb1and2_done, NULL);

	if (cl.cl == UNICODE_WB_Hebrew_Letter)
	{
		i->prevclass=cl;

		rc=result(i, 0);  /* WB7b */

		WB4_END(i);

		if (rc == 0)
			rc=result(i, 0); /* WB7c */

		return rc;
	}

	int wb3c_applies=WB3C_APPLIES_AFTER_WB4(i, cl);

	rc=seen_wb7bc_end_handler(i);

	if (wb3c_applies)
		return result(i, 0);
	if (rc == 0)
		rc=(*i->next_handler)(i, cl);

	return rc;
}

/*
** Seen Hebrew_Letter Double_Quote, with the second
** character's status not returned yet, and now sot or something else.
*/

static int seen_wb7bc_end_handler(unicode_wb_info_t i)
{
	int rc=wb7bc_done(i, i->prev2class, i->prevclass);

	if (rc)
		return rc;

	WB4_END(i);

	return 0;
}

static int wb7bc_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl)
{
	if (prevclass.cl == UNICODE_WB_Numeric && cl.cl == UNICODE_WB_Numeric)
		return result(i, 0); /* WB8 */

	if (AHLetter(prevclass) && cl.cl == UNICODE_WB_Numeric)
		return result(i, 0); /* WB9 */

	if (prevclass.cl == UNICODE_WB_Numeric && AHLetter(cl))
		return result(i, 0); /* WB10 */

	if (prevclass.cl == UNICODE_WB_Numeric &&
	    (cl.cl == UNICODE_WB_MidNum || MidNumLetQ(cl)))
	{
		i->prev2class=prevclass;
		SET_HANDLER(seen_wb1112_handler, seen_wb1112_end_handler);
		return 0;
	}

	return wb1112_done(i, prevclass, cl);
}

/*
**              Numeric     (MidNum | MidNumLet )     ?
**
**                               prevclass            cl
**
** Seen Numeric (MidNum | MidNumLet), with the second character's status
** not returned yet.
*/

static int seen_wb1112_handler(unicode_wb_info_t i, wb_info_t cl)
{
	int rc;

	WB4_APPLY(i, cl);

	SET_HANDLER(wb1and2_done, NULL);

	if (cl.cl == UNICODE_WB_Numeric)
	{
		i->prevclass=cl;

		rc=result(i, 0);  /* WB12 */

		WB4_END(i);

		if (rc == 0)
			rc=result(i, 0); /* WB11 */

		return rc;
	}

	int wb3c_applies=WB3C_APPLIES_AFTER_WB4(i, cl);

	rc=seen_wb1112_end_handler(i);

	if (wb3c_applies)
		return result(i, 0);

	if (rc == 0)
		rc=(*i->next_handler)(i, cl);

	return rc;
}

/*
** Seen Numeric (MidNum | MidNumLet), with the second character's status
** not returned yet, and now sot.
*/

static int seen_wb1112_end_handler(unicode_wb_info_t i)
{
	int rc=wb1112_done(i, i->prev2class, i->prevclass);

	if (rc)
		return rc;

	WB4_END(i);

	return 0;
}

static int wb1112_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl)
{
	if (prevclass.cl == UNICODE_WB_Katakana &&
	    cl.cl == UNICODE_WB_Katakana)
		return result(i, 0); /* WB13 */

	switch (prevclass.cl) {
	case UNICODE_WB_ALetter:
	case UNICODE_WB_Hebrew_Letter:
	case UNICODE_WB_Numeric:
	case UNICODE_WB_Katakana:
	case UNICODE_WB_ExtendNumLet:
		if (cl.cl == UNICODE_WB_ExtendNumLet)
			return result(i, 0); /* WB13a */
	}

	if (prevclass.cl == UNICODE_WB_ExtendNumLet)
		switch (cl.cl) {
		case UNICODE_WB_ALetter:
		case UNICODE_WB_Hebrew_Letter:
		case UNICODE_WB_Numeric:
		case UNICODE_WB_Katakana:
			return result(i, 0); /* WB13b */
		}

	if (prevclass.cl == UNICODE_WB_Regional_Indicator &&
	    cl.cl == UNICODE_WB_Regional_Indicator)
	{
		SET_HANDLER(seen_wb1516_handler, 0);
		return result(i, 0);
	}

	return wb1516_done(i, prevclass, cl);
}

static int seen_wb1516_handler(unicode_wb_info_t i, wb_info_t cl)
{
	WB4_APPLY(i, cl);

	SET_HANDLER(wb1and2_done, NULL);

	int wb3c_applies=WB3C_APPLIES_AFTER_WB4(i, cl);

	WB4_END(i);

	if (wb3c_applies)
		return result(i, 0);

	if (cl.cl == UNICODE_WB_Regional_Indicator)
	{
		wb_info_t prevclass=i->prevclass;

		i->prevclass=cl;

		return wb1516_done(i, prevclass, cl);
	}

	return (*i->next_handler)(i, cl);
}

static int wb1516_done(unicode_wb_info_t i, wb_info_t prevclass, wb_info_t cl)
{
	return result(i, 1); /* WB999 */
}

/* --------------------------------------------------------------------- */

struct unicode_wbscan_info {
	unicode_wb_info_t wb_handle;

	int found;
	size_t cnt;
};

static int unicode_wbscan_callback(int, void *);

unicode_wbscan_info_t unicode_wbscan_init()
{
	unicode_wbscan_info_t i=calloc(1, sizeof(struct unicode_wbscan_info));

	if (!i)
		return NULL;

	if ((i->wb_handle=unicode_wb_init(unicode_wbscan_callback, i)) == NULL)
	{
		free(i);
		return NULL;
	}

	return i;
}

int unicode_wbscan_next(unicode_wbscan_info_t i, char32_t ch)
{
	if (!i->found)
		unicode_wb_next(i->wb_handle, ch);

	return i->found;
}

size_t unicode_wbscan_end(unicode_wbscan_info_t i)
{
	size_t n;

	unicode_wb_end(i->wb_handle);

	n=i->cnt;
	free(i);
	return n;
}

static int unicode_wbscan_callback(int flag, void *arg)
{
	unicode_wbscan_info_t i=(unicode_wbscan_info_t)arg;

	if (flag && i->cnt > 0)
		i->found=1;

	if (!i->found)
		++i->cnt;
	return 0;
}
