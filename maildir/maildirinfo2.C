/*
** Copyright 2018-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<ctype.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	"maildirinfo.h"
#include	<courier-unicode.h>
#include	<algorithm>

/*
** Split a string at periods, convert each split range between charsets.
*/
static std::string foldername_filename_convert(const std::string &src_chset,
					       const std::string &dst_chset,
					       std::string_view foldername)
{
	std::string s;

	auto b=foldername.begin(), e=foldername.end();

	while (b != e)
	{
		if (*b == '.')
		{
			s += '.';
			++b;
		}

		auto p=b;

		b=std::find(b, e, '.');

		bool errflag;

		s += unicode::iconvert::convert(std::string{p, b},
						src_chset,
						dst_chset,
						errflag);

		if (errflag)
		{
			s.clear();
			break;
		}
	}
	if (s.empty())
		errno=EILSEQ;
	return s;
}

std::string maildir::imap_foldername_to_filename(bool utf8_format,
						 std::string_view foldername)
{
	if (utf8_format)
		return std::string{foldername};

	return foldername_filename_convert
		(unicode_x_imap_modutf7,
		 "utf-8",
		 foldername);
}

std::string maildir::imap_filename_to_foldername(bool utf8_format,
						 std::string_view filename)
{
	if (utf8_format)
		return std::string{filename};

	return foldername_filename_convert
		("utf-8",
		 unicode_x_imap_modutf7,
		 filename);
}

std::string imap_filename_to_foldername(bool utf8_format,
					const std::string &filename);

extern "C"
char *imap_foldername_to_filename(int utf8_format, const char *foldername)
{
	auto s=maildir::imap_foldername_to_filename(utf8_format, foldername);

	if (s.empty())
		return NULL;

	return strdup(s.c_str());
}

extern "C"
char *imap_filename_to_foldername(int utf8_format, const char *filename)
{
	auto s=maildir::imap_filename_to_foldername(utf8_format, filename);

	if (s.empty())
		return NULL;

	return strdup(s.c_str());
}
