#ifndef testsuitecpp_H
#define testsuitecpp_H

#include "rfc2045.h"
#include <string_view>
#include <string>
#include <iostream>
#include <iomanip>
#include <vector>
#include <stdlib.h>

struct parsed_mime_info {
	size_t startpos, endpos, startbody, endbody,
		nlines, nbodylines;
	bool mime1;
	rfc2045::entity::errors_t errors;
	const char *content_type;
	const char *content_type_charset;
	const char *content_type_boundary;
	rfc2045::cte content_transfer_encoding;
	bool has8bitheader;
	bool has8bitbody;
	bool has8bitcontentchar;
	bool haslongquotedline;

	std::vector<parsed_mime_info> subentities;

	bool operator!=(const rfc2045::entity &e) const
	{
		return ! operator==(e);
	}

	bool operator==(const rfc2045::entity &entity) const
	{
		if (subentities.size() != entity.subentities.size())
			return false;

		auto p=subentities.begin();

		for (auto &se:entity.subentities)
		{
			if (*p != se)
				return false;
			++p;
		}

		return entity.startpos == startpos &&
			entity.endpos == endpos &&
			entity.startbody == startbody &&
			entity.endbody == endbody &&
			entity.nlines == nlines &&
			entity.nbodylines == nbodylines &&
			entity.mime1 == mime1 &&
			entity.errors == errors &&
			entity.content_type == content_type &&
			entity.content_type_charset
			== content_type_charset &&
			entity.content_type_boundary
			== content_type_boundary &&
			entity.content_transfer_encoding
			== content_transfer_encoding &&
			entity.has8bitheader == has8bitheader &&
			entity.has8bitbody   == has8bitbody &&
			entity.has8bitcontentchar == has8bitcontentchar
			&&
			entity.haslongquotedline == haslongquotedline;
	}

#if UPDATE_TESTSUITECPP

	static void dump_entity(const rfc2045::entity &entity,
				std::string_view line_pfix)
	{
		std::string errcodes;

		if (entity.errors & RFC2045_ERR8BITCONTENT)
			errcodes += "|RFC2045_ERR8BITCONTENT";
		if (entity.errors & RFC2045_ERR2COMPLEX)
			errcodes += "|RFC2045_ERR2COMPLEX";
		if (entity.errors & RFC2045_ERRBADBOUNDARY)
			errcodes += "|RFC2045_ERRBADBOUNDARY";
		if (entity.errors & RFC2045_ERR8BITINQP)
			errcodes += "|RFC2045_ERR8BITINQP";
		if (entity.errors & RFC2045_ERRBADHEXINQP)
			errcodes += "|RFC2045_ERRBADHEXINQP";
		if (entity.errors & RFC2045_ERRUNDECLARED8BIT)
			errcodes += "|RFC2045_ERRUNDECLARED8BIT";
		if (entity.errors & RFC2045_ERRWRONGBOUNDARY)
			errcodes += "|RFC2045_ERRWRONGBOUNDARY";

		if (entity.errors & RFC2045_ERRFATAL)
			errcodes += "|RFC2045_ERRFATAL";

		if (errcodes.empty())
			errcodes="0";
		else
			errcodes.erase(errcodes.begin(),
				       errcodes.begin()+1);

		std::cout << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.startpos
			  << ", // startpos\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.endpos
			  << ", // endpos\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.startbody
			  << ", // startbody\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.endbody
			  << ", // endbody\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.nlines
			  << ", // nlines\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.nbodylines
			  << ", // nbodylines\n" << line_pfix
			  << std::left << std::setw(5) << std::setfill(' ')
			  << entity.mime1
			  << ", // mime1\n" << line_pfix
			  << errcodes
			  << ", \"" << entity.content_type
			  << "\", \"" << entity.content_type_charset
			  << "\",\n" << line_pfix
			  << "\"" << entity.content_type_boundary
			  << "\", ";

		switch (entity.content_transfer_encoding) {
		case rfc2045::cte::sevenbit:
			std::cout << "cte::sevenbit";
			break;
		case rfc2045::cte::eightbit:
			std::cout << "cte::eightbit";
			break;
		case rfc2045::cte::qp:
			std::cout << "cte::qp";
			break;
		case rfc2045::cte::base64:
			std::cout << "cte::base64";
			break;
		case rfc2045::cte::error:
			std::cout << "error\n";
			break;
		}
		std::cout << ",\n" << line_pfix
			  << "    " << entity.has8bitheader
			  << ", // has8bitheader\n" << line_pfix
			  << "    " << entity.has8bitbody
			  << ", // has8bitbody\n" << line_pfix
			  << "    " << entity.has8bitcontentchar
			  << ", // has8bitcontentchar\n" << line_pfix
			  << "    " << entity.haslongquotedline;

		if (entity.subentities.empty())
		{
			std::cout << "  // haslongquotedline\n";
		}
		else
		{
			std::cout << ", // haslongquotedline\n"
				  << line_pfix << "{\n";

			std::string next_sep;

			next_sep.reserve(line_pfix.size()+1);
			next_sep.insert(next_sep.begin(),
					line_pfix.begin(),
					line_pfix.end());
			next_sep += "\t";

			const char *sep="";
			for (const auto &s:entity.subentities)
			{
				std::cout << sep
					  << line_pfix << "    {\n";
				dump_entity(s, next_sep);
				std::cout << line_pfix << "    }";
				sep=",\n";
			}
			std::cout << "\n" << line_pfix << "}\n";
		}
	}
#else
	static void dump(const rfc2045::entity &entity,
			 std::string_view line_pfix="")
	{
		std::string nl;

		nl.reserve(line_pfix.size()+2);
		nl="\n";
		nl += line_pfix;

		std::cout << line_pfix <<
			"    startpos: " << entity.startpos
			  << nl << "      endpos: " << entity.endpos
			  << nl << "   startbody: " << entity.startbody
			  << nl << "     endbody: " << entity.endbody
			  << nl << "      nlines: " << entity.nlines
			  << nl << "  nbodylines: " << entity.nbodylines
			  << nl << "        mime: " << entity.mime1
			  << nl << "      errors: " << entity.errors
			  << nl << "content-type: "
			  << entity.content_type
			  << nl << "     charset: "
			  << entity.content_type_charset
			  << nl << "    boundary: "
			  << entity.content_type_boundary
			  << nl << "    encoding: ";

		switch (entity.content_transfer_encoding) {
		case rfc2045::cte::error:
			std::cout << "ERROR";
			break;
		case rfc2045::cte::sevenbit:
			std::cout << "7bit";
			break;
		case rfc2045::cte::eightbit:
			std::cout << "8bit";
			break;
		case rfc2045::cte::base64:
			std::cout << "base64";
		case rfc2045::cte::qp:
			std::cout << "quoted-printable";
			break;
		}
		std::cout << nl << " 8bit header: "
			  << entity.has8bitheader
			  << nl << "   8bit body: "
			  << entity.has8bitbody
			  << nl << "8bit content: "
			  << entity.has8bitcontentchar
			  << nl << " long quoted: "
			  << entity.haslongquotedline
			  << "\n";

		if (entity.subentities.size())
		{
			std::string sep{line_pfix};

			sep += std::string(40, '=');

			nl[0]='\t';
			nl.push_back('\t');

			for (const auto &se:entity.subentities)
			{
				std::cout << sep << "\n";
				dump(se, nl);

				sep=nl+std::string(32, '-');
			}
		}
	}
#endif
};
#endif
