#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	"normalization_defs.h"
#define exclusion_table
#include	"normalization.h"

#include <iostream>
#include <iomanip>

static void dump(const char32_t *s,
		 size_t n)
{
	std::cout << '"';
	for (size_t i=0;
	     n == (size_t)-1 ? s[i] != 0 : i<n; ++i)
	{
		if (s[i] < 128)
		{
			std::cout << (char)s[i];
		}
		else
		{
			std::cout << "\\u"
				  << std::hex
				  << std::setw(4)
				  << std::setfill('0')
				  << s[i]
				  << std::dec
				  << std::setw(0);
		}
	}
	std::cout << "\"\n";
}

void testdecompose()
{
	static const struct {
		const char32_t *before;
		int flags;
		const char32_t *after;
	} tests[]={
		{ U"1\u1ea0\u1ea1 \u1ea4 2",
		  0,
		  U"1A\u0323a\u0323 A\u0302\u0301 2",
		},
		{ U"1\u1ea0\u1ea1 \u1ea4 2",
		  UNICODE_DECOMPOSE_FLAG_QC,
		  U"1\u1ea0\u1ea1 \u1ea4 2",
		},
		{ U"\u1ebf2\u00c0",
		  0,
		  U"e\u0302\u03012A\u0300",
		},
		{ U"\u1e0b\uf900\u0323",
		  0,
		  U"d\u0307\u8c48\u0323",
		},
		{ U"\u0374",
		  0,
		  U"\u02b9",
		},
		{ U"\u0374",
		  UNICODE_DECOMPOSE_FLAG_QC,
		  U"\u02b9",
		},
		{ U"\u0374",
		  UNICODE_DECOMPOSE_FLAG_COMPAT,
		  U"\u02b9",
		},
		{ U"\u2460",
		  0,
		  U"\u2460"},
		{ U"\u2460",
		  UNICODE_DECOMPOSE_FLAG_COMPAT,
		  U"1"},

		{ U"\u1eb4",
		  0,
		  U"\u0041\u0306\u0303" },
	};

	for (const auto &t:tests)
	{
		size_t i;

		for (i=0; t.before[i]; ++i)
			;

		char32_t *before=(char32_t *)malloc((i+1) * sizeof(char32_t));

		for (i=0; (before[i]=t.before[i]) != 0; ++i)
			     ;

		unicode_decomposition_t info;

		unicode_decomposition_init(&info, before, (size_t)-1, NULL);
		info.decompose_flags=t.flags;
		unicode_decompose(&info);
		unicode_decomposition_deinit(&info);

		if (info.string[info.string_size] != 0)
		{
			std::cerr << "C decompose failed, not \\0 terminated."
				  << std::endl;
			exit(1);
		}
		if (std::u32string{t.after} !=
		    std::u32string{info.string, info.string+info.string_size})
		{
			std::cerr << "C decompose failed:\nExpected: ";
			dump(t.after, (size_t)-1);
			std::cerr << "Received: ";
			dump(info.string, info.string_size);
			exit(1);
		}

		free(info.string);

		std::u32string us{t.before};
		unicode::decompose(us, t.flags);
		if (std::u32string{t.after} != us)
		{
			std::cerr << "C++ decompose failed:\nExpected: ";
			dump(t.after, (size_t)-1);
			std::cerr << "Received: ";
			dump(us.c_str(), us.size());
			exit(1);
		}
	}
}

static const uint8_t rangetab[][2]={
	{0x0040 & 0xFF, 0x0041 & 0xFF},
	{0x0045 & 0xFF, 0x0046 & 0xFF},
	{0x0340 & 0xFF, 0x0341 & 0xFF},
	{0x0343 & 0xFF, 0x0344 & 0xFF},
};

static const size_t starting_indextab[]={
	0,
	3
};

static const char32_t starting_pagetab[]={
	0,
	2,
};

void testtablookup()
{
	static const struct {
		char32_t ch;
		int found;
	} tests[]={
		{0, 0},
		{0x3F, 0},
		{0x40, 1},
		{0x41, 1},
		{0x42, 0},
		{0x43, 0},
		{0x44, 0},
		{0x45, 1},
		{0x46, 1},
		{0x47, 0},
		{0xFF, 0},
		{0x0141, 0},
		{0x0201, 0},
		{0x0241, 0},
		{0x0301, 0},
		{0x033F, 0},
		{0x0340, 1},
		{0x0341, 1},
		{0x0342, 0},
		{0x0343, 1},
		{0x0441, 0},
	};

	for (const auto &t:tests)
	{
		if (unicode_tab_lookup(t.ch,
				       starting_indextab,
				       starting_pagetab,
				       sizeof(starting_indextab)/
				       sizeof(starting_indextab[0]),
				       rangetab,
				       sizeof(rangetab)/
				       sizeof(rangetab[0]),
				       NULL,
				       0) != t.found)
		{
			std::cerr << "unicode_tab_lookup failed for "
				  << t.ch << std::endl;
		}
	}

}

void testcompose1()
{
	for (size_t i=0; i<sizeof(canonical_compositions)/
		     sizeof(canonical_compositions[0]); ++i)
	{
		char32_t string[2]={canonical_compositions[i][0],
			canonical_compositions[i][1]};
		size_t new_size;

		if (unicode_compose(string, 2,
				    UNICODE_COMPOSE_FLAG_ONESHOT, &new_size)
		    || new_size != 1
		    || string[0] != canonical_compositions[i][2])
		{
			std::cerr << "testcompose1: failed to compose "
				  << std::hex << std::setw(4)
				  << std::setfill('0')
				  << canonical_compositions[i][0]
				  << " with "
				  << std::hex << std::setw(4)
				  << std::setfill('0')
				  << canonical_compositions[i][1]
				  << std::endl;
			exit(1);
		}

		string[0]=canonical_compositions[i][1];
		string[1]=canonical_compositions[i][0];

		if (unicode_compose(string, 2, 0, &new_size)
		    || new_size != 2
		    || string[0] != canonical_compositions[i][1]
		    || string[1] != canonical_compositions[i][0])
		{
			std::cerr << "testcompose1: should not compose "
				  << std::hex << std::setw(4)
				  << std::setfill('0')
				  << canonical_compositions[i][1]
				  << " with "
				  << std::hex << std::setw(4)
				  << std::setfill('0')
				  << canonical_compositions[i][0]
				  << std::endl;
			exit(1);
		}
	}
}

void testcompose2()
{
	static const struct {
		std::u32string decomposed;
		std::u32string composed;
		int flags;
	} tests[]={
		{
			U"\u0d47\u0d3e\u0d47\u0d3e",
			U"\u0d4b\u0d4b",
			0,
		},
		{
			U"0z\u0335\u0327\u0324\u03011",
			U"0\u017a\u0335\u0327\u03241",
			0,
		},
		{
			U"0z\u0301\u0335\u0327\u03241",
			U"0\u017a\u0335\u0327\u03241",
			0,
		},
		{
			U"\u0041\u0306\u0303",
			U"\u1eb4",
			0,
		},
		{
			U"\u0041\u0335\u0306\u0303",
			U"\u1eb4\u0335",
			0,
		},
		{
			U"\u0041\u0335\u0306\u0303",
			U"\u1eb4",
			unicode::compose_flag_removeunused,
		},
		{
			U"a\u0041\u0335\u0306\u0303b",
			U"a\u1eb4\u0335b",
			0,
		},
		{
			U"a\u0041\u0335\u0306\u0303b",
			U"a\u1eb4b",
			unicode::compose_flag_removeunused,
		},
		{
			U"\u0306\u0303",
			U"\u0306\u0303",
			0,
		},
		{
			U"\u0306\u0303",
			U"",
			unicode::compose_flag_removeunused,
		},
		{
			U"\u0306\u0303a",
			U"\u0306\u0303a",
			0,
		},
		{
			U"\u0306\u0303a",
			U"a",
			unicode::compose_flag_removeunused,
		},
	};

	for (const auto &t:tests)
	{
		std::u32string s=t.decomposed;

		unicode::compose(s, t.flags);
		if (s != t.composed)
		{
			std::cerr << "testcompose2 failed"
				  << std::endl;
		}
	}
}


#include <map>
#include <tuple>

int main(int argc, char **argv)
{
	testdecompose();
	testtablookup();
	testcompose1();
	testcompose2();

	return 0;
}
