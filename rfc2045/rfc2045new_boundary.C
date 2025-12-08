/*
** Copyright 2025 S. Varshavchik.
** See COPYING for distribution information.
**
*/

#include	"rfc2045.h"
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <charconv>

namespace {
#if 0
}
#endif

template<typename int_t> struct strbuf {

	char str[sizeof(int_t)*2];

	strbuf(int_t i)
	{
		char fmtbuf[ sizeof(int_t)*2];

		auto p=std::to_chars(fmtbuf, fmtbuf+sizeof(fmtbuf),
				     i, 16).ptr;

		auto q=str+sizeof(int_t)*2-(p-fmtbuf);

		std::fill(str, q, '0');
		std::copy(fmtbuf, p, q);
	}
};

struct pid_str : strbuf<pid_t> {

	pid_str() : strbuf{getpid()} {}

};

struct time_str : strbuf<time_t> {

	time_str() : strbuf{time(NULL)} {}
};

#if 0
{
#endif
}

std::string rfc2045::entity::new_boundary(unsigned &counter)
{
	static const pid_str pid_value;
	static const time_str time_value;

	strbuf<unsigned> counter_str{++counter};

	char boundary[ sizeof(pid_value.str) +
		     sizeof(time_value.str) +
		     sizeof(counter_str.str) + 4 ];

	char *p=boundary;

	*p++='=';
	*p++='_';

	p=std::copy(pid_value.str, pid_value.str+sizeof(pid_value.str), p);
	*p++='_';
	p=std::copy(time_value.str, time_value.str+sizeof(time_value.str), p);
	*p++='_';
	p=std::copy(counter_str.str,
		    counter_str.str+sizeof(counter_str.str), p);

	return std::string{boundary, p};
}
