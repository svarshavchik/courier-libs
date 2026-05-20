/*
** Copyright 2000-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/
#if    HAVE_CONFIG_H
#include       "rfc2045_config.h"
#endif
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	"rfc2045.h"
#include	<tuple>
#include	<string_view>

/*
** ---------------------------------------------------------------------
** Attempt to parse Content-Base: and Content-Location:, and return the
** "base" of all the relative URLs in the section.
** ---------------------------------------------------------------------
*/

static std::tuple<std::string_view, std::string_view> get_method_path(
	std::string_view p
)
{
	for (size_t i=0; i<p.size(); ++i)
	{
		if (p[i] == ':')
		{
			++i;
			return {p.substr(0, i), p.substr(i)};
		}

		if (!isalpha( (int)(unsigned char)p[i]))
			break;
	}

	return {std::string_view{}, p};
}

std::string rfc2045::append_url(std::string_view base,
				std::string_view location)
{
	auto [base_method, base_path]=get_method_path(base);
	auto [loc_method, loc_path]=get_method_path(location);

	if (!loc_method.empty())
	{
		return std::string{location.begin(), location.end()};
	}

	loc_method=base_method;

	std::string buf;

	buf.reserve(loc_method.size()+base_path.size()+loc_path.size()+3);

	buf.append(loc_method.begin(), loc_method.end());

	auto s=buf.size();

	buf.append(base_path.begin(), base_path.end());
	buf.push_back('/');

	if (loc_path.substr(0, 1) == "/")
	{
		size_t r;

		if (loc_path.substr(1, 1) == "/")
			/* Location is absolute */
		{
			buf.resize(s);
		}

		/* Relative to top of base */

		else if ( std::string_view{buf}.substr(s, 2) == "//" &&
			  (r=std::string_view{buf}.substr(s+2).find('/')) !=
			  std::string_view::npos )
		{
			buf.resize(s+2+r);
		}
		else
			buf.resize(s);
	}

	buf.append(loc_path.begin(), loc_path.end());

	return (buf);
}
