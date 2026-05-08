/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc822hdr.h"
#include	"rfc822/rfc2047.h"
#include	<courier-unicode.h>
#include	"numlib/numlib.h"
#include	"searchinfo.h"
#include	"imapwrite.h"
#include	"imaptoken.h"


extern time_t rfc822_parsedt(const char *);
extern imapscaninfo current_maildir_info;
extern char *current_mailbox;

extern bool get_flagname(std::string s, struct imapflags *flags);
void get_message_flags( struct imapscanmessageinfo *,
			char *, struct imapflags *);
extern bool valid_keyword(const std::string &kw);

static void fill_search_preparse(searchiter);

static void fill_search_veryquick(searchiter,
				  unsigned long, struct imapflags *);

static void fill_search_quick(searchiter, unsigned long, struct stat *);

static int search_evaluate(searchiter);

/*
**	search_internal() does the main heavylifting of searching the
**	maildir for qualifying messages.  It calls a callback function
**	when a matching message is found.
**
**	For a plain SEARCH, the callback function merely prints the message
**	number.
*/

void contentsearch::search_internal(searchiter si,
				    const std::string &charset,
				    search_callback_t callback_func)
{
	searchiter p;

	for (p=searchlist.begin(); p != searchlist.end(); ++p)
		fill_search_preparse(p);

	/* Shortcuts for keyword-based searches */

	if (si->type == search_msgkeyword && si->bs.empty() &&
	    si->search_keyword)
	{
		search_byKeyword(searchlist.end(), si, charset, callback_func);
	}
	else if (si->type == search_and &&
		 si->a->type == search_msgkeyword && si->a->bs.empty()
		 && si->a->search_keyword)
	{
		search_byKeyword(si->b, si->a, charset, callback_func);
	}
	else for (size_t i=0; i<current_maildir_info.msgs.size(); i++)
		search_oneatatime(si, i, charset, callback_func);
}

void contentsearch::search_byKeyword(searchiter tree,
				     searchiter keyword,
				     const std::string &charset,
				     search_callback_t callback_func)
{
	for (auto &msg:current_maildir_info.msgs)
		msg.found_in_search=false;

	current_maildir_info.keywords->messages(
		keyword->as,
		[&]
		(const keyword_meta &meta)
		{
			current_maildir_info.msgs.at(meta.index)
				.found_in_search=true;
		}
	);

	for (size_t n=0; n<current_maildir_info.msgs.size(); n++)
	{
		if (!current_maildir_info.msgs[n].found_in_search)
			continue;

		if (tree==searchlist.end())
		{
			callback_func(n);
		}
		else
		{
			search_oneatatime(tree, n, charset, callback_func);
		}
	}
}

/*
** Evaluate the search tree for a given message.
*/

void contentsearch::search_oneatatime(searchiter si,
				      unsigned long i,
				      const std::string &charset,
				      search_callback_t callback_func)
{
	searchiter p;
	imapflags	flags;
	struct	stat	stat_buf;
	int	rc;

	{
		for (p=searchlist.begin(); p != searchlist.end(); ++p)
			p->value= -1;	/* Search result unknown */

		/* First, see if non-content search will be sufficient */

		get_message_flags(&current_maildir_info.msgs.at(i), 0, &flags);

		for (p=searchlist.begin(); p != searchlist.end(); ++p)
			fill_search_veryquick(p, i, &flags);

		if ((rc=search_evaluate(si)) >= 0)
		{
			if (rc > 0)
				callback_func(i);
			return;
		}

		rfc822::fdstreambuf fp{
			imapscan_openfile(&current_maildir_info, i)
		};

		if (fp.error() || fstat(fp.fileno(), &stat_buf))
			return;

		/* First, see if non-content search will be sufficient */

		for (p=searchlist.begin(); p != searchlist.end(); ++p)
			fill_search_quick(p, i, &stat_buf);

		if ((rc=search_evaluate(si)) < 0)
		{
			/* No, search the headers then */

			rfc2045::entity message;
			{
				std::istreambuf_iterator<char> b{&fp}, e;
				rfc2045::entity::line_iter<false>::iter parser{
					b, e
				};

				message.parse(parser);
			}

			fill_search_header(charset, message, fp,
					   &current_maildir_info.msgs.at(i));
			rc=search_evaluate(si);

			if (rc < 0)
			{
				/* Ok, search message contents */
				fill_search_body(message, fp,
						 &current_maildir_info.msgs.at(i));

				/*
				** If there are still UNKNOWN nodes, change
				** them to fail.
				*/

				for (p=searchlist.begin();
				     p != searchlist.end(); ++p)
					if (p->value < 0)
						p->value=0;

				rc=search_evaluate(si);
			}
		}

		if (rc > 0)
		{
			callback_func(i);
		}
	}
}

/* Check if the given index is included in the specified message set */

static bool is_in_set(const char *msgset, unsigned long n)
{
unsigned long i, j;

	while (isdigit((int)(unsigned char)*msgset))
	{
		i=0;
		while (isdigit((int)(unsigned char)*msgset))
		{
			i=i*10 + (*msgset++-'0');
		}
		if (*msgset != ':')
			j=i;
		else
		{
			j=0;
			++msgset;
			if (*msgset == '*')
			{
				++msgset;
				/*
				** Ok, we don't really need to know the upper
				** end, just hack it.
				*/
				j=i;
				if (j < n)
					j=n;
			}
			else
				while (isdigit((int)(unsigned char)*msgset))
				{
					j=j*10 + (*msgset++-'0');
				}
		}
		if (n >= i && n <= j)	return (true);
		if (*msgset == 0 || *msgset++ != ',')	break;
	}
	return (false);
}

/*
** Search date comparisons compare the dates only, not the time.
** We convert all timestamps to midnight GMT on their respective dates.
** Use convenient RFC822 functions for that purpose.
*/

static bool decode_date(const std::string &p, time_t &tret)
{
	std::string s;

        /* Convert to format rfc822_parsedt likes */

	auto i=p.find(' ');

	if (i == p.npos)
		i=p.size();

	s.reserve(i+sizeof("00:00:00"));

	s=p.substr(0, i);
	s += " 00:00:00";
	while (i)
	{
		if (s[--i] == '-')
			s[i]=' ';
	}

	return rfc822_parsedate_chk(s.c_str(), &tret) == 0;
}

/* Given a time_t that falls on, say, 3-Aug-1999 9:50:43 local time,
** calculate the time_t for midnight 3-Aug-1999 UTC.  Search date comparisons
** are done against midnight UTCs */

static time_t timestamp_to_day(time_t t)
{
char	buf1[60], buf2[80];

	rfc822_mkdate_buf(t, buf1);	/* Converts to local time */
	(void)strtok(buf1, " ");	/* Skip weekday */
	strcpy(buf2, strtok(0, " "));
	strcat(buf2, " ");
	strcat(buf2, strtok(0, " "));
	strcat(buf2, " ");
	strcat(buf2, strtok(0, " "));
	strcat(buf2, " 00:00:00");

	rfc822_parsedate_chk(buf2, &t);

	return t;
}

static std::string timestamp_for_sorting(time_t t)
{
	struct tm *tm=localtime(&t);
	char	buf[200];

	buf[0]=0;
	if ( strftime(buf, sizeof(buf), "%Y.%m.%d.%H.%M.%S", tm) == 0)
		buf[0]=0;

	return buf;
}

static void fill_search_preparse(searchiter p)
{
	switch (p->type) {
	case search_msgflag:
		{
			imapflags flags;

			p->search_keyword=false;

			if (get_flagname(p->as, &flags))
			{
				p->bs.clear();
				p->bs.insert(p->bs.end(),
					     reinterpret_cast<char *>(&flags),
					     reinterpret_cast<char *>(&flags+1)
				);
			}
		}
		break;

	case search_msgkeyword:
		p->search_keyword=valid_keyword(p->as);
		break;
	default:
		break;
	}
}

/* Evaluate non-content search nodes */

static void fill_search_veryquick(searchiter p,
	unsigned long msgnum, struct imapflags *flags)
{
	switch (p->type) {
	case search_msgflag:
		{
			p->value=0;
			if (p->as == "\\RECENT" &&
				current_maildir_info.msgs[msgnum].recentflag)
				p->value=1;

			if (!p->bs.empty())
			{
				imapflags *f=reinterpret_cast<imapflags *>(
					&p->bs[0]
				);

				if (f->seen && flags->seen)
					p->value=1;
				if (f->answered && flags->answered)
					p->value=1;
				if (f->deleted && flags->deleted)
					p->value=1;
				if (f->flagged && flags->flagged)
					p->value=1;
				if (f->drafts && flags->drafts)
					p->value=1;
			}
			break;
		}

	case search_msgkeyword:
		p->value=0;
		if (p->search_keyword)
		{
			current_maildir_info.msgs.at(
				msgnum
			).keywords.enumerate(
				[&]
				(const std::string &keyword)
				{
					if (mail::keywords::keywordeq{
						}(keyword, p->as))
						p->value=1;
				});
		}
		break;
	case search_messageset:
		if (is_in_set(p->as.c_str(), msgnum+1))
			p->value=1;
		else
			p->value=0;
		break;
	case search_all:
		p->value=1;
		break;
	case search_uid:
		if (is_in_set(p->as.c_str(),
			      current_maildir_info.msgs[msgnum].uid))
			p->value=1;
		else
			p->value=0;
		break;
	case search_reverse:
		p->value=1;
		break;
	default:
		break;
	}
}

static void fill_search_quick(searchiter p,
	unsigned long msgnum, struct stat *stat_buf)
{
	switch (p->type)	{
	case search_before:
		p->value=0;
		{
			time_t t;

			if (decode_date(p->as, t) &&
			    timestamp_to_day(stat_buf->st_mtime) < t)
				p->value=1;
		}
		break;
	case search_since:
		p->value=0;
		{
			time_t t;

			if (decode_date(p->as, t) &&
			    timestamp_to_day(stat_buf->st_mtime) >= t)
				p->value=1;
		}
		break;
	case search_on:
		p->value=0;
		{
			time_t t;

			if (decode_date(p->as, t) &&
			    timestamp_to_day(stat_buf->st_mtime) == t)
				p->value=1;
		}
		break;
	case search_smaller:
		p->value=0;
		{
			unsigned long n;

			if (sscanf(p->as.c_str(), "%lu", &n) > 0 &&
			    (unsigned long)stat_buf->st_size < n)
				p->value=1;
		}
		break;
	case search_larger:
		p->value=0;
		{
			unsigned long n;

			if (sscanf(p->as.c_str(), "%lu", &n) > 0 &&
			    (unsigned long)stat_buf->st_size > n)
				p->value=1;
		}
		break;
	case search_orderedsubj:
	case search_references1:
	case search_references2:
	case search_references3:
	case search_references4:
	case search_arrival:
	case search_cc:
	case search_date:
	case search_from:
	case search_reverse:
	case search_size:
	case search_to:

		/* DUMMY nodes for SORT/THREAD.  Make sure that the
		** dummy node is CLEARed */

		p->as.clear();
		p->bs.clear();

		switch (p->type)	{
		case search_arrival:
			p->as=timestamp_for_sorting(stat_buf->st_mtime);
			p->value=1;
			break;
		case search_size:
			{
			char	buf[NUMBUFSIZE], buf2[NUMBUFSIZE];
			char *q;

				libmail_str_size_t(stat_buf->st_size, buf);
				sprintf(buf2, "%*s", (int)(sizeof(buf2)-1), buf);
				for (q=buf2; *q == ' '; *q++='0')
					;
				p->as=buf2;
				p->value=1;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/* Evaluate search results.  Returns: 0 - false, 1 - true, -1 - unknown
** (partial search on message metadata, like size or flags, in hopes of
** preventing a search
** of message contents).
*/

static int search_evaluate(searchiter si)
{
int	rc, rc2;

	switch (si->type)	{
	case search_orderedsubj:	/* DUMMIES for THREAD and SORT */
	case search_references1:
	case search_references2:
	case search_references3:
	case search_references4:
        case search_arrival:
        case search_cc:
        case search_date:
        case search_from:
        case search_reverse:
        case search_size:
        case search_to:
		rc = search_evaluate(si->a);
		if (rc == 0) return 0;
		if (si->value < 0)  return (-1);
		break;
	case search_not:
		rc=search_evaluate(si->a);
		if (rc >= 0)	rc= 1-rc;
		break;
	case search_and:
		rc=search_evaluate(si->a);
		rc2=search_evaluate(si->b);

		rc=  rc > 0 && rc2 > 0 ? 1:
			rc == 0 || rc2 == 0 ? 0:-1;
		break;
	case search_or:
		rc=search_evaluate(si->a);
		rc2=search_evaluate(si->b);

		rc=  rc > 0 || rc2 > 0 ? 1:
			rc == 0 && rc2 == 0 ? 0:-1;
		break;
	default:
		rc=si->value;
		break;
	}
	return (rc);
}

/* ------- header search -------- */

struct fill_search_header_info {

	contentsearch &cs;

	fill_search_header_info(contentsearch &cs) : cs{cs} {}

	std::string utf8buf;
};

static bool headerfilter_func(
	std::string_view name,
	std::string_view value,
	fill_search_header_info &info
);

static int fill_search_header_utf8(
	const char *buf,
	size_t len,
	fill_search_header_info &info
);

static void fill_search_header_done(
	std::string_view name,
	fill_search_header_info &info
);

void contentsearch::fill_search_header(
	const std::string &charset,
	const rfc2045::entity &rfcp,
	rfc822::fdstreambuf &fp,
	struct imapscanmessageinfo *mi
)
{
	searchiter sip;
	struct fill_search_header_info decodeinfo{*this};

	/* Consider the following dummy nodes as evaluated */

	for (sip=searchlist.begin(); sip != searchlist.end(); ++sip)
		switch (sip->type) {
		case search_orderedsubj:
		case search_references1:
		case search_references2:
		case search_references3:
		case search_references4:
		case search_cc:
		case search_date:
		case search_from:
		case search_to:
			sip->value=1;
			break;
		default:
			break;
		}

	search_set_charset_conv(charset);

	rfc822::mime_decoder src{
		[&](const char *buf, size_t len)
		{
			return fill_search_header_utf8(buf, len, decodeinfo);
		},
		fp,
		unicode::utf_8
	};

	src.decode_body=false;
	src.headerfilter=[&](std::string_view name, std::string_view value)
	{
		return headerfilter_func(name, value, decodeinfo);
	};
	src.header_name_suppress=true;
	src.headerdone=[&](std::string_view name)
	{
		fill_search_header_done(name, decodeinfo);
	};

	src.decode<false>(rfcp);
}

static bool headerfilter_func(std::string_view name, std::string_view value,
			   fill_search_header_info &info)
{
	searchiter sip;
	bool isto=(name=="to");
	bool iscc=(name=="cc");
	bool isfrom=(name=="from");
	bool isinreplyto=(name=="in-reply-to");
	bool isdate=(name=="date");
	bool isreferences=(name=="references");
	bool ismessageid=(name=="message-id");

	for (sip=info.cs.searchlist.begin();
	     sip != info.cs.searchlist.end(); ++sip)
	{
		if (sip->type == search_text && sip->value <= 0)
		{
			/*
			** Full message search. Reset the search engine,
			** feed it "Headername: "
			*/

			sip->sei.reset();

			for (unsigned char c:name)
			{
				sip->sei << (char32_t)c;
				if (sip->sei)
					sip->value=1;
			}
			sip->sei << ':';
			sip->sei << ' ';

			if (sip->sei)
				sip->value=1;
		}

		if ( (sip->type == search_cc && iscc && sip->as.empty()) ||
		     (sip->type == search_from && isfrom && sip->as.empty()) ||
		     (sip->type == search_to && isto && sip->as.empty()) ||
		     (sip->type == search_references1 &&
		      isinreplyto && sip->bs.empty()))
		{
			rfc822::tokens t{value};
			rfc822::addresses a{t};
			std::string s;

			if (!a.empty())
			{
				a[0].address.display_address(
					unicode::utf_8,
					std::back_inserter(s)
				);
			}

			if (sip->type == search_references1)
			{
				sip->bs.reserve(s.size()+2);
				sip->bs="<";
				sip->bs += s;
				sip->bs += ">";
			}
			else
				sip->as=s;
		}

		switch (sip->type) {
		case search_orderedsubj:

			if (isdate && sip->bs.empty())
			{
				sip->bs=value;
			}
			break;

		case search_date:

			if (isdate && sip->as.empty())
			{
				auto msg_time=rfc822::parse_date(value);
				if (msg_time)
					sip->as=timestamp_for_sorting(
						*msg_time
					);
			}
			break;

		case search_sentbefore:
		case search_sentsince:
		case search_senton:

			if (sip->value > 0)
				break;

			if (isdate)
			{
				time_t given_time;

				if (!decode_date(sip->as, given_time))
					break;

				auto msg_time_opt=rfc822::parse_date(value);

				if (!msg_time_opt)
					break;

				auto &msg_time=*msg_time_opt;
				msg_time=timestamp_to_day(msg_time);
				sip->value=0;
				if ((sip->type == search_sentbefore &&
					msg_time < given_time) ||
					(sip->type == search_sentsince &&
						msg_time>=given_time))
					sip->value=1;

				if (sip->type == search_senton &&
					msg_time == given_time)
					sip->value=1;
			}
			break;

		case search_references1:
			if (isreferences && sip->as.empty())
			{
				sip->as=value;
			}
			break;
		case search_references2:
			if (isdate && sip->as.empty())
			{
				sip->as=value;
			}
			break;
		case search_references4:
			if (ismessageid && sip->as.empty())
			{
				sip->as=value;
			}
			break;
		default:
			break;
		}
	}
	info.utf8buf.clear();
	return 1;
}

static int fill_search_header_utf8(
	const char *buf,
	size_t len,
	fill_search_header_info &info
)
{
	info.utf8buf.insert(info.utf8buf.end(), buf, buf+len);
	return 0;
}

static void fill_search_header_done(
	std::string_view name,
	fill_search_header_info &info)
{
	searchiter sip;
	bool issubject=name == "subject";

	if (!info.utf8buf.empty() &&
	    info.utf8buf.back() == '\n')
		info.utf8buf.pop_back();

	for (sip=info.cs.searchlist.begin();
	     sip != info.cs.searchlist.end(); ++sip)
		switch (sip->type) {
		case search_references3:
			if (issubject && sip->as.empty())
			{
				sip->as=info.utf8buf;
			}
			break;
		case search_orderedsubj:

			if (issubject && sip->as.empty())
			{
				auto [p, flag]=rfc822::coresubj(
					info.utf8buf
				);
				sip->as=std::move(p);
			}
			break;
		case search_header:

			if (sip->cs.empty() || sip->cs != name)
				break;

			/* FALLTHRU */

		case search_text:
			if (sip->value > 0)
				break;

			sip->sei.reset();

			{
				auto ret=unicode::iconvert::tou::convert(
					info.utf8buf,
					unicode::utf_8
				);

				if (std::get<bool>(ret))
					break;

				for (auto uc:std::get<std::u32string>(ret))
				{
					sip->sei << uc;
				}

				sip->sei << ' ';

				if (sip->sei)
				{
					sip->value=1;
				}
			}
			break;
		default:
			break;
		}
}

static int fill_search_body_ucs4(
	const char *str, size_t n,
	contentsearch &cs
);

void contentsearch::fill_search_body(
	const rfc2045::entity &rfcp,
	rfc822::fdstreambuf &fp,
	struct imapscanmessageinfo *mi
)
{
	rfc822::mime_decoder decodersrc{
		[this]
		(const char *p, size_t len)
		{
			return fill_search_body_ucs4(p, len, *this);
		},
		fp,
		unicode_u_ucs4_native
	};

	searchiter sip;

	decodersrc.decode_header=false;

	for (sip=searchlist.begin(); sip != searchlist.end(); ++sip)
		if ((sip->type == search_text || sip->type == search_body)
		    && sip->value <= 0)
		{
			decodersrc.decode<false>(rfcp);
			break;
		}
}

static int fill_search_body_ucs4(
	const char *str, size_t n,
	contentsearch &cs
)
{
	searchiter sip;
	const char32_t *u=reinterpret_cast<const char32_t *>(str);
	int notfound=1;

	n /= sizeof(char32_t);

	for (sip=cs.searchlist.begin();
	     sip != cs.searchlist.end(); ++sip)
		if ((sip->type == search_text || sip->type == search_body)
		    && sip->value <= 0)
		{
			size_t i;

			notfound=0;

			for (i=0; i<n; i++)
			{
				sip->sei << u[i];

				if (sip->sei)
				{
					sip->value=1;
					break;
				}
			}
		}

	return notfound;
}
