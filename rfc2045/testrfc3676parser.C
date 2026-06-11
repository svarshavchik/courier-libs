/*
** Copyright 2011 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"rfc3676parser.h"

#include	<stdlib.h>
#include	<stdio.h>

class TPP: public mail::textplainparser {
public:
	using mail::textplainparser::textplainparser;
	using mail::textplainparser::operator<<;
private:
	void line_begin(size_t quote_level) override;
	void line_contents(const char32_t *txt, size_t txt_size) override;
	void line_flowed_notify() override;
	void line_end() override;
};

void TPP::line_begin(size_t quote_level)
{
	printf("[%d: ", (int)quote_level);
}

void TPP::line_contents(const char32_t *txt,
			 size_t txt_size)
{
	while (txt_size--)
		putchar(*txt++);
}

void TPP::line_flowed_notify()
{
	printf("...");
}

void TPP::line_end()
{
	printf("]\n");
}

int main(int argc, char **argv)
{
	int n=0;
	char buf[BUFSIZ];
	if (argc > 1)
		n=atoi(argv[1]);

	TPP parser("utf-8", n != 0, n == 2);
	while (fgets(buf, sizeof(buf), stdin))
		parser << buf;

	parser.end();

	printf("\n");
	return (0);
}
