#include "config.h"

/*
** Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"filter.h"
#include	"buf.h"
#include	"sqwebmail.h"
#include	<string.h>

static int converted_text(const char *buf,
			  size_t size,
			  void *arg)
{
	struct filter_info *info=(struct filter_info *)arg;

	(*info->handler_func)(buf, size, info->func_arg);
	return 0;
}

void	filter_start(struct filter_info *info,
		     const char *charset,
		     void (*handler)(const char *, size_t, void *),
		     void *arg)
{
	info->conversion_error=0;
	info->handle=unicode_convert_init(unicode_u_ucs4_native,
					    charset,
					    converted_text, info);

	if (info->handle == NULL)
		info->conversion_error=1;

	info->handler_func=handler;
	info->func_arg=arg;
	info->u_w=0;

	info->linesize=MYLINESIZE + 10;
	info->prevchar='\n';
}

void	filter(struct filter_info *info,
	       const char32_t *ptr, size_t cnt)
{
	size_t i, prev;

	if (!info->handle)
		return;

	i=prev=0;

	while (1)
	{
		const char *p=0;
		char32_t prevchar=info->prevchar;

		if (i < cnt)
		{
			info->prevchar=ptr[i];

			if (ptr[i] == '\n')
			{
				info->u_w=0;
			}
			else
			{
				/* Force-break excessively long lines */

				size_t uc_w=unicode_wcwidth(ptr[i]);

				if (uc_w + info->u_w > info->linesize &&
				    unicode_grapheme_break(prevchar, ptr[i]))
				{
					char32_t nl='\n';

					if (prev < i)
						unicode_convert_uc
							(info->handle,
							 ptr+prev, i-prev);
					prev=i;

					unicode_convert_uc(info->handle,
							     &nl, 1);
					info->u_w=0;
				}

				info->u_w += uc_w;
			}

			switch (ptr[i]) {
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
				++i;
				continue;
			}
		}

		if (prev < i)
			unicode_convert_uc(info->handle, ptr+prev, i-prev);
		prev=++i;

		if (!p)
			break;

		while (*p)
		{
			char32_t uc= *p++;

			unicode_convert_uc(info->handle, &uc, 1);
		}
	}
}

void	filter_passthru(struct filter_info *info,
			const char32_t *ptr, size_t cnt)
{
	unicode_convert_uc(info->handle, ptr, cnt);
}

void	filter_end(struct filter_info *info)
{
	if (info->handle)
	{
		unicode_convert_deinit(info->handle,
					 &info->conversion_error);
		info->handle=NULL;
	}
}
