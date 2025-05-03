/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<iostream>
#include	<iterator>
#include	<string_view>
#include	<sstream>

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

void rfc2047decode_test()
{
	struct teststr {
		std::string_view input;
		const char *expected_output;
	} tests[]={
		{"", ""},
		{"=", "[utf-8::=]"},
		{"==", "[utf-8::==]"},
		{"==?iso-8859-1*en_US?q?quoted=20printable?=", "[utf-8::=][iso-8859-1:en_US:quoted printable]"},
		{"=?iso-8859-1*?q?qp1?==", "[iso-8859-1::qp1][utf-8::=]"},
		{"=?iso-8859-1?q?qp2?===?iso-8859-1*en?q?qp3?===", "[iso-8859-1::qp2][utf-8::=][iso-8859-1:en:qp3][utf-8::==]"},
		{"=?iso-8859-1?q?a?= =?iso-8859-1?q?b?=", "[iso-8859-1::a][iso-8859-1::b]"},
		{"=?iso-8859-1?q?q=6A?=   =?iso-8859-1*en?q?=6a?=", "[iso-8859-1::qj][iso-8859-1:en:j]"},
		{"=?iso-8859-1?b?MTIz?=", "[iso-8859-1::123]"},
		{"=?iso-8859-1?b?MTI=?=", "[iso-8859-1::12]"},
		{"=?iso-8859-1?b?MQ==?=", "[iso-8859-1::1]"},
		{"=?iso-8859-1?b?M===?=", "[iso-8859-1::6]"},
		{"=?", ":error"},
		{"=?iso-8859-1?x12", "[utf-8::12]:error"},
		{"=?iso-8859-1?q", ":error"},
		{"=?iso-8859-1?qr", "[utf-8::r]:error"},
		{"=?iso-8859-1?q?r?", "[iso-8859-1::r]:error"},
		{"=?iso-8859-1?q?r?st", "[iso-8859-1::r?st]:error"},
		{"=?iso-8859-1?q?=", "[iso-8859-1::]:error"},
		{"=?iso-8859-1?q?=a", "[iso-8859-1::]:error"},
		{"=?iso-8859-1?q?=2", "[iso-8859-1::]:error"},
		{"=?iso-8859-1?q?=2x", "[iso-8859-1::][utf-8::x]:error"},
		{"=?iso-8859-1?b?MTIz?", "[iso-8859-1::123]:error"},
		{"=?iso-8859-1?b?MTI=?", "[iso-8859-1::12]:error"},
		{"=?iso-8859-1?b?MQ==?", "[iso-8859-1::1]:error"},
		{"=?iso-8859-1?b?M===?", "[iso-8859-1::6]:error"},
		{"=?iso-8859-1?b?MTIz", "[iso-8859-1::123]:error"},
		{"=?iso-8859-1?b?MTI=", "[iso-8859-1::12]:error"},
		{"=?iso-8859-1?b?MQ==", "[iso-8859-1::1]:error"},
		{"=?iso-8859-1?b?M===", "[iso-8859-1::6]:error"},
	};

	for (auto &t:tests)
	{
		std::ostringstream o;

		bool errflag=false;

		rfc2047::decode(
			t.input.begin(),
			t.input.end(),
			[&]
			(auto &charset,
			 auto &language, auto cb)
			{
				o << "[" << charset
				  << ":" << language
				  << ":";

				std::ostreambuf_iterator<char> iter{o};
				cb(iter);
				o << "]";
			},
			[&]
			(std::string_view::iterator &b,
			 std::string_view::iterator &e,
			 std::string_view error)
			{
				errflag=true;
			});
		if (errflag)
			o << ":error";

		// std::cout << "{\"" << t.input << "\", \""
		//	  << o.str() << "\"},\n";

		if (o.str() != t.expected_output)
		{
			std::cout << "rfc2047_decode error: "
				  << t.input
				  << "\n    expected: " << t.expected_output
				  << "\n    result:   " << o.str()
				  << "\n";
		}
	}
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

	rfc2047decode_test();
}
