#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<iostream>

int main(int argc, char **argv)
{
	if (argc > 1)
	{
		char32_t c=atoi(argv[1]);
		unicode_bidi_bracket_type_t bt;

		std::cout << unicode_bidi_mirror(c) << " "
			  << unicode_bidi_bracket_type(c, &bt) << " ";
		std::cout << (char)bt << std::endl;
	}
	return 0;
}
