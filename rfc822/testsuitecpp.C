/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<iostream>
#include	<iterator>

auto tokenize(const char *p)
{
	std::cout << "Tokenize: " << p << "\n";

	rfc822::tokens tp(p, [](size_t){});

	for (auto &c:tp)
	{
		if (c.type == '\0' || c.type == '"' || c.type == '(')
		{
			std::cout << (c.type == '"' ? "Quote":
				      c.type == '(' ? "Comment":"Atom"
			) << ": " << c.str << "\n";
		}
		else
		{
			char ch=c.type;
			std::cout << "Token: "
				  << (ch ? std::string_view{&ch, 1} :
				      std::string_view{"atom"})
				  << "\n";
		}
	}
	return (tp);
}

rfc822::addresses doaddr(rfc822::tokens &t)
{
	auto addresses=rfc822::addresses{t};

	std::cout << "----\n";

	addresses.print(std::ostreambuf_iterator<char>(std::cout));
	std::cout << "\n";

	return addresses;
}

int main()
{
	tokenize("(Break 1");
	tokenize("(Break 2\\");
	tokenize("(Break 3\\))");
	tokenize("(Break 4())");
	tokenize("\"Quote 1");
	tokenize("\"Quote 2\\");
	tokenize("\"Quote 3\\\"");
	tokenize("=?Atom 1()");
	tokenize("=?Atom 2?");
	tokenize("=?Atom 3?=");
	tokenize("<>");

	auto t1=tokenize(
		"nobody@example.com (Nobody (is) here\\) right)"
	), t2=tokenize(
		"Distribution  list: nobody@example.com daemon@example.com"
	), t3=tokenize(
		"Mr Nobody <nobody@example.com>, Mr. Nobody <nobody@example.com>"
	), t4=tokenize(
		"nobody@example.com, <nobody@example.com>, Mr. Nobody <nobody@example.com>"
	), t5=tokenize("=?UTF-8?Q?Test?= <nobody@example.com>, foo=bar <nobody@example.com>"
	), t6=tokenize("\"Quoted \\\\ \\\" String\" <nobody@example.com>,"
		       "\"Trailing slash \\\\\" <nobody@example.com>"
	);

	auto a1=doaddr(t1),
		a2=doaddr(t2),
		a3=doaddr(t3),
		a4=doaddr(t4),
		a5=doaddr(t5),
		a6=doaddr(t6);

	std::cout << "[" << a4.wrap(70) << "]\n"
		  << "[" << a4.wrap(160) << "]\n"
		  << "[" << a4.wrap(10) << "]\n";
	#define FIVEUTF8 "\xe2\x85\xa4"

#define FIVETIMES4 FIVEUTF8 FIVEUTF8 FIVEUTF8 FIVEUTF8

#define FIVETIMES16 FIVETIMES4 FIVETIMES4 FIVETIMES4 FIVETIMES4

#define FIVEMAX FIVETIMES16 FIVETIMES4 FIVETIMES4

	std::cout << rfc2047::encode(FIVEMAX, "utf-8",
				     rfc2047_qp_allow_any).first << "\n";

	std::cout << rfc2047::encode(FIVEMAX FIVEUTF8, "utf-8",
				     rfc2047_qp_allow_any).first << "\n";

	std::cout << rfc2047::encode(FIVEMAX "\xcc\x80", "utf-8",
				     rfc2047_qp_allow_any).first << "\n";
}
