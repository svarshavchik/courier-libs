#include <string>
#include <vector>
#include <iostream>
#include <sstream>
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
		0  // has8bitcontentchar
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

void test34_setup(std::stringstream &ss, const char *nl)
{
	ss << "Mime-Version: 1.0" << nl
	   << "Content-Type: multipart/mixed; boundary=aaa" << nl
	   << nl
	   << "--aaa" << nl
	   << "Content-Type: text/plain" << nl
	   << "Content-Description: MIME" << nl
	   << " section 1" << nl
	   << nl
	   << "--aaa--" << nl;

	ss.seekg(0);
}

template<bool crlf>
void test3()
{
	const char *nl = crlf ? "\r\n":"\n";

	std::stringstream ss;

	test34_setup(ss, nl);

	auto b=std::istreambuf_iterator<char>{ss};
	auto e=std::istreambuf_iterator<char>{};

	typename rfc2045::entity::line_iter<crlf>::iter iter{b, e};

	rfc2045::entity entity;

	entity.parse(iter);

	if (entity.subentities.size() != 1)
	{
		std::cout << "test2 (" << (crlf ? "\\r\\n":"\\n") << "): "
			" unexpected parsing error.\n";
		exit(1);
	}

	typename rfc2045::entity::line_iter<crlf>::headers h{
		entity.subentities[0],
		*ss.rdbuf()
	};

#if UPDATE_TESTSUITECPP
	bool flag;

	do
	{
		const auto &[n, c]=h.name_content();

		std::cout << "{\n\t\"" << h.current_header() << "\",\n"
			"\t\"" << n << "\", \"" << c << "\", ";

		flag=h.next();

		std::cout << flag << "\n},\n";
	} while (flag);
#else
	static const struct {
		const char *header, *name, *content;
		bool next;
	} default_test[]={
		{
			"content-type: text/plain",
			"content-type", "text/plain", 1
		},
		{
			"content-description: MIME section 1",
			"content-description", "MIME section 1", 1
		},
		{
			"",
			"", "", 0
		},
	};

	for (const auto &t:default_test)
	{
		auto full=h.current_header();
		const auto &[n, c]=h.name_content();

		if (full != t.header || n != t.name || c != t.content)
		{
			std::cout << "test3: unexpected result (default_test)\n"
				"Got: " << full << "\n    " << n
				  << "\n    " << c << "\n"
				"Exp: " << t.header << "\n    " << t.name
				  << "\n    " << t.content << "\n";
			exit(1);
		}

		if (h.next() != t.next)
		{
			std::cout << "test3: unexpected result (default_test)\n"
				  << "Got next=" << !t.next
				  << " for \"" << t.header << "\"\n";
			exit(1);
		}
	}
#endif
}

template<bool crlf>
void test4()
{
	const char *nl = crlf ? "\r\n":"\n";

	std::stringstream ss;

	test34_setup(ss, nl);

	auto b=std::istreambuf_iterator<char>{ss};
	auto e=std::istreambuf_iterator<char>{};

	typename rfc2045::entity::line_iter<crlf>::iter iter{b, e};

	rfc2045::entity entity;

	entity.parse(iter);

	if (entity.subentities.size() != 1)
	{
		std::cout << "test2 (" << (crlf ? "\\r\\n":"\\n") << "): "
			" unexpected parsing error.\n";
		exit(1);
	}

	typename rfc2045::entity::line_iter<crlf>::headers h{
		entity.subentities[0],
		*ss.rdbuf()
	};

	h.name_lc=false;
	h.keep_eol=true;

#if UPDATE_TESTSUITECPP
	bool flag;

	do
	{
		const auto &[n, c]=h.name_content();

		std::cout << "{";

		for (const auto &str
			     : std::array<std::string_view, 3>{
			     h.current_header(), n, c})
		{
			std::cout << "\n\t";

			if (str.empty())
			{
				std::cout << "\"\"";
			}
			else
			{
				bool has_prev=false;
				bool prev_was_nl=true;

				for (char c:str)
				{
					if (c != '\n')
					{
						if (prev_was_nl)
						{
							if (has_prev)
							{
								std::cout <<
									" + ";
							}
							std::cout <<
								"std::string{"
								"\"";
						}
						std::cout << c;
						has_prev=true;
						prev_was_nl=false;
					}
					else
					{
						if (has_prev)
						{
							if (!prev_was_nl)
								std::cout <<
									"\"}";
							std::cout << " + ";
						}
						std::cout << "nl";
						has_prev=true;
						prev_was_nl=true;
					}
				}

				if (!prev_was_nl)
					std::cout << "\"}";
			}
			std::cout << ",";
		}

		flag=h.next();

		std::cout << " " << flag << "\n},\n";
	} while (flag);
#else
	const struct {
		std::string header, name, content;
		bool next;
	} default_test[]={
		{
			std::string{"Content-Type: text/plain"} + nl,
			std::string{"Content-Type"},
			std::string{"text/plain"} + nl, 1
		},
		{
			std::string{"Content-Description: MIME"} + nl + std::string{" section 1"} + nl,
			std::string{"Content-Description"},
			std::string{"MIME"} + nl + std::string{" section 1"} + nl, 1
		},
		{
			nl,
			nl,
			"", 0
		},
	};

	for (const auto &t:default_test)
	{
		auto full=h.current_header();
		const auto &[n, c]=h.name_content();

		if (full != t.header || n != t.name || c != t.content)
		{
			std::cout << "test3: unexpected result (default_test)\n"
				"Got: " << full << "\n    " << n
				  << "\n    " << c << "\n"
				"Exp: " << t.header << "\n    " << t.name
				  << "\n    " << t.content << "\n";
			exit(1);
		}

		if (h.next() != t.next)
		{
			std::cout << "test3: unexpected result (default_test)\n"
				  << "Got next=" << !t.next
				  << " for \"" << t.header << "\"\n";
			exit(1);
		}
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

	// test3<false>();

	test4<false>();
#else
	for (int i=0; i<100; ++i)
	{
		test1<false>();
		test1<true>();
	}

	test2<false>();
	test2<true>();

	test3<false>();
	test3<true>();

	test4<false>();
	test4<true>();
#endif
	return 0;
}
