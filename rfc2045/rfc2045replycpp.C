/*
** Copyright 2000-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include "rfc2045_config.h"
#include	"rfc2045reply.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	<time.h>
#include	<sys/time.h>

static std::string mksalutation_datefmt(std::string_view fmt,
					std::string_view date)
{
	time_t t;

	if (fmt.empty())
	{
		fmt="%a, %d %b %Y %H:%M:%S %z";
	}

	std::string date_s{date.begin(), date.end()};

	if (rfc822_parsedate_chk(date_s.c_str(), &t) == 0)
	{
		struct tm tmbuf;

		if (localtime_r(&t, &tmbuf))
		{
			std::string fmtstr{fmt.begin(), fmt.end()};
			char fmtbuf[1024];

			fmtbuf[strftime(fmtbuf,
					sizeof(fmtbuf)-1,
					fmtstr.c_str(), &tmbuf)]=0;

			date_s=fmtbuf;
		}
	}
	return date_s;
}

namespace {

	struct addnewline {
		std::string s;

		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		addnewline &operator*()
		{
			return *this;
		}
		addnewline &operator++()
		{
			return *this;
		}
		addnewline &operator++(int)
		{
			return *this;
		}

		addnewline &operator=(std::string l)
		{
			if (!s.empty())
				s += " \n";
			s += std::move(l);

			return *this;
		}
	};
}

std::string rfc2045::reply::mksalutation(std::string_view salutation_template,
					 std::string_view newsgroup,
					 std::string_view message_id,
					 std::string_view newsgroups,
					 std::string_view sender_addr,
					 std::string_view sender_name,
					 std::string_view date,
					 std::string_view subject)
{
	std::string subj_decoded;

	rfc822::display_header(
		"subject",
		subject,
		charset,
		std::back_inserter(subj_decoded));

	std::string salutation;

	auto i=salutation_template.size();
	auto p=salutation_template.begin();
	for (; i; --i, ++p)
	{
		const char *fmt_start=0, *fmt_end=0;

		if (*p != '%' || i == 1)
		{
			salutation.push_back(*p);
			continue;
		}

		++p;
		--i;

		if (*p == '{')
		{
			fmt_start= ++p;
			--i;

			while (i)
			{
				if (*p == '}')
				{
					fmt_end=p;
					++p;
					--i;
					break;
				}
			}

			if (!fmt_end)
			{
				--p;
				++i;
				continue;
			}
		}

		if (!i)
			break;

		switch (*p)	{
		case 'n':
			salutation += "\n";
			break;
		case 'C':
			salutation += newsgroup;
			break;
		case 'i':
			salutation += message_id;
			break;
		case 'N':
			salutation += newsgroups;
			break;
		case 'f':
			salutation += sender_addr;
			break;
		case 'F':
			salutation += sender_name;
			break;
		case 'd':
			if (!date.empty())
				salutation += mksalutation_datefmt(
					{
						fmt_start,
						static_cast<size_t>(
							fmt_end-fmt_start
						)
					},
					date);
			break;
		case 's':
			salutation += subj_decoded;
			break;
		default:
			salutation.push_back(*p);
			break;
		}
	}

	addnewline adder;

	unicode::fromu_string_converter make_wrapped{
		adder,
		charset
	};

	rfc822::wrap_line_unicode wrapper{make_wrapped, 75};

	rfc822::display_header_unicode_lb do_wrap{wrapper, wrapper};
	bool errflag;

	unicode::iconvert::tou::convert(
		salutation.begin(),
		salutation.end(),
		charset,
		errflag,
		do_wrap
	);

	do_wrap.finish();
	wrapper.finish();

	return adder.s;
}

std::string rfc2045::reply::mkreferences(std::string_view oldref,
					 std::string_view oldmsgid)
{
	std::string buf;

	/* Create new references header */

	buf.reserve(oldref.size()+oldmsgid.size()+4);

	buf += oldref;
	buf += ",";
	buf += oldmsgid;

	/* Do wrapping the RIGHT way, by
	** RFC822 parsing the References: header
	*/

	rfc822::tokens tp{buf};
	rfc822::addresses ap{tp};

	/* Keep only the last 20 message IDs */

	size_t i=0;
	if (ap.size() > 20)	i=ap.size()-20;

	std::string s;

	for (; i < ap.size(); ++i)
	{
		if (ap[i].address.empty())
			continue;

		if (!s.empty())
			s += "            ";

		s += "<";
		ap[i].address.print(std::back_inserter(s));
		s += ">\n";
	}

	return s;
}

void rfc2045::reply::wrap_raw::wrap(std::string_view raw_header)
{
	const char *next_eolseq=nullptr;

	while (!raw_header.empty())
	{
		if (unicode_isspace(
			    static_cast<unsigned char>(raw_header[0])
		    ))
		{
			raw_header.remove_prefix(1);
			continue;
		}

		size_t s=70;

		if (raw_header.size() <= s)
			s=raw_header.size();
		else
		{
			while (s)
			{
				if (unicode_isspace(
					    static_cast<unsigned char>(
						    raw_header[s])))
				{
					while (unicode_isspace(
						       static_cast<
						       unsigned char>(
							       raw_header[s-1]
						       )))
						--s;
					break;
				}
				--s;
			}

			if (s == 0)
			{
				for (s=70; s<raw_header.size(); ++s)
				{
					if (unicode_isspace(
						    static_cast<unsigned char>(
							    raw_header[s])))
						break;
				}
			}
		}

		if (next_eolseq)
			out({next_eolseq});
		next_eolseq="\n  ";
		out({raw_header.data(), s});
		raw_header.remove_prefix(s);
	}
}
