/*
** Copyright 1998 - 2010 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>
#include	<courier-unicode.h>
#include	"searchinfo.h"
#include	"imapwrite.h"
#include	"imaptoken.h"

searchinfo::searchinfo(std::list<searchinfo> &searchlist)
	: a{searchlist.end()}, b{searchlist.end()}
{
}

searchiter contentsearch::alloc_search()
{
	searchlist.emplace_front(searchlist);

	return searchlist.begin();
}

searchiter contentsearch::alloc_parsesearch()
{
	return alloc_search_andlist();
}

searchiter contentsearch::alloc_searchextra(searchiter top, search_type t)
{
	searchiter si;

	if (t == search_references1)
	{
		/* Automatically add third and second dummy node */

		top=alloc_searchextra(top, search_references4);
		top=alloc_searchextra(top, search_references3);
		top=alloc_searchextra(top, search_references2);
	}
	si=alloc_search();
	si->type=t;
	si->a=top;
	return (si);
}

searchiter contentsearch::alloc_search_andlist()
{
	searchiter si, a, b;
	struct imaptoken *t;

	si=alloc_search_notkey();
	if (si == searchlist.end())
		return si;

	while ((t=currenttoken())->tokentype != IT_RPAREN && t->tokentype !=
		IT_EOL)
	{
		if ((a=alloc_search_notkey()) == searchlist.end())
			return searchlist.end();
		b=alloc_search();
		b->type=search_and;
		b->a=si;
		b->b=a;
		si=b;
	}
	return (si);
}

searchiter contentsearch::alloc_search_notkey()
{
	struct imaptoken *t=currenttoken();

	if (t->tokentype == IT_ATOM && strcmp(t->tokenbuf, "NOT") == 0)
	{
		searchiter si=alloc_search();

		si->type=search_not;
		nexttoken();
		if ((si->a=alloc_search_key()) == searchlist.end())
			return (searchlist.end());
		return (si);
	}
	return (alloc_search_key());
}

searchiter contentsearch::alloc_search_key()
{
	struct imaptoken *t=currenttoken();
	searchiter si;
	const char *keyword;

	if (t->tokentype == IT_LPAREN)
	{
		nexttoken();
		if ((si=alloc_search_andlist()) == searchlist.end() ||
			currenttoken()->tokentype != IT_RPAREN)
			return (searchlist.end());
		nexttoken();
		return (si);
	}

	if (t->tokentype != IT_ATOM && t->tokentype != IT_NUMBER)
		return (searchlist.end());

	keyword=t->tokenbuf;

	if (strcmp(keyword, "ALL") == 0)
	{
	searchiter si;

		(si=alloc_search())->type=search_all;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "OR") == 0)
	{
	searchiter si;

		si=alloc_search();
		si->type=search_or;
		nexttoken();
		if ((si->a=alloc_search_notkey()) == searchlist.end()
		    ||
		    (si->b=alloc_search_notkey()) == searchlist.end())
			return (searchlist.end());
		return (si);
	}

	if (strcmp(keyword, "HEADER") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_header;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->cs=t->tokenbuf;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "BCC") == 0 ||
		strcmp(keyword, "CC") == 0 ||
		strcmp(keyword, "FROM") == 0 ||
		strcmp(keyword, "TO") == 0 ||
		strcmp(keyword, "SUBJECT") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_header;
		si->cs=keyword;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "BEFORE") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_before;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "BODY") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_body;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}
	if (strcmp(keyword, "LARGER") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_larger;
		t=nexttoken();
		if (t->tokentype != IT_NUMBER)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "ON") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_on;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "SENTBEFORE") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_sentbefore;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "SENTON") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_senton;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=keyword;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "SENTSINCE") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_sentsince;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "SINCE") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_since;
		t=nexttoken();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "SMALLER") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_smaller;
		t=nexttoken();
		if (t->tokentype != IT_NUMBER)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "TEXT") == 0)
	{
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_text;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "UID") == 0)
	{
	searchiter si;
	struct imaptoken *t;

		si=alloc_search();
		si->type=search_uid;
		t=nexttoken();
		if (!ismsgset(t))
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "KEYWORD") == 0
		|| strcmp(keyword, "UNKEYWORD") == 0)
	{
	int	isnot= *keyword == 'U';
	struct imaptoken *t;
	searchiter si;

		si=alloc_search();
		si->type=search_msgkeyword;
		t=nexttoken_okbracket();
		if (t->tokentype != IT_ATOM &&
		    t->tokentype != IT_NUMBER &&
		    t->tokentype != IT_QUOTED_STRING)
			return (searchlist.end());
		si->as=t->tokenbuf;
		nexttoken();

		if (isnot)
		{
		searchiter si2=alloc_search();

			si2->type=search_not;
			si2->a=si;
			si=si2;
		}
		return (si);
	}
	if (strcmp(keyword, "ANSWERED") == 0 ||
		strcmp(keyword, "DELETED") == 0 ||
		strcmp(keyword, "DRAFT") == 0 ||
		strcmp(keyword, "FLAGGED") == 0 ||
		strcmp(keyword, "RECENT") == 0 ||
		strcmp(keyword, "SEEN") == 0)
	{
	searchiter si;

		si=alloc_search();
		si->type=search_msgflag;
		si->as.reserve(strlen(keyword)+1);
		si->as="\\";
		si->as += keyword;
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "UNANSWERED") == 0 ||
		strcmp(keyword, "UNDELETED") == 0 ||
		strcmp(keyword, "UNDRAFT") == 0 ||
		strcmp(keyword, "UNFLAGGED") == 0 ||
		strcmp(keyword, "UNSEEN") == 0)
	{
	searchiter si;
	searchiter si2;

		si=alloc_search();
		si->type=search_msgflag;
		si->as.reserve(strlen(keyword));
		si->as="\\";
		si->as += keyword+2;
		nexttoken();

		si2=alloc_search();
		si2->type=search_not;
		si2->a=si;
		return (si2);
	}

	if (strcmp(keyword, "NEW") == 0)
	{
		searchiter si, si2;

		si=alloc_search();
		si->type=search_and;
		si2=si->a=alloc_search();
		si2->type=search_msgflag;
		si2->as="\\RECENT";
		si2=si->b=alloc_search();
		si2->type=search_not;
		si2=si2->a=alloc_search();
		si2->type=search_msgflag;
		si2->as="\\SEEN";
		nexttoken();
		return (si);
	}

	if (strcmp(keyword, "OLD") == 0)
	{
		searchiter si, si2;

		si=alloc_search();
		si->type=search_not;
		si2=si->a=alloc_search();
		si2->type=search_msgflag;
		si2->as="\\RECENT";
		nexttoken();
		return (si);
	}

	if (ismsgset(t))
	{
		si=alloc_search();
		si->type=search_messageset;
		si->as=t->tokenbuf;
		nexttoken();
		return (si);
	}

	return searchlist.end();
}

/*
** We are about to search in charset 'textcharset'.  Make sure that all
** search_text nodes in the search string are in that character set.
*/

void contentsearch::search_set_charset_conv(const std::string &charset)
{
	for (auto &si:searchlist)
	{
		if (si.type != search_text && si.type != search_body
		    && si.type != search_header)
			continue;
		if (si.value > 0)
			continue; /* Already found, no need to do this again */

		if (!si.sei.setString(si.as, charset))
		{
			si.value=0;
			continue;
		}
	}
}


#if 0

void debug_search(searchiter si)
{
	if (!si)	return;

	switch (si->type) {
	case search_messageset:
		writes("MESSAGE SET: ");
		writes(si->as);
		return;
	case search_all:
		writes("ALL");
		return;
	case search_msgflag:
		writes("FLAG \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_msgkeyword:
		writes("KEYWORD \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_not:
		writes("NOT (");
		debug_search(si->a);
		writes(")");
		return;
	case search_and:
		writes("AND (");
		debug_search(si->a);
		writes(", ");
		debug_search(si->b);
		writes(")");
		return;
	case search_or:
		writes("OR (");
		debug_search(si->a);
		writes(", ");
		debug_search(si->b);
		writes(")");
		return;
	case search_header:
		writes("HEADER \"");
		writeqs(si->cs);
		writes("\" \"");
		writeqs(si->bs);
		writes("\"");
		return;
	case search_before:
		writes("BEFORE \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_body:
		writes("BODY \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_larger:
		writes("LARGER \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_on:
		writes("ON \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_sentbefore:
		writes("SENTBEFORE \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_senton:
		writes("SENTON \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_sentsince:
		writes("SENTSINCE \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_since:
		writes("SINCE \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_smaller:
		writes("SMALLER \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_text:
		writes("TEXT \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_uid:
		writes("UID \"");
		writeqs(si->as);
		writes("\"");
		return;
	case search_orderedsubj:
		writes("ORDEREDSUBJ ");
		debug_search(si->a);
		return;
	case search_references1:
		writes("REFERENCES[References/In-Reply-To]=");
		writeqs(si->as ? si->as:"");
		writes("/");
		writeqs(si->bs ? si->bs:"");
		writes(" ");
		debug_search(si->a);
		return;
	case search_references2:
		writes("REFERENCES[Date:]=");
		writeqs(si->as ? si->as:"");
		writes(" ");
		debug_search(si->a);
		return;
	case search_references3:
		writes("REFERENCES[Subject]=");
		writeqs(si->as ? si->as:"");
		writes(" ");
		debug_search(si->a);
		return;
	case search_references4:
		writes("REFERENCES[Message-ID]=");
		writeqs(si->as ? si->as:"");
		writes(" ");
		debug_search(si->a);
		return;
	case search_arrival:
		writes("ARRIVAL");
		return;
	case search_cc:
		writes("CC");
		return;
	case search_date:
		writes("DATE");
		return;
	case search_from:
		writes("FROM");
		return;
	case search_reverse:
		writes("REVERSE");
		return;
	case search_size:
		writes("SIZE");
		return;
	case search_to:
		writes("TO");
		return;
	}
}

#endif
