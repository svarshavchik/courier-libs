/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<string>
#include	<string_view>
#include	<iostream>

int main()
{
	std::string_view testaddresses=
		"Test Address 1 <testaddress1@example.com>,\n"
		"Test испытание <testaddress2@example.com>,\n"
		"\"Test испытание\" <testaddress2@example.com>,\n"
		"test3@испытание.net (NobÒdy)\n";

	rfc822::tokens t{testaddresses};
	rfc822::addresses a{t};

	std::vector<std::string> wrapped;

	auto res=a.encode_wrapped("utf-8", 160, std::back_inserter(wrapped));

	static_assert(std::is_same_v<decltype(res), decltype(
			      std::back_inserter(wrapped)
		      )>);

	if (wrapped != std::vector<std::string>{
			"Test Address 1 <testaddress1@example.com>,"
				" Test =?utf-8?B?0LjRgdC/0YvRgtCw0L3QuNC1?="
				" <testaddress2@example.com>,",

				"\"Test =?utf-8?B?0LjRgdC/0YvRgtCw0L3QuNC1?=\""
				" <testaddress2@example.com>, "
				"test3@xn--80akhbyknj4f.net"
				" (=?utf-8?Q?Nob=C3=92dy?=)"
		})
	{
		for (auto &addr:wrapped)
			std::cout << addr << "\n";
	}
	return 0;
}
