/*
** Copyright 1998 - 1999 S. Varshavchik.
** See COPYING for distribution information.
*/

#ifndef	HAVE_CONFIG_H
#include	"config.h"
#endif

#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <ctype.h>
#include    <sys/types.h>

#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"fetchinfo.h"


/* This file contains functions to parse a FETCH attribute list */

static void alloc_headerlist(bool, std::list<fetchinfo> &sublist);
static char *good_section(char *);

bool fetchinfo_alloc(bool oneonly, std::list<fetchinfo> &list)
{
	struct imaptoken *tok;

	while ((tok=currenttoken())->tokentype == IT_ATOM)
	{
		if (oneonly && !list.empty())	break;
		list.emplace_back();
		auto p=--list.end();

		p->name=tok->tokenbuf;

		if (p->name == "ALL" ||
			p->name == "BODYSTRUCTURE" ||
			p->name == "ENVELOPE" ||
			p->name == "FLAGS" ||
			p->name == "FAST" ||
			p->name == "FULL" ||
			p->name == "INTERNALDATE" ||
			p->name == "RFC822" ||
			p->name == "RFC822.HEADER" ||
			p->name == "RFC822.SIZE" ||
			p->name == "RFC822.TEXT" ||
			p->name == "UID")
		{
			nexttoken();
			continue;
		}
		if (p->name != "BODY" && p->name != "BODY.PEEK")
			break;
		if (nexttoken()->tokentype != IT_LBRACKET)	continue;

		/* Parse BODY[ ... ] */

		if ((tok=nexttoken())->tokentype != IT_RBRACKET)
		{
		char	*s;

			if ( (tok->tokentype != IT_ATOM &&
				tok->tokentype != IT_NUMBER) ||
				!(s=good_section(tok->tokenbuf)))
			{
				return (false);
			}
			p->hasbodysection=true;
			p->bodysection=tok->tokenbuf;

			if (strcmp(s, "HEADER.FIELDS") == 0 ||
				strcmp(s, "HEADER.FIELDS.NOT") == 0)
			{
				/* Must be followed by header list */

				if ((tok=nexttoken_nouc())->tokentype
						!= IT_LPAREN)
				{
					alloc_headerlist(true, p->bodysublist);
					if (p->bodysublist.empty())
					{
						return false;
					}
				}
				else
				{
					nexttoken_nouc();
					alloc_headerlist(false, p->bodysublist);
					if ( currenttoken()->tokentype
						!= IT_RPAREN)
					{
						return false;
					}
				}
			}
			tok=nexttoken();

		}
		else p->hasbodysection=true;

		if (tok->tokentype != IT_RBRACKET)
			return false;

		tok=nexttoken();
		if (tok->tokentype == IT_ATOM && tok->tokenbuf[0] == '<' &&
			tok->tokenbuf[strlen(tok->tokenbuf)-1] == '>' &&
			(p->ispartial=sscanf(tok->tokenbuf+1, "%lu.%lu",
				&p->partialstart, &p->partialend)) > 0)
			nexttoken();
	}
	return (true);
}

/* Just validate that the syntax of the attribute is correct */

static char *good_section(char *p)
{
int	has_mime=0;

	while (isdigit((int)(unsigned char)*p))
	{
		if (*p == '0')	return (0);
		has_mime=1;
		while (isdigit((int)(unsigned char)*p))	++p;
		if (*p == '\0')
			return (p);

		if (*p != '.')	return (0);
		++p;
	}

	if (strcmp(p, "HEADER") == 0 ||
		strcmp(p, "HEADER.FIELDS") == 0 ||
		strcmp(p, "HEADER.FIELDS.NOT") == 0 ||
		strcmp(p, "TEXT") == 0)
		return (p);

	if (strcmp(p, "MIME") == 0 && has_mime)	return (p);
	return (0);
}

/* Header list looks like atoms to me */

static void alloc_headerlist(bool oneonly, std::list<fetchinfo> &sublist)
{
	struct imaptoken *tok;

	while ((tok=currenttoken())->tokentype == IT_ATOM ||
	       tok->tokentype == IT_QUOTED_STRING ||
	       tok->tokentype == IT_NUMBER)
	{
		sublist.emplace_back();

		auto p=--sublist.end();
		p->name=tok->tokenbuf;
		if (oneonly)
			break;
		nexttoken_nouc();
	}
}
