/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<iostream>
#include	<iterator>
#include	<string_view>
#include	<type_traits>
#include	<utility>
#include	<sstream>
#include	<unistd.h>

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

static void printaddress_test()
{
	rfc822::tokens t{
		std::string_view{
			"Nobody1 <test1@example.com>, test2@example.com, "
			"=?iso-8859-1?q?No?= =?iso-8859-1?q?b=D2dy?= <test3@xn--80akhbyknj4f.net>"
		}
	};

	rfc822::addresses a{t};

#if 1
	static const struct {
		const char *str;
		const char32_t *ustr;
		const char *str2;
		const char *str3;
		const char32_t *ustr2;
	} results[]={
		{
			"Nobody1/"
			"test1@example.com/"
			"Nobody1/"
			"test1@example.com",

			U"Nobody1/"
			U"test1@example.com",

			"Nobody1 <test1@example.com>",
			"Nobody1 <test1@example.com>",
			U"Nobody1 <test1@example.com>"
		},
		{
			"/"
			"test2@example.com/"
			"/"
			"test2@example.com",

			U"/"
			U"test2@example.com",

			"test2@example.com",
			"test2@example.com",
			U"test2@example.com"
		},
		{
			"=?iso-8859-1?q?No?= =?iso-8859-1?q?b=D2dy?=/"
			"test3@xn--80akhbyknj4f.net/"
			"NobÒdy/"
			"test3@испытание.net",

			U"NobÒdy/"
			U"test3@испытание.net",

			"=?iso-8859-1?q?No?= =?iso-8859-1?q?b=D2dy?= <test3@xn--80akhbyknj4f.net>",
			"NobÒdy <test3@испытание.net>",
			U"NobÒdy <test3@испытание.net>"
		},
	};

	auto resb=std::begin(results);
	auto rese=std::end(results);

#define CHECK_TEST_RESULTS 1
#else
#define CHECK_TEST_RESULTS 0
#endif
	for (auto &i:a)
	{
		std::string names, addresss;
		auto nameiter=std::back_insert_iterator(names);

		std::u32string nameu, addressu;
		auto nameuiter=std::back_insert_iterator(nameu);

		i.name.print(nameiter);

		nameiter=i.address.print(std::back_insert_iterator(addresss));

		std::string named, addressd;

		auto namediter=std::back_insert_iterator(named);
		i.display_name("utf-8", namediter);

		namediter=i.display_address(
			"utf-8",
			std::back_insert_iterator(addressd)
		);


		i.unicode_name(nameuiter);
		nameuiter=i.unicode_address(
			std::back_insert_iterator(addressu)
		);

		std::string email_address, email_address2;
		std::string display_address, display_address2;
		std::u32string email_address_unicode, email_address_unicode2;;

		auto email_address_iter=std::back_inserter(email_address);
		auto display_address_iter=std::back_inserter(display_address);
		auto email_address_unicode_iter=
			std::back_inserter(email_address_unicode);

		i.print(email_address_iter);
		email_address_iter=i.print(std::back_inserter(email_address2));

		i.display("utf-8", display_address_iter);
		display_address_iter=i.display(
			"utf-8", std::back_inserter(display_address2));
		i.unicode(email_address_unicode_iter);
		email_address_unicode_iter=
			i.unicode(std::back_inserter(email_address_unicode2));

#if CHECK_TEST_RESULTS
		auto result=names + "/" + addresss + "/" +
			named + "/" + addressd;

		auto exp=resb != rese ? resb->str:"";

		if (result != exp)
		{
			std::cout << "printaddress_test expected (1): "
				  << exp << "\n";
			std::cout << "printaddress_test actual (1):   "
				  << result << "\n";
		}

		auto uresult=nameu + U"/" + addressu;
		auto uexp=resb != rese ? resb->ustr:U"";

		if (uresult != uexp)
		{
			std::cout << "printaddress_test expected (2): "
				  << unicode::iconvert::fromu::convert(
					  uexp, "utf-8"
				  ).first << "\n";
			std::cout << "printaddress_test actual (2):   "
				  << unicode::iconvert::fromu::convert(
					  uresult, "utf-8"
				  ).first << "\n";
		}

		if (email_address != (resb != rese ? resb->str2:""))
		{
			std::cout << "printaddress_test expected (3): "
				  << (resb != rese ? resb->str2:"") << "\n";
			std::cout << "printaddress_test actual (3):   "
				  << email_address << "\n";
		}

		if (display_address != (resb != rese ? resb->str3:""))
		{
			std::cout << "printaddress_test expected (4): "
				  << (resb != rese ? resb->str3:"") << "\n";
			std::cout << "printaddress_test actual (4):   "
				  << display_address << "\n";
		}

		if (email_address_unicode != (resb != rese ? resb->ustr2:U""))
		{
			std::cout << "printaddress_test expected (4): "
				  << unicode::iconvert::fromu::convert(
					  (resb != rese ? resb->ustr2:U""),
					  "utf-8"
				  ).first << "\n";
			std::cout << "printaddress_test actual (4):   "
				  << unicode::iconvert::fromu::convert(
					  email_address_unicode, "utf-8"
				  ).first << "\n";
		}
		if (resb != rese)
			++resb;
#else
		std::cout <<
			"\t\t{\n"
			"\t\t\t\"" << names << "/\"\n"
			"\t\t\t\"" << addresss << "/\"\n"
			"\t\t\t\"" << named << "/\"\n"
			"\t\t\t\"" << addressd << "\",\n\n"
			"\t\t\tU\"" << unicode::iconvert::fromu::convert(
				nameu, "utf-8"
			).first << "/\"\n"
			"\t\t\tU\"" << unicode::iconvert::fromu::convert(
				addressu, "utf-8"
			).first << "\",\n\n"
			"\t\t\t\"" << email_address << "\",\n"
			"\t\t\t\"" << display_address << "\",\n"
			"\t\t\tU\"" << unicode::iconvert::fromu::convert(
				email_address_unicode, "utf-8"
			).first << "\"\n"
			"\t\t},\n";
#endif
	}
#if CHECK_TEST_RESULTS

	if (resb != rese)
	{
		std::cout << "printaddress_test: too few test results\n";
	}
#endif

	t=rfc822::tokens{
		std::string_view{
			"Test =?utf-8?b?0LjRgdC/0YvRgtCw0L3QuNC1?= <test5@xn--80akhbyknj4f.net>"
		}};
	a=rfc822::addresses{t};

	for (auto &i:a)
	{
		std::string name;

		i.display_name("iso-8859-1", std::back_inserter(name));
		name += "\n";
		i.display_address("iso-8859-1", std::back_inserter(name));
		name += "\n";
		i.display("iso-8859-1", std::back_inserter(name));
		name += "\n";

		if (name !=
		    "Test (decoding error)\n"
		    "test5@.net(decoding error)\n"
		    "Test (decoding error) <test5@.net(decoding error)>\n")
		{
			std::cout << "print_address unexpected error test "
				"results:\n" << name;
		}
	}
}

void template_compile_test(std::vector<rfc822::address> &va,
			   std::string &s)
{
	std::ostringstream os;
	std::ostreambuf_iterator<char> o{os};

	va.resize(1);
	static_assert(std::is_same_v<decltype(va[0].name.print(o)), void>,
		      "name.print returns void");
	o=va[0].name.print(std::ostreambuf_iterator<char>(os));

	std::u32string us;
	auto usb=std::back_inserter(us);

	static_assert(std::is_same_v<decltype(va[0].name.unicode_address(usb)),
		      void>, "name.unicode_address returns void");
	usb=va[0].name.unicode_address(std::back_inserter(us));

	static_assert(std::is_same_v<decltype(va[0].name.unicode_name(usb)),
		      void>, "unicode_name returns void");
	usb=va[0].name.unicode_name(std::back_inserter(us));

	static_assert(std::is_same_v<decltype(va[0].name.display_address(
						      "utf-8", o)),
		      void>, "name.display_address returns void");
	o=va[0].name.display_address("utf-8", std::ostreambuf_iterator<char>(os));

	static_assert(std::is_same_v<decltype(va[0].name.display_name(
						      "utf-8", o)),
		      void>, "name.display_name returns void");
	o=va[0].name.display_name("utf-8", std::ostreambuf_iterator<char>(os));

	static_assert(std::is_same_v<decltype(va[0].print(o)), void>,
		      "print returns void");

	o=va[0].print(std::ostreambuf_iterator<char>(os));
	static_assert(std::is_same_v<decltype(va[0].unicode_address(usb)),
		      void>, "unicode_address returns void");
	usb=va[0].unicode_address(std::back_inserter(us));

	static_assert(std::is_same_v<decltype(va[0].unicode_name(usb)),
		      void>, "unicode_name returns void");
	usb=va[0].unicode_name(std::back_inserter(us));

	static_assert(std::is_same_v<decltype(va[0].display_address(
						      "utf-8", o)
		      ), void>, "display_address returns void");
	o=va[0].display_address("utf-8", std::ostreambuf_iterator<char>(os));

	static_assert(std::is_same_v<decltype(va[0].display_name("utf-8", o)),
		      void>, "display_name returns void");
	o=va[0].display_name("utf-8", std::ostreambuf_iterator<char>(os));

	static_assert(
		std::is_same_v<
		decltype(
			rfc822::display_header_unicode(
				"x", "y",
				std::declval<std::back_insert_iterator<
				std::u32string> &>())),
		void>,
		"display_header_unicode returns void");

	static_assert(
		std::is_same_v<
		decltype(
			rfc822::display_header_unicode(
				"x", "y",
				std::declval<std::back_insert_iterator<
				std::u32string> &&>())),
		std::back_insert_iterator<std::u32string>>,
		"display_header_unicode returns iterator");

	static_assert(
		std::is_same_v<
		decltype(
			rfc822::display_header(
				"x", "y",
				std::declval<const std::string &>(),
				std::declval<std::back_insert_iterator<
				std::string> &>())),
		void>,
		"display_header returns void");

	static_assert(
		std::is_same_v<
		decltype(
			rfc822::display_header(
				"x", "y",
				std::declval<const std::string &>(),
				std::declval<std::back_insert_iterator<
				std::string> &&>())),
		std::back_insert_iterator<std::string>>,
		"display_header returns iterator");

}

static void unquote_name_test()
{
	rfc822::tokens t{
		std::string_view{
			"John Doe <john@example.com>,"
			"\"John Q. Public\" <john@example.com>,"
			"\"John \\\"Q.\\\" Public\" <john@example.com>,"
			"john@example.com (John Doe),"
			"john@example.com"
		}};
	rfc822::addresses a{t};

	std::string names;

	for (auto &an:a)
	{
		static_assert(
			std::is_same_v<decltype(
				an.unquote_name(
					std::declval<
					std::back_insert_iterator<std::string>&
					>()
				)), void>, "unquote_name updates iter by ref");
		static_assert(
			std::is_same_v<decltype(
				an.unquote_name(
					std::declval<
					std::back_insert_iterator<std::string>
					>()
				)), std::back_insert_iterator<std::string>>,
			"unquote_name returns iter by value");
		an.unquote_name(std::back_inserter(names));
		names.push_back('\n');
	}

	if (names !=
	    "John Doe\n"
	    "John Q. Public\n"
	    "John \"Q.\" Public\n"
	    "John Doe\n"
	    "john@example.com\n")
	{
		std::cout << "Unexpected result of unquote_name:"
			  << names;
	}
}

int main()
{
	alarm(60);
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
	), t7=tokenize("undisclosed-recipients: ;"
	), t8=tokenize("mailing-list: nobody@example.com, nobody@example.com; "
		       "admins: all@example.com;"
	);

	auto a1=doaddr(t1),
		a2=doaddr(t2),
		a3=doaddr(t3),
		a4=doaddr(t4),
		a5=doaddr(t5),
		a6=doaddr(t6),
		a7=doaddr(t7),
		a8=doaddr(t8);

	std::vector<std::string> lines=a4.wrap(70);

	const char *sep="[";

	for (auto &l:lines)
	{
		std::cout << sep << l;

		sep="\n";
	}

	std::cout << "]\n";

	std::vector<std::u32string> ulines=a4.wrap_unicode(70);
	std::vector<std::string> linesu;

	for (auto &u:ulines)
		linesu.push_back(unicode::iconvert::fromu::convert(
					 u, "utf-8").first);
	if (linesu != lines)
	{
		std::cout << "Unicode version:\n";
		for (auto &l:lines)
			std::cout << l << "\n";
	}

	lines=a4.wrap(160);
	sep="[";

	for (auto &l:lines)
	{
		std::cout << sep << l;

		sep="\n";
	}
	std::cout << "]\n";

	ulines=a4.wrap_unicode(160);
	linesu.clear();

	for (auto &u:ulines)
		linesu.push_back(unicode::iconvert::fromu::convert(
					 u, "utf-8").first);
	if (linesu != lines)
	{
		std::cout << "Unicode version:\n";
		for (auto &l:lines)
			std::cout << l << "\n";
	}

	lines=a4.wrap(16);
	sep="[";

	for (auto &l:lines)
	{
		std::cout << sep << l;

		sep="\n";
	}
	std::cout << "]\n";

	ulines=a4.wrap_unicode(16);
	linesu.clear();

	for (auto &u:ulines)
		linesu.push_back(unicode::iconvert::fromu::convert(
					 u, "utf-8").first);
	if (linesu != lines)
	{
		std::cout << "Unicode version:\n";
		for (auto &l:lines)
			std::cout << l << "\n";
	}

	std::vector<std::string> check2;
	auto check2_push=[&](std::string &&s)
	{
		check2.push_back(std::move(s));
	};

	rfc822::addresses::print_wrapped(
		a4.begin(), a4.end(), 16,
		check2_push);

	if (check2 != lines)
	{
		std::cout << "Unexpected results from print_wrapped() (1)\n";
	}

	check2.clear();
	auto check2_pushb=rfc822::addresses::print_wrapped(
		a4.begin(), a4.end(), 16,
		[&](std::string &&s)
		{
			check2.push_back(std::move(s));
		});
	(void)check2_pushb;

	if (check2 != lines)
	{
		std::cout << "Unexpected results from print_wrapped() (2)\n";
	}

	std::vector<std::u32string> ucheck2;
	auto ucheck2_push=[&](std::u32string &&s)
	{
		ucheck2.push_back(std::move(s));
	};

	rfc822::addresses::unicode_wrapped(
		a4.begin(), a4.end(), 16,
		ucheck2_push);

	if (ucheck2 != ulines)
	{
		std::cout << "Unexpected results from unicode_wrapped() (1)\n";
	}

	ucheck2.clear();
	auto ucheck2_pushb=rfc822::addresses::unicode_wrapped(
		a4.begin(), a4.end(), 16,
		[&](std::u32string &&s)
		{
			ucheck2.push_back(std::move(s));
		});
	(void)ucheck2_pushb;

	if (ucheck2 != ulines)
	{
		std::cout << "Unexpected results from unicode_wrapped() (2)\n";
	}

	if (a4.wrap_display(16, "utf-8") != lines)
	{
		std::cout << "Unexpected result from wrap_display() (1)\n";
	}

	check2.clear();
	rfc822::addresses::wrap_display(a4.begin(), a4.end(), 16, "utf-8",
					check2_push);
	if (check2 != lines)
	{
		std::cout << "Unexpected result from wrap_display() (2)\n";
	}

	check2.clear();

	auto check2_pushb2=
		rfc822::addresses::wrap_display(
			a4.begin(), a4.end(), 16, "utf-8",
			[&](std::string &&s)
			{
				check2.push_back(std::move(s));
			});
	(void)check2_pushb2;
	if (check2 != lines)
	{
		std::cout << "Unexpected result from wrap_display() (3)\n";
	}

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
	printaddress_test();
	unquote_name_test();

	std::vector<rfc822::address> vra;
	std::string ss;

	template_compile_test(vra, ss);

	std::string s;

	auto biter=std::back_inserter(s);
	biter=rfc822::display_header(
		"TO",
		"=?iso-8859-1?q?No?= =?iso-8859-1?q?b=D2dy?="
		" <test3@xn--80akhbyknj4f.net>, Nobody <nobody@example.com>",
		"utf-8",
		std::move(biter));

	std::cout << s << "\n";

	s.clear();

	auto cout_iter=std::ostreambuf_iterator<char>(std::cout);
	rfc822::display_header(
		"SUBJECT",
		"=?iso-8859-1?q?No?= =?iso-8859-1?q?b=D2dy?="
		" <test3@xn--80akhbyknj4f.net>, nobody@example.com",
		"utf-8",
		cout_iter);
	std::cout << "\n";
}
