/*
** Copyright 1998 - 2009 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"imapwrite.h"
#include	<stdio.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	<string.h>
#include	"imapd.h"

#include <string>
#include <algorithm>

#define	MAX_HEADER_SIZE	8192

void msgappends(void (*writefunc)(const char *, size_t),
		const char *s, size_t l)
{
	size_t i, j;

	std::string buf;

	if (!enabled_utf8 &&
	    std::find_if(s, s+l, [](char c) { return c & 0x80; }) != s+l)
	{
		buf=rfc2047::encode(s, s+l, rfc2047::qp_allow_any).first;
		s=buf.c_str();
		l=buf.size();
	}

	for (i=j=0; i<l; i++)
	{
		if (s[i] == '"' || s[i] == '\\')
		{
			(*writefunc)(s+j, i-j);
			(*writefunc)("\\", 1);
			j=i;
		}
	}
	(*writefunc)(s+j, i-j);
}

static void doenvs(void (*writefunc)(const char *, size_t), const char *s)
{
	size_t	i,j;

	while ( s && *s && isspace((int)(unsigned char)*s))
		++s;

	for (i=j=0; s && s[i]; i++)
		if ( !isspace((int)(unsigned char)s[i]))
			j=i+1;

	if (j == 0)
		(*writefunc)("NIL", 3);
	else
	{
		(*writefunc)("\"", 1);
		msgappends(writefunc, s, j);
		(*writefunc)("\"", 1);
	}
}

static void doenva(void (*writefunc)(const char *, size_t),
		   const char *s)
{
	rfc822::tokens t{s};
	rfc822::addresses a{t};

	if (a.empty())
	{
		(*writefunc)("NIL", 3);
		return;
	}

	(*writefunc)("(", 1);
	for (auto &a: a)
	{
		(*writefunc)("(", 1);

		std::string s;

		s.reserve(a.display_name(
			unicode::utf_8,
			rfc822::length_counter{}, true));

		a.display_name(unicode::utf_8,
			std::back_inserter(s),
			true);

		if (a.address.empty())
		{
			if (s == ";")
			{
				(*writefunc)("NIL NIL NIL NIL)", 16);
				continue;
			}
			size_t r=s.rfind(':');
			if (r != std::string::npos && r+1 == s.size())
				s.resize(r);

			(*writefunc)("NIL NIL \"", 9);
			msgappends(writefunc, s.c_str(), s.size());
			(*writefunc)("\" NIL)", 6);
			continue;
		}

		doenvs(writefunc, s.c_str());
		(*writefunc)(" NIL \"", 6);	/* TODO @domain list */
		s.clear();

		s.reserve(a.display_address(
			unicode::utf_8,
			rfc822::length_counter{}));

		a.display_address(
			unicode::utf_8,
			std::back_inserter(s));

		size_t r=s.find('@');
		if (r == std::string::npos)
			r=s.size();
		msgappends(writefunc, s.c_str(), r);

		(*writefunc)("\" \"", 3);

		if (r < s.size())
		{
			++r;
			msgappends(writefunc, s.c_str()+r, s.size()-r);
		}
		(*writefunc)("\")", 2);
	}
	(*writefunc)(")", 1);
}

void msgenvelope(void (*writefunc)(const char *, size_t),
		rfc822::fdstreambuf &fp,
		const rfc2045::entity &rfcp)
{
	std::string date, subject, from, sender, replyto, to, cc, bcc,
		inreplyto, msgid;

	rfc2045::entity::line_iter<false>::headers headers{rfcp, fp};

	do
	{
		std::string *hdrp=nullptr;

		const auto &[name, content] = headers.name_content();
		if (name == "date")	hdrp= &date;
		else if (name == "subject")	hdrp= &subject;
		else if (name == "from")	hdrp= &from;
		else if (name == "sender")	hdrp= &sender;
		else if (name == "reply-to")	hdrp= &replyto;
		else if (name == "to")	hdrp= &to;
		else if (name == "cc")	hdrp= &cc;
		else if (name == "bcc")	hdrp= &bcc;
		else if (name == "in-reply-to")	hdrp= &inreplyto;
		else if (name == "message-id")	hdrp= &msgid;
		if (!hdrp)	continue;

		if (!content.empty())
		{
			if (!hdrp->empty())
				*hdrp += ",";

			*hdrp += content;

			if (hdrp->size() > MAX_HEADER_SIZE)
				hdrp->resize(MAX_HEADER_SIZE);
		}
	} while (headers.next());

	if (replyto.empty())
		replyto=from;

	if (sender.empty())
		sender=from;

	(*writefunc)("(", 1);
	doenvs(writefunc, date.c_str());
	(*writefunc)(" ", 1);
	doenvs(writefunc, subject.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, from.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, sender.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, replyto.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, to.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, cc.c_str());
	(*writefunc)(" ", 1);
	doenva(writefunc, bcc.c_str());
	(*writefunc)(" ", 1);
	doenvs(writefunc, inreplyto.c_str());
	(*writefunc)(" ", 1);
	doenvs(writefunc, msgid.c_str());
	(*writefunc)(")", 1);
}
