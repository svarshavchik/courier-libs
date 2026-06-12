/*
** Copyright 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include "rfc2045_config.h"
#include	"rfc3676parser.h"
#include	<stdlib.h>
#include	<string.h>

#define NONFLOWED_WRAP_REDUCE	74

#define NONFLOWED_THRESHOLD_EXCEEDED	30

#define EMIT_LINE_BEGIN() do {			\
		(this->*line_begin_handler)();	\
	} while (0)

#define EMIT_LINE_CONTENTS(uc, cnt) do {			\
		(this->*line_content_handler)(uc, cnt);	\
	} while (0)

#define EMIT_LINE_END() do {			\
		(this->*line_end_handler)();	\
	} while (0)

static int parse_unicode(const char *, size_t, void *);

rfc3676_parser_info::rfc3676_parser_info(
	const char *charset,
	bool isflowed,
	bool isdelsp,
	int (*line_begin)(size_t, void *),
	int (*line_contents)(const char32_t *, size_t, void *),
	int (*line_flowed_notify)(void *),
	int (*line_end)(void *),
	void *arg
) : charset{charset},
    isflowed{isflowed},
    isdelsp{isdelsp},
    line_begin{line_begin},
    line_contents{line_contents},
    line_flowed_notify{line_flowed_notify},
    line_end{line_end},
    arg{arg}
{
}

/*
** The top layer initializes the conversion to unicode.
*/

rfc3676_parser_struct::rfc3676_parser_struct(const rfc3676_parser_info &info)
	: info{info}
{
	if ((uhandle=unicode_convert_init(info.charset,
						unicode_u_ucs4_native,
						parse_unicode,
						this)) == NULL)
	{
		unknown_charset=true;

		if ((uhandle=unicode_convert_init(
			     unicode::iso_8859_1,
			     unicode_u_ucs4_native,
			     parse_unicode,
			     this
		     )) == NULL)
		{
			return;
		}
	}

	if (!info.isflowed)
		this->info.isdelsp=false; /* Sanity check */

	line_handler=&rfc3676_parser_struct::scan_crlf;
	content_handler=&rfc3676_parser_struct::start_of_line;
	has_previous_quote_level=false;
	previous_quote_level=0;

	line_begin_handler=&rfc3676_parser_struct::emit_line_begin;
	line_content_handler=&rfc3676_parser_struct::emit_line_contents;
	line_end_handler=&rfc3676_parser_struct::emit_line_end;

	unicode_buf_init(&nonflowed_line, (size_t)-1);
	unicode_buf_init(&nonflowed_next_word, (size_t)-1);

	if (!info.isflowed)
	{
		line_begin_handler=&rfc3676_parser_struct::nonflowed_line_begin;
		line_content_handler=&rfc3676_parser_struct::nonflowed_line_contents;
		line_end_handler=&rfc3676_parser_struct::nonflowed_line_end_default;
	}
}

rfc3676_parser_struct::~rfc3676_parser_struct()
{
	int rc=0;
	end(&rc);
}

int rfc3676parser(rfc3676_parser_t handle,
		  const char *txt,
		  size_t txt_cnt)
{
	if (handle->errflag)
		return handle->errflag; /* Error occurred previously */

	/* Convert to unicode and invoke parse_unicode() */

	return unicode_convert(handle->uhandle, txt, txt_cnt);
}

/*
** Convert char stream from iconv into char32_ts, then pass them to the
** current handler, until all converted char32_ts are consumed.
*/

static int parse_unicode(const char *ucs4, size_t nbytes, void *arg)
{
	rfc3676_parser_t handle=(rfc3676_parser_t)arg;
	char32_t ucs4buf[128];
	const char32_t *p;

	/* Keep going until there's an error, or everything is consumed. */

	while (handle->errflag == 0 && nbytes)
	{
		/* Do it in pieces, using the temporary char32_t buffer */

		size_t cnt=nbytes;

		if (cnt > sizeof(ucs4buf))
			cnt=sizeof(ucs4buf);

		memcpy(ucs4buf, ucs4, cnt);

		ucs4 += cnt;
		nbytes -= cnt;

		cnt /= sizeof(char32_t);
		p=ucs4buf;

		/* Keep feeding it to the current handler */

		rfc3676parser_unicode(handle, p, cnt);
	}

	return handle->errflag;
}

int rfc3676parser_unicode(rfc3676_parser_t handle,
			  const char32_t *p,
			  size_t cnt)
{
	while (handle->errflag == 0 && cnt)
	{
		size_t n=(handle->*(handle->line_handler))(p, cnt);

		if (handle->errflag == 0)
		{
			cnt -= n;
			p += n;
		}
	}

	return handle->errflag;
}

void rfc3676_parser_struct::end(int *errptr)
{
	if (!uhandle)
		return;

	/* Finish unicode conversion */

	int rc=unicode_convert_deinit(uhandle, errptr);

	if (rc == 0)
		rc=errflag;

	if (rc == 0)
	{
		(this->*line_handler)(NULL, 0);
		rc=errflag;
	}

	if (lb)
	{
		int rc2=unicode_lbc_end(lb);

		if (rc2 && rc == 0)
			rc=rc2;
	}

	unicode_buf_deinit(&nonflowed_line);
	unicode_buf_deinit(&nonflowed_next_word);

	uhandle=nullptr;
	*errptr=rc;
}

/*
** Look for a CR that might precede an LF.
*/

size_t rfc3676_parser_struct::scan_crlf(const char32_t *ptr, size_t cnt)
{
	size_t i;

	if (ptr == NULL)
	{
		if (errflag == 0)
			(this->*content_handler)(NULL, 0);
		return 0;
	}

	for (i=0; ptr && i<cnt; ++i)
	{
		if (ptr[i] == '\r')
			break;
	}

	if (i)
	{
		size_t consumed=0;

		while (i && errflag == 0)
		{
			size_t n=(this->*content_handler)(ptr, i);

			ptr += n;
			consumed += n;
			i -= n;
		}
		return consumed;
	}

	/* Consume the first character, the CR */

	line_handler=&rfc3676_parser_struct::scan_crlf_seen_cr;
	return 1;
}

/*
** Check the first character after a CR.
*/

size_t rfc3676_parser_struct::scan_crlf_seen_cr(const char32_t *ptr, size_t cnt)
{
	char32_t cr='\r';

	line_handler=&rfc3676_parser_struct::scan_crlf;

	if (ptr == NULL || *ptr != '\n')
	{
		/*
		** CR was not followed by a NL.
		** Restore it in the char stream.
		*/

		while (errflag == 0)
			if ((this->*content_handler)(&cr, 1))
				break;
	}

	return scan_crlf(ptr, cnt);
}

/*
** From this point on, CRLF are collapsed into NLs, so don't need to worry
** about them.
*/


/*
** Check for an EOF indication at the start of the line.
*/

size_t rfc3676_parser_struct::start_of_line(const char32_t *ptr, size_t cnt)
{
	if (ptr == NULL)
	{
		if (has_previous_quote_level)
			EMIT_LINE_END(); /* Last line was flowed */

		return cnt; /* EOF */
	}

	/* Begin counting the quote level */

	content_handler=&rfc3676_parser_struct::count_quote_level;
	quote_level=0;
	return count_quote_level(ptr, cnt);
}

/*
** Count leading > in flowed content.
*/

size_t rfc3676_parser_struct::count_quote_level(const char32_t *ptr, size_t cnt)
{
	size_t i;

	if (ptr == NULL) /* EOF, pretend that the quote level was counted */
	{
		content_handler=&rfc3676_parser_struct::counted_quote_level;
		return counted_quote_level(ptr, cnt);
	}

	for (i=0; i<cnt; ++i)
	{
		if (ptr[i] != '>' || !info.isflowed)
		{
			content_handler=&rfc3676_parser_struct::counted_quote_level;

			if (i == 0)
				return counted_quote_level(ptr, cnt);
			break;
		}
		++quote_level;
	}

	return i;
}

/*
** This line's quote level has now been counted.
*/

size_t rfc3676_parser_struct::counted_quote_level(const char32_t *ptr, size_t cnt)
{
	was_previous_quote_level=0;

	/*
	** If the previous line was flowed and this line has the same
	** quote level, make the flow official.
	*/

	if (has_previous_quote_level &&
	    quote_level == previous_quote_level)
	{
		/* Remember that this line was flowed into */
		was_previous_quote_level=1;
	}
	else
	{
		/*
		** If the previous line was flowed, but this line carries
		** a different quote level, force-terminate the previous
		** line, before beginning this line.
		*/
		if (has_previous_quote_level)
			EMIT_LINE_END();

		EMIT_LINE_BEGIN();
	}

	has_previous_quote_level=0;
	/* Assume this line won't be flowed, until shown otherwise */


	if (!info.isflowed)
	{
		/*
		** No space-stuffing, or sig block checking, if this is not
		** flowed content.
		*/
		content_handler=&rfc3676_parser_struct::scan_content_line;
		return scan_content_line(ptr, cnt);
	}


	content_handler=&rfc3676_parser_struct::start_content_line;

	if (ptr != NULL && *ptr == ' ')
		return 1; /* Remove stuffed space */

	return start_content_line(ptr, cnt);
}

/*
** Minor deviation from RFC3676, but this fixes a lot of broken text.
**
** If the previous line was flowed, but this is an empty line (optionally
** space-stuffed), unflow the last line (make it fixed), and this becomes
** a fixed line too. Example:
**
** this is the last end of a paragraph[SPACE]
** [SPACE]
** This is the first line of the next paragraph.
**
** Strict RFC3676 rules will parse this as a flowed line, then a fixed line,
** resulting in no paragraph breaks.
*/

size_t rfc3676_parser_struct::start_content_line(const char32_t *ptr, size_t cnt)
{
	/*
	** We'll start scanning for the signature block, as soon as
	** this check is done.
	*/
	content_handler=&rfc3676_parser_struct::check_signature_block;
	sig_block_index=0;

	if (ptr && *ptr == '\n' && was_previous_quote_level)
	{
		EMIT_LINE_END();
		EMIT_LINE_BEGIN();
		was_previous_quote_level=0;
	}

	return check_signature_block(ptr, cnt);
}


static const char32_t sig_block[]={'-', '-', ' '};

/* Checking for a magical sig block */

size_t rfc3676_parser_struct::check_signature_block(const char32_t *ptr, size_t cnt)
{
	if (ptr && *ptr == sig_block[sig_block_index])
	{
		if (++sig_block_index == sizeof(sig_block)
		    /sizeof(sig_block[0]))

			/* Well, it's there, but does a NL follow? */
			content_handler=&rfc3676_parser_struct::seen_sig_block;
		return 1;
	}

	return seen_notsig_block(ptr, cnt);
}

size_t rfc3676_parser_struct::seen_sig_block(const char32_t *ptr, size_t cnt)
{
	if (ptr == NULL || *ptr == '\n')
	{
		/*
		** If the previous line was flowed, the sig block is not
		** considered to be flowable-into content, so terminate
		** the previous line before emitting the sig block.
		*/

		if (was_previous_quote_level)
		{
			EMIT_LINE_END();
			EMIT_LINE_BEGIN();
			was_previous_quote_level=0;
		}

		/* Pass through the sig block */

		content_handler=&rfc3676_parser_struct::start_of_line;

		EMIT_LINE_CONTENTS(sig_block,
				   sizeof(sig_block)/sizeof(sig_block[0]));
		EMIT_LINE_END();
		return ptr ? 1:0;
	}

	return seen_notsig_block(ptr, cnt);
}

/* This is not a sig block line */

size_t rfc3676_parser_struct::seen_notsig_block(const char32_t *newptr, size_t newcnt)
{
	const char32_t *ptr;
	size_t i;

	if (was_previous_quote_level)
		emit_line_flowed_wrap();

	content_handler=&rfc3676_parser_struct::scan_content_line;

	ptr=sig_block;
	i=sig_block_index;

	while (i && errflag == 0)
	{
		size_t n=(this->*content_handler)(ptr, i);

		ptr += n;
		i -= n;
	}

	return (this->*content_handler)(newptr, newcnt);
}

/*
** Pass through the line, until encountering an NL, or a space in flowable
** content.
*/

size_t rfc3676_parser_struct::scan_content_line(const char32_t *ptr, size_t cnt)
{
	size_t i;

	for (i=0; ptr && i<cnt && ptr[i] != '\n' &&
	     (ptr[i] != ' ' || !info.isflowed); ++i)
	{
		;
	}

	/* Pass through anything before the NL or potentially flowable SP */

 	if (i)
		EMIT_LINE_CONTENTS(ptr, i);

	if (i)
		return i;

	if (ptr && ptr[i] == ' ')
	{
		content_handler=&rfc3676_parser_struct::seen_content_sp;
		return 1;
	}

	/* NL. This line does not flow */
	EMIT_LINE_END();

	content_handler=&rfc3676_parser_struct::start_of_line;

	return ptr ? 1:0;
}

size_t rfc3676_parser_struct::seen_content_sp(const char32_t *ptr, size_t cnt)
{
	char32_t sp=' ';

	content_handler=&rfc3676_parser_struct::scan_content_line;

	if (ptr == NULL || *ptr != '\n')
	{
		/*
		** SP was not followed by the NL. Pass through the space,
		** then resume scanning.
		*/
		EMIT_LINE_CONTENTS(&sp, 1);
		return (this->*content_handler)(ptr, cnt);
	}

	/* NL after a SP -- flowed line */

	if (!info.isdelsp)
		EMIT_LINE_CONTENTS(&sp, 1);

	has_previous_quote_level=1;
	previous_quote_level=quote_level;
	content_handler=&rfc3676_parser_struct::start_of_line;
	return ptr ? 1:0;
}

/**************************************************************************/

/*
** At this point, the processing has reduced to the following API:
**
** + begin logical line
**
** + contents of the logical line (multiple consecutive invocations)
**
** + the logical line has flowed onto the next physical line
**
** + end of logical line
**
** The third one, logical line flowed, is normally used for flowed text,
** by definition. But, it may also be get used if non-flowed text gets
** rewrapped when broken formatting is detected.
**
** Provide default implementations of the other three API calls that
** simply invoke the corresponding user callback.
*/

void rfc3676_parser_struct::emit_line_begin()
{
	if (errflag == 0)
		errflag=(*info.line_begin)(quote_level, info.arg);
}

void rfc3676_parser_struct::emit_line_flowed_wrap()
{
	if (errflag == 0 && info.line_flowed_notify)
		errflag=(*info.line_flowed_notify)(info.arg);
}

void rfc3676_parser_struct::emit_line_contents(const char32_t *uc, size_t cnt)
{
	if (errflag == 0 && cnt > 0)
		errflag=(*info.line_contents)(uc, cnt, info.arg);
}

void rfc3676_parser_struct::emit_line_end()
{
	if (errflag == 0)
		errflag=(*info.line_end)(info.arg);
}

/*
** When processing a non-flowed text, handle broken mail formatters (I'm
** looking at you, Apple Mail) that spew out quoted-printable content with
** each decoded line forming a single paragraph. This is heuristically
** detected by looking for lines that exceed a wrapping threshold, then
** rewrapping them.
**
** Redefine the three line API calls to launder the logical line via
** the linebreak API.
*/

/*
** A non-flowed line begins. Initialize the linebreaking module.
*/
void rfc3676_parser_struct::nonflowed_line_begin()
{
	if (lb)
	{
		/* Just in case */

		int rc=unicode_lbc_end(lb);

		if (rc && errflag == 0)
			errflag=rc;
	}

	if ((lb=unicode_lbc_init(
		&rfc3676_parser_struct::nonflowed_line_process_trampoline,
		this)) == NULL)
	{
		if (errflag == 0)
			errflag=-1;
	}

	if (lb)
		unicode_lbc_set_opts(lb,
				     UNICODE_LB_OPT_PRBREAK);

	unicode_buf_clear(&nonflowed_line);
	unicode_buf_clear(&nonflowed_next_word);

	nonflowed_line_width=0;
	nonflowed_next_word_width=0;

	nonflowed_line_process=&rfc3676_parser_struct::initial_nonflowed_line;
	nonflowed_line_end=&rfc3676_parser_struct::initial_nonflowed_end;
	emit_line_begin(); /* Fallthru - user callback */

	nonflowed_line_target_width=
		quote_level < NONFLOWED_WRAP_REDUCE - 20 ?
		NONFLOWED_WRAP_REDUCE - quote_level:20;
}

/*
** Process contents of non-flowed lines. The contents are submitted to the
** linebreaking API.
*/

void rfc3676_parser_struct::nonflowed_line_contents(
	const char32_t *uc,
	size_t cnt
)
{
	if (!lb)
		return;

	while (cnt)
	{
		if (errflag == 0)
			errflag=unicode_lbc_next(lb, *uc);

		++uc;
		--cnt;
	}
}

/*
** End of non-flowed content. Terminate the linebreaking API, then invoke
** the current end-of-line handler.
*/
void rfc3676_parser_struct::nonflowed_line_end_default()
{
	if (lb)
	{
		int rc=unicode_lbc_end(lb);

		if (rc && errflag == 0)
			errflag=rc;

		lb=NULL;
	}

	(this->*nonflowed_line_end)();
	emit_line_end(); /* FALLTHRU */
}

/*
** Callback from the linebreaking API, gives us the next unicode character
** and its linebreak property. Look up the unicode character's width, then
** invoke the current handler.
*/
int rfc3676_parser_struct::nonflowed_line_process_trampoline(
	int linebreak_opportunity,
	char32_t ch,
	void *dummy
)
{
	rfc3676_parser_t handle=(rfc3676_parser_t)dummy;

	(handle->*(handle->nonflowed_line_process))(
		linebreak_opportunity,
		ch,
		unicode_wcwidth(ch)
	);

	return 0;
}

/*
** Collecting initial nonflowed line.
*/

void rfc3676_parser_struct::initial_nonflowed_line(
	int linebreak_opportunity,
	char32_t ch,
	size_t ch_width
)
{
	/*
	** Collect words into nonflowed_line as long as it fits within the
	** targeted width.
	*/
	if (linebreak_opportunity != UNICODE_LB_NONE &&
	    nonflowed_line_width +
	    nonflowed_next_word_width <= nonflowed_line_target_width)
	{
		unicode_buf_append_buf(&nonflowed_line,
				       &nonflowed_next_word);
		nonflowed_line_width +=
			nonflowed_next_word_width;

		unicode_buf_clear(&nonflowed_next_word);
		nonflowed_next_word_width=0;
	}

	/*
	** Add the character to the growing word.
	**
	** If the line's size now exceeds the target width by quite a bit,
	** we've had enough!
	*/

	unicode_buf_append(&nonflowed_next_word, &ch, 1);
	nonflowed_next_word_width += ch_width;

	if (nonflowed_line_width + nonflowed_next_word_width
	    > nonflowed_line_target_width
	    + NONFLOWED_THRESHOLD_EXCEEDED)
		begin_forced_rewrap();
}

/*
** End of line handler. The line did not reach its threshold, so output it.
*/
void rfc3676_parser_struct::initial_nonflowed_end()
{
	emit_line_contents(
			   unicode_buf_ptr(&nonflowed_line),
			   unicode_buf_len(&nonflowed_line)
			);

	emit_line_contents(
			   unicode_buf_ptr(&nonflowed_next_word),
			   unicode_buf_len(&nonflowed_next_word)
			);
}

/*
** Check for the abnormal situation where we're ready to wrap something but
** nonflowed_line is empty because all this text did not have a linebreaking
** opportunity.
*/

void rfc3676_parser_struct::check_abnormal_line()
{
	size_t n, i;
	const char32_t *p;

	if (unicode_buf_len(&nonflowed_line) > 0)
		return;

	/* Extreme times call for extreme measures */

	n=unicode_buf_len(&nonflowed_next_word);
	p=unicode_buf_ptr(&nonflowed_next_word);

	for (i=n; i>0; --i)
	{
		if (i < n && unicode_grapheme_break(p[i-1], p[i]))
		{
			n=i;
			break;
		}
	}

	unicode_buf_append(&nonflowed_line, p, n);
	unicode_buf_remove(&nonflowed_next_word, 0, n);

	/*
	** Recalculate the width of the growing word, now.
	*/

	nonflowed_next_word_width=0;
	p=unicode_buf_ptr(&nonflowed_next_word);

	for (i=0; i<unicode_buf_len(&nonflowed_next_word); ++i)
		nonflowed_next_word_width +=
			unicode_wcwidth(p[i]);
}

/*
** We've decided that the line is too long, so begin rewrapping it.
*/

/*
** Emit nonflowed_line as the rewrapped line. Clear the buffer.
*/
void rfc3676_parser_struct::emit_rewrapped_line()
{
	check_abnormal_line();
	emit_line_contents(
			unicode_buf_ptr(&nonflowed_line),
			unicode_buf_len(&nonflowed_line)
		);

	emit_line_flowed_wrap();

	/* nonflowed_line is now empty */
	unicode_buf_clear(&nonflowed_line);
	nonflowed_line_width=0;
}

void rfc3676_parser_struct::begin_forced_rewrap()
{
	nonflowed_line_process=&rfc3676_parser_struct::forced_rewrap_line;
	nonflowed_line_end=&rfc3676_parser_struct::forced_rewrap_end;
	emit_rewrapped_line();
}

void rfc3676_parser_struct::forced_rewrap_line(
			       int linebreak_opportunity,
			       char32_t ch,
			       size_t ch_width)
{
	if (linebreak_opportunity != UNICODE_LB_NONE)
	{
		/* Found a linebreaking opportunity */

		if (nonflowed_line_width
		    + nonflowed_next_word_width
		    > nonflowed_line_target_width)
		{
			/* Accumulated word is too long */
			emit_rewrapped_line();
		}

		unicode_buf_append_buf(&nonflowed_line,
				       &nonflowed_next_word);

		nonflowed_line_width +=
			nonflowed_next_word_width;
		unicode_buf_clear(&nonflowed_next_word);
		nonflowed_next_word_width=0;
	}

	/*
	** Check for another excessively long line.
	*/

	if (nonflowed_line_width == 0 &&
	    nonflowed_next_word_width + ch_width
	    > nonflowed_line_target_width)
	{
		emit_rewrapped_line();
	}

	unicode_buf_append(&nonflowed_next_word, &ch, 1);
	nonflowed_next_word_width += ch_width;
}

void rfc3676_parser_struct::forced_rewrap_end()
{
	initial_nonflowed_end(); /* Same logic, for now */
}
