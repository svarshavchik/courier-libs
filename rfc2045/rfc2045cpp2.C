/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"rfc2045/rfc2045.h"

rfc2045::entity_parser_base::entity_parser_base()=default;

rfc2045::entity_parser_base::~entity_parser_base()=default;

template<bool crlf> rfc2045::entity_parser<crlf>::~entity_parser()
{
	// Call parsed_entity() to insure that the execution thread will get
	// stopped, if we bailed out early without asking for the parsed_entity
	// and then join the execution thread.
	(void)this->parsed_entity();

	parsing_thread.join();
}

namespace {
#if 0
}
#endif

// Define beginning/ending input iterators that the execution thread uses
// to parse content that was fed into the entity parser.

struct parser_end_iter {
};

struct parser_beg_iter {

	rfc2045::entity_parser_base &entity_parser;

	mutable std::string buffer;

	mutable std::string::iterator b{buffer.begin()}, e{b};

	char store;

	// If b==e on exit, there are no more chunks to parse.

	char operator*() const
	{
		while (b == e)
		{
			if (!entity_parser.get_next_chunk(buffer))
				return 0;

			b=buffer.begin();
			e=buffer.end();
		}

		return *b;
	}

	parser_beg_iter &operator++()
	{
		operator*();
		if (b != e)
			++b;
		return *this;
	}

	const char *operator++(int)
	{
		store=operator*();
		if (b != e)
			++b;
		return &store;
	}

	bool operator!=(const parser_end_iter &ei) const
	{
		return !operator==(ei);
	}

	bool operator==(const parser_end_iter &ei) const
	{
		operator*();
		return b == e;
	}

	void drain()
	{
		while (b != e)
		{
			b=e;
			operator*();
		}
	}
};

#if 0
{
#endif
}

template<bool crlf> rfc2045::entity_parser<crlf>::entity_parser()
{
	parsing_thread=std::thread{
		[this]
		{
			parser_beg_iter b{*this};
			parser_end_iter e;

			typename entity::template line_iter<crlf>::
				template iter<parser_beg_iter,
					      parser_end_iter> i{b, e};

			entity_getting_parsed.parse(i);

			b.drain();
		}};
}

template class rfc2045::entity_parser<false>;
template class rfc2045::entity_parser<true>;
