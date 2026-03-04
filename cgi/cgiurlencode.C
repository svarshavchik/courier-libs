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

extern "C" void error(const char *);

static void enomem()
{
	error("Out of memory.");
}

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

static char *cgiurlencode_common(std::string_view s, const char *punct)
{
	char *buf=static_cast<char *>(malloc(cgi_encode::estimate(s, punct)+1));

	sink_c c{buf};

	cgi_encode::algorithm(c, s, punct);

	*c.ptr=0;
	return buf;
}

#define COMMONENCODE "\"?;<>/:%@+#"

const char cgi_encode::default_encode[]=COMMONENCODE "&=";

const char cgi_encode::noamp[]=COMMONENCODE "=";

const char cgi_encode::noeq[]=COMMONENCODE "&";

extern "C" char *cgiurlencode(const char *buf)
{
	return (cgiurlencode_common(buf, cgi_encode::default_encode));
}

extern "C" char *cgiurlencode_noamp(const char *buf)
{
	return (cgiurlencode_common(buf, cgi_encode::noamp));
}

extern "C" char *cgiurlencode_noeq(const char *buf)
{
	return (cgiurlencode_common(buf, cgi_encode::noeq));
}
