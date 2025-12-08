/*
** Copyright 1998 - 2001 S. Varshavchik.  See COPYING for
** distribution information.
*/

#if	HAVE_CONFIG_H
#include "rfc2045_config.h"
#endif
#include	"rfc2045.h"

rfc2045::entity::autoconvert_meta::autoconvert_meta()
	: appid{"librfc2045"}
{
}

rfc2045::entity::autoconvert_meta::~autoconvert_meta()=default;
std::string_view rfc2045::entity::autoconvert_meta::rwheader(
	const entity &e,
	std::string_view lcname,
	std::string_view full_header)
{
	return full_header;
}

bool rfc2045::entity::autoconvert_check(convert rwmode)
{
	bool flag=false; // Flag - rewriting suggested

	if (content_type.value == "multipart/signed")
	{
		subentities.clear();
		return flag;
	}

	// hasnon7bit: 8bit chars in this section or subsections

	for (auto &e:subentities)
	{
		if (e.autoconvert_check(rwmode))	flag=true;

		// Make it easier to make decisions
		if (e.has8bitchars)
			has8bitchars=true;
		if (e.hasraw8bitchars)
			hasraw8bitchars=true;
	}

	if (!subentities.empty())
	{
		// If something inside needed to be rewritten, set
		// rewrite_transfer_encoding to 8bit.

		if (flag)
			rewrite_transfer_encoding=cte::eightbit;
	}
	else if (mime1)
	{
		if (!has_content_type_header)
		{
			flag=true;
		}

		if (std::string_view{content_type.value}.substr(0, 5)
		    == "text/")
		{
			if (content_type.parameters.try_emplace(
				    "charset",
				    "utf-8",
				    "en",
				    rfc2045_getdefaultcharset()
			    ).second)
				flag=true;
		}

		if (!has_content_transfer_encoding)
		{
			content_transfer_encoding=hasraw8bitchars ?
				cte::eightbit:cte::sevenbit;
			flag=true;
		}

		/* Check for conversions */

		auto te=content_transfer_encoding;

		bool is8bitte=false;

		switch (te) {
		case cte::base64:
		case cte::qp:
		case cte::sevenbit: // 7 bit contents
		case cte::error:
			break;

		case cte::eightbit:
			is8bitte=true;
			break;
		}

		// Transfer encoding of 8bit, but no 8bits
		if (is8bitte && !hasraw8bitchars)
		{
			rewrite_transfer_encoding=cte::sevenbit;
			flag=true;
		}

		if (rwmode == convert::sevenbit &&
		    (is8bitte || haslongline))
		{
			rewrite_transfer_encoding =
				haslongline || hasraw8bitchars
				? cte::qp:cte::sevenbit;
			flag=true;
		}
		else if (te == cte::qp &&
			 ((rwmode == convert::eightbit &&
			   !haslongline)
			  ||
			  (rwmode == convert::eightbit_always)))
		{
			rewrite_transfer_encoding=hasraw8bitchars ?
				cte::eightbit:cte::sevenbit;
			flag=true;
		}
	}
	return (flag);
}
