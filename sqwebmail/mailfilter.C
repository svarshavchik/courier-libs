#include "config.h"

/*
** Copyright 2000-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"mailfilter.h"
#include	"sqwebmail.h"
#include	"maildir.h"
#include	"auth.h"
#include	"rfc2045/rfc2045.h"
#include	"maildir/autoresponse.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirfilter.h"
#include	"numlib/numlib.h"
#include	"cgi/cgi.h"
#include	<fstream>
#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>

extern void list_folder(std::string_view);
void output_attrencoded(const char *);

static const char *internal_err=0;

static void clrfields()
{
	cgi_put("currentfilternum", "");
	cgi_put("rulename", "");
	cgi_put("filtertype", "");
	cgi_put("hasrecipienttype", "");
	cgi_put("hasrecipientaddr", "");
	cgi_put("headername", "");
	cgi_put("headervalue", "");
	cgi_put("headermatchtype", "");
	cgi_put("action", "");
	cgi_put("forwardaddy", "");
	cgi_put("bouncemsg", "");
	cgi_put("savefolder", "");
	cgi_put("sizecompare", "");
	cgi_put("bytecount", "");
	cgi_put("continuefiltering", "");
	cgi_put("autoresponse_choose", "");
	cgi_put("autoresponse_dsn", "");
	cgi_put("autoresponse_regexp", "");
	cgi_put("autoresponse_dupe", "");
	cgi_put("autoresponse_days", "");
	cgi_put("autoresponse_from", "");
	cgi_put("autoresponse_noquote", "");
}

void mailfilter_list()
{
	maildirfilter mf;
	unsigned cnt;

	if (!maildir::filter::load(mf, "."))
		return;

	cnt=0;
	for (auto &r :mf)
	{
		std::string p=unicode::iconvert::convert(
			r.rulename_utf8,
			unicode::utf_8,
			sqwebmail_content_charset
		);

		printf("<option value=\"%u\">", cnt);
		output_attrencoded(p.c_str());
		printf("</option>");
		++cnt;
	}
}

void mailfilter_init()
{
	unsigned n;
	maildirfilter mf;

	if (*cgi("import"))
	{
		if (!maildir::filter::import("."))
		{
			printf("%s", getarg("BADIMPORT"));
			return;
		}
	}

	if (*cgi("internalerr"))
	{
		const char *p=internal_err;

		if (*cgi("currentfilternum"))
			printf("<input name=\"currentfilternum\""
			       " type=\"hidden\""
			       " value=\"%s\" />", cgi("currentfilternum"));

		if (p)
			printf("%s", getarg(p));
	}
	internal_err=0;

	if (*cgi("do.save"))
	{
		if (!maildir::filter::commit(".") ||
		    !maildir::filter::import("."))
			printf("%s", getarg("INTERNAL"));
		else
			printf("%s", getarg("UPDATED"));
		clrfields();
	}

	if (*cgi("do.add"))
		clrfields();

	if (!maildir::filter::load(mf, "."))
	{
		printf("%s", getarg("BADIMPORT"));
		return;
	}

	std::string	 p=cgi("currentfilter");
	if (p.empty())
	{
		return;
	}
	n=atoi(p.c_str());

	if (*cgi("do.moveup") && n > 0 && n < mf.size())
	{
		std::swap(mf[n-1], mf[n]);
		maildir::filter::save(mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.movedown") && n >= 0 && n+1 < mf.size())
	{
		std::swap(mf[n], mf[n+1]);
		maildir::filter::save(mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.delete") && n >= 0 && n < mf.size())
	{
		mf.erase(mf.begin()+n);
		maildir::filter::save(mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.edit") && n >= 0 && n < mf.size())
	{
		maildirfilterrule &r=mf[n];
		static std::string namebuf;
		static std::string headernamebuf;
		static std::string headervaluebuf;
		static std::string actionbuf;
		char	numbuf[NUMBUFSIZE+1];

		printf("<input name=\"currentfilternum\""
			" type=\"hidden\""
			" value=\"%s\" />", p.c_str());

		cgi_put("filtertype",
			r.type == startswith ||
			r.type == endswith ||
			r.type == contains ?
				r.flags & MFR_BODY ? "body":"header":
			r.type == hasrecipient
					? "hasrecipient":
			r.type == mimemultipart ?
				r.flags & MFR_DOESNOT ?
					"nothasmultipart":
					"hasmultipart":
			r.type == islargerthan ? "hassize":
			r.type == anymessage
					? "anymessage":
			r.type == textplain ?
				r.flags & MFR_DOESNOT ?
					"nothastextplain":
					"hastextplain":""
				) ;

		cgi_put("continuefiltering",
				r.flags & MFR_CONTINUE ? "1":"");

		cgi_put("headermatch",
			r.type == startswith ?
				r.flags & MFR_DOESNOT ? "notstartswith":"startswith":
			r.type == endswith ?
				r.flags & MFR_DOESNOT ? "notendswith":"endswith":
			r.type == contains ?
				r.flags & MFR_DOESNOT ? "notcontains":"contains":"");

		namebuf=unicode::iconvert::convert(
			r.rulename_utf8,
			unicode::utf_8,
			sqwebmail_content_charset
		);

		cgi_put("rulename", namebuf);

		p=r.fieldname_utf8;
		if (r.type != startswith &&
			r.type != endswith &&
			r.type != contains)	p="";
		if (r.flags & MFR_BODY)	p="";

		headernamebuf=unicode::iconvert::convert(
			p,
			unicode::utf_8,
			sqwebmail_content_charset
		);

		cgi_put("headername", headernamebuf);

		p=r.fieldvalue_utf8;
		if (r.type != startswith &&
			r.type != endswith &&
			r.type != contains &&
			r.type != hasrecipient &&
			r.type != islargerthan)	p="";

		if (r.type == islargerthan)
			p=libmail_str_size_t(
				atol(p.c_str())+( r.flags & MFR_DOESNOT ? 1:0),
				numbuf
			);

		headervaluebuf=unicode::iconvert::convert(
			p,
			unicode::utf_8,
			sqwebmail_content_charset
		);

		cgi_put("hasrecipientaddr", "");
		cgi_put("headervalue", "");
		cgi_put("bytecount", "");
		cgi_put("sizecompare", "");

		cgi_put(r.type == hasrecipient ? "hasrecipientaddr":
			r.type == islargerthan ? "bytecount":
				"headervalue", headervaluebuf.c_str());

		if (r.type == hasrecipient)
		{
			cgi_put("hasrecipienttype",
				r.flags & MFR_DOESNOT ? "nothasrecipient":
					"hasrecipient");
		}
		if (r.type == islargerthan)
		{
			cgi_put("sizecompare",
				r.flags & MFR_DOESNOT
					? "issmallerthan":"islargerthan");
		}
		actionbuf=r.tofolder;

		cgi_put("bouncemsg", "");
		cgi_put("forwardaddy", "");
		cgi_put("savefolder", "");

		cgi_put("autoresponse_regexp",
			r.flags & MFR_PLAINSTRING ? "":"1");

		if (*actionbuf.c_str() == '!')
		{
			cgi_put("action", "forwardto");
			cgi_put("forwardaddy", actionbuf.c_str()+1);
		}
		else if (*actionbuf.c_str() == '*')
		{
			cgi_put("action", "bounce");
			cgi_put("bouncemsg", actionbuf.c_str()+1);
		}
		else if (*actionbuf.c_str() == '+')
		{
			maildir_filter_autoresp_info mfai;
			static std::string autoresp_name_buf;
			static char days_buf[NUMBUFSIZE];
			static std::string fromhdr;

			if (!maildir_filter_autoresp_info_init_str(mfai, actionbuf.c_str()+1))
				enomem();

			autoresp_name_buf=mfai.name;

			cgi_put("action", "autoresponse");
			cgi_put("autoresponse_choose", autoresp_name_buf);
			cgi_put("autoresponse_dsn",
				mfai.mode == MAILDIR_FILTER_AUTORESP_MODE_DSN
				? "1":"");

			cgi_put("autoresponse_dupe",
				mfai.days > 0 ? "1":"");

			libmail_str_size_t(mfai.days, days_buf);
			cgi_put("autoresponse_days", mfai.days ?
				days_buf:"");

			fromhdr=r.fromhdr;
			cgi_put("autoresponse_from", fromhdr);

			if (mfai.mode == MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE)
			{
				cgi_put("autoresponse_noquote", "1");
			}
		}
		else if (actionbuf == "exit")
		{
			cgi_put("action", "purge");
		}
		else
		{
			cgi_put("action", "savefolder");
			cgi_put("savefolder",
				actionbuf == "." ? INBOX:
				*actionbuf.c_str() == '.' ? actionbuf.c_str()+1:actionbuf.c_str());
		}
	}
	else if (!(p=cgi("currentfilternum")).empty())
	{
		printf("<input name=\"currentfilternum\""
			" type=\"hidden\""
			" value=\"%s\" />", p.c_str());
	}
}

void mailfilter_listfolders()
{
	const char *f=cgi("savefolder");

	printf("<select name=\"savefolder\">");

	auto folders=maildir_listfolders(INBOX, ".");
	for (auto &folder:folders)
	{
		const char *p=folder.c_str();
		int selected=0;

		if (strcmp(p, INBOX) &&
		    strncmp(p, INBOX ".", sizeof(INBOX)))
			continue;

		printf("<option value=\"");
		output_attrencoded(p);
		if (strcmp(p, f) == 0)
			selected=1;

		if (strcmp(p, INBOX) == 0)
		{
			p=getarg("INBOX");
			selected=0;
			if (strcmp(f, ".") == 0)
				selected=1;
		}
		else if (strcmp(p, INBOX "." DRAFTS) == 0)
			p=getarg("DRAFTS");
		else if (strcmp(p, INBOX "." TRASH) == 0)
			p=getarg("TRASH");
		else if (strcmp(p, INBOX "." SENT) == 0)
			p=getarg("SENT");
		else
			p=strchr(p, '.')+1;

		printf("\"");
		if (selected)
			printf(" selected='selected'");
		printf(">");
		list_folder(p);
		printf("</option>\n");
	}
	printf("</select>");
}

void mailfilter_submit()
{
	maildirfilter mf;
	const char *p;
	enum maildirfiltertype type;
	int flags=0;
	const char *rulename=0;
	std::string fieldname;
	std::string_view fieldvalue;
	std::string tofolder;
	int	err_num;
	char	numbuf[NUMBUFSIZE];
	const char *autoreply_from="";

	if (!maildir::filter::load(mf, "."))
		return;

	p=cgi("currentfilternum");
	size_t n=mf.size();
	if (*p)
	{
		size_t n1=atoi(p);
		if (n1 < mf.size())
			n=n1;
	}

	rulename=cgi("rulename");

	p=cgi("filtertype");
	if (strcmp(p, "hasrecipient") == 0)
	{
		type=hasrecipient;
		if (strcmp(cgi("hasrecipienttype"), "nothasrecipient") == 0)
			flags |= MFR_DOESNOT;

		fieldvalue=cgi("hasrecipientaddr");
	}
	else if (strcmp(p, "hastextplain") == 0)
	{
		type=textplain;
	}
	else if (strcmp(p, "nothastextplain") == 0)
	{
		type=textplain;
		flags |= MFR_DOESNOT;
	}
	else if (strcmp(p, "hasmultipart") == 0)
	{
		type=mimemultipart;
	}
	else if (strcmp(p, "nothasmultipart") == 0)
	{
		type=mimemultipart;
		flags |= MFR_DOESNOT;
	}
	else if (strcmp(p, "hassize") == 0)
	{
		unsigned long n=atol(cgi("bytecount"));

		type=islargerthan;
		if (strcmp(cgi("sizecompare"), "issmallerthan") == 0)
		{
			flags |= MFR_DOESNOT;
			if (n)	--n;
		}
		fieldvalue=libmail_str_size_t(n, numbuf);
	}
	else if (strcmp(p, "anymessage") == 0)
	{
		type=anymessage;
	}
	else
	{
		if (strcmp(p, "body") == 0)
		{
			flags |= MFR_BODY;
		}

		fieldname=cgi("headername");
		p=cgi("headermatchtype");
		type=strcmp(p, "startswith") == 0 ||
			strcmp(p, "notstartswith") == 0 ? startswith:
			strcmp(p, "contains") == 0 ||
			strcmp(p, "notcontains") == 0 ? contains:endswith;
		if (strncmp(p, "not", 3) == 0)
			flags |= MFR_DOESNOT;
		fieldvalue=cgi("headervalue");
	}

	if (*cgi("continuefiltering"))
	{
		flags |= MFR_CONTINUE;
	}

	if (!*cgi("autoresponse_regexp"))
		flags |= MFR_PLAINSTRING;

	p=cgi("action");
	if (strcmp(p, "forwardto") == 0)
	{
		p=cgi("forwardaddy");
		tofolder.reserve(strlen(p)+1);
		tofolder="!";
		tofolder += p;
	}
	else if (strcmp(p, "bounce") == 0)
	{
		p=cgi("bouncemsg");
		tofolder.reserve(strlen(p)+1);
		tofolder="*";
		tofolder += p;
	}
	else if (strcmp(p, "autoresponse") == 0)
	{
		maildir_filter_autoresp_info mfaii;

		p=cgi("autoresponse_choose");

		bool is_text_plain;
		{
			std::ifstream i;

			mail::autoresponse::open(i, "", p);

			if (!i)
			{
				internal_err="AUTOREPLY";
				cgi_put("internal_err", "1");
				return;
			}

			rfc2045::entity message;

			message.mime1=true;
			std::istreambuf_iterator<char> b{i.rdbuf()}, e;
			rfc2045::entity::line_iter<false>::iter parser{b, e};

			message.parse(parser);
			is_text_plain=
				message.content_type.value == "text/plain";
		}

		if (!mail::autoresponse::validate("", p))
		{
			internal_err="AUTOREPLY";
			cgi_put("internal_err", "1");
			return;
		}
		mfaii.name=p;

		p=cgi("autoresponse_dsn");

		if (*p)
		{
			if (!is_text_plain)
			{
				internal_err="MIMEAUTOREPLY";
				cgi_put("internalerr", "1");
				return;
			}
			mfaii.mode=MAILDIR_FILTER_AUTORESP_MODE_DSN;
		}

		p=cgi("autoresponse_dupe");
		if (*p)
		{
			p=cgi("autoresponse_days");
			mfaii.days=atoi(p);
		}

		p=cgi("autoresponse_noquote");

		if (*p)
		{
			if (!is_text_plain)
			{
				internal_err="MIMEAUTOREPLY";
				cgi_put("internalerr", "1");
				return;
			}
			mfaii.mode=MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE;
		}

		auto q=maildir_filter_autoresp_info_asstr(mfaii);

		if (q.empty())
			enomem();

		tofolder.reserve(q.size()+1);
		tofolder="+";
		tofolder += q;
		autoreply_from=cgi("autoresponse_from");
	}
	else if (strcmp(p, "purge") == 0)
	{
		tofolder = "exit";
	}
	else
	{
		tofolder=cgi("savefolder");
	}

	if (size_t n=fieldname.rfind(':');
		!fieldname.empty() && n+1 == fieldname.size())
	{
		fieldname.resize(n);
	}

	if (n >= mf.size())
	{
		mf.push_back({});
		n=mf.size()-1;
	}

	if (maildir_filter_ruleupdate(
		mf,
		mf[n],
		rulename,
		type,
		flags,
		fieldname,
		fieldvalue,
		tofolder.c_str(),
		autoreply_from,
		sqwebmail_content_charset.c_str(),
		err_num))
	{
		maildir::filter::save(mf, ".", login_returnaddr());
		clrfields();
		return;
	}

	internal_err="INTERNAL";
	if (err_num == MF_ERR_BADRULENAME)
		internal_err= "BADRULENAME";
	if (err_num == MF_ERR_EXISTS)
		internal_err= "EXISTS";
	if (err_num == MF_ERR_BADRULEHEADER)
		internal_err= "BADHEADER";
	if (err_num == MF_ERR_BADRULEVALUE)
		internal_err= "BADVALUE";
	if (err_num == MF_ERR_BADRULEFOLDER)
		internal_err= "ERRTOFOLDER";
	if (err_num == MF_ERR_BADFROMHDR)
		internal_err= "FROMHDR";

	cgi_put("internalerr", "1");
}

int mailfilter_folderused(const char *foldername)
{
	maildirfilter mf;
	bool	rc;

	if (!maildir::filter::has(".") ||
	    !maildir::filter::import("."))	return (0);

	rc=maildir::filter::load(mf, ".");
	maildir::filter::cancel(".");
	if (!rc)
		return (0);

	for (auto &r:mf)
	{
		if (r.tofolder.empty())	continue;
		if (r.tofolder == foldername)
		{
			return (-1);
		}
	}
	return (0);
}

int mailfilter_autoreplyused(const char *autoreply)
{
	maildirfilter mf;
	bool	rc;

	if (!maildir::filter::has(".") ||
	    !maildir::filter::import("."))	return (0);

	rc=maildir::filter::load(mf, ".");
	maildir::filter::cancel(".");
	if (!rc)
		return (0);

	for (auto &r:mf)
	{
		maildir_filter_autoresp_info mfai;

		if (r.tofolder.empty())	continue;
		if (r.tofolder[0] != '+')
			continue;

		if (!maildir_filter_autoresp_info_init_str(
			mfai,
			std::string_view{r.tofolder}.substr(1)))
			enomem();

		if (autoreply == mfai.name)
		{
			return (-1);
		}
	}
	return (0);
}

void mailfilter_cleanup()
{
	internal_err=0;
}
