/*
** Copyright 2003-2025 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/
#include	"encode.h"
#include	<string.h>
#include	<stdlib.h>
#include	<courier-unicode.h>
#include	<functional>

namespace {
#if 0
}
#endif

struct libmail_encode_autodetect {

	bool use7bit{false};
	bool binaryflag{false};

	size_t l{0};
	bool longline{false};

	size_t charcnt{0};
	size_t bit8cnt{0};

	const char *encoding{0};

	void operator()(unsigned char ch)
	{
		++charcnt;

		++l;
		if (ch < 0x20 || ch >= 0x80)
		{
			if (ch != '\t' && ch != '\r' && ch != '\n')
			{
				++bit8cnt;
				l += 2;
			}
		}

		if (ch == 0)
		{
			binaryflag=true;
			encoding="base64";
		}

		if (ch == '\n')	l=0;
		else if (l > 990)
		{
			longline=true;
		}
	}

	operator const char *()
	{
		if (!encoding)
		{
			if (use7bit || longline)
			{
				if (bit8cnt > charcnt / 10)
					encoding="base64";
				else
					encoding="quoted-printable";
			}
			else
			{
				encoding=bit8cnt ? "8bit":"7bit";
			}
		}
		return encoding;
	}
};

#if 0
{
#endif
}

const char *libmail_encode_autodetect_fp(FILE *fp, int use7bit,
					 int *binaryflag)
{
	return libmail_encode_autodetect_fpoff(fp, use7bit, 0, -1,
					       binaryflag);
}

const char *libmail_encode_autodetect_fpoff(FILE *fp, int use7bit,
					    off_t start_pos, off_t end_pos,
					    int *binaryflag)
{
	off_t orig_pos = ftell(fp);
	off_t pos = orig_pos;
	const char *rc;

	if (start_pos >= 0)
	{
		if (fseek(fp, start_pos, SEEK_SET) == (off_t)-1)
			return NULL;
		else
			pos = start_pos;
	}

	libmail_encode_autodetect detect;

	detect.use7bit=use7bit != 0;

	while (!detect.encoding)
	{
		if (end_pos >= 0 && pos > end_pos)
			break;

		++pos;

		auto ch=getc(fp);

		if (ch == EOF)
			break;

		detect(ch);
	}

	rc=detect;

	if (binaryflag)
		*binaryflag=detect.binaryflag ? 1:0;

	if (fseek(fp, orig_pos, SEEK_SET) == (off_t)-1)
		return NULL;
	return rc;
}

const char *libmail_encode_autodetect_buf(const char *str, int use7bit)
{
	libmail_encode_autodetect detect;

	detect.use7bit=use7bit != 0;

	while (*str)
	{
		if (detect.encoding)
			break;

		detect(*str++);
	}

	return detect;
}
