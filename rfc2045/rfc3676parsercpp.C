/*
** Copyright 2011 S. Varshavchik.
** See COPYING for distribution information.
**
*/

#include	"rfc3676parser.h"

mail::textplainparser::textplainparser(
	const std::string &charset,
	bool isflowed,
	bool isdelsp
)
	: rfc3676_parser_struct(
		charset.c_str(),
		isflowed,
		isdelsp)
{
}

mail::textplainparser::~textplainparser()
{
	end();
}

void mail::textplainparser::end(bool &unicode_errflag)
{
	int rc=0;

	rfc3676_parser_struct::end(&rc);

	unicode_errflag=rc != 0;
}

void mail::textplainparser::line_begin(size_t quote_level)
{
	if (quote_level)
	{
		std::vector<char32_t> vec;

		vec.reserve(quote_level+1);
		vec.insert(vec.end(), quote_level, '>');
		vec.push_back(' ');
		line_contents(&vec[0], vec.size());
	}
}

void mail::textplainparser::line_contents(const char32_t *data,
					  size_t cnt)
{
}

void mail::textplainparser::line_flowed_notify()
{
}

void mail::textplainparser::line_end()
{
	char32_t nl='\n';

	line_contents(&nl, 1);
}
