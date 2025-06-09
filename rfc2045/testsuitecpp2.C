#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>

#if 0
#define UPDATE_TESTSUITECPP 1
#endif

std::string events;

#define RFC2045_ENTITY_PARSER_TEST(msg)		\
	do { events += msg "\n"; } while (0)

#define RFC2045_ENTITY_PARSER_DECL(d) d


#include "testsuitecpp.H"
#include "rfc2045.h"
#include "rfc2045cpp.C"

template<bool crlf>
rfc2045::entity runtest(std::string message,
			size_t chunksize)
{
	events.clear();

	rfc2045::entity_parser<crlf> parser;

	auto b=message.begin(), e=message.end();

	while (b != e)
	{
		if (static_cast<size_t>(e-b) < chunksize)
			chunksize=e-b;

		auto p=b;
		b += chunksize;
		parser.parse(p, b);
	}

	return parser.parsed_entity();
}

template<bool crlf>
void test1()
{
	const char *nl = crlf ? "\r\n":"\n";
	auto entity=runtest<crlf>(
		std::string{
			"Mime-Version: 1.0"
		}
		+ nl
		+ "Content-Type: text"
		+ nl
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl
		+ std::string(40, '=')
		+ nl, 100);

#if UPDATE_TESTSUITECPP
	parsed_mime_info::dump_entity(entity, "");

	std::cout << events;
#else
	using cte=rfc2045::cte;

	static const parsed_mime_info expected_results{
		0    , // startpos
		38+crlf*3   , // endpos
		38+crlf*3   , // startbody
		325+crlf*10  , // endbody
		3    , // nlines
		7    , // nbodylines
		1    , // mime1
		0, "text", "iso-8859-1",
		"", cte::sevenbit,
		0, // has8bitheader
		0, // has8bitbody
		0, // has8bitcontentchar
		0  // haslongquotedline
	};

	if (expected_results != entity)
	{
		std::cout << "test1 parsing error ("
			  << (crlf ? "\\r\\n":"\\n")
			  << "):\n";
		parsed_mime_info::dump(entity);
		exit(1);
	}

	if (events !=
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "end_of_parse set\n"
	    "end_of_parse received\n"
	    "thread finished\n")
	{
		std::cout << "test1 unexpected event sequence:\n"
			  << events;
		exit(1);
	}
#endif
}

template<bool crlf>
void test2()
{
	const char *nl = crlf ? "\r\n":"\n";
	auto entity=runtest<crlf>(
		std::string{
			"Mime-Version: 1.0"
		}
		+ nl
		+ "Content-Type: text"
		+ nl
		+ "Content-Transfer-Encoding: something"
		+ nl
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl
		+ std::string(40, 'A')
		+ nl, 100);

#if UPDATE_TESTSUITECPP
	parsed_mime_info::dump_entity(entity, "");

	std::cout << events;
#else

	if (events !=
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "parser: chunk\n"
	    "retrieved next chunk\n"
	    "end_of_parse set\n"
	    "end_of_parse received\n"
	    "thread finished\n")
	{
		std::cout << "test2 unexpected event sequence:\n"
			  << events;
		exit(1);
	}

	if (!(entity.errors & RFC2045_ERRUNKNOWNTE))
	{
		std::cout << "test2 did not receive expected error\n";
		exit(1);
	}
#endif
}

int main()
{
	alarm(60);
#if UPDATE_TESTSUITECPP
	// test1<false>();
	// test1<true>();

	// test2<false>();
#else
	for (int i=0; i<100; ++i)
	{
		test1<false>();
		test1<true>();
	}

	test2<false>();
	test2<true>();
#endif
	return 0;
}
