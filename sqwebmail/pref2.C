/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"pref.h"
#include	"config.h"
#include	"sqwebmail.h"
#include	"sqconfig.h"
#include	<string.h>
#include	<stdlib.h>
#include	<charconv>
#include	<string_view>
#include	<system_error>

int pref_flagisoldest1st, pref_flagfullheaders;
int pref_showhtml;
int pref_flagsortorder;
int pref_flagpagesize;
int pref_autopurge;
int pref_noflowedtext;
int pref_noarchive;
int pref_noautorenamesent;
int pref_startofweek;
int pref_wikifmt;

std::string pref_from;
std::string pref_ldap;

#define	OLDEST1ST	"OLDEST1ST"
#define	FULLHEADERS	"FULLHEADERS"
#define	SORTORDER	"SORT"
#define	PAGESIZE	"PAGESIZE"
#define	AUTOPURGE_V	"AUTOPURGE"
#define	NOHTML		"NOHTML"
#define	FROM		"FROM"
#define	LDAP		"LDAP"
#define FLOWEDTEXT	"NOFLOWEDTEXT"
#define NOARCHIVE	"NOARCHIVE"
#define NOAUTORENAMESENT	"NOAUTORENAMESENT"
#define STARTOFWEEK	"STARTOFWEEK"
#define WIKITEXT		"WIKITEXT"

static const char hex[]="0123456789ABCDEF";

static int nybble(char c)
{
	auto p=strchr(hex, c);

	if (p)	return (p-hex);
	return (0);
}

static void decode(std::string &str)
{
	auto t=str.data();

	for (auto s=t; *s; s++)
	{
		if (*s != '+')
		{
			*t++ = *s;
			continue;
		}
		if (s[1] == 0 || s[2] == 0)
			continue;
		*t++ = nybble(s[1]) * 16 + nybble(s[2]);
		s += 2;
	}
	str.resize(t-str.data());
}

void pref_init()
{
	auto p=read_sqconfig(".", CONFIGFILE, 0);
	pref_flagisoldest1st=0;
	pref_flagfullheaders=0;
	pref_flagsortorder=0;
	pref_flagpagesize=10;
	pref_noarchive=0;

	{
		const char *autorenamesent=AUTORENAMESENT;

		const char *p=getenv("SQWEBMAIL_AUTORENAMESENT");
		if (p && *p)
			autorenamesent = p;

		pref_noautorenamesent=strncmp(autorenamesent, "no", 2) == 0;
	}

	pref_startofweek=0;
	pref_autopurge=AUTOPURGE;
	pref_showhtml=1;
	pref_wikifmt=0;

	pref_from.clear();
	pref_ldap.clear();

	if (p && p->size())
	{
		std::string_view q{*p};

		while (q.size())
		{
			auto r=q.find(' ');
			if (r == std::string_view::npos)
				r=q.size();
			auto w=q.substr(0, r);

			if (r == q.size())
				q=std::string_view{};
			else
				q=q.substr(r+1);

			if (w == OLDEST1ST)
				pref_flagisoldest1st=1;
			if (w == FULLHEADERS)
				pref_flagfullheaders=1;
			if (w == NOHTML)
				pref_showhtml=0;
			if (w == WIKITEXT)
				pref_wikifmt=1;

			auto equals=w.find('=');
			if (equals != std::string_view::npos)
			{
				auto key=w.substr(0, equals);
				auto value=w.substr(equals+1);

				if (key == SORTORDER && !value.empty())
					pref_flagsortorder=value[0];
				if (key == PAGESIZE && !value.empty())
					std::from_chars(value.data(), value.data()+value.size(), pref_flagpagesize);
				if (key == AUTOPURGE_V && !value.empty())
					std::from_chars(value.data(), value.data()+value.size(), pref_autopurge);
				if (key == FLOWEDTEXT && !value.empty())
					std::from_chars(value.data(), value.data()+value.size(), pref_noflowedtext);
				if (key == NOARCHIVE && !value.empty())
					std::from_chars(value.data(), value.data()+value.size(), pref_noarchive);
				if (key == NOAUTORENAMESENT && !value.empty())
					std::from_chars(value.data(), value.data()+value.size(), pref_noautorenamesent);
				if (key == FROM && !value.empty())
				{
					pref_from=value;
					decode(pref_from);
				}
				if (key == LDAP && !value.empty())
				{
					pref_ldap=value;
					decode(pref_ldap);
				}
				if (key == STARTOFWEEK && !value.empty())
				{
					int n;
					if (std::from_chars(value.data(), value.data()+value.size(), n).ec == std::errc())
					{
						if (n >= 0 && n < 7)
							pref_startofweek=n;
					}
				}
			}
		}
	}
	switch (pref_flagpagesize)	{
	case 20:
	case 50:
	case 100:
	case 250:
		break;
	default:
		pref_flagpagesize=10;
		break;
	}

	if (pref_autopurge < 0)	pref_autopurge=0;
	if (pref_autopurge > MAXPURGE)	pref_autopurge=MAXPURGE;

	switch (pref_flagsortorder)	{
	case 'F':
	case 'S':
		break;
	default:
		pref_flagsortorder='D';
		break;
	}
}

static void append_str(
	std::string &prefs,
	std::string_view label,
	std::string_view value
)
{
	prefs.append(" ");
	prefs.append(label);
	prefs.append("=");

	for (unsigned char q:value)
	{
		if (q <= ' ' || q >= 127 || q == '+')
		{
			char buf[10];

			buf[0]='+';
			buf[1]=hex[(q>>4)&0x0F];
			buf[2]=hex[q&0x0F];
			buf[3]=0;
			prefs.append(buf);
			continue;
		}
		prefs.push_back(q);
	}
}

void pref_update()
{
	std::string buf;
	char numbuf[20];

	buf.reserve(256);
	buf.append(SORTORDER "=");
	buf.push_back(pref_flagsortorder);
	buf.append(" " PAGESIZE "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_flagpagesize)).ptr=0;
	buf.append(numbuf);
	buf.append(" " AUTOPURGE_V "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_autopurge)).ptr=0;
	buf.append(numbuf);
	buf.append(" " FLOWEDTEXT "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_noflowedtext)).ptr=0;
	buf.append(numbuf);
	buf.append(" " NOARCHIVE "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_noarchive)).ptr=0;
	buf.append(numbuf);
	buf.append(" " NOAUTORENAMESENT "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_noautorenamesent)).ptr=0;
	buf.append(numbuf);
	buf.append(" " STARTOFWEEK "=");
	*(std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
			pref_startofweek)).ptr=0;
	buf.append(numbuf);

	if (pref_flagisoldest1st)
		buf.append(" " OLDEST1ST);

	if (pref_flagfullheaders)
		buf.append(" " FULLHEADERS);

	if (!pref_showhtml)
		buf.append(" " NOHTML);

	if (pref_wikifmt)
		buf.append(" " WIKITEXT);

	append_str(buf, FROM, pref_from);
	append_str(buf, LDAP, pref_ldap);
	write_sqconfig(".", CONFIGFILE, buf.c_str());
}
