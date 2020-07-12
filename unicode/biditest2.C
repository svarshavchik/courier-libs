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

		if (std::get<1>(ret) != paragraph_embedding_level)
		{
			std::cerr << "Regression, line "
				  << linenum
				  << ": expected "
				  << paragraph_embedding_level
				  << " paragraph embedding level, got "
				  << (int)std::get<1>(ret)
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

		unicode::bidi_extra_cleanup(s, levels);

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
				}
			}

			auto logical_string=s;
			auto logical_levels=levels;

			unicode::bidi_logical_order(logical_string,
						    logical_levels,
						    paragraph);

			auto new_string=unicode::bidi_embed(logical_string,
							    logical_levels,
							    paragraph);

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
			unicode::bidi_extra_cleanup(new_string,
						    std::get<0>(ret));

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
				case LRM: std::cerr << "LRM"; break;
				case RLM: std::cerr << "RLM"; break;
				case RLI: std::cerr << "RLI"; break;
				case LRI: std::cerr << "LRI"; break;
				case RLO: std::cerr << "RLO"; break;
				case LRO: std::cerr << "LRO"; break;
				case PDF: std::cerr << "PDF"; break;
				case PDI: std::cerr << "PDI"; break;
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

int main(int argc, char **argv)
{
	DEBUGDUMP=fopen("/dev/null", "w");
	if (!DEBUGDUMP)
	{
		perror("/dev/null");
		exit(1);
	}

	latin_test();
	character_test();
	return 0;
}
