#include "config.h"
#include "pcp.h"
#include "pcpretr.h"
#include "calendardir.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libintl.h>
#include <string.h>

#include "rfc822/rfc822.h"
#include "rfc822/rfc2047.h"
#include "rfc2045/rfc2045.h"

extern "C" const char *charset;

static int list_msg_rfc822(const rfc2045::entity &, rfc822::fdstreambuf &);

static int tcmp(const void *a, const void *b)
{
	struct xretr_time_list *ap=*(struct xretr_time_list **)a;
	struct xretr_time_list *bp=*(struct xretr_time_list **)b;

	return ( ap->from < bp->from ? -1:
		 ap->from > bp->from ? 1:
		 ap->to < bp->to ? -1:
		 ap->to > bp->to ? 1:0);
}


extern "C" void dump_rfc822_hdr(const char *ptr, size_t cnt,
				void *dummy)
{
	fwrite(ptr, cnt, 1, stdout);
}

extern "C" int do_show_retr(struct PCP_retr *r, void *vp)
{
	struct xretrinfo *xr=(struct xretrinfo *)vp;
	struct xretr_participant_list *p;
	struct xretr_time_list *t, **tt;
	unsigned cnt, i;
	int rc;

	if (fseek(xr->tmpfile, 0L, SEEK_SET) < 0
	    || lseek(fileno(xr->tmpfile), 0L, SEEK_SET) < 0)
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	if (xr->time_list == NULL)
	{
		fclose(xr->tmpfile);
		return (0);
	}

	printf(gettext("Event: %s\n"), r->event_id);

	for (cnt=0, t=xr->time_list; t; t=t->next)
		++cnt;

	tt=(struct xretr_time_list **)malloc(cnt * sizeof(*t));
	if (!tt)
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	for (cnt=0, t=xr->time_list; t; t=t->next)
		tt[cnt++]=t;

	qsort(tt, cnt, sizeof(*tt), tcmp);

	for (i=0; i<cnt; i++)
	{
		char fromto[500];

		if (pcp_fmttimerange(fromto, sizeof(fromto),
				     tt[i]->from, tt[i]->to) < 0)
			strcpy(fromto, "******");
		printf(gettext("       %s\n"), fromto);
	}
	free(tt);

	if (xr->status & LIST_CANCELLED)
		printf(gettext("    **** CANCELLED ****\n"));
	if (xr->status & LIST_BOOKED)
		printf(gettext("    **** EVENT NOT YET COMMITED ****\n"));

	for (p=xr->participant_list; p; p=p->next)
		printf(gettext("    Participant: %s\n"), p->participant);


	rfc822::fdstreambuf fd{dup(fileno(xr->tmpfile))};

	if (fd.error())
	{
		fclose(xr->tmpfile);
		return (-1);
	}

	rfc2045::entity message;
	{
		std::istreambuf_iterator<char> b{&fd}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	rc=list_msg_rfc822(message, fd);
	fclose(xr->tmpfile);
	return (rc);
}

static int list_msg_mime(const rfc2045::entity &, rfc822::fdstreambuf &);

static int list_msg_rfc822(const rfc2045::entity &rfc, rfc822::fdstreambuf &fp)
{

	rfc2045::entity::line_iter<false>::headers headers{rfc, fp};

	headers.name_lc=false;
	headers.keep_eol=true;

	do
	{
		const auto &[name, content] = headers.name_content();

		if (name.empty() && content == "\n")
			continue;

		fwrite(name.data(), name.size(), 1, stdout);
		printf(": ");

		std::string s;
		rfc822::display_header(name, content, charset,
				       std::back_inserter(s));
		printf("%s", s.c_str());
	} while (headers.next());
	printf("\n");
	return (list_msg_mime(rfc, fp));
}

static int list_msg_rfc822_part(const rfc2045::entity &rfc, rfc822::fdstreambuf &fp)
{
	for (auto &subentity:rfc.subentities)
	{
		return (list_msg_rfc822(subentity, fp));
	}
	return (0);
}


static int list_msg_mime_multipart(const rfc2045::entity &, rfc822::fdstreambuf &);
static int list_msg_mime_multipart_alternative(const rfc2045::entity &, rfc822::fdstreambuf &);
static int list_msg_textplain(const rfc2045::entity &, rfc822::fdstreambuf &);

static int (*mime_handler(const rfc2045::entity &rfc))(const rfc2045::entity &, rfc822::fdstreambuf &)
{
        if (rfc.content_type.value == "multipart/alternative")
		return ( &list_msg_mime_multipart_alternative);
	if (std::string_view{rfc.content_type.value}.substr(0, 10)
	    == "multipart/")
		return ( &list_msg_mime_multipart);

        if (rfc2045_message_headers_content_type(
		    rfc.content_type.value.c_str()
	    ))
		return ( &list_msg_rfc822_part );

        if (rfc.content_type.value == "text/plain"
	    || rfc.content_type.value == "text/rfc822-headers"
	    || rfc.content_type.value == "message/delivery-status")
		return ( &list_msg_textplain);
	return (NULL);
}


static int list_msg_mime(const rfc2045::entity &rfc, rfc822::fdstreambuf &fp)
{
	int (*handler)(const rfc2045::entity &, rfc822::fdstreambuf &)=
		mime_handler(rfc);

	char buffer[NUMBUFSIZE+10];

	if (handler)
		return ( (*handler)(rfc, fp));

	std::string disposition_name, disposition_filename;

	{
		rfc2045::entity::rfc2231_header content_disposition{
			rfc.content_disposition
		};

		auto iter=content_disposition.parameters.find("name");

		if (iter != content_disposition.parameters.end())
		{
			disposition_name=iter->second.value;
		}

		iter=content_disposition.parameters.find("filename");

		if (iter != content_disposition.parameters.end())
		{
			disposition_filename=iter->second.value;
		}
	}

	std::string content_name;

	{
		auto iter=rfc.content_type.parameters.find("name");

		if (iter != rfc.content_type.parameters.end())
			content_name=iter->second.value;
	}

	if (disposition_filename.empty())
		disposition_filename=disposition_name;
	if (disposition_filename.empty())
		disposition_filename=content_name;

	printf(gettext("Attachment: %s (%s)\n"), rfc.content_type.value.c_str(),
	       libmail_str_sizekb(rfc.endbody - rfc.startbody, buffer));
	if (!disposition_filename.empty())
		printf("    %s\n", disposition_filename.c_str());
	printf("\n");

	return (0);
}

static int list_msg_mime_multipart(const rfc2045::entity &rfc,
				   rfc822::fdstreambuf &fp)
{
	bool first=true;

	for (auto &q:rfc.subentities)
	{
		if (!first)
			printf("\n    ------------------------------\n\n");
		first=false;

		int rc=list_msg_mime(q, fp);
		if (rc)
			return (rc);
	}
	return (0);
}

static int list_msg_mime_multipart_alternative(const rfc2045::entity &rfc,
					       rfc822::fdstreambuf &fp)
{
	const rfc2045::entity *first=nullptr, *last=nullptr;

	for (auto &q:rfc.subentities)
	{
		if (!first)
			first=&q;
		if ( mime_handler(q) != NULL)
			last=&q;
	}

	return (last ? list_msg_mime(*last, fp):
		first ? list_msg_mime(*first, fp):0);
}

static int list_msg_textplain(const rfc2045::entity &rfc, rfc822::fdstreambuf &fp)
{
	std::string charset_lc{charset};

	rfc2045::entity::tolowercase(charset_lc);
        if (rfc.content_type_charset() != charset_lc)
        {
		auto cs=rfc.content_type_charset();

		printf(gettext("    (The following text was converted from %s)\n\n"),
		       std::string{cs.begin(), cs.end()}.c_str());
	}

	rfc822::mime_decoder decoder{
		[]
		(const char *ptr, size_t n)
		{
			fwrite(ptr, n, 1, stdout);
		},
		fp, charset};

	decoder.decode_header=false;
	decoder.decode<false>(rfc);
	printf("\n");
	return (0);
}
