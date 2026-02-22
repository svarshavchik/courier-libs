#include "config.h"
#include "addressbook.h"
#include	<string_view>
#include	<string>
#include	<iostream>
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"

static std::string result;

extern "C" {
	void maildir_writemsg(int, const char *ptr, size_t n)
	{
		result.append(ptr, ptr+n);
	}

	void maildir_writemsgstr(int, const char *ptr)
	{
		std::string_view ptr_s{ptr};

		maildir_writemsg(0, ptr_s.data(), ptr_s.size());
	}
}

int newdraftfd;
extern void create_addrheader(std::string_view header,
			      std::string_view value);

const char *ab_find(const char *ptr)
{
	std::string_view ptr_s{ptr};

	if (ptr_s == "courier")
	{
		return "\"courier-announce mailing list\" <courier-announce@lists.sourceforge.net>, "
			"\"courier-users mailing list\" <courier-users@lists.sourceforge.net>";
	}
	if (ptr_s == "courier-imap")
	{
		return "\"courier-announce mailing list\" <courier-announce@lists.sourceforge.net>, "
			"\"courier-imap mailing list\" <courier-imap@lists.sourceforge.net>";
	}

	if (ptr_s == "encodedname" || ptr_s == "испытание")
		return "испытание <test@испытание.com>";

	if (ptr_s == "encodedname2")
		return "\"Test A. Name\" <test2@example.com>";

	if (ptr_s == "encodedname3")
		return "\"Test испы.тание Name\" <test3@example.com>";

	return 0;
}

// #define UPDATE_TESTS 1

static void testaddressbook()
{
	static const struct {
		const char *header;
		const char *encoded;
		const char *display;
	} tests[]={

		// Test 1
		{
			"encodedname,"
				" encodedname2,"
			" short@example.com,"
			" encodedname3",
			"To: =?utf-8?B?0LjRgdC/0YvRgtCw0L3QuNC1?= <test@xn--80akhbyknj4f.com>,\n"
			"  \"Test A. Name\" <test2@example.com>, short@example.com,\n"
			"  Test =?utf-8?B?0LjRgdC/0Ysu0YLQsNC90LjQtQ==?= Name <test3@example.com>\n"
			"",
			"To:\n"
			"испытание <test@испытание.com>\n"
			"\"Test A. Name\" <test2@example.com>\n"
			"short@example.com\n"
			"\"Test испы.тание Name\" <test3@example.com>\n"
		},

		// Test 2
		{
			"courier,"
			" \"webmail\""
			" <courier-webmail@lists.sourceforge.net>,"
			" courier-imap",
			"To: \"courier-announce mailing list\" <courier-announce@lists.sourceforge.net>,\n"
			"  \"courier-users mailing list\" <courier-users@lists.sourceforge.net>,\n"
			"  \"webmail\" <courier-webmail@lists.sourceforge.net>,\n"
			"  \"courier-imap mailing list\" <courier-imap@lists.sourceforge.net>\n"
			"",
			"To:\n"
			"\"courier-announce mailing list\" <courier-announce@lists.sourceforge.net>\n"
			"\"courier-users mailing list\" <courier-users@lists.sourceforge.net>\n"
			"\"webmail\" <courier-webmail@lists.sourceforge.net>\n"
			"\"courier-imap mailing list\" <courier-imap@lists.sourceforge.net>\n"
		},
	};

	size_t n=0;

	for (const auto &t:tests)
	{
		result.clear();
		++n;
		create_addrheader("To: ", t.header);

		rfc822::tokens toks{result};
		rfc822::addresses a{toks};

#if UPDATE_TESTS
		std::cout << "\n\t\t// Test " << n
			<< "\n\t\t{\n\t\t\t\"";

		for (auto &c:std::string_view{t.header})
		{
			if (c == '\\' || c == '"')
				std::cout << '\\';
			if (c == ' ')
				std::cout << "\"\n\t\t\t\"";
			std::cout << c;
		}
		std::cout << "\",\n\t\t\t\"";
		for (auto c:result)
		{
			if (c == '\\' || c == '"')
				std::cout << '\\';
			if (c == '\n')
				std::cout << "\\n\"\n\t\t\t\"";
			else
				std::cout << c;
		}
		std::cout << "\",";
#else
		if (result != t.encoded)
		{
			std::cerr << "Test " << n << " failed. "
				" Expected:\n"
				  << t.encoded
				  << "\nActual:\n"
				  << result << "\n";
			exit(1);
		}

		std::string display;

#endif

		for (auto &addr:a)
		{
			std::string s;

			addr.display(unicode::utf_8, std::back_inserter(s));

#if UPDATE_TESTS
			std::cout << "\n\t\t\t\"";

			for (auto c:s)
			{
				if (c == '\\' || c == '"')
					std::cout << '\\';
				std::cout << c;
			}

			std::cout << "\\n\"";
#else
			display += s;
			display += "\n";
#endif
		}

#if UPDATE_TESTS
		std::cout << "\n\t\t},\n";
#else
		if (display != t.display)
		{
			std::cerr << "Test " << n << " failed. "
				" Expected:\n"
				  << t.display
				  << "\nActual:\n"
				  << display << "\n";
			exit(1);
		}
#endif
	}
};

int main(int argc, char **argv)
{
	testaddressbook();
	return 0;
}
