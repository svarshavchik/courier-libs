/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"cgi.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string_view>

namespace {

	struct sink_counter : cgi_encode::sink {
		size_t n=0;
		void operator()(char) override
		{
			++n;
		}
	};

	struct sink_c : cgi_encode::sink {

		char *ptr;

		sink_c(char *ptr) : ptr{ptr} {}

		void operator()(char c) override
		{
			*ptr++=c;
		}
	};
}

void cgi_encode::algorithm(cgi_encode::sink &sinking, std::string_view s,
			   const char *punct)
{
	static const char hex[]="0123456789ABCDEF";

	for (unsigned char c:s)
	{
		if (strchr(punct, c) || c < 32 || c >= 127)
		{
			sinking('%');
			sinking(hex[c >> 4]);
			sinking(hex[c & 15]);
		}
		else
		{
			sinking(c == ' ' ? '+':c);
		}
	}
}

size_t cgi_encode::estimate(std::string_view s,  const char *punct)
{
	sink_counter counter;

	algorithm(counter, s, punct);

	return counter.n;
}

#define COMMONENCODE "\"?;<>/:%@+#"

const char cgi_encode::default_encode[]=COMMONENCODE "&=";

const char cgi_encode::noamp[]=COMMONENCODE "=";

const char cgi_encode::noeq[]=COMMONENCODE "&";
