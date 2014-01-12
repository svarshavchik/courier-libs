#include "config.h"
/*
*/

/*
** Copyright 2000-2010 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#include	"mailfilter.h"
#include	"sqwebmail.h"
#include	"maildir.h"
#include	"auth.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirfilter.h"
#include	"maildir/autoresponse.h"
#include	"numlib/numlib.h"
#include	"cgi/cgi.h"
#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>

extern void list_folder(const char *);
extern void output_attrencoded(const char *);
extern const char *sqwebmail_content_charset;

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
struct maildirfilter mf;
struct maildirfilterrule *r;
unsigned cnt;

	memset(&mf, 0, sizeof(mf));

	if (maildir_filter_loadmaildirfilter(&mf, "."))
	{
		maildir_filter_freerules(&mf);
		return;
	}

	for (cnt=0, r=mf.first; r; r=r->next, ++cnt)
	{
		char *p=unicode_convert_fromutf8(r->rulename_utf8,
						   sqwebmail_content_charset,
						   NULL);

		printf("<option value=\"%u\">", cnt);
		output_attrencoded(p ? p:r->rulename_utf8);
		printf("</option>");
		if (p)
			free(p);
	}
	maildir_filter_freerules(&mf);
}

void mailfilter_init()
{
const char *p;
unsigned n;
struct maildirfilter mf;
struct maildirfilterrule *r;

	if (*cgi("import"))
	{
		if (maildir_filter_importmaildirfilter("."))
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
		if (maildir_filter_exportmaildirfilter(".") ||
			maildir_filter_importmaildirfilter("."))
			printf("%s", getarg("INTERNAL"));
		else
			printf("%s", getarg("UPDATED"));
		clrfields();
	}

	if (*cgi("do.add"))
		clrfields();
	memset(&mf, 0, sizeof(mf));

	if (maildir_filter_loadmaildirfilter(&mf, "."))
	{
		maildir_filter_freerules(&mf);
		printf("%s", getarg("BADIMPORT"));
		return;
	}
	if (*(p=cgi("currentfilter")) == 0)
	{
		maildir_filter_freerules(&mf);
		return;
	}
	n=atoi(p);

	for (r=mf.first; n && r; r=r->next)
		--n;
	if (!r)
	{
		maildir_filter_freerules(&mf);
		return;
	}

	if (*cgi("do.moveup") && r)
	{
		maildir_filter_ruleup(&mf, r);
		maildir_filter_savemaildirfilter(&mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.movedown") && r)
	{
		maildir_filter_ruledown(&mf, r);
		maildir_filter_savemaildirfilter(&mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.delete") && r)
	{
		maildir_filter_ruledel(&mf, r);
		maildir_filter_savemaildirfilter(&mf, ".", login_returnaddr());
		clrfields();
	}
	else if (*cgi("do.edit"))
	{
	static char *namebuf=0;
	static char *headernamebuf=0;
	static char *headervaluebuf=0;
	static char *actionbuf=0;
	char	numbuf[NUMBUFSIZE+1];

		printf("<input name=\"currentfilternum\""
			" type=\"hidden\""
			" value=\"%s\" />", p);

		cgi_put("filtertype",
			r->type == startswith || 
			r->type == endswith ||
			r->type == contains ?
				r->flags & MFR_BODY ? "body":"header":
			r->type == hasrecipient
					? "hasrecipient":
			r->type == mimemultipart ?
				r->flags & MFR_DOESNOT ?
					"nothasmultipart":
					"hasmultipart":
			r->type == islargerthan ? "hassize":
			r->type == anymessage
					? "anymessage":
			r->type == textplain ?
				r->flags & MFR_DOESNOT ?
					"nothastextplain":
					"hastextplain":""
				) ;

		cgi_put("continuefiltering",
				r->flags & MFR_CONTINUE ? "1":"");

		cgi_put("headermatch",
			r->type == startswith ?
				r->flags & MFR_DOESNOT ? "notstartswith":"startswith":
			r->type == endswith ?
				r->flags & MFR_DOESNOT ? "notendswith":"endswith":
			r->type == contains ?
				r->flags & MFR_DOESNOT ? "notcontains":"contains":"");

		if (namebuf)	free(namebuf);
		p=r->rulename_utf8 ? r->rulename_utf8:"";

		namebuf=unicode_convert_fromutf8(p, sqwebmail_content_charset,
						   NULL);

		if (!namebuf)	enomem();
		cgi_put("rulename", namebuf);

		p=r->fieldname_utf8 ? r->fieldname_utf8:"";
		if (r->type != startswith &&
			r->type != endswith &&
			r->type != contains)	p="";
		if (r->flags & MFR_BODY)	p="";

		if (headernamebuf)	free(headernamebuf);
		headernamebuf=unicode_convert_fromutf8(p,
							 sqwebmail_content_charset,
							 NULL);
		if (!headernamebuf)	enomem();
		cgi_put("headername", headernamebuf);

		p=r->fieldvalue_utf8 ? r->fieldvalue_utf8:"";
		if (r->type != startswith &&
			r->type != endswith &&
			r->type != contains &&
			r->type != hasrecipient &&
			r->type != islargerthan)	p="";

		if (r->type == islargerthan)
			p=libmail_str_size_t( atol(p)+( r->flags & MFR_DOESNOT ? 1:0),
				numbuf);

		if (headervaluebuf)	free(headervaluebuf);

		headervaluebuf=
			unicode_convert_fromutf8(p,
						   sqwebmail_content_charset,
						   NULL);
		if (!headervaluebuf)	enomem();

		cgi_put("hasrecipientaddr", "");
		cgi_put("headervalue", "");
		cgi_put("bytecount", "");
		cgi_put("sizecompare", "");

		cgi_put(r->type == hasrecipient ? "hasrecipientaddr":
			r->type == islargerthan ? "bytecount":
				"headervalue", headervaluebuf);

		if (r->type == hasrecipient)
			cgi_put("hasrecipienttype",
				r->flags & MFR_DOESNOT ? "nothasrecipient":
					"hasrecipient");
		if (r->type == islargerthan)
			cgi_put("sizecompare",
				r->flags & MFR_DOESNOT
					? "issmallerthan":"islargerthan");
		if (actionbuf)	actionbuf=0;
		p=r->tofolder;
		if (!p)	p="";
		actionbuf=malloc(strlen(p)+1);
		if (!actionbuf)	enomem();
		strcpy(actionbuf, p);

		cgi_put("bouncemsg", "");
		cgi_put("forwardaddy", "");
		cgi_put("savefolder", "");

		cgi_put("autoresponse_regexp",
			r->flags & MFR_PLAINSTRING ? "":"1");

		if (actionbuf[0] == '!')
		{
			cgi_put("action", "forwardto");
			cgi_put("forwardaddy", actionbuf+1);
		}
		else if (actionbuf[0] == '*')
		{
			cgi_put("action", "bounce");
			cgi_put("bouncemsg", actionbuf+1);
		}
		else if (actionbuf[0] == '+')
		{
			struct maildir_filter_autoresp_info mfai;
			static char *autoresp_name_buf=0;
			static char days_buf[NUMBUFSIZE];
			static char *fromhdr=0;

			if (maildir_filter_autoresp_info_init_str(&mfai, actionbuf+1))
				enomem();

			if (autoresp_name_buf)
				free(autoresp_name_buf);

			if ((autoresp_name_buf=strdup(mfai.name)) == NULL)
				enomem();

			cgi_put("action", "autoresponse");
			cgi_put("autoresponse_choose", autoresp_name_buf);
			cgi_put("autoresponse_dsn",
				mfai.dsnflag ? "1":"");
			
			cgi_put("autoresponse_dupe",
				mfai.days > 0 ? "1":"");

			libmail_str_size_t(mfai.days, days_buf);
			cgi_put("autoresponse_days", mfai.days ?
				days_buf:"");
			maildir_filter_autoresp_info_free(&mfai);

			if (fromhdr)
				free(fromhdr);
			fromhdr=strdup(r->fromhdr ? r->fromhdr:"");
			if (!fromhdr)
				enomem();
			cgi_put("autoresponse_from", fromhdr);

			if (mfai.noquote)
				cgi_put("autoresponse_noquote", "1");
		}
		else if (strcmp(actionbuf, "exit") == 0)
		{
			cgi_put("action", "purge");
		}
		else
		{
			cgi_put("action", "savefolder");
			cgi_put("savefolder",
				strcmp(actionbuf, ".") == 0 ? INBOX:
				*actionbuf == '.' ? actionbuf+1:actionbuf);
		}
	}
	else if (*(p=cgi("currentfilternum")) != 0)
	{
		printf("<input name=\"currentfilternum\""
			" type=\"hidden\""
			" value=\"%s\" />", p);
	}
	
	

	maildir_filter_freerules(&mf);
}

void mailfilter_listfolders()
{
char	**folders;
int	i;
const char *f=cgi("savefolder");

	printf("<select name=\"savefolder\">");

	maildir_listfolders(INBOX, ".", &folders);

	for (i=0; folders[i]; i++)
	{
		const char *p=folders[i];
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

	maildir_freefolders(&folders);
	printf("</select>");
}

void mailfilter_submit()
{
struct maildirfilter mf;
struct maildirfilterrule *r;
const char *p;
enum maildirfiltertype type;
int flags=0;
const char *rulename=0;
const char *fieldname=0;
const char *fieldvalue=0;
char *tofolder=0;
char *fieldname_cpy;
int	err_num;
char	numbuf[NUMBUFSIZE];
const char *autoreply_from="";

	memset(&mf, 0, sizeof(mf));
	if (maildir_filter_loadmaildirfilter(&mf, "."))
	{
		maildir_filter_freerules(&mf);
		return;
	}

	r=0;
	p=cgi("currentfilternum");
	if (*p)
	{
	unsigned n=atoi(p);

		for (r=mf.first; r && n; r=r->next)
			--n;
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
			flags |= MFR_BODY;

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
		flags |= MFR_CONTINUE;

	if (!*cgi("autoresponse_regexp"))
		flags |= MFR_PLAINSTRING;

	p=cgi("action");
	if (strcmp(p, "forwardto") == 0)
	{
		p=cgi("forwardaddy");
		tofolder=malloc(strlen(p)+2);
		if (!tofolder)	enomem();
		strcat(strcpy(tofolder, "!"), p);
	}
	else if (strcmp(p, "bounce") == 0)
	{
		p=cgi("bouncemsg");
		tofolder=malloc(strlen(p)+2);
		if (!tofolder)	enomem();
		strcat(strcpy(tofolder, "*"), p);
	}
	else if (strcmp(p, "autoresponse") == 0)
	{
		struct maildir_filter_autoresp_info mfaii;
		char *q;

		p=cgi("autoresponse_choose");

		if (maildir_filter_autoresp_info_init(&mfaii, p))
		{
			internal_err="AUTOREPLY";
			cgi_put("internal_err", "1");
			return;
		}

		p=cgi("autoresponse_dsn");

		if (*p)
			mfaii.dsnflag=1;

		p=cgi("autoresponse_dupe");
		if (*p)
		{
			p=cgi("autoresponse_days");
			mfaii.days=atoi(p);
		}

		p=cgi("autoresponse_noquote");

		if (*p)
			mfaii.noquote=1;

		q=maildir_filter_autoresp_info_asstr(&mfaii);
		maildir_filter_autoresp_info_free(&mfaii);

		if (!q)
			enomem();

		if (!(tofolder=malloc(strlen(q)+2)))
		{
			free(q);
			enomem();
		}

		tofolder[0]='+';
		strcpy(tofolder+1, q);
		free(q);
		autoreply_from=cgi("autoresponse_from");
	}
	else if (strcmp(p, "purge") == 0)
	{
		tofolder = strdup("exit");
		if (!tofolder) enomem();
	}
	else
	{
		tofolder=strdup(cgi("savefolder"));
		if (!tofolder)	enomem();
	}

	fieldname_cpy=NULL;

	if (fieldname)
	{
		char *p;

		fieldname_cpy=strdup(fieldname);

		p=strrchr(fieldname_cpy, ':');

		if (p && p[1] == 0)
			*p=0;
	}

	if (!r)
		r=maildir_filter_appendrule(&mf, rulename, type, flags, fieldname_cpy,
					    fieldvalue, tofolder, autoreply_from, sqwebmail_content_charset, &err_num);
	else if (maildir_filter_ruleupdate(&mf, r, rulename, type, flags, fieldname_cpy,
					   fieldvalue, tofolder, autoreply_from, sqwebmail_content_charset, &err_num))
		r=0;
	free(tofolder);
	if (fieldname_cpy)
		free(fieldname_cpy);
	if (r)
	{
		maildir_filter_savemaildirfilter(&mf, ".", login_returnaddr());
		maildir_filter_freerules(&mf);
		clrfields();
		return;
	}
	maildir_filter_freerules(&mf);

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
struct maildirfilter mf;
struct maildirfilterrule *r;
int	rc;

	memset(&mf, 0, sizeof(mf));
	if (maildir_filter_hasmaildirfilter(".") ||
		maildir_filter_importmaildirfilter("."))	return (0);

	rc=maildir_filter_loadmaildirfilter(&mf, ".");
	maildir_filter_endmaildirfilter(".");
	if (rc)
	{
		maildir_filter_freerules(&mf);
		return (0);
	}

	for (r=mf.first; r; r=r->next)
	{
		if (r->tofolder == 0)	continue;
		if (strcmp(foldername, r->tofolder) == 0)
		{
			maildir_filter_freerules(&mf);
			return (-1);
		}
	}
	maildir_filter_freerules(&mf);
	return (0);
}

int mailfilter_autoreplyused(const char *autoreply)
{
	struct maildirfilter mf;
	struct maildirfilterrule *r;
	int	rc;

	memset(&mf, 0, sizeof(mf));
	if (maildir_filter_hasmaildirfilter(".") ||
	    maildir_filter_importmaildirfilter("."))	return (0);

	rc=maildir_filter_loadmaildirfilter(&mf, ".");
	maildir_filter_endmaildirfilter(".");
	if (rc)
	{
		maildir_filter_freerules(&mf);
		return (0);
	}

	for (r=mf.first; r; r=r->next)
	{
		struct maildir_filter_autoresp_info mfai;

		if (r->tofolder == 0)	continue;
		if (r->tofolder[0] != '+')
			continue;

		if (maildir_filter_autoresp_info_init_str(&mfai, r->tofolder+1))
			enomem();

		if (strcmp(autoreply, mfai.name) == 0)
		{
			maildir_filter_autoresp_info_free(&mfai);
			maildir_filter_freerules(&mf);
			return (-1);
		}
		maildir_filter_autoresp_info_free(&mfai);
	}
	maildir_filter_freerules(&mf);
	return (0);
}

void mailfilter_cleanup()
{
	internal_err=0;
}
