#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<iostream>
#include	<sstream>
#include	<fstream>
#include	<cstdint>
#include	<iomanip>

FILE *DEBUGDUMP;

int main(int argc, char **argv)
{
	std::ifstream fp("BidiCharacterTest.txt");

	if (!fp.is_open())
	{
		std::cerr << "Cannot open BidiCharacterTest.txt" << std::endl;
		exit(1);
	}

	DEBUGDUMP=fopen("/dev/null", "w");
	if (!DEBUGDUMP)
	{
		perror("/dev/null");
		exit(1);
	}

	std::string buf;

	size_t linenum=0;

	while (1)
	{
		buf.clear();

		if (std::getline(fp, buf).eof() && buf.empty())
			break;
		++linenum;

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
	}
	return 0;
}

#define BIDI_DEBUG

extern "C" {
#if 0
}
#endif

#include "unicode_bidi.c"

}
