/*
** Copyright 2001-2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"rfc822.h"
#include	"rfc2047.h"
#include	"rfc822hdr.h"
#include	<string_view>

#define lc(x) ((x) >= 'A' && (x) <= 'Z' ? (x) + ('a'-'A'):(x))

namespace {

	bool headercmp(std::string_view a, std::string_view b)
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
}

bool rfc822::header_is_addr(std::string_view header_name)
{
	return RFC822HDR_IS_ADDR(headercmp, header_name);
}
