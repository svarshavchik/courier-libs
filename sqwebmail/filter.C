#include "config.h"

/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"filter.h"
#include	"sqwebmail.h"
#include	<string.h>

int filter_info::converted_text(const char *buf,
				size_t size,
				void *arg)
{
	struct filter_info *info=(struct filter_info *)arg;

	(*info->handler_func)(buf, size, info->func_arg);
	return 0;
}

filter_info::filter_info(const char *charset,
			 void (*handler)(const char *, size_t, void *),
			 void *arg)
	: handle{unicode_convert_init(unicode_u_ucs4_native,
				      charset,
				      converted_text, this)},
	  handler_func{handler},
	  func_arg{arg},
	  linesize{MYLINESIZE + 10}
{
	if (handle == NULL)
		conversion_error=true;
}

filter_info::~filter_info()
{
	flush();
}

void filter_info::operator()(const char32_t *ptr, size_t cnt)
{
	if (!lb_handle)
	{
		lb_handle=unicode_lb_init(lb_callback, this);
		pending_text.clear();
		pending_lb_cnt=0;
	}

	pending_text.insert(pending_text.end(), ptr, ptr+cnt);
	unicode_lb_next_cnt(lb_handle, ptr, cnt);
}

int filter_info::lb_callback(int unicode_lb_value, void *voidarg)
{
	auto info=(filter_info *)voidarg;

	if (unicode_lb_value != UNICODE_LB_NONE)
		info->flush_word();

	++info->pending_lb_cnt;

	return 0;
}

void filter_info::flush_word()
{
	auto word_size=pending_lb_cnt;

	pending_lb_cnt=0;
	if (word_size > pending_text.size())
		abort();

	do_flush_word(word_size);
	pending_text.erase(pending_text.begin(),
			   pending_text.begin()+word_size);
}

void filter_info::do_flush_word(size_t word_size)
{
	if (word_size > pending_text.size())
		abort();

	if (!handle)
		return;

	size_t l=0;
	size_t j=0;

	for (size_t i=0; i<word_size; ++i)
	{
		auto this_width=unicode_wcwidth(pending_text[i]);
		l += this_width;

		if (l > linesize)
		{
			if (unicode_grapheme_break(prevchar, pending_text[i]))
			{
				do_flush_word_chunk(j, i);

				static const char32_t br[]=
					{'<','b','r','/','>'};

				unicode_convert_uc(handle, br, std::size(br));
				j=i;
				l=this_width;
			}
		}
		prevchar=pending_text[i];
	}
	do_flush_word_chunk(j, word_size);
}

void filter_info::do_flush_word_chunk(size_t first_offset,
				      size_t last_offset)
{
	for (size_t i=first_offset; i<last_offset; ++i)
	{
		const char *p;

		switch (pending_text[i]) {
		case '<':
			p="&lt;";
			break;
		case '>':
			p="&gt;";
			break;
		case '&':
			p="&amp;";
			break;
		case '"':
			p="&quot;";
			break;
		default:
			continue;
		}

		if (i > first_offset)
			unicode_convert_uc(handle,
					   pending_text.data()+first_offset,
					   i-first_offset);

		while (*p)
		{
			char32_t uc= *p++;

			unicode_convert_uc(handle, &uc, 1);
		}
		first_offset=i+1;
	}

	if (first_offset < last_offset)
		unicode_convert_uc(handle,
				   pending_text.data()+first_offset,
				   last_offset-first_offset);
}

void filter_info::passthru(const char32_t *ptr, size_t cnt)
{
	if (lb_handle)
	{
		unicode_lb_end(lb_handle);
		lb_handle=nullptr;
	}

	if (pending_lb_cnt != pending_text.size())
		abort();
	if (pending_text.size())
		flush_word();

	if (cnt)
		unicode_convert_uc(handle, ptr, cnt);
}

void filter_info::flush()
{
	passthru(nullptr, 0);

	if (handle)
	{
		int conversion_error=0;

		unicode_convert_deinit(handle,
				       &conversion_error);
		if (conversion_error)
			this->conversion_error=true;
		handle=NULL;
	}
}
