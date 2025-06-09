#include <iostream>
#include <tuple>
#include <vector>
#include <string_view>
#include <algorithm>
#include "rfc2045/rfc2045.h"

template<bool crlf, typename test_types>
void testrfc2045line_iter_testset(int &testnum, test_types &tests)
{
	for (auto &t:tests)
	{
		++testnum;

		std::string_view s{t.teststr};

		auto b=s.begin();
		auto e=s.end();

		typename rfc2045::entity::line_iter<crlf>::iter iter{b, e};

		std::vector<std::tuple<std::string, size_t>> output;

		iter.entered_body();
		while (std::holds_alternative<
		       rfc2045::entity_parse_meta::eof_no>(
			       iter.eof()
		       ))
		{
			const auto &[p, q]=iter.current_line();

			std::string s{p, q};
			bool ignore;

			output.emplace_back(
				s,
				iter.consume_line_and_update_position(
					rfc2045::cte::eightbit,
					ignore
				)
			);

		}

		if (output != std::vector<std::tuple<std::string, size_t>>{
				t.output.begin(), t.output.end()})

		{
			std::cout << "testrfc2045iter test "
				  << testnum
				  << " failed:\n";

			for (auto &[s, n]:output)
			{
				for (char c:s)
				{
					if (c == '\r')
					{
						std::cout << "\\r";
					}
					else if (c == '\n')
					{
						std::cout << "\\n";
					}
					else
					{
						std::cout << c;
					}
				}

				std::cout << ":" << n << "\n";
			}
			exit(1);
		}
	}
}

static void testrfc2045line_iter()
{
	int testnum=0;

	static const struct {
		const char *teststr;
		std::vector<std::tuple<const char *, size_t>> output;

	} testlf[]={
		{
			"line1\n"
			"line2",
			{
				{"line1", 6},
				{"line2", 5}
			}
		},
		{
			"line1\n",
			{
				{"line1", 6},
			}
		},
	}, testcrlf[]={
		{
			"line3\nline4\r\nline5\r\n",
			{
				{ "line3\nline4", 13 },
				{ "line5", 7},
			}
		},
		{
			"line7",
			{
				{ "line7", 5},
			}
		},
		{
			"line8\r",
			{
				{ "line8\r", 6},
			}
		}
	};
	testrfc2045line_iter_testset<false>(testnum, testlf);
	testrfc2045line_iter_testset<true>(testnum, testcrlf);

	std::string toolarge(static_cast<size_t>(2000),
			     static_cast<char>('a'));

	const struct {
		std::string teststr;
		std::vector<std::tuple<std::string, size_t>> output;

	} testlargeheader1[]={
		{
			std::string{"line10\n" + toolarge + "\n"},
			{
				{"line10", 7},
				{toolarge.substr(0, 1024), 2001},
			}
		},
		{
			std::string{"line11\n" + toolarge + "\n"
				    "nextline\n"},
			{
				{"line11", 7},
				{toolarge.substr(0, 1024), 2001},
				{"nextline", 9},
			}
		},
		{
			std::string{"line12\n" + toolarge},
			{
				{"line12", 7},
				{toolarge.substr(0, 1024), 2000},
			}
		},
	}, testlargeheader2[]={
		{
			std::string{"line13\r\n" + toolarge + "\r\n"},
			{
				{"line13", 8},
				{toolarge.substr(0, 1024), 2002},
			}
		},
		{
			std::string{"line14\r\n" + toolarge + "\r\n"
				    "nextline\r\n"},
			{
				{"line14", 8},
				{toolarge.substr(0, 1024), 2002},
				{"nextline", 10},
			}
		},
		{
			std::string{"line15\r\n" + toolarge},
			{
				{"line15", 8},
				{toolarge.substr(0, 1024), 2000},
			}
		},
	};
	testrfc2045line_iter_testset<false>(testnum, testlargeheader1);
	testrfc2045line_iter_testset<true>(testnum, testlargeheader2);
}

void testrfc2045foldedline_iter()
{
	size_t testnum=0;

	static const struct {
		const char *teststr;
		std::vector<std::tuple<const char *, const char *, const char *,
				       size_t, size_t>> results;
	} tests[] = {
		{
			"",
			{
				{
					"",
					"",
					"", 0, 0
				}
			}
		},
		{
			"Header: online\n",
			{
				{
					"header: online",
					"header",
					"online", 15, 1
				},
				{
					"",
					"",
					"", 15, 1
				}
			}
		},
		{
			"header:onelinenonl",
			{
				{
					"header:onelinenonl",
					"header",
					"onelinenonl", 18, 1
				},
				{
					"",
					"",
					"", 18, 1
				}
			}
		},
		{
			"header1:    line\n\n",
			{
				{
					"header1:    line",
					"header1",
					"line", 17, 1
				},
				{
					"",
					"",
					"", 18, 2
				}
			}
		},
		{
			"Header: line1\n\t\t\tline2\nHeader: line3\n\n",
			{
				{
					"header: line1 line2",
					"header",
					"line1 line2", 23, 2
				},
				{
					"header: line3",
					"header",
					"line3", 37, 3
				},
				{
					"",
					"",
					"", 38, 4
				}
			}
		},
		{
			"Header: line4\n line5\n line6\n",
			{
				{
					"header: line4 line5 line6",
					"header",
					"line4 line5 line6", 28, 3
				},
				{
					"",
					"",
					"", 28, 3
				}
			}
		},
		{
			"Header: line7\n line8\n line9",
			{
				{
					"header: line7 line8 line9",
					"header",
					"line7 line8 line9", 27, 3
				},
				{
					"",
					"",
					"", 27, 3
				}
			}
		}
	};

#if 0
#define UPDATE_FOLDEDLINETEST 1
	const char *sep="";
#endif
	for (const auto &t:tests)
	{
		++testnum;

		std::string_view teststr{t.teststr};

		rfc2045::entity entity;

		auto b=teststr.begin();
		auto e=teststr.end();
		rfc2045::entity::line_iter<false>::iter iter{b, e};
		rfc2045::entity_parse_meta::scope scope{iter, &entity};
		std::string s;

#if UPDATE_FOLDEDLINETEST
		std::cout << sep << "{\n\t\"";
		for (char c:std::string_view{t.teststr})
		{
			switch (c) {
			case '\n':
				std::cout << "\\n";
				break;
			case '\r':
				std::cout << "\\r";
				break;
			case '\t':
				std::cout << "\\t";
				break;
			default:
				std::cout << c;
			}
		}

		std::cout << "\",\n\t{";

		sep="\n";
#else
		std::vector<std::tuple<std::string, std::string, std::string,
				       size_t, size_t>> results;
#endif

		do
		{
			const auto &[name, contents] =
				iter.next_folded_header_line(entity, s);
#if UPDATE_FOLDEDLINETEST
			std::cout << sep << "\t\t{\n\t\t\t\""
				  << s
				  << "\",\n\t\t\t\"" << name
				  << "\",\n\t\t\t\"" << contents
				  << "\", " << entity.endpos
				  << ", " << entity.nlines << "\n\t\t}";
			sep=",\n";
#else
			results.emplace_back(s, name, contents,
					     entity.endpos, entity.nlines);
#endif
		} while (!s.empty());

#if UPDATE_FOLDEDLINETEST
		std::cout << "\n\t}\n}";
		sep=",\n";
#else
		if (results.size() != t.results.size()
		    || !std::equal(results.begin(), results.end(),
				   t.results.begin()))
		{
			std::cout << "testrfc2045foldedline_iter test "
				  << testnum << " failed:\n";

			for (auto &[header, name, value, endpos, nlines]
				     :results)
			{
				std::cout << "\"" << header << "\", \""
					  << name << "\", \""
					  << value << "\", "
					  << endpos << ", " << nlines
					  << "\n";
			}
			exit(1);
		}
#endif
	}
#if UPDATE_FOLDEDLINETEST
	std::cout << "\n";
#endif
}

void testrfc2231headers()
{
	static const struct {
		const std::string_view header;
		const char *value;
		std::unordered_map<std::string_view,
				   std::tuple<const char *, const char *,
					      const char *>> parameters;
	} tests[]={
		{
			"TEXT/PLAIN; "
			"CHARSET=\"iso-8859-1\"",
			"text/plain",
			{
				{"charset", {"utf-8", "en", "iso-8859-1"}}
			}
		},
		{
			"text/html;;novalue;quotedvalue=\"quoted\";",
			"text/html",
			{
				{"novalue", {"utf-8", "en", ""}},
				{"quotedvalue", {"utf-8", "en", "quoted"}}
			}
		},
		{
			"text/plain; "
			"nonstandard1==?utf-8?q?nob=c3=92dy?=; "
			"nonstandard2=\"=?utf-8?q?nob=c3=92dy?=\"; "
			"notrailing=semicolon",
			"text/plain",
			{
				{"nonstandard1", {"utf-8", "en", "nobÒdy"}},
				{"nonstandard2", {"utf-8", "en", "nobÒdy"}},
				{"notrailing", {"utf-8", "en", "semicolon"}}
			}
		},
		{
			"text/plain; "
			"bells_and_whistles*0*=UTF-8'EN_US'A%6c%6C%20; "
			"bells_and_whistles*1=the%bells; "
			"bells_and_whistles*2*=%20and%20more%2E;",
			"text/plain",
			{
				{"bells_and_whistles", {"utf-8", "en_us", "All the%bells and more."}}
			}
		}
	};

#if 1

	size_t testnum=0;
#else
	const char *sep="";

#define UPDATE_RFC2231TEST 1
#endif

	for (auto &t:tests)
	{
		rfc2045::entity::rfc2231_header header{t.header};

		std::map<std::string,
			 rfc2045::entity::header_parameter_value
			 > sorted_parameters{
			header.parameters.begin(),
			header.parameters.end()
		};

#if UPDATE_RFC2231TEST

		std::cout << sep << "{\n\t\"";

		for (char c:t.header)
		{
			if (c == '"' || c == '\\')
				std::cout << "\\";
			std::cout << c;

			if (c == ' ')
			{
				std::cout << "\"\n\t\"";
			}
		}

		std::cout << "\",\n\t\"" << header.value << "\",\n"
			"\t{";

		sep="\n";

		for (auto &[paramname, paramvalue] : sorted_parameters)
		{
			std::cout << sep << "\t\t{\""
				  << paramname << "\", {\""
				  << paramvalue.charset << "\", \""
				  << paramvalue.language << "\", \""
				  << paramvalue.value << "\"}}";
			sep=",\n";
		}
		std::cout << "\n\t}\n}";
		sep=",\n";
#else
		std::map<std::string,
			 rfc2045::entity::header_parameter_value
			 > expected_sorted_parameters;

		for (auto &[name, value] : t.parameters)
		{
			auto &[charset, language, contents] = value;

			expected_sorted_parameters.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(name),
				std::forward_as_tuple(charset,
						      language,
						      contents));
		}

		++testnum;
		if (header.value != t.value ||
		    sorted_parameters != expected_sorted_parameters)
		{
			std::cout << "rfc2231 test " << testnum << " failed:\n";
			std::cout << header.value << "\n";

			for (auto &[paramname, paramvalue] : sorted_parameters)
			{
				std::cout << paramname << ": "
					  << paramvalue.charset << ", "
					  << paramvalue.language << ": "
					  << paramvalue.value << "\n";
			}
			exit(1);
		}
#endif
	}

#if UPDATE_RFC2231TEST
	std::cout << "\n";
#endif
}

#if 0
#define UPDATE_TESTSUITECPP 1
#endif

#include "testsuitecpp.H"

void testmimeparse()
{
	using cte=rfc2045::cte;

	static const struct {
		const char *message;

		parsed_mime_info parsed;
	} tests1[]={

		// Test 1
		{
			"",
			0    , // startpos
			0    , // endpos
			0    , // startbody
			0    , // endbody
			0    , // nlines
			0    , // nbodylines
			0    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 2
		{
			"\n",
			0    , // startpos
			1    , // endpos
			1    , // startbody
			1    , // endbody
			1    , // nlines
			0    , // nbodylines
			0    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 3
		{
			"Subject: something\n"
			"\n",
			0    , // startpos
			20   , // endpos
			20   , // startbody
			20   , // endbody
			2    , // nlines
			0    , // nbodylines
			0    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 4
		{
			"Subject: something\n",
			0    , // startpos
			19   , // endpos
			19   , // startbody
			19   , // endbody
			1    , // nlines
			0    , // nbodylines
			0    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 5
		{
			"Subject: something",
			0    , // startpos
			18   , // endpos
			18   , // startbody
			18   , // endbody
			1    , // nlines
			0    , // nbodylines
			0    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 6
		{
			"Subject: something\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=\"UTF-8\"\n"
			"\n"
			"Content-Type: text/plain\n",
			0    , // startpos
			80   , // endpos
			80   , // startbody
			105  , // endbody
			4    , // nlines
			1    , // nbodylines
			1    , // mime1
			0, "text/plain", "utf-8",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 7
		{
			"Subject: msg\n"
			"Mime-Version: 1.0\n"
			"Content-Type: message/rfc822\n"
			"\n"
			"Subject: inner msg\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=utf-8\n"
			"Content-Transfer-Encoding: 8BIT\n"
			"\n"
			"Hello\n",
			0    , // startpos
			61   , // endpos
			61   , // startbody
			177  , // endbody
			4    , // nlines
			6    , // nbodylines
			1    , // mime1
			0, "message/rfc822", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					61   , // startpos
					171  , // endpos
					171  , // startbody
					177  , // endbody
					5    , // nlines
					1    , // nbodylines
					1    , // mime1
					0, "text/plain", "utf-8",
					"", cte::eightbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 8
		{
			"Subject: multipart\n"
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=aa\n"
			"\n"
			"Preamble\n"
			"--aa\n"
			"Content-Type: text/plain; charset=iso-8859-1\n"
			"\n"
			"text\n"
			"\n"
			"--aasuffix\n"
			"Content-Type: text/html; charset='iso-8859-1\n"
			"\n"
			"html\n"
			"\n"
			"--aa--\n"
			"Postamble\n",
			0    , // startpos
			81   , // endpos
			81   , // startbody
			227  , // endbody
			4    , // nlines
			13   , // nbodylines
			1    , // mime1
			0, "multipart/mixed", "iso-8859-1",
			"aa", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					95   , // startpos
					141  , // endpos
					141  , // startbody
					146  , // endbody
					2    , // nlines
					2    , // nbodylines
					1    , // mime1
					0, "text/plain", "iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				},
				{
					158  , // startpos
					204  , // endpos
					204  , // startbody
					209  , // endbody
					2    , // nlines
					2    , // nbodylines
					1    , // mime1
					0, "text/html", "'iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 9
		{
			"Subject: multipart\n"
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=aa\n"
			"\n"
			"--aa\n"
			"Content-Type: multipart/alternative; boundary=ab\n"
			"\n"
			"--ab\n"
			"Content-Type: text/html\n"
			"\n"
			"html\n"
			"--ab\n"
			"Content-Type: text/plain\n"
			"\n"
			"plain\n"
			"\n"
			"--ab--\n"
			"--aa\n"
			"Content-Type: text/csv\n"
			"\n"
			"a,b,c\n"
			"\n"
			"--aa--\n",
			0    , // startpos
			81   , // endpos
			81   , // startbody
			259  , // endbody
			4    , // nlines
			19   , // nbodylines
			1    , // mime1
			0, "multipart/mixed", "iso-8859-1",
			"aa", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					86   , // startpos
					136  , // endpos
					136  , // startbody
					215  , // endbody
					2    , // nlines
					10   , // nbodylines
					1    , // mime1
					0, "multipart/alternative", "iso-8859-1",
					"ab", cte::eightbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0, // haslongquotedline
					{
						{
							141  , // startpos
							166  , // endpos
							166  , // startbody
							170  , // endbody
							2    , // nlines
							1    , // nbodylines
							1    , // mime1
							0, "text/html", "iso-8859-1",
							"", cte::sevenbit,
							0, // has8bitheader
							0, // has8bitbody
							0, // has8bitcontentchar
							0  // haslongquotedline
						},
						{
							176  , // startpos
							202  , // endpos
							202  , // startbody
							208  , // endbody
							2    , // nlines
							2    , // nbodylines
							1    , // mime1
							0, "text/plain", "iso-8859-1",
							"", cte::sevenbit,
							0, // has8bitheader
							0, // has8bitbody
							0, // has8bitcontentchar
							0  // haslongquotedline
						}
					}
				},
				{
					221  , // startpos
					245  , // endpos
					245  , // startbody
					251  , // endbody
					2    , // nlines
					2    , // nbodylines
					1    , // mime1
					0, "text/csv", "iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 10
		{
			"Subject: messages\n"
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/digest; boundary=xxx\n"
			"\n"
			"--xxx\n"
			"\n"
			"Subject: one\n"
			"\n"
			"one\n"
			"\n"
			"--xxx\n"
			"\n"
			"Subject: two\n"
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/alternative; boundary=yyy\n"
			"\n"
			"Preamble\n"
			"--yyyy\n"
			"Content-Type: text/html\n"
			"\n"
			"html\n"
			"\n"
			"--yyy\n"
			"Content-Type: text/plain\n"
			"\n"
			"plain\n"
			"--yyy--\n"
			"--xxx\n"
			"\n"
			"Subject: three\n"
			"\n"
			"Three\n"
			"\n"
			"--xxx--\n",
			0    , // startpos
			82   , // endpos
			82   , // startbody
			328  , // endbody
			4    , // nlines
			30   , // nbodylines
			1    , // mime1
			0, "multipart/digest", "iso-8859-1",
			"xxx", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					88   , // startpos
					89   , // endpos
					89   , // startbody
					107  , // endbody
					1    , // nlines
					4    , // nbodylines
					1    , // mime1
					0, "message/rfc822", "iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0, // haslongquotedline
					{
						{
							89   , // startpos
							103  , // endpos
							103  , // startbody
							107  , // endbody
							2    , // nlines
							2    , // nbodylines
							0    , // mime1
							0, "text/plain", "iso-8859-1",
							"", cte::sevenbit,
							0, // has8bitheader
							0, // has8bitbody
							0, // has8bitcontentchar
							0  // haslongquotedline
						}
					}
				},
				{
					114  , // startpos
					115  , // endpos
					115  , // startbody
					289  , // endbody
					1    , // nlines
					15   , // nbodylines
					1    , // mime1
					0, "message/rfc822", "iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0, // haslongquotedline
					{
						{
							115  , // startpos
							197  , // endpos
							197  , // startbody
							289  , // endbody
							4    , // nlines
							11   , // nbodylines
							1    , // mime1
							0, "multipart/alternative", "iso-8859-1",
							"yyy", cte::eightbit,
							0, // has8bitheader
							0, // has8bitbody
							0, // has8bitcontentchar
							0, // haslongquotedline
							{
								{
									213  , // startpos
									238  , // endpos
									238  , // startbody
									243  , // endbody
									2    , // nlines
									2    , // nbodylines
									1    , // mime1
									0, "text/html", "iso-8859-1",
									"", cte::sevenbit,
									0, // has8bitheader
									0, // has8bitbody
									0, // has8bitcontentchar
									0  // haslongquotedline
								},
								{
									250  , // startpos
									276  , // endpos
									276  , // startbody
									281  , // endbody
									2    , // nlines
									1    , // nbodylines
									1    , // mime1
									0, "text/plain", "iso-8859-1",
									"", cte::sevenbit,
									0, // has8bitheader
									0, // has8bitbody
									0, // has8bitcontentchar
									0  // haslongquotedline
								}
							}
						}
					}
				},
				{
					296  , // startpos
					297  , // endpos
					297  , // startbody
					319  , // endbody
					1    , // nlines
					4    , // nbodylines
					1    , // mime1
					0, "message/rfc822", "iso-8859-1",
					"", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0, // haslongquotedline
					{
						{
							297  , // startpos
							313  , // endpos
							313  , // startbody
							319  , // endbody
							2    , // nlines
							2    , // nbodylines
							0    , // mime1
							0, "text/plain", "iso-8859-1",
							"", cte::sevenbit,
							0, // has8bitheader
							0, // has8bitbody
							0, // has8bitcontentchar
							0  // haslongquotedline
						}
					}
				}
			}
		},

		// Test 11
		{
			"Mime-Version: 1.0\n"
			"Content-Type: message/rfc822\n"
			"\n"
			"Subject: испытание\n"
			"\n",
			0    , // startpos
			48   , // endpos
			48   , // startbody
			77   , // endbody
			3    , // nlines
			2    , // nbodylines
			1    , // mime1
			0, "message/rfc822", "iso-8859-1",
			"", cte::eightbit,
			0, // has8bitheader
			1, // has8bitbody
			1, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					48   , // startpos
					77   , // endpos
					77   , // startbody
					77   , // endbody
					2    , // nlines
					0    , // nbodylines
					0    , // mime1
					0, "text/plain", "iso-8859-1",
					"", cte::sevenbit,
					1, // has8bitheader
					0, // has8bitbody
					1, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 12
		{
			"Mime-Version: 1.0\n"
			"Content-Type: message/rfc822\n"
			"\n"
			"Subject: test\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=utf-8\n"
			"Content-Transfer-Encoding: 8bit\n"
			"\n"
			"испытание\n",
			0    , // startpos
			48   , // endpos
			48   , // startbody
			172  , // endbody
			3    , // nlines
			6    , // nbodylines
			1    , // mime1
			0, "message/rfc822", "iso-8859-1",
			"", cte::eightbit,
			0, // has8bitheader
			1, // has8bitbody
			1, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					48   , // startpos
					153  , // endpos
					153  , // startbody
					172  , // endbody
					5    , // nlines
					1    , // nbodylines
					1    , // mime1
					0, "text/plain", "utf-8",
					"", cte::eightbit,
					0, // has8bitheader
					1, // has8bitbody
					1, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 13
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=AAA\n"
			"\n"
			"--aaa\n"
			"Content-Type: text/plain\n"
			"Subject: испытание\n"
			"\n"
			"Test\n"
			"\n"
			"--aaa--\n",
			0    , // startpos
			63   , // endpos
			63   , // startbody
			137  , // endbody
			3    , // nlines
			7    , // nbodylines
			1    , // mime1
			0, "multipart/mixed", "iso-8859-1",
			"aaa", cte::eightbit,
			0, // has8bitheader
			1, // has8bitbody
			1, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					69   , // startpos
					123  , // endpos
					123  , // startbody
					128  , // endbody
					3    , // nlines
					2    , // nbodylines
					1    , // mime1
					0, "text/plain", "iso-8859-1",
					"", cte::sevenbit,
					1, // has8bitheader
					0, // has8bitbody
					1, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 14
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=aaa\n"
			"\n"
			"--AAA\n"
			"Content-Type: text/plain; charset=utf-8\n"
			"Content-Transfer-Encoding: 8bit\n"
			"\n"
			"испытание\n"
			"Test\n"
			"\n"
			"--aaa--\n",
			0    , // startpos
			63   , // endpos
			63   , // startbody
			175  , // endbody
			3    , // nlines
			8    , // nbodylines
			1    , // mime1
			0, "multipart/mixed", "iso-8859-1",
			"aaa", cte::eightbit,
			0, // has8bitheader
			1, // has8bitbody
			1, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					69   , // startpos
					142  , // endpos
					142  , // startbody
					166  , // endbody
					3    , // nlines
					3    , // nbodylines
					1    , // mime1
					0, "text/plain", "utf-8",
					"", cte::eightbit,
					0, // has8bitheader
					1, // has8bitbody
					1, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 15
		{
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=iso-8859-1\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"=A0=20=ff=FF\n",
			0    , // startpos
			108  , // endpos
			108  , // startbody
			121  , // endbody
			4    , // nlines
			1    , // nbodylines
			1    , // mime1
			0, "text/plain", "iso-8859-1",
			"", cte::qp,
			0, // has8bitheader
			0, // has8bitbody
			1, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 16
		{
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=iso-8859-1\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"и\n",
			0    , // startpos
			108  , // endpos
			108  , // startbody
			111  , // endbody
			4    , // nlines
			1    , // nbodylines
			1    , // mime1
			RFC2045_ERR8BITINQP, "text/plain", "iso-8859-1",
			"", cte::qp,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 17
		{
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=iso-8859-1\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"=ZZ\n",
			0    , // startpos
			108  , // endpos
			108  , // startbody
			112  , // endbody
			4    , // nlines
			1    , // nbodylines
			1    , // mime1
			RFC2045_ERRBADHEXINQP, "text/plain", "iso-8859-1",
			"", cte::qp,
			0, // has8bitheader
			0, // has8bitbody
			1, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 18
		{
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain; charset=utf-8\n"
			"\n"
			"и",
			0    , // startpos
			59   , // endpos
			59   , // startbody
			61   , // endbody
			3    , // nlines
			1    , // nbodylines
			1    , // mime1
			RFC2045_ERRUNDECLARED8BIT, "text/plain", "utf-8",
			"", cte::sevenbit,
			0, // has8bitheader
			1, // has8bitbody
			1, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 19
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed\n"
			"Test\n",
			0    , // startpos
			53   , // endpos
			53   , // startbody
			53   , // endbody
			3    , // nlines
			0    , // nbodylines
			1    , // mime1
			RFC2045_ERRBADBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
			"", cte::sevenbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0  // haslongquotedline
		},

		// Test 20
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=AA\n"
			"\n"
			"\n"
			"--AA\n"
			"Content-Type: multipart/mixed; boundary=AA1\n"
			"\n"
			"--AA1\n"
			"Content-Type: text/plain\n"
			"\n"
			"Test\n"
			"\n"
			"--AA1--\n"
			"--AA--\n",
			0    , // startpos
			62   , // endpos
			62   , // startbody
			113  , // endbody
			3    , // nlines
			4    , // nbodylines
			1    , // mime1
			RFC2045_ERRBADBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
			"aa", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					68   , // startpos
					113  , // endpos
					113  , // startbody
					113  , // endbody
					2    , // nlines
					0    , // nbodylines
					1    , // mime1
					RFC2045_ERRBADBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
					"aa1", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 21
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=AA11\n"
			"\n"
			"\n"
			"--AA11\n"
			"Content-Type: multipart/mixed; boundary=AA1\n"
			"\n"
			"--AA1\n"
			"Content-Type: text/plain\n"
			"\n"
			"Test\n"
			"\n"
			"--AA1--\n"
			"--AA11--\n",
			0    , // startpos
			64   , // endpos
			64   , // startbody
			117  , // endbody
			3    , // nlines
			4    , // nbodylines
			1    , // mime1
			RFC2045_ERRBADBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
			"aa11", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					72   , // startpos
					117  , // endpos
					117  , // startbody
					117  , // endbody
					2    , // nlines
					0    , // nbodylines
					1    , // mime1
					RFC2045_ERRBADBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
					"aa1", cte::sevenbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		},

		// Test 22
		{
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=AA11\n"
			"\n"
			"\n"
			"--AA11\n"
			"Content-Type: multipart/mixed; boundary=AA12\n"
			"\n"
			"--AA11\n"
			"Content-Type: text/plain\n"
			"\n"
			"Test\n"
			"\n"
			"--AA12--\n"
			"--AA11--\n",
			0    , // startpos
			64   , // endpos
			64   , // startbody
			118  , // endbody
			3    , // nlines
			4    , // nbodylines
			1    , // mime1
			RFC2045_ERRWRONGBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
			"aa11", cte::eightbit,
			0, // has8bitheader
			0, // has8bitbody
			0, // has8bitcontentchar
			0, // haslongquotedline
			{
				{
					72   , // startpos
					118  , // endpos
					118  , // startbody
					118  , // endbody
					2    , // nlines
					0    , // nbodylines
					1    , // mime1
					RFC2045_ERRWRONGBOUNDARY|RFC2045_ERRFATAL, "multipart/mixed", "iso-8859-1",
					"aa12", cte::eightbit,
					0, // has8bitheader
					0, // has8bitbody
					0, // has8bitcontentchar
					0  // haslongquotedline
				}
			}
		}
	};

#if UPDATE_TESTSUITECPP
	const char *sep="";
#endif

	size_t testnum=0;

	for (const auto &t:tests1)
	{
		++testnum;
		rfc2045::entity entity;

		std::string_view message{t.message};
		auto b=message.begin(), e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		entity.parse(parser);

#if UPDATE_TESTSUITECPP
		b=message.begin(); e=message.end();
		std::cout << sep << "\n// Test " << testnum << "\n{\n\t\"";

		const char *end_sep="\"";
		sep="";
		while (b != e)
		{
			auto p=b;
			b=std::find(b, e, '\n');

			std::cout << sep;

			for (char c:std::string_view(p, b-p))
			{
				if (c == '\\' || c == '"')
					std::cout << '\\';

				std::cout << c;
			}

			if (b != e)
			{
				++b;
				std::cout << "\\n";
			}
			std::cout << "\"";
			sep="\n\t\"";
			end_sep="";
		}

		std::cout << end_sep << ",\n";

		parsed_mime_info::dump_entity(entity, "\t");

		std::cout << "}";
		sep=",\n";
#else
		if (t.parsed != entity)
		{
			std::cout << "Simple mime parsing test " << testnum
				  << " failed:\n";

			t.parsed.dump(entity);
			exit(1);
		}
#endif
	}
#if UPDATE_TESTSUITECPP
	std::cout << "\n";
#endif

#define LONGQUOTEDCHUNKSIZE	20
#define LONGQUOTEDLINESIZE	1020 // Must be multiple of 20

	static const struct {
		const char *prefix;
		const char *repeat;
		size_t nrepeats;
		const char *suffix;
		bool haslongquotedline;

	} tests2[]={

		// Test 100
		{
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain\n"
			"\n",
			"12345678901234567890\n", LONGQUOTEDLINESIZE / 20,
			"",
			0
		},

		// Test 101
		{
			"Mime-Version: 1.0\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n",
			"12345678901234567890=\n", LONGQUOTEDLINESIZE / 20 - 1,
			"12345678901234567890\n",
			0
		},

		// Test 102
		{
			"Mime-Version: 1.0\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n",
			"12345678901234567890=\n", LONGQUOTEDLINESIZE / 20 - 1,
			"1234567890123456778901\n",
			1
		},

		// Test 103
		{
			"Mime-Version: 1.0\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n",
			"=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20", LONGQUOTEDLINESIZE / 20,
			"\n",
			0
		},

		// Test 104
		{
			"Mime-Version: 1.0\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n",
			"=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20=20", LONGQUOTEDLINESIZE / 20 + 1,
			"\n",
			1
		}
	};

#if 0
	const char *sep="";

#define UPDATE_MIMELONGLINE 1
#endif

	testnum=99;

	for (const auto &t:tests2)
	{
		++testnum;
		rfc2045::entity entity;

		std::string message;

		std::string_view prefix{t.prefix};
		std::string_view repeat{t.repeat};
		std::string_view suffix{t.suffix};

		message.reserve(prefix.size() +
				repeat.size()*t.nrepeats +
				suffix.size());
		message=prefix;
		for (size_t i=0; i<t.nrepeats; ++i)
			message += repeat;
		message += suffix;

		auto b=message.data();
		auto e=b+message.size();
		rfc2045::entity::line_iter<false>::iter parser{b, e};

		parser.longquotedlinesize=LONGQUOTEDLINESIZE;

		entity.parse(parser);

#if UPDATE_MIMELONGLINE
		std::cout << sep << "\n/" "/ Test " << testnum << "\n{\n";

		for (const auto &stropt: std::array<
			     std::tuple<std::string_view,
			     std::optional<size_t>>, 3>{
			     {
				     { t.prefix, std::nullopt },
				     { t.repeat, t.nrepeats },
				     { t.suffix, std::nullopt}
			     }
		     })
		{
			const auto &[str, opt] = stropt;

			auto b=str.begin(), e=str.end();

			const char *str_sep="";

			std::cout << "\t\"";
			while (b != e)
			{
				std::cout << str_sep;

				auto p=b;
				b=std::find(b, e, '\n');

				for (char c:std::string_view(p, b-p))
				{
					if (c == '\\' || c == '"')
						std::cout << '\\';
					std::cout << c;
				}

				if (b != e)
				{
					++b;
					std::cout << "\\n";
				}
				str_sep="\"\n\t\"";
			}
			std::cout << "\",";
			if (opt)
			{
				std::cout << " LONGQUOTEDLINESIZE / "
					  << LONGQUOTEDCHUNKSIZE;

				if (LONGQUOTEDLINESIZE / LONGQUOTEDCHUNKSIZE
				    != *opt)
				{
					if (LONGQUOTEDLINESIZE /
					    LONGQUOTEDCHUNKSIZE < *opt)
						std::cout << " + " <<
							*opt
							- LONGQUOTEDLINESIZE /
							LONGQUOTEDCHUNKSIZE;
					else
						std::cout << " - " <<
							(LONGQUOTEDLINESIZE /
							 LONGQUOTEDCHUNKSIZE
							 - *opt);
				}
				std::cout << ",";
			}
			std::cout << "\n";
		}
		std::cout << "\t" << entity.haslongquotedline << "\n}";
		sep=",\n";
#else
		if (entity.haslongquotedline != t.haslongquotedline)
		{
			std::cout << "Test " << testnum << ":\n";
			std::cout << "   haslongquotedline: "
				  << entity.haslongquotedline
				  << "\n";
			exit(1);
		}
#endif
	}

#if UPDATE_MIMELONGLINE
	std::cout << "\n";
#endif

	{
		std::string message{
			"Subject: test\n"
			"Mime-Version: 1.0\n"
			"Content-Type: multipart/mixed; boundary=\"aaa\"\n"
			"Content-Transfer-Encoding: base64\n"
			"\n"
			"--aaa\n"
			"Content-Type: text/plain\n"
			"\n"
			"--aaa--\n"
		};

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity entity;
		rfc2045::entity::line_iter<false>::iter parser{b, e};
		entity.parse(parser);

		if (!(entity.errors & RFC2045_ERRINVALIDBASE64))
		{
			std::cout << "bad base64 test 1 failed\n";
			exit(1);
		}
	}

	{
		std::string message{
			"Subject: test\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain\n"
			"Content-Transfer-Encoding: base64\n"
			"\n"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n"
			" \n"
		};

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity entity;
		rfc2045::entity::line_iter<false>::iter parser{b, e};
		entity.parse(parser);

		if (entity.errors)
		{
			std::cout << "base base64 test 2 failed\n";
			exit(1);
		}
	}

	{
		std::string message{
			"Subject: test\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain\n"
			"Content-Transfer-Encoding: base64\n"
			"\n"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=~\n"
			" \t\r\n"
		};

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity entity;
		rfc2045::entity::line_iter<false>::iter parser{b, e};
		entity.parse(parser);

		if (!(entity.errors & RFC2045_ERRINVALIDBASE64))
		{
			std::cout << "bad base64 test 3 failed\n";
			exit(1);
		}
	}

	{
		std::string message{
			"Subject: test\n"
			"Mime-Version: 1.0\n"
			"Content-Type: text/plain\n"
			"Content-Transfer-Encoding: base64\n"
			"\n"
		};

		message += std::string(2000, ' ');

		message +=
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=~\n"
			" \t\r\n";

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity entity;
		rfc2045::entity::line_iter<false>::iter parser{b, e};
		entity.parse(parser);

		if (!(entity.errors & RFC2045_ERRINVALIDBASE64))
		{
			std::cout << "bad base64 test 4 failed\n";
			exit(1);
		}
	}
}

void testmimelimits()
{
	std::string message{
		"Subject: test\n"
		"Mime-Version: 1.0\n"
		"Content-Type: multipart/mixed; boundary=\"aaa\"\n"
		"\n"
	};

	for (int i=0; i<10; i++)
	{
		message += "\n--aaa\n"
			"Content-Type: text/plain\n\n\n";
	}

	message += "\n--aaa--\n";

	// There are 11 mime entities.

	{
		rfc2045::entity entity;

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};
		parser.mimeentityparselimit=11;
		entity.parse(parser);

		if (entity.errors)
		{
			std::cout << "limit test 1 failed\n";
			exit(1);
		}
	}

	{
		rfc2045::entity entity;

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};
		parser.mimeentityparselimit=10;
		entity.parse(parser);

		if (!(entity.errors & RFC2045_ERR2COMPLEX))
		{
			std::cout << "limit test 2 failed\n";
			exit(1);
		}
	}

	// Create 10 nested MIME entities, for a total of 11 entities
	message="Content-Type: text/plain\n\nTest\n";

	for (int i=0; i<10; ++i)
	{
		char boundary[4];

		boundary[0]='A';
		boundary[1]='A';
		boundary[2]='A'+i;
		boundary[3]=0;

		message =
			std::string{"Content-Type: multipart/mixed; boundary="}
			+ boundary + "\n"
				"\n--" + boundary + "\n" + message
				+ "\n--" + boundary + "--\n";
	}

	message="Mime-Version: 1.0\n" + message;

	{
		rfc2045::entity entity;

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};
		parser.mimeentitynestedlimit=11;
		entity.parse(parser);

		if (entity.errors)
		{
			std::cout << "limit test 3 failed\n";
			exit(1);
		}
	}

	{
		rfc2045::entity entity;

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};
		parser.mimeentitynestedlimit=10;
		entity.parse(parser);

		if (!(entity.errors & RFC2045_ERR2COMPLEX))
		{
			std::cout << "limit test 4 failed\n";
			exit(1);
		}
	}

	for (size_t s=50; s<52; ++s)
	{
		message="Mime-Version: 1.0\n"

			// The two spaces is intentional
			"Content-Transfer-Encoding:  quoted-printable\n"
			"\n"
			"=20=20=20=20=20=20=20=20=20=20"
			+ std::string(15, ' ') + "=\n"
			+ std::string(s-25, ' ')+"\n";

		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		parser.longquotedlinesize=50;

		rfc2045::entity entity;
		entity.parse(parser);

		if (entity.haslongquotedline != s-50)
		{
			std::cout << "limit test 5, part "
				  << s-49 << ", failed\n";
			exit(1);
		}
	}

	message="Subject: ";

	message += std::string(16, '1')+"\n";
	message += "    " + std::string(50-1-25, '1')+"\n";
	message += "Mime-Version: 1.0\n"
		"\nTest\n";

	for (size_t s=49; s<51; ++s)
	{
		auto b=message.begin();
		auto e=message.end();

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		parser.longunfoldedheadersize=s;

		rfc2045::entity entity;
		entity.parse(parser);

		if ( (entity.errors & RFC2045_ERRLONGUNFOLDEDHEADER)
		     != (s <= 50 ? RFC2045_ERRLONGUNFOLDEDHEADER:0))
		{
			std::cout << "limit test 6, part "
				  << s-48 << ", failed\n";
			exit(1);
		}
	}
}

int main()
{
	testrfc2045line_iter();
	testrfc2045foldedline_iter();
	testrfc2231headers();
	testmimeparse();
	testmimelimits();
	return 0;
}
