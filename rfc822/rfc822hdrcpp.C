/*
** Copyright 2001-2025 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"rfc822.h"
#include	"rfc2047.h"
#include	<string_view>

#define lc(x) ((x) >= 'A' && (x) <= 'Z' ? (x) + ('a'-'A'):(x))

bool rfc822::headercmp(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;

	auto p=b.begin();

	for (char c:a)
	{
		if (lc(c) !=  lc(*p))
			return false;
		++p;
	}
	return true;
}

bool rfc822::header_is_addr(std::string_view header_name,
			    bool include_in_reply_to)
{
	if (include_in_reply_to && headercmp(header_name, "in-reply-to"))
		return true;

	return headercmp(header_name, "from") ||
		headercmp(header_name, "to") ||
		headercmp(header_name, "cc") ||
		headercmp(header_name, "bcc") ||
		headercmp(header_name, "in-reply-to") ||
		headercmp(header_name, "resent-from") ||
		headercmp(header_name, "resent-to") ||
		headercmp(header_name, "resent-cc") ||
		headercmp(header_name, "resent-bcc");
}
