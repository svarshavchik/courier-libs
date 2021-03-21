#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<iostream>
#include	<iterator>
#include	<sstream>
#include	<fstream>
#include	<cstdint>
#include	<iomanip>
#include	<algorithm>
#include	<unistd.h>

FILE *DEBUGDUMP;

#define BIDI_DEBUG

extern "C" {
#if 0
}
#endif

#include "unicode_bidi.c"

}

void latin_test()
{
	for (char32_t c=32; c<256; c++)
	{
		std::u32string s;

		s += c;

		std::vector<unicode_bidi_level_t> levels={UNICODE_BIDI_LR};

		auto new_string=unicode::bidi_embed(s, levels,
						    UNICODE_BIDI_LR);

		if (new_string != s)
		{
			std::cerr << "Character " << (int)c
				  << " does not work." << std::endl;
			exit(1);
		}
	}

	std::u32string s;
	std::vector<unicode_bidi_level_t> levels;

	for (char32_t c=32; c<256; c++)
	{
		s += c;
		levels.push_back(UNICODE_BIDI_LR);
	}

	auto new_string=unicode::bidi_embed(s, levels,
					    UNICODE_BIDI_LR);

	if (new_string != s)
	{
		std::cerr << "iso-8859-1 string does not work."
			  << std::endl;
		exit(1);
	}
}

void character_test()
{
	std::ifstream fp("BidiCharacterTest.txt");

	if (!fp.is_open())
	{
		std::cerr << "Cannot open BidiCharacterTest.txt" << std::endl;
		exit(1);
	}

	std::string buf;

	size_t linenum=0;
	size_t nextlogline=0;
	std::string logmsg;

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
		auto p=buf.find('#');

		if (p != buf.npos)
			buf=buf.substr(0, p);

		p=buf.find(';');

		if (p == buf.npos)
			continue;

		std::istringstream chars{buf.substr(0, p)};

		auto q=buf.find(';', ++p);

		if (q == buf.npos)
		{
			std::cerr << "Cannot parse line " << linenum
				  << std::endl;
			exit(2);
		}

		int direction;

		if (!(std::istringstream{buf.substr(p, q-p)} >> direction))
		{
			std::cerr << "Cannot parse line " << linenum
				  << std::endl;
			exit(3);
		}

		p=++q;
		q=buf.find(';', p);

		if (q == buf.npos)
		{
			std::cerr << "Cannot parse line " << linenum
				  << std::endl;
			exit(4);
		}

		int paragraph_embedding_level;

		if (!(std::istringstream{buf.substr(p, q-p)} >>
		      paragraph_embedding_level))
		{
			std::cerr << "Cannot parse line " << linenum
				  << std::endl;
			exit(5);
		}
		p=++q;
		q=buf.find(';', p);

		if (q == buf.npos)
		{
			std::cerr << "Cannot parse line " << linenum
				  << std::endl;
			exit(6);
		}

		std::vector<unicode_bidi_level_t> levels;

		{
			std::istringstream level_s{buf.substr(p, q-p)};

			std::string s;

			while (level_s >> s)
			{
				size_t l;

				if (!(std::istringstream{s} >> l))
				{
					l=UNICODE_BIDI_SKIP;
				}
				levels.push_back(l);
			}
		}

		std::vector<size_t> render_order;

		{
			size_t n;

			std::istringstream order_i{buf.substr(++q)};

			while (order_i >> n)
				render_order.push_back(n);
		}
		std::u32string s;
		uintmax_t c;

		while (chars >> std::hex >> c)
			s.push_back(c);

		auto ret=direction == UNICODE_BIDI_LR ||
			direction == UNICODE_BIDI_RL
			? unicode::bidi_calc(s, direction)
			: unicode::bidi_calc(s);

		if (std::get<1>(ret).direction != paragraph_embedding_level)
		{
			std::cerr << "Regression, line "
				  << linenum
				  << ": expected "
				  << paragraph_embedding_level
				  << " paragraph embedding level, got "
				  << (int)std::get<1>(ret).direction
				  << std::endl;
			exit(1);
		}

		if (std::get<0>(ret) != levels)
		{
			fclose(DEBUGDUMP);
			DEBUGDUMP=stderr;

			(void)(direction == UNICODE_BIDI_LR ||
			       direction == UNICODE_BIDI_RL
			       ? unicode::bidi_calc(s, direction)
			       : unicode::bidi_calc(s));

			std::cerr << "Regression, line "
				  << linenum
				  << ": embedding levels"
				  << std::endl
				  << "   Expected:";

			for (int l:levels)
			{
				std::cerr << " ";
				if (l == UNICODE_BIDI_SKIP)
					std::cerr << "x";
				else
					std::cerr << l;
			}

			std::cerr << std::endl
				  << "     Actual:";

			for (int l:std::get<0>(ret))
			{
				std::cerr << " ";
				if (l == UNICODE_BIDI_SKIP)
					std::cerr << "x";
				else
					std::cerr << l;
			}
			std::cerr << std::endl;
			exit(1);
		}

		std::vector<size_t> actual_render_order;

		size_t n=0;

		std::generate_n(std::back_inserter(actual_render_order),
				s.size(),
				[&] { return n++; });

		unicode::bidi_reorder
			(s, levels,
			 [&]
			 (size_t index,
			  size_t n)
			 {
				 auto b=actual_render_order.begin();
				 std::reverse(b+index, b+index+n);
			 });

		size_t cleaned_size=unicode_bidi_cleaned_size(s.c_str(),
							      s.size(), 0);

		n=0;
		unicode::bidi_cleanup
			(s, levels,
			 [&]
			 (size_t i)
			 {
				 actual_render_order.erase
					 (actual_render_order.begin()+i-n);
				 ++n;
			 });

		if (cleaned_size != s.size())
		{
			std::cerr << "Regression, line "
				  << linenum
				  << ": default cleaned size"
				  << std::endl
				  << "   Expected size: " << cleaned_size
				  << ", actual size: " << s.size()
				  << std::endl;
			exit(1);
		}
		if (render_order != actual_render_order)
		{
			std::cerr << "Regression, line "
				  << linenum
				  << ": render order"
				  << std::endl
				  << "   Expected:";
			for (auto n:render_order)
			{
				std::cerr << " " << n;
			}
			std::cerr << std::endl
				  << "     Actual:";

			for (auto n:actual_render_order)
			{
				std::cerr << " " << n;
			}
			std::cerr << std::endl;
			exit(1);
		}

		unicode::bidi_cleanup(s, levels,
				      [](size_t) {},
				      UNICODE_BIDI_CLEANUP_CANONICAL);

		auto dump_ls=
			[&]
			(const std::u32string &s,
			 const std::vector<unicode_bidi_level_t> &l)
			{
				for (size_t i=0; i<s.size(); ++i)
				{
					std::cerr << " " << std::hex
						  << std::setw(4)
						  << std::setfill('0')
						  << s[i] << "/"
						  << std::dec
						  << (int)l[i];
				}
			};

		for (int pass=0; pass<4; pass++)
		{
			int paragraph=pass & 1;
			int use_default=pass & 2;

			for (size_t i=0; i<s.size(); ++i)
			{
				/* L1 */
				switch (unicode_bidi_type(s[i])) {
				case UNICODE_BIDI_TYPE_S:
				case UNICODE_BIDI_TYPE_B:
					levels.at(i)=paragraph;
				default:
					break;
				}
			}

			auto logical_string=s;
			auto logical_levels=levels;

			unicode::bidi_logical_order(logical_string,
						    logical_levels,
						    paragraph,
						    []
						    (size_t, size_t) {},
						    0,
						    logical_string.size());

			std::u32string new_string;

			unicode::bidi_embed
				(logical_string,
				 logical_levels,
				 paragraph,
				 [&]
				 (const char32_t *string,
				  size_t n,
				  bool is_part_of_string)
				 {
					 if ((std::less_equal<const char32_t *>
						 {}(logical_string.c_str(),
						    string) &&
							 std::less<const
							 char32_t *>
						 {}(string,
						    logical_string.c_str()
						    +logical_string.size()))
						 != is_part_of_string)
					 {
						 std::cerr <<
							 "bidi_embed passed in "
							 "wrong value for "
							 "is_part_of_string"
							   << std::endl;
						 exit(1);
					 }

					 new_string.insert
						 (new_string.end(),
						  string,
						  string+n);
				 });

			auto save_string=new_string;

			if (use_default)
			{
				auto marker=unicode::bidi_embed_paragraph_level
					(new_string, paragraph);

				if (marker)
					new_string.insert(0, 1, marker);

				ret=unicode::bidi_calc(new_string);
			}
			else
			{
				ret=unicode::bidi_calc(new_string, paragraph);
			}

			unicode::bidi_reorder(new_string, std::get<0>(ret));

			cleaned_size=unicode_bidi_cleaned_size
				(new_string.c_str(),
				 new_string.size(),
				 UNICODE_BIDI_CLEANUP_CANONICAL);

			unicode::bidi_cleanup(new_string,
					      std::get<0>(ret),
					      []
					      (size_t)
					      {
					      },
					      UNICODE_BIDI_CLEANUP_CANONICAL);

			if (cleaned_size != new_string.size())
			{
				std::cerr << "Regression, line "
					  << linenum
					  << ": canonoical cleaned size"
					  << std::endl
					  << "   Expected size: "
					  << cleaned_size
					  << ", actual size: "
					  << new_string.size()
					  << std::endl;
				exit(1);
			}

			/* New string is now back in logical order */

			if (new_string == s && std::get<0>(ret) == levels)
				continue;

			fclose(DEBUGDUMP);
			DEBUGDUMP=stderr;

			std::cerr << "Regression, line "
				  << linenum
				  << ": embedding markers"
				  << std::endl
				  << "   Paragraph embedding level: "
				  << paragraph;

			if (use_default)
				std::cerr << " (defaulted)";

			std::cerr << std::endl
				  << "String (1):";

			dump_ls(s, levels);

			std::cerr << std::endl << "String (2):";

			dump_ls(new_string, std::get<0>(ret));
			std::cerr << std::endl;

			std::cerr << "Embedding:";
			dump_ls(logical_string, logical_levels);
			std::cerr << std::endl;

			unicode::bidi_embed(logical_string,
					    logical_levels,
					    paragraph);

			std::cerr << std::endl
				  << "Embedded string:";

			for (auto c:save_string)
			{
				std::cerr << " ";

				switch (c) {
				case UNICODE_LRM: std::cerr << "LRM"; break;
				case UNICODE_RLM: std::cerr << "RLM"; break;
				case UNICODE_ALM: std::cerr << "ALM"; break;
				case UNICODE_RLI: std::cerr << "RLI"; break;
				case UNICODE_LRI: std::cerr << "LRI"; break;
				case UNICODE_RLO: std::cerr << "RLO"; break;
				case UNICODE_LRO: std::cerr << "LRO"; break;
				case UNICODE_PDF: std::cerr << "PDF"; break;
				case UNICODE_PDI: std::cerr << "PDI"; break;
				default:
					std::cerr << std::hex << std::setw(4)
						  << std::setfill('0')
						  << c;
					break;
				}
			}
			std::cerr << std::dec << std::endl << std::flush;

			ret=unicode::bidi_calc(save_string, paragraph);
			unicode::bidi_reorder(save_string, std::get<0>(ret));
			exit(1);
		}
	}
	std::cout << std::endl;
}

void exception_test()
{
	std::u32string s{U"שלום"};

	auto res=unicode::bidi_calc(s);

	int thrown=0;
	int caught=0;

	try
	{
		unicode::bidi_reorder(s, std::get<0>(res),
				      [&]
				      (size_t, size_t)
				      {
					      ++thrown;
					      throw 42;
				      });
	} catch(int n)
	{
		caught += n;
	}

	if (thrown != 1 || caught != 42)
	{
		std::cerr << "Exception handling failed"
			  << std::endl;
	}
}

void partial_reorder_cleanup()
{
	std::u32string s{U"שלום"};

	auto res=unicode::bidi_calc(s);

	unicode::bidi_reorder(s, std::get<0>(res));

	unicode::bidi_cleanup(s, std::get<0>(res),
			      []
			      (size_t)
			      {
			      },
			      0,
			      0, 3);
}

void null_character_test()
{
	std::u32string s{{0}};

	auto res=unicode::bidi_calc(s);

	unicode::bidi_reorder(s, std::get<0>(res));

	unicode::bidi_cleanup(s, std::get<0>(res),
			      []
			      (size_t)
			      {
			      },
			      UNICODE_BIDI_CLEANUP_EXTRA,
			      0, 3);

	s=U"";
	res=unicode::bidi_calc(s, UNICODE_BIDI_RL);

	if (std::get<1>(res).direction != UNICODE_BIDI_RL)
	{
		std::cerr << "Paragraph embedding level not honored"
			  << std::endl;
		exit(1);
	}
}

void direction_test()
{
	static const struct {
		std::u32string str;
		unicode_bidi_level_t direction;
		int is_explicit;
		bool needs_embed;
	} tests[]={
		{
			U"Hello",
			UNICODE_BIDI_LR,
			1,
			false,
		},
		{
			U" ",
			UNICODE_BIDI_LR,
			0,
			false,
		},
		{
			U"",
			UNICODE_BIDI_LR,
			0,
			false,
		},
		{
			U"שלום",
			UNICODE_BIDI_RL,
			1,
			false,
		},
		{
			U"Helloש",
			UNICODE_BIDI_LR,
			1,
			false,
		},
		{
			U"Hello" + std::u32string{unicode::literals::LRO}
			+ U"ש",
			UNICODE_BIDI_LR,
			1,
			true,
		},
	};

	for (const auto &t:tests)
	{
		auto ret=unicode::bidi_get_direction(t.str);

		if (ret.direction != t.direction ||
		    ret.is_explicit != t.is_explicit)
		{
			std::cerr << "direction_test failed\n";
			exit(1);
		}

		std::u32string s=t.str;
		auto levels=std::get<0>(unicode::bidi_calc(s, t.direction));
		unicode::bidi_reorder(s, levels);
		unicode::bidi_cleanup(s, levels);

		if (unicode::bidi_needs_embed(s, levels, &t.direction)
		    != t.needs_embed)
		{
			std::cerr << "needs embed failed\n";
			exit(1);
		}
	}
}

void direction_test2()
{
	static const struct {
		std::u32string str;
		std::vector<unicode_bidi_level_t> directions;
		unicode_bidi_level_t direction;
		bool needs_embed;
	} tests[]={
		{
			U"Hello world!",
			{UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR,
			 UNICODE_BIDI_LR},
			UNICODE_BIDI_LR,
			false,
		},
		{
			U"Hello world!",
			{UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL,
			 UNICODE_BIDI_RL},
			UNICODE_BIDI_LR,
			true,
		},
	};

	for (const auto &t:tests)
	{
		if (t.str.size() != t.directions.size())
		{
			std::cerr << "direction_test2 bad data\n";
			exit(1);
		}

		if (unicode::bidi_needs_embed(t.str, t.directions, &t.direction)
		    != t.needs_embed)
		{
			std::cerr << "direction-test2 failed\n";
			exit(1);
		}
	}
}

void composition_test()
{
	typedef std::tuple<unicode_bidi_level_t,
			   size_t, size_t, size_t,
			   size_t> results_t;

	static const struct {
		std::u32string str;
		std::vector<unicode_bidi_level_t> levels;
		std::vector<results_t> results;
	} tests[] = {
		// Test 1
		{
			U"a\u0303\u0303b\u0303\u0303c",
			{0, 0, 0, 0, 0, 0, 0},
			{
				results_t{0, 0, 7, 1, 2},
				results_t{0, 0, 7, 4, 2},
			}
		},
		// Test 2
		{
			U"\u0303ab\u0303",
			{0, 0, 0, 0},
			{
				results_t{0, 0, 4, 0, 1},
				results_t{0, 0, 4, 3, 1},
			}
		},
		// Test 3
		{
			U"a\u0303\u0303b",
			{0, 0, 1, 1},
			{
				results_t{0, 0, 2, 1, 1},
				results_t{1, 2, 2, 2, 1},
			}
		},
		// Test 4
		{
			U"\u0303a\u0303a",
			{0, 0, 0, 0},
			{
				results_t{0, 0, 4, 0, 1},
				results_t{0, 0, 4, 2, 1},
			}
		},
	};

	int testnum=0;

	for (const auto &t:tests)
	{
		++testnum;

		std::vector<std::tuple<unicode_bidi_level_t,
				       size_t, size_t, size_t, size_t>> actual;

		auto copy=t.str;

		unicode::bidi_combinings(copy, t.levels,
					 [&]
					 (unicode_bidi_level_t level,
					  size_t level_start,
					  size_t n_chars,
					  size_t comb_start,
					  size_t n_comb_chars)
					 {
						 actual.emplace_back
							 (level,
							  level_start,
							  n_chars,
							  comb_start,
							  n_comb_chars);

						 auto b=copy.begin()+comb_start;
						 auto e=b+n_comb_chars;

						 if (comb_start + n_comb_chars
						     < level_start + n_chars)
							 ++e;

						 while (b < e)
						 {
							 --e;
							 std::swap(*b, *e);
							 ++b;
						 }
					 });

		if (actual != t.results)
		{
			std::cerr << "composition test " << testnum
				  << " failed\n";
			exit(1);
		}
	}
}
int main(int argc, char **argv)
{
	DEBUGDUMP=fopen("/dev/null", "w");
	if (!DEBUGDUMP)
	{
		perror("/dev/null");
		exit(1);
	}
	exception_test();
	composition_test();
	partial_reorder_cleanup();
	null_character_test();
	latin_test();
	character_test();
	direction_test();
	direction_test2();
	return 0;
}
