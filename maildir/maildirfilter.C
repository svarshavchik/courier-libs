/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"maildirfilter.h"
#include	"maildirfiltertypelist.h"
#include	"maildirgetquota.h"
#include	"mailbot.h"

#include	"numlib/numlib.h"
#include	<courier-unicode.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<errno.h>
#include	<sys/types.h>
#include	"maildirmisc.h"

#if HAVE_SYSEXITS_H
#include	<sysexits.h>
#else
#define	EX_NOPERM	77
#endif

#if	HAVE_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include	<pcre2.h>
#endif

#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include <charconv>
#include <fstream>

maildirfilterrule *maildir_filter_appendrule(maildirfilter &r,
					std::string_view name,
					enum maildirfiltertype type,
					int flags,
					std::string_view header,
					std::string_view value,
					std::string_view folder,
					std::string_view fromhdr,
					std::string charset,
					int &errcode)
{
	r.push_back({});

	auto &p=r.back();

	errcode=MF_ERR_INTERNAL;

	if (!maildir_filter_ruleupdate(
		r,
		p,
		name,
		type,
		flags,
		header,
		value,
		folder,
		fromhdr,
		charset,
		errcode
	))
	{
		r.pop_back();
		return (nullptr);
	}
	return (&p);
}

static bool maildir_filter_ruleupdate_utf8(
	maildirfilter &r,
	maildirfilterrule &p,
	std::string_view name,
	enum maildirfiltertype type,
	int flags,
	std::string_view header,
	std::string_view value,
	std::string_view folder,
	std::string_view fromhdr,
	int &errcode
);

bool maildir_filter_ruleupdate(
	maildirfilter &r,
	maildirfilterrule &p,
	std::string_view name,
	enum maildirfiltertype type,
	int flags,
	std::string_view header,
	std::string_view value,
	std::string_view folder,
	std::string_view fromhdr,
	std::string charset,
	int &errcode
)
{
	bool errflag;

	auto name_utf8=unicode::iconvert::convert(
		name,
		charset,
		unicode::utf_8,
		errflag
	);

	if (errflag)
	{
		errcode=MF_ERR_BADRULENAME;
		return false;
	}

	auto header_utf8=unicode::iconvert::convert(
		header,
		charset,
		unicode::utf_8,
		errflag
	);

	if (errflag)
	{
		errcode=MF_ERR_BADRULEHEADER;
		return false;
	}

	auto value_utf8=unicode::iconvert::convert(
		value,
		charset,
		unicode::utf_8,
		errflag
	);

	if (errflag)
	{
		errcode=MF_ERR_BADRULEVALUE;
		return false;
	}
	return maildir_filter_ruleupdate_utf8(
		r,
		p,
		name_utf8,
		type,
		flags,
		header_utf8,
		value_utf8,
		folder,
		fromhdr,
		errcode
	);
}

static bool maildir_filter_ruleupdate_utf8(
	maildirfilter &r,
	maildirfilterrule &p,
	std::string_view name,
	enum maildirfiltertype type,
	int flags,
	std::string_view header,
	std::string_view value,
	std::string_view folder,
	std::string_view fromhdr,
	int &errcode
)
{
/*
** Before creating a new rule, validate all input.
*/

	errcode=0;

	/* rule name: may not contain quotes or control characters. */
	errcode=MF_ERR_BADRULENAME;
	if (name.empty() || name.size() > 200)
		return false;

	for (char c : name)
		if ((unsigned char)c < ' ' || c == '\'' || c == '"' ||
			c == '`')
			return false;

	/* rule name: may not already exist */
	errcode=MF_ERR_EXISTS;

	for (auto &pom : r)
	{
	    if (&p!=&pom && name==pom.rulename_utf8)
		return false;
	}

	/* rule type: we must know what it is */

	switch (type)	{
	case startswith:
	case endswith:
	case contains:
	case hasrecipient:
	case mimemultipart:
	case textplain:
	case islargerthan:
	case anymessage:
		break;
	default:
		errcode=MF_ERR_BADRULETYPE;
		return false;
	} ;

	/* header: */

	errcode=MF_ERR_BADRULEHEADER;

	if (header.size() > 200)	return false;
	if (header.empty())
	{
		switch (type)	{
		case hasrecipient:
		case islargerthan:
		case mimemultipart:
		case textplain:
		case anymessage:
			break;
		case contains:
		case startswith:
		case endswith:
			if (flags & MFR_BODY)
				break;
			/* FALLTHRU */
		default:
			/* required */

			return false;
		}
	}
	else for (char c : header)
	{
		/* no control characters */
		if ((unsigned char)c <= ' ' || c == MDIRSEP[0] ||
		    c == '\'' ||
		    c == '\\' || c == '"' || c == '`' || c == '/')
			return false;
	}

	/* rule pattern */

	errcode=MF_ERR_BADRULEVALUE;

	if (value.size() > 200)	return false;
	if (value.empty())
	{
		switch (type)	{
		case mimemultipart:
		case textplain:
		case anymessage:
			break;
		default:
			/* required */

			return false;
		}
	}
	else if (!(flags & MFR_PLAINSTRING))
	{
		/*
		** Let PCRE decide if this is a valid pattern.
		**
		** One exception: the forward slash character, and some other
		** special characters, must always be escaped.
		*/

		for (auto c=value.data(); c < value.data()+value.size(); )
		{
			if (*c == '/' || *c == '$' || *c == '!'
				|| *c == '`' || (int)(unsigned char)*c < ' '
				|| *c == '\'' || *c == '"') return false;
						/* must be escaped */

			if (type == islargerthan)
			{
				if (!isdigit((int)(unsigned char)*c))
					return false;
			}

			if (*c == '(')
			{
				if (type == hasrecipient)	return false;
				++c;
				if (c >= value.data()+value.size() || *c == ')')	return false;
				continue;
			}
			if (*c == ')')
			{
				if (type == hasrecipient)	return false;
				++c;
				continue;
			}
			if (*c == '[')	/* This is a set */
			{
				if (type == hasrecipient)	return false;
				++c;

				char prevch=0;

				for (; c < value.data()+value.size(); c++)
				{
					if ((int)(unsigned char) *c < ' ')
						return false;

					if (prevch == '\\')
					{
						prevch=0;
						continue;
					}

					if (*c == '\'' || *c == '"' ||
						*c == '`')
						return false; /* must be quoted*/

					prevch=*c;

					if (prevch == ']')
						break;
				}

				if (c >= value.data()+value.size())
					return false;
				++c;
				continue;
			}

			if (*c == '\\')
			{
				if (type == hasrecipient)	return false;
				++c;
			}

			if (c >= value.data()+value.size())
				return false;
			++c;
		}

#if HAVE_PCRE2
		switch (type) {
		case contains:
		case startswith:
		case endswith:
			{
				int errcode;
				PCRE2_SIZE errindex;
				pcre2_code *pcre_regexp=
					pcre2_compile(
						reinterpret_cast<PCRE2_SPTR>(
							value.data()
						),
						value.size(),
						PCRE2_UTF,
						&errcode,
						&errindex,
						NULL);

				if (pcre_regexp == NULL)
					return false;
				pcre2_code_free(pcre_regexp);
			}
			break;
		default:
			break;
		}
#endif
	}

	/* validate FROM header */

	errcode=MF_ERR_BADFROMHDR;

	while (fromhdr.size() && fromhdr[0] &&
		unicode_isspace((int)(unsigned char)fromhdr[0]))
		fromhdr.remove_prefix(1);

	for (char ch:fromhdr)
		if ((int)(unsigned char)ch < ' ')
			return false;

	errcode=MF_ERR_BADRULEFOLDER;

	/* validate name of destination folder */

	if (folder.empty() || folder.size() > 200)
		return false;

	if (folder[0] == '*' || folder[0] == '!')
	{
		/* Forward, or bounce with an error */

		for (char ch:folder)
		{
			if (strchr("'\"$\\`;(){}#&<>~", ch) ||
				(unsigned char)ch < ' ')
				return false;
		}
	}
	else if (folder[0] == '+')	/* Autorespond */
	{
		maildir_filter_autoresp_info ai;

		if (!maildir_filter_autoresp_info_init_str(
			ai,
			folder.substr(1)))
			return false;
	}
	else if (folder == "exit")	/* Purge */
	{
	}
	else
	{
		if (folder != INBOX &&
			std::string_view{folder}.substr(0, sizeof(INBOX))
				!= INBOX ".")
			return false;

		auto s=maildir::name2dir(".", folder);

		if (s.empty())
			return false;
	}

	/* OK, we're good */

	errcode=MF_ERR_INTERNAL;

	p.rulename_utf8=std::string{name.begin(), name.end()};
	p.type=type;
	p.fieldname_utf8=std::string{header.begin(), header.end()};
	p.fieldvalue_utf8=std::string{value.begin(), value.end()};
	p.tofolder=std::string{folder.begin(), folder.end()};
	p.fromhdr=std::string{fromhdr.begin(), fromhdr.end()};
	p.flags=flags;
	return true;
}

static void print_pattern(std::ofstream &f, int flags, std::string_view v)
{
	if (!(flags & MFR_PLAINSTRING))
	{
		f << ((v.size() &&
			unicode_isspace((unsigned char)v[0])) ? "\\":"")
			<< v;
		return;
	}

	for (unsigned char ch:v)
	{
		if (((int)ch) <= 0x80 && !unicode_isalnum(ch))
			f << '\\';
		f << ch;
	}
}

bool maildir_filter_saverules(
	const maildirfilter &r,
	const std::string &filename,
	std::string_view maildirpath,
	std::string_view fromaddr
)
{
	std::ofstream f{filename};
	if (!f) return false;

	f << "#MFMAILDROP=2\n"
	     "#\n"
	     "# DO NOT EDIT THIS FILE.  This is an automatically"
	     " generated filter.\n"
	     "\n";

	f << "FROM='";
	for (char ch:fromaddr)
	{
		if (ch == '\'' || ch == '\\')
			f << '\\';
		f << ch;
	}
	f << "'\n";

	for (const auto &p:r)
	{
		f << "##Op:" << typelist[p.type].name << "\n";
		f << "##Header:" << p.fieldname_utf8 << "\n";
		f << "##Value:" << p.fieldvalue_utf8 << "\n";
		f << "##Folder:" <<
			(p.tofolder == INBOX ? ".":
			std::string_view{p.tofolder}.substr(0, sizeof(INBOX)) == INBOX "."
			? std::string_view{p.tofolder}.substr(p.tofolder.find('.')):
			std::string_view{p.tofolder})
			<< "\n";
		f << "##From:" << p.fromhdr << "\n";

		if (p.flags & MFR_PLAINSTRING)
			f << "##PlainString\n";
		if (p.flags & MFR_DOESNOT)
			f << "##DoesNot\n";
		if (p.flags & MFR_BODY)
			f << "##Body\n";
		if (p.flags & MFR_CONTINUE)
			f << "##Continue\n";

		f << "##Name:" << p.rulename_utf8 << "\n\n";

		f << "\nif (";

		if (p.flags & MFR_DOESNOT)
			f << "!";
		f << "(";

		switch (p.type)	{
		case startswith:
			if (p.flags & MFR_BODY)
			{
				f << "/^";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "/:b";
			}
			else
			{
				f << "/^" << p.fieldname_utf8 << ": *";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "/";
			}
			break;
		case endswith:
			if (p.flags & MFR_BODY)
			{
				f << "/";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "$/:b";
			}
			else
			{
				f << "/^" << p.fieldname_utf8 << ":.*";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "$/";
			}
			break;
		case contains:
			if (p.flags & MFR_BODY)
			{
				f << "/";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "/:b";
			}
			else
			{
				f << "/^" << p.fieldname_utf8 << ":.*";
				print_pattern(f, p.flags, p.fieldvalue_utf8);
				f << "/";
			}
			break;
		case hasrecipient:
			f << "hasaddr(\"" << p.fieldvalue_utf8 << "\")";
			break;
		case mimemultipart:
			f << "/^Content-Type: *multipart\\/mixed/";
			break;
		case textplain:
			f << " (! /^Content-Type:/) || "
					"/^Content-Type: *text\\/plain$/ || "
					"/^Content-Type: *text\\/plain;/";
			break;
		case islargerthan:
			f << "$SIZE > " << p.fieldvalue_utf8;
			break;
		case anymessage:
			f << "1";
			break;
		}
		f << "))\n"
			"{\n";

		if (*p.tofolder.c_str() == '!')
		{
			f << "    " << (p.flags & MFR_CONTINUE ? "cc":"to")
				<< " \"| $SENDMAIL -f \" '\"\"' \" " << p.tofolder.substr(1)
				<< "\"\n";
		}
		else if (*p.tofolder.c_str() == '*')
		{
			f << "    echo \"" << p.tofolder.substr(1) << "\"\n"
				"    EXITCODE=" << EX_NOPERM << "\n"
				"    exit\n";
		}
		else if (*p.tofolder.c_str() == '+')
		{
			struct maildir_filter_autoresp_info ai;

			if (maildir_filter_autoresp_info_init_str(
				ai,
				std::string_view{p.tofolder}.substr(1)))
			{
				if (!p.fromhdr.empty())
				{
					f << "    AUTOREPLYFROM='";

					for (auto cp : p.fromhdr)
					{
						if (cp == '\'' || cp == '\\')
							f << '\\';
						f << cp;
					}
					f << "'\n";
				}
				else
					f << "    AUTOREPLYFROM=\"$FROM\"\n"
						;

				f << "   `" << MAILBOT << " -A \"X-Sender: $FROM\""
					" -A \"From: $AUTOREPLYFROM\"";
				if (ai.mode ==
				    MAILDIR_FILTER_AUTORESP_MODE_DSN)
					f << " -M \"$FROM\"";
				f << " -m \"" << maildirpath << "/autoresponses/" << ai.name << "\"";
				if (ai.mode ==
				    MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE)
					f << " -N";
				if (ai.days > 0)
					f << " -d \"" << maildirpath << "/autoresponses/"
						<< ai.name << ".dat\" -D " << ai.days;
				f << " $SENDMAIL -t -f \"\"`\n"
					"    if ($RETURNCODE != 0)\n"
					"    {\n"
					"      EXITCODE=$RETURNCODE\n"
					"      exit\n"
					"    }\n"
					;
			}
		}
		else if (p.tofolder == "exit")
		{
			f << "    exit\n";
		}
		else
		{
			auto s=maildir::name2dir(maildirpath, p.tofolder);

			if (s.empty())
				f << "  # INTERNAL ERROR in "
					"maildir::name2dir\n";
			else
			{
				f << "   " << (p.flags & MFR_CONTINUE ? "cc":"to")
					<< " \"" << s << "/.\"\n";
			}
		}
		f << "}\n\n";
	}
	f.flush();
	f << "to \"" << maildirpath << "/.\"\n"
		<< std::flush;
	f.close();
	if (f.fail())
		return false;
	if (chmod(filename.c_str(), 0600))
		return false;

	return true;
}

int maildir_filter_loadrules(maildirfilter &r, const std::string &filename)
{
	std::ifstream f{filename};
	if (!f) return MF_LOADNOTFOUND;

	enum	maildirfiltertype new_type;
	std::string	new_header;
	std::string	new_value;
	std::string	new_folder;
	std::string	new_autoreplyfrom;

	int	flags=0;

	std::string buf;

	if (!std::getline(f, buf) ||
		std::string_view{buf}.substr(0, 12) != "#MFMAILDROP=")
	{
		return (MF_LOADFOREIGN);
	}

	if (std::from_chars(buf.data()+12, buf.data()+buf.size(), flags).ec
		!= std::errc{} || (flags != 1 && flags != 2))
	{
		return (MF_LOADFOREIGN);
	}

	new_type=contains;
	flags=0;

	while ( std::getline(f, buf))
	{
		int	i;

		if (std::string_view{buf}.substr(0, 2) != "##")	continue;
		std::string_view p{buf};
		p.remove_prefix(2);
		while ( !p.empty() && unicode_isspace((unsigned char)*p.data()))
			p.remove_prefix(1);

		size_t colon=p.find(':');
		if (colon == std::string_view::npos)
			colon=p.size();
		std::string_view header_name=p.substr(0, colon);
		if (colon < p.size())
			++colon;
		std::string_view header_value=p.substr(colon);

		while (!header_value.empty() &&
			unicode_isspace((unsigned char)*header_value.data()))
			header_value.remove_prefix(1);

		if (header_name.size() == 4 &&
			std::equal(header_name.begin(), header_name.end(),
				"from",
				[](unsigned char a, unsigned char b)
				{
					return unicode_lc(a)==b;
				}))
		{
			new_autoreplyfrom={header_value.begin(),
				header_value.end()};
			continue;
		}

		if (header_name.size() == 2 &&
			std::equal(header_name.begin(), header_name.end(), "op",
				[](unsigned char a, unsigned char b)
				{
					return unicode_lc(a)==b;
				}))
		{
			for (i=0; typelist[i].name; i++)
				if (strlen(typelist[i].name) ==
					header_value.size() &&
				    std::equal(header_value.begin(),
					header_value.end(),
					typelist[i].name,
					[](unsigned char a, char32_t b)
					{
						return unicode_lc(a)==b;
					}))
					break;
			if (!typelist[i].name)
			{
				return (MF_LOADFOREIGN);
			}
			new_type=typelist[i].type;
			continue;
		}

		if (header_name.size() == 6 &&
			std::equal(header_name.begin(), header_name.end(),
				"header",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			new_header={header_value.begin(),
				header_value.end()};
			continue;
		}

		if (header_name.size() == 5 &&
			std::equal(header_name.begin(), header_name.end(),
				"value",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			new_value={header_value.begin(),
				header_value.end()};
			continue;
		}

		if (header_name.size() == 6 &&
			std::equal(header_name.begin(), header_name.end(),
				"folder",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			if (*header_value.data() == '.')
			{
				new_folder=INBOX;
			}
			else
				new_folder.clear();

			if (header_value != ".")
				new_folder += header_value;
			continue;
		}

		if (header_name.size() == 11 &&
			std::equal(header_name.begin(), header_name.end(),
				"plainstring",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			flags |= MFR_PLAINSTRING;
			continue;
		}

		if (header_name.size() == 7 &&
			std::equal(header_name.begin(), header_name.end(),
				"doesnot",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			flags |= MFR_DOESNOT;
			continue;
		}

		if (header_name.size() == 8 &&
			std::equal(header_name.begin(), header_name.end(),
				"continue",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			flags |= MFR_CONTINUE;
			continue;
		}

		if (header_name.size() == 4 &&
			std::equal(header_name.begin(), header_name.end(),
				"body",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
			flags |= MFR_BODY;
			continue;
		}

		if (header_name.size() == 4 &&
			std::equal(header_name.begin(), header_name.end(),
				"name",
				[](unsigned char a, char32_t b)
				{
					return unicode_lc(a)==b;
				}))
		{
		int dummy;

			if (!maildir_filter_appendrule(
				r,
				header_value,
				new_type,
				flags,
				new_header,
				new_value,
				new_folder,
				new_autoreplyfrom,
				"utf-8",
				dummy))
			{
				return (dummy);
			}
			new_type=contains;
			new_header.clear();
			new_value.clear();
			new_folder.clear();
			new_autoreplyfrom.clear();
			flags=0;
		}
	}
	return (MF_LOADOK);
}

bool maildir_filter_autoresp_info_init_str(
	maildir_filter_autoresp_info &i,
	std::string_view c)
{
	auto n=c.find_first_not_of(" \t\r\n");

	if (n == c.npos)
	{
		errno=EINVAL;
		return (false);
	}
	c.remove_prefix(n);

	auto m=c.find_first_of(" \t\r\n");
	if (m == c.npos)
	{
		m=c.size();
	}
	auto word=c.substr(0, m);
	i.name=std::string{word.begin(), word.end()};
	c.remove_prefix(m);

	while (!c.empty())
	{
		switch (c[0]) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			c.remove_prefix(1);
			continue;
		}

		m=c.find_first_of(" \t\r\n");
		if (m == c.npos)
		{
			m=c.size();
		}
		word=c.substr(0, m);
		c.remove_prefix(m);

		int n;
		if (word.substr(0, 4) == "dsn=" &&
			std::from_chars(word.substr(4).data(),
				word.substr(4).data()+word.substr(4).size(),
				n).ec == std::errc{} && n)
			i.mode=MAILDIR_FILTER_AUTORESP_MODE_DSN;
		else if (word.substr(0, 5) == "days=" &&
			std::from_chars(word.substr(5).data(),
				word.substr(5).data()+word.substr(5).size(),
				i.days).ec == std::errc{})
			;
		else if (word == "noquote")
			i.mode=MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE;
	}
	return (true);
}

std::string maildir_filter_autoresp_info_asstr(maildir_filter_autoresp_info &i)
{
	char days_buf[NUMBUFSIZE+1];

	const char *mode_arg="";
	const char *days1_arg="";
	const char *days2_arg="";

	switch (i.mode) {
	case MAILDIR_FILTER_AUTORESP_MODE_DSN:
		mode_arg=" dsn=1";
		break;
	case MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE:
		mode_arg=" noquote";
		break;
	}

	if (i.days > 0)
	{
		*std::to_chars(days_buf, days_buf+NUMBUFSIZE,
			i.days).ptr=0;
		days1_arg=" days=";
		days2_arg=days_buf;
	}

	std::string s;
	s.reserve(i.name.size()+1+strlen(mode_arg)+strlen(days1_arg)+
		strlen(days2_arg));
	s += i.name;
	s += mode_arg;
	s += days1_arg;
	s += days2_arg;

	return (s);
}
