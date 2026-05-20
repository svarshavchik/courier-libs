/*
** Copyright 1998 - 2001 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"
#include	<stdio.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	<string.h>

#include <string>
#include <algorithm>

extern void msgenvelope(void (*)(const char *, size_t),
			rfc822::fdstreambuf &,
			const rfc2045::entity &);

extern void msgappends(void (*)(const char *, size_t), const char *, size_t);

static void do_param_list(void (*writefunc)(const char *, size_t),
	const rfc2231::header::parameters_t &parameters)
{
	std::vector<rfc2231::header::parameters_t
		::const_iterator> v;

	v.reserve(parameters.size());

	for (auto i=parameters.begin(); i != parameters.end(); ++i)
		v.push_back(i);

	// We want to iterate in the original order of appearance.
	std::sort(v.begin(), v.end(),
		  []
		  (rfc2231::header::parameters_t::const_iterator a,
		   rfc2231::header::parameters_t::const_iterator b)
		  {
			  return a->second.index < b->second.index;
		  });
	int	flag;
	const char	*p;

	flag=0;
	p="(";
	for (auto &i: v)
	{
		auto &a=*i;

		(*writefunc)(p, strlen(p));
		(*writefunc)("\"", 1);
		if (!a.first.empty())
			msgappends(writefunc, a.first.c_str(), a.first.size());
		(*writefunc)("\" \"", 3);
		if (!a.second.value.empty())
		{
#if	IMAP_CLIENT_BUGS

			/* NETSCAPE */

			std::string u=a.second.value;

			u.erase(std::remove(u.begin(), u.end(), '\\'),
				u.end());

			msgappends(writefunc, u.c_str(), u.size());
#else
			msgappends(writefunc, a.second.value.c_str(),
				a.second.value.size());
#endif
		}
		(*writefunc)("\"", 1);
		flag=1;
		p=" ";
	}
	if (flag)
		(*writefunc)(")", 1);
	else
		(*writefunc)("NIL", 3);
}

static void contentstr( void (*writefunc)(const char *, size_t),
	const std::string_view s)
{
	if (s.empty())
	{
		(*writefunc)("NIL", 3);
		return;
	}

	(*writefunc)("\"", 1);
	msgappends(writefunc, s.data(), s.size());
	(*writefunc)("\"", 1);
}


static void do_disposition(
	void (*writefunc)(const char *, size_t),
	std::string_view s)
{
	rfc2231::header	disposition{s, false};

	if ( disposition.value.empty() && disposition.parameters.empty())
	{
		(*writefunc)("NIL", 3);
		return;
	}
	(*writefunc)("(", 1);

	if (!disposition.value.empty())
	{
		(*writefunc)("\"", 1);
		msgappends(writefunc, disposition.value.c_str(),
			disposition.value.size());
		(*writefunc)("\"", 1);
	}
	else
		(*writefunc)("\"\"", 2);

	(*writefunc)(" ", 1);
	do_param_list(writefunc, disposition.parameters);
	(*writefunc)(")", 1);
}

void msgbodystructure( void (*writefunc)(const char *, size_t), int dox,
	rfc822::fdstreambuf &fp, const rfc2045::entity &rfcp)
{
	(*writefunc)("(", 1);

	if (!rfcp.subentities.empty() &&
	    !rfc2045::message_content_type(
			rfcp.content_type.value
	    )
	)
		/* MULTIPART */
	{
		for (auto &childp: rfcp.subentities)
			msgbodystructure(writefunc, dox, fp, childp);

		(*writefunc)(" \"", 2);
		size_t slash=rfcp.content_type.value.find('/');
		if (slash != std::string::npos)
			msgappends(writefunc,
				rfcp.content_type.value.c_str()+slash+1,
				rfcp.content_type.value.size()-slash-1);
		(*writefunc)("\"", 1);

		if (dox)
		{
			(*writefunc)(" ", 1);
			do_param_list(writefunc, rfcp.content_type.parameters);

			(*writefunc)(" ", 1);

			do_disposition(writefunc, rfcp.content_disposition);

			(*writefunc)(" ", 1);
			contentstr(writefunc, rfcp.content_language);
		}
	}
	else
	{
		char	buf[40];

		std::string_view mybuf=rfcp.content_type.value;

		auto q=std::find_if(mybuf.begin(),
				    mybuf.end(),
				    []
				    (char c)
				    {
					    return c == ' ' || c == '/';
				    });

		(*writefunc)("\"", 1);
		msgappends(writefunc, mybuf.data(), q-mybuf.begin());
		(*writefunc)("\" \"", 3);

		while (q != mybuf.end() && (*q == ' ' || *q == '/'))
			++q;

		auto p=q;

		q=std::find_if(q, mybuf.end(),
			       []
			       (char c)
			       {
				       return c == ' ' || c == '/';
			       });


		msgappends(writefunc, &*p, q-p);
		(*writefunc)("\" ", 2);

		do_param_list(writefunc, rfcp.content_type.parameters);

		(*writefunc)(" ", 1);
		if (rfcp.content_id.empty())
			contentstr(writefunc, rfcp.content_id);
		else
		{
			(*writefunc)("\"<", 2);
			msgappends(writefunc, rfcp.content_id.c_str(),
				rfcp.content_id.size());
			(*writefunc)(">\"", 2);
		}
		(*writefunc)(" ", 1);
		contentstr(writefunc, rfcp.content_description);

		(*writefunc)(" \"", 2);

		auto cte=rfc2045::to_cte(
			rfcp.content_transfer_encoding
		);
		msgappends(writefunc, cte, strlen(cte));
		(*writefunc)("\" ", 2);

		sprintf(buf, "%lu", (unsigned long)
			rfcp.rfc822_body_size());
		(*writefunc)(buf, strlen(buf));

		mybuf=rfcp.content_type.value;

		mybuf=mybuf.substr(0, 5);

		if (mybuf == "text" || mybuf == "text/")
		{
			(*writefunc)(" ", 1);
			sprintf(buf, "%lu",
				(unsigned long)rfcp.rfc822_bodylines());
			(*writefunc)(buf, strlen(buf));
		}

		if (rfc2045::message_content_type(
			rfcp.content_type.value
		) && !rfcp.subentities.empty())
		{
			(*writefunc)(" ", 1);
			msgenvelope(writefunc, fp, rfcp.subentities[0]);
			(*writefunc)(" ", 1);
			msgbodystructure(writefunc, dox, fp, rfcp.subentities[0]);
			(*writefunc)(" ", 1);
			sprintf(buf, "%lu",
				(unsigned long)rfcp.rfc822_bodylines());
			(*writefunc)(buf, strlen(buf));
		}

		if (dox)
		{
			(*writefunc)(" ", 1);
			contentstr(writefunc, rfcp.content_md5);

			(*writefunc)(" ", 1);
			do_disposition(writefunc, rfcp.content_disposition);

			(*writefunc)(" NIL", 4);
				/* TODO Content-Language: */
		}
	}
	(*writefunc)(")", 1);
}
