/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"rfc2045.h"

void rfc2045::entity_parse_meta::report_error(
	rfc2045::entity_info::errors_t code
)
{
	for (auto &e:parsing_entities)
		e->errors |= code;
}

bool rfc2045::entity_parse_meta::fatal_error()
{
	return !parsing_entities.empty() // Sanity check
		&& parsing_entities[0]->errors & RFC2045_ERRFATAL;
}

void rfc2045::entity_parse_meta::consumed_header_line(size_t c)
{
	if (parsing_entities.empty())
		abort();

	auto b=parsing_entities.begin();
	auto e=parsing_entities.end();

	--e;
	++(*e)->nlines;
	(*e)->endpos += c;

	// And this is the body of all of its parents.

	while (b != e)
	{
		++(*b)->nbodylines;
		(*b)->endbody += c;
		++b;
	}
}

void rfc2045::entity_parse_meta::consumed_body_line(size_t c)
{
	for (auto &ptr:parsing_entities)
	{
		++ptr->nbodylines;
		ptr->endbody += c;
	}
}

// The constructor takes the entire header value as a parameter and fully
// parses it.

rfc2045::entity::rfc2231_header::rfc2231_header(
	const std::string_view &s
)
{
	rfc822::tokens tokens{s};

	auto tb=tokens.begin(), te=tokens.end();

	// Value, up to the first semicolon.
	auto semicolon=std::find_if(tb, te,
				    []
				    (const auto &t)
				    {
					    return t.type == ';';
				    });

	rfc822::tokens::unquote(tb, semicolon,
				std::back_inserter(value));

	tolowercase(value);

	// Value of a structured header parameter, parsed according to RFC 2231.
	// This is preliminary parsing, once it's parsed this'll get reassembled
	// into a single parsed string, representing the value.

	struct rfc2231_parsed_parameters {
		std::string charset{"utf-8"};
		std::string language{"en"};
		std::map<std::optional<int>, std::string> values;
	};

	// First, parse the parameters into a temporary structure.
	std::unordered_map<std::string,
			   rfc2231_parsed_parameters> parsed_parameters;

	while (semicolon != te)
	{
		if (++semicolon == te)
			break;
		tb=semicolon;

		// Find the next semicolon, for the next parameter
		semicolon=std::find_if(tb, te,
				       []
				       (const auto &t)
				       {
					       return t.type == ';';
				       });

		if (semicolon == tb)
			continue; // Multiple semicolons, quietly ignore.

		// = marks the end of the parameter name.
		auto equal_to=std::find_if(tb, semicolon,
				       []
				       (const auto &t)
				       {
					       return t.type == '=';
				       });

		// Grab the entire name=value

		std::string name, value;

		rfc822::tokens::unquote(tb, equal_to,
					std::back_inserter(name));

		// Everything after the = is the value.
		if (equal_to != semicolon)
		{
			++equal_to;
			rfc822::tokens::unquote(equal_to, semicolon,
						std::back_inserter(value));
		}

		tolowercase(name);

		// Let's dig into the parameter name, to see if RFC 2231 is used
		// by checking for a trailing *, first.

		bool trailing_squid=false;
		std::optional<int> key;

		if (!name.empty() && name[name.size()-1] == '*')
		{
			trailing_squid=true;
			name.pop_back(); // Get rid of the trailing squid
		}

		// If there's a * in the name, we have a piece of the parameter

		auto delim=name.rfind('*');
		if (delim < name.size())
		{
			key=0;
			for (auto b=name.begin()+delim,
				     e=name.end(); ++b != e; )
			{
				if (*b >= '0' && *b <= '9')
					key= (*key)*10 + (*b-'0');
			}
			name.resize(delim);
		}
		else if (!trailing_squid)
		{
			// Non-standard: handle RFC2047-quoted content in
			// parameter values, but only for non-RFC2231
			// parameters.

			std::string buffer;

			rfc2047::decode(
				value.begin(), value.end(),
				[&]
				(const auto &charset, const auto &language,
				 auto &&callback)
				{
					auto striter=std::back_inserter(buffer);
					callback(striter);
				});
			value=std::move(buffer);

		}
		auto parameter=parsed_parameters.try_emplace(name).first;

		// If the key-less, or key=0 ends in a squid, extract the
		// charset and the language.

		if ((!key || *key == 0) && trailing_squid)
		{
			auto p=std::find(value.begin(), value.end(), '\'');
			auto q=p;
			if (q != value.end())
				++q;

			auto r=std::find(q, value.end(), '\'');
			auto s=r;
			if (s != value.end())
				++s;

			tolowercase(value.begin(), r);

			parameter->second.charset.assign(value.begin(), p);
			parameter->second.language.assign(q, r);

			value.erase(value.begin(), s);
		}

		// The only thing left to do, in the case of a trailing
		// squid, is %-decode this thing.

		if (trailing_squid)
		{
			auto p=value.begin(), q=value.end(), r=p;
			while (p != q)
			{
				if (*p != '%')
				{
					*r++=*p++;
					continue;
				}

				// Poor man's hex decoder.

				if (++p != q)
				{
					unsigned char c=*p++;

					if (c > '9')
					{
						c += 10-('a' & 15);
					}
					c &= 15;

					if (p != q)
					{
						unsigned char d=*p++;

						if (d > '9')
						{
							d += 10-('a' & 15);
						}
						d &= 15;

						*r++ = static_cast<char>(
							(c << 4) | d
						);
					}
				}
			}
			value.erase(r, value.end());
		}

		parameter->second.values.emplace(key, value);
	}

	// Now, assemble the parsed parameters.

	std::string assembled_value;

	for (auto &[name, parsed_parameter_values] : parsed_parameters)
	{
		assembled_value.clear();

		// If the only value is the one that did NOT use RFC 2231
		// encoding, we'll take it.
		auto ptr=parsed_parameter_values.values.begin();

		if (auto ptr2=ptr; ptr2 != parsed_parameter_values.values.end()
		    && !ptr2->first &&
		    ++ptr2 == parsed_parameter_values.values.end())
		{
			assembled_value=ptr->second;
		}
		else
		{
			// Otherwise we will IGNORE the "legacy" value.

			for (auto &[key, value] :
				     parsed_parameter_values.values)
			{
				if (!key)
					continue; // Legacy
				assembled_value += value;
			}
		}

		parameters.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(name),
			std::forward_as_tuple(
				std::move(parsed_parameter_values.charset),
				std::move(parsed_parameter_values.language),
				std::move(assembled_value)));
	}
}

rfc2045::entity_parse_meta::scope::scope(entity_parse_meta &info,
					 entity *e)
	: info{info}
{
	info.parsing_entities.push_back(e);
}

rfc2045::entity_parse_meta::scope::~scope()
{
	auto last=info.parsing_entities.back();

	if (last->has8bitheader || last->has8bitbody)
	{
		last->has8bitcontentchar=true;
	}

	if (last->has8bitbody)
	{
		if (last->multipart() ||
		    rfc2045_message_content_type(last->content_type.c_str()))
		{
			last->content_transfer_encoding=cte::eightbit;
		}
		else
		{
			if (last->content_transfer_encoding != cte::eightbit)
			{
				info.report_error(RFC2045_ERRUNDECLARED8BIT);
			}
		}
	}

	info.parsing_entities.pop_back();

	if (!info.parsing_entities.empty())
	{
		auto parent=info.parsing_entities.back();

		if (last->has8bitbody || last->has8bitheader)
			parent->has8bitbody=true;

		if (last->has8bitcontentchar)
			parent->has8bitcontentchar=true;
	}
}
