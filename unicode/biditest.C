#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<iostream>
#include	<fstream>
#include	<sstream>
#include	<string>
#include	<algorithm>
#include	<utility>
#include	<iomanip>
#include	<numeric>
#include	<unistd.h>

std::vector<std::string> testcase;

FILE *DEBUGDUMP;

int main(int argc, char **argv)
{
	if (argc > 1)
	{
		char32_t c=atoi(argv[1]);
		unicode_bidi_bracket_type_t bt;

		std::cout << unicode_bidi_mirror(c) << " "
			  << unicode_bidi_bracket_type(c, &bt) << " ";
		std::cout << (char)bt << std::endl;
		exit (0);
	}

	DEBUGDUMP=fopen("/dev/null", "w");
	if (!DEBUGDUMP)
	{
		perror("/dev/null");
		exit(1);
	}
	std::ifstream fp("BidiTest.txt");

	if (!fp.is_open())
	{
		std::cerr << "Cannot open BidiTest.txt" << std::endl;
		exit(1);
	}
	size_t linenum=0;
	size_t nextlogline=0;
	std::string logmsg;

	std::string buf;

	std::vector<unicode_bidi_level_t> expected_levels;

	std::vector<size_t> expected_reorder;

	while (1)
	{
		buf.clear();

		bool iseof=std::getline(fp, buf).eof() && buf.empty();

		if (iseof || ++linenum >= nextlogline)
		{
			alarm(300);
			std::cout << logmsg;

			std::ostringstream o;

			o << std::setw(6) << linenum << " lines processed... ";

			logmsg=o.str();

			std::cout << logmsg << std::flush;

			std::fill(logmsg.begin(), logmsg.end(), '\b');

			nextlogline += 20000;
		}
		if (iseof)
			break;
		buf.erase(std::find(buf.begin(), buf.end(), '#'), buf.end());

		if (buf.substr(0, 8) == "@Levels:")
		{
			expected_levels.clear();

			std::istringstream i(buf);

			std::string word;

			i >> word;

			while (i >> word)
			{
				int n;

				std::stringstream words(word);

				if (words >> n)
				{

					expected_levels.push_back(n);
				}
				else
				{
					expected_levels
						.push_back(UNICODE_BIDI_SKIP);
				}
			}
			continue;
		}



		if (buf.substr(0, 9) == "@Reorder:")
		{
			expected_reorder.clear();

			std::istringstream i(buf);

			std::string word;

			i >> word;

			size_t n;

			while (i >> n)
			{
				expected_reorder.push_back(n);
			}
			continue;
		}


		if (buf.substr(0, 1) == "@")
			continue;

		size_t semicolon=buf.find(';');

		if (semicolon == buf.npos)
			continue;

		testcase.clear();

		{
			std::istringstream i(buf.substr(0, semicolon));

			std::string word;

			while (i >> word)
				testcase.push_back(word);
		}

		int n;

		{
			std::istringstream i(buf.substr(semicolon+1));

			if (!(i >> n))
			{
				std::cerr << "Cannot parse paragraph bitset: "
					  << buf.substr(semicolon+1)
					  << " on line " << linenum
					  << std::endl;
				abort();
			}
		}

		//if (linenum != 98950)
		//	continue;

		std::vector<unicode_bidi_level_t> actual_levels;

		std::u32string dummy_input;

		dummy_input.resize(testcase.size());

		static const unicode_bidi_level_t level_0=0;
		static const unicode_bidi_level_t level_1=1;

		static const unicode_bidi_level_t *levels[3]=
			{0, &level_0, &level_1};

		for (auto level:levels)
		{
			if (n & 1)
			{
				auto ret=level ?
					unicode::bidi_calc(dummy_input,*level)
					: unicode::bidi_calc(dummy_input);
				actual_levels=std::get<0>(ret);

				int matched=0;

				if (actual_levels.size() ==
				    expected_levels.size())
				{
					size_t i=0;

					matched=1;

					for (i=0; i<actual_levels.size(); ++i)
					{
						if (expected_levels[i] ==
						    UNICODE_BIDI_SKIP)
							continue;
						if (expected_levels[i] !=
						    actual_levels[i])
						{
							matched=0;
							break;
						}
					}
				}

				if (!matched)
				{
					fclose(DEBUGDUMP);
					DEBUGDUMP=stderr;
					std::cout << std::endl
						  << std::flush;
					unicode_bidi_calc(&dummy_input[0],
							  testcase.size(),
							  &actual_levels[0],
							  level);

					std::cerr << "Regression, line "
						  << linenum;

					if (!level)
					{
						std::cerr << ", auto";
					}
					else
					{
						std::cerr <<
							(*level ? ", RTL"
							 : ", LTR");
					}
					std::cerr << ": expected";

					for (int l:expected_levels)
					{
						std::cerr << " " << l;
					}
					std::cerr << std::endl
						  << "Received:";

					for (int l:actual_levels)
					{
						std::cerr << " " << l;
					}
					std::cerr << std::endl;
					exit(1);
				}

				std::vector<size_t> actual_reorder;

				actual_reorder.resize(testcase.size());

				std::iota(actual_reorder.begin(),
					  actual_reorder.end(), 0);

				unicode::bidi_reorder
					(dummy_input,
					 actual_levels,
					 [&]
					 (size_t s, size_t cnt)
					 {
						 auto *b=&actual_reorder[s];
						 auto *e=b+cnt;

						 while (b < e)
						 {
							 --e;
							 std::swap(*b, *e);
							 ++b;
						 }
					 });

				auto b=actual_reorder.begin(), p=b,
					e=actual_reorder.end();

				auto q=actual_levels.begin();

				while (b != e)
				{
					if (*q != UNICODE_BIDI_SKIP)
					{
						*p=*b;
						++p;
					}
					++b;
					++q;
				}
				actual_reorder.erase(p, e);

				if (actual_reorder != expected_reorder)
				{
					fclose(DEBUGDUMP);
					DEBUGDUMP=stderr;
					std::cout << std::endl
						  << std::flush;
					unicode_bidi_calc(&dummy_input[0],
							  testcase.size(),
							  &actual_levels[0],
							  level);

					std::cerr << "Regression, line "
						  << linenum;

					if (!level)
					{
						std::cerr << ", auto";
					}
					else
					{
						std::cerr <<
							(*level ? ", RTL"
							 : ", LTR");
					}
					std::cerr << ": expected reorder";

					for (auto o:expected_reorder)
					{
						std::cerr << " " << o;
					}
					std::cerr << std::endl
						  << "Moved: ";
					for (auto o:actual_reorder)
					{
						std::cerr << " " << o;
					}
					std::cerr << std::endl;
					exit(1);
				}
			}

			n >>= 1;
		}
	}
	std::cout << std::endl;
	return 0;
}

#define UNICODE_BIDI_TEST(i) (buf[i]=fudge_unicode_bidi(i))

#define BIDI_DEBUG

extern "C" {
#if 0
}
#endif

#include "unicode_bidi.c"

enum_bidi_type_t fudge_unicode_bidi(size_t i)
{
	if (i >= testcase.size())
	{
		std::cerr << "Test case:";

		for (auto &n:testcase)
			std::cerr << " " << n;
		std::cerr << ": no value #" << i << std::endl;
		abort();
	}

	for (const auto &cn:bidiclassnames)
	{
		if (testcase[i] == cn.classname)
		{
			return cn.classenum;
		}
	}

	std::cerr << "Test case:";

	for (auto &n:testcase)
		std::cerr << " " << n;
	std::cerr << ": unknown value: " << testcase[i];
	abort();
}

#if 0
{
#endif
}
