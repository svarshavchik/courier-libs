/*
** Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	"sqwebmail.h"
#include	"newmsg.h"
#include	"cgi/cgi.h"
#include	"sqconfig.h"
#include	"auth.h"
#include	"maildir.h"
#include	"token.h"
#include	"pref.h"
#include	"folder.h"
#include	"filter.h"
#include	"gpg.h"
#include	"addressbook.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirgetquota.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"gpglib/gpglib.h"
#include	"courierauth.h"
#include	<string.h>
#include	<stdio.h>
#include	<signal.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	<ctype.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	<errno.h>
#include	"htmllibdir.h"
#include	<courier-unicode.h>

extern const char *sqwebmail_content_charset;
extern int spell_start(const char *);
extern const char *sqwebmail_mailboxid;
extern const char *sqwebmail_folder;
extern void print_safe_len(const char *, size_t, void (*)(const char *, size_t));
extern void call_print_safe_to_stdout(const char *, size_t);
extern void print_attrencodedlen(const char *, size_t, int, FILE *);
extern void output_attrencoded_nltobr(const char *);
extern void output_attrencoded_oknl(const char *);
extern void output_attrencoded(const char *);
extern void output_scriptptrget();
extern void output_form(const char *);
extern void output_urlencoded(const char *);

extern char *newmsg_newdraft(const char *, const char *, const char *,
				const char *);
extern char *newmsg_createdraft(const char *);
extern char *newmsg_createsentmsg(const char *, int *);
extern int ishttps();

static void newmsg_header(const char *label, const char *field,
			  const char *encoded, const char *val)
{
int		hdrmaxlen=512;
const char	*p=getarg("HDRMAXLEN");

	if (p && (atoi(p) > hdrmaxlen))
		hdrmaxlen=atoi(p);

	printf("<tr><th align=\"right\"><p class=\"new-message-header\">"
	       "<span class=\"new-message-header-%s\">%s</span></p></th>"
	       "<td width=\"6\">&nbsp;</td>",
	       field, label);

	printf("<td><input type=\"text\" name=\"%s\" size=\"50\" maxlength=\"%d\""
	       " class=\"new-message-header-text\" value=\"",
		field, hdrmaxlen);
	if (encoded)
	{
		char	*s;

		s=rfc822_display_hdrvalue_tobuf("subject",
						encoded,
						sqwebmail_content_charset,
						NULL,
						NULL);

		if (!s)
			s=strdup(encoded);

		if (!s)	enomem();
		output_attrencoded(s);
		free(s);
	}
	else if (val)
		output_attrencoded(val);
	printf("\" /></td></tr>\n");
}

static void printc(char c, void *dummy)
{
	char b[2];

	b[0]=c;
	b[1]=0;
	output_attrencoded(b);
}

static void printsep(const char *c, void *dummy)
{
	output_attrencoded(c);
}

static void newmsg_header_rfc822(const char *label, const char *field,
				 const char *encoded, const char *val,
				 int is_readonly)
{
int		hdrmaxlen=512;
const char	*p=getarg("HDRMAXLEN");

	if (p && (atoi(p) > hdrmaxlen))
		hdrmaxlen=atoi(p);

	printf("<tr><th align=\"right\"><p class=\"new-message-header\">"
	       "<span class=\"new-message-header-%s\">%s</span></p></th>"
	       "<td width=\"6\">&nbsp;</td>",
	       field, label);

	printf("<td><input type=\"text\" name=\"%s\" size=\"50\" maxlength=\"%d\""
	       " class=\"new-message-header-text\" value=\"",
		field, hdrmaxlen);
	if (encoded)
	{
		struct rfc822t *t=rfc822t_alloc_new(encoded, NULL, NULL);
		struct rfc822a *a=t ? rfc822a_alloc(t):NULL;

		if (a)
		{
			rfc2047_print_unicodeaddr(a, sqwebmail_content_charset,
						  printc,
						  printsep, NULL);
			rfc822a_free(a);
		}

		if (t)
			rfc822t_free(t);
	}
	else if (val)
		output_attrencoded(val);
	printf("\"%s /></td></tr>\n",
		is_readonly ? " readonly=\"readonly\"":"");
}

static const char *ispreviewmsg()
{
const char *p=cgi("previewmsg");

	if (*p == 0)
		p=cgi("addressbook_to");

	if (*p == 0)
		p=cgi("addressbook_cc");

	if (*p == 0)
		p=cgi("addressbook_bcc");

	return (p);
}

void newmsg_hiddenheader(const char *label, const char *value)
{
	printf("<input type=\"hidden\" name=\"%s\" value=\"", label);
	output_attrencoded(value);
	printf("\" />");
}

/* ---------------------------------------------------- */

static size_t show_textarea_start_of_line(struct show_textarea_info *,
					  const char *, size_t);

static size_t show_textarea_quoted_text(struct show_textarea_info *,
					const char *, size_t);

static size_t show_textarea_notseen_sp(struct show_textarea_info *,
				       const char *, size_t);

static size_t show_textarea_seen_sp(struct show_textarea_info *,
				    const char *, size_t);

static size_t show_textarea_seen_spnl(struct show_textarea_info *,
				      const char *, size_t);

static size_t show_textarea_check_sig(struct show_textarea_info *,
				      const char *, size_t);

static size_t show_textarea_ignore_sig(struct show_textarea_info *,
				       const char *, size_t);

void show_textarea_init(struct show_textarea_info *info,
			int stop_at_sig)
{
	info->handler=show_textarea_start_of_line;
	info->stop_at_sig=stop_at_sig;
}

void show_textarea(struct show_textarea_info *info,
		   const char *ptr, size_t cnt)
{
	while (cnt)
	{
		size_t n= (*info->handler)(info, ptr, cnt);

		cnt -= n;
		ptr += n;
	}
}

static size_t show_textarea_start_of_line(struct show_textarea_info *info,
					  const char *ptr, size_t cnt)
{
	if (*ptr == '>')
	{
		info->handler=show_textarea_quoted_text;
		return show_textarea_quoted_text(info, ptr, cnt);
	}

	info->handler=show_textarea_notseen_sp;

	if (*ptr == ' ')
		return 1; /* Consume space-stuffed space */

	if (info->stop_at_sig)
	{
		info->handler=show_textarea_check_sig;
		info->sig_index=0;
		return show_textarea_check_sig(info, ptr, cnt);
	}
	return show_textarea_notseen_sp(info, ptr, cnt);
}

static size_t show_textarea_quoted_text(struct show_textarea_info *info,
					const char *ptr, size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; i++)
	{
		if (ptr[i] == '\n')
		{
			++i;
			info->handler=show_textarea_start_of_line;
			break;
		}
	}

	if (i)
		print_attrencodedlen(ptr, i, 1, stdout);
	return i;
}

static size_t show_textarea_notseen_sp(struct show_textarea_info *info,
				       const char *ptr, size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; i++)
	{
		if (ptr[i] == '\n')
		{
			++i;
			info->handler=show_textarea_start_of_line;
			break;
		}

		if (ptr[i] == ' ')
		{
			if (i)
				print_attrencodedlen(ptr, i, 1, stdout);
			info->handler=show_textarea_seen_sp;
			return i+1;
		}
	}

	if (i)
		print_attrencodedlen(ptr, i, 1, stdout);
	return i;
}

static size_t show_textarea_seen_sp(struct show_textarea_info *info,
				    const char *ptr, size_t cnt)
{
	if (*ptr != '\n')
	{
		info->handler=show_textarea_notseen_sp;
		print_attrencodedlen(" ", 1, 1, stdout);
		return show_textarea_notseen_sp(info, ptr, cnt);
	}

	info->handler=show_textarea_seen_spnl;
	return 1;
}

static size_t show_textarea_seen_spnl(struct show_textarea_info *info,
				      const char *ptr, size_t cnt)
{
	if (*ptr == '>' || *ptr == '\n') /* Fix this */
	{
		print_attrencodedlen("\n", 1, 1, stdout);
		return show_textarea_start_of_line(info, ptr, cnt);
	}

	info->handler=show_textarea_notseen_sp;
	return show_textarea_notseen_sp(info, ptr, cnt);
}

static size_t show_textarea_check_sig(struct show_textarea_info *info,
				      const char *ptr, size_t cnt)
{
	static const char sig[]={'-', '-', ' ', '\n'};
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		if (i + info->sig_index >= sizeof(sig))
		{
			info->handler=show_textarea_ignore_sig;
			return cnt;
		}

		if (ptr[i] != sig[i+info->sig_index])
		{
			info->handler=show_textarea_notseen_sp;

			show_textarea(info, sig, info->sig_index);
			show_textarea(info, ptr, cnt);
			return cnt;
		}
	}

	info->sig_index += cnt;
	return cnt;
}

static size_t show_textarea_ignore_sig(struct show_textarea_info *info,
				       const char *ptr, size_t cnt)
{
	return cnt;
}

/*
** Return all from/to/cc/bcc addresses in the message.
*/

char *newmsg_alladdrs(FILE *fp)
{
	char	*headers=NULL;
	struct rfc822t *t;
	struct rfc822a *a;
	char *p, *q;
	int l, i;

	if (fp)
	{
		char *header, *value;

		rewind(fp);

		/* First, combine all the headers into one header. */

		while ((header=maildir_readheader(fp, &value, 1)) != 0)
		{
			char *newh;

			if (strcmp(header, "from") &&
			    strcmp(header, "to") &&
			    strcmp(header, "cc") &&
			    strcmp(header, "bcc"))
				continue;

			if (headers)
			{
				newh=realloc(headers, strlen(headers)
					     +strlen(value)+2);
				if (!newh)
					continue;
				strcat(newh, ",");
				headers=newh;
			}
			else
			{
				newh=malloc(strlen(value)+1);
				if (!newh)
					continue;
				*newh=0;
				headers=newh;
			}
			strcat(headers, value);
		}

	}

	/* Now, parse the header, and extract the addresses */

	t=rfc822t_alloc_new(headers ? headers:"", NULL, NULL);
	a= t ? rfc822a_alloc(t):NULL;

	l=1;
	for (i=0; i < (a ? a->naddrs:0); i++)
	{
		p=rfc822_getaddr(a, i);
		if (p)
		{
			++l;
			l +=strlen(p);
			free(p);
		}
	}
	p=malloc(l);
	if (p)
		*p=0;

	for (i=0; i < (a ? a->naddrs:0); i++)
	{
		q=rfc822_getaddr(a, i);
		if (q)
		{
			if (p)
			{
				strcat(strcat(p, q), "\n");
			}
			free(q);
		}
	}

	rfc822a_free(a);
	rfc822t_free(t);
	free(headers);
	return (p);
}

static int show_textarea_trampoline(const char *ptr, size_t cnt, void *arg)
{
	show_textarea( (struct show_textarea_info *)arg, ptr, cnt);
	return 0;
}

void newmsg_showfp(FILE *fp, int *attachcnt)
{
	struct	rfc2045 *p=rfc2045_fromfp(fp), *q;

	if (!p)	enomem();

	/* Here's a nice opportunity to count all attachments */

	*attachcnt=0;

	for (q=p->firstpart; q; q=q->next)
		if (!q->isdummy)	++*attachcnt;
	if (*attachcnt)	--*attachcnt;
	/* Not counting the 1st MIME part */

	{
		const char *content_type;
		const char *content_transfer_encoding;
		const char *charset;

		rfc2045_mimeinfo(p, &content_type,
				 &content_transfer_encoding, &charset);

		if (content_type &&
		    strcmp(content_type, "multipart/alternative") == 0)
			*attachcnt=0;
	}

	q=rfc2045_searchcontenttype(p, "text/plain");

	if (q)
	{
		struct rfc2045src *src=rfc2045src_init_fd(fileno(fp));

		if (src)
		{
			struct show_textarea_info info;

			show_textarea_init(&info, 1);

			rfc2045_decodetextmimesection(src, q,
						      sqwebmail_content_charset,
						      NULL,
						      &show_textarea_trampoline,
						      &info);
			rfc2045src_deinit(src);
			show_textarea(&info, "\n", 1);
		}
	}
	rfc2045_free(p);
}

void newmsg_preview(const char *p)
{
	size_t pos;

	maildir_remcache(INBOX "." DRAFTS);
	if (maildir_name2pos(INBOX "." DRAFTS, p, &pos) == 0)
	{
		const char *save_folder=sqwebmail_folder;
		cgi_put("showdraft", "1");
		sqwebmail_folder=INBOX "." DRAFTS;
		folder_showmsg(INBOX "." DRAFTS, pos);
		sqwebmail_folder=save_folder;
				/* show_preview(draftmessagefilename); */
	}
}

/* ---------------------------------------------------- */

void newmsg_init(const char *folder, const char *pos)
{
	const char	*tolab=getarg("TOLAB");
	const char	*cclab=getarg("CCLAB");
	const char	*bcclab=getarg("BCCLAB");
	const char	*subjectlab=getarg("SUBJECTLAB");
	const char	*messagelab=getarg("MESSAGELAB");
	const char	*sendlab=getarg("SENDLAB");
	const char	*previewlab=getarg("PREVIEWLAB");
	const char	*forwardsep=getarg("FORWARDLAB");
	const char	*savedraft=getarg("SAVEDRAFT");
	const char	*attachmentslab=getarg("ATTACHMENTS");
	const char      *uploadlab=getarg("UPLOAD");
	const char	*replysalutation=getarg("SALUTATION");
	const char	*checkspellingdone=getarg("SPELLCHECKDONE");
	const char	*checkspelling=getarg("CHECKSPELLING");
	const char	*quotaerr=getarg("QUOTAERR");
	const char	*fromlab=getarg("FROMLAB");
	const char	*replytolab=getarg("REPLYTOLAB");
	const char	*addressbooklab=getarg("ADDRESSBOOK");
	const char	*select1=getarg("SELECT1");
	const char	*select2=getarg("SELECT2");
	const char	*text1=getarg("TEXT1");
	const char	*text2=getarg("TEXT2");
	char	*draftmessage;
	char	*draftmessagefilename;
	const	char *p;
	FILE	*fp;
	int	attachcnt=0;
	char	*cursubj, *curto, *curcc, *curbcc, *curfrom, *curreplyto;
	int wbnochangingfrom;

	/* Picking up an existing draft? */

	p=cgi("draft");
	if (*p)
	{
		CHECKFILENAME(p);
	}

	if (*p)
	{
		draftmessage=strdup(p);
		if (!draftmessage)	enomem();
		p="";
	}
	else
	{
		draftmessage=newmsg_newdraft(folder, pos,
			forwardsep, replysalutation);

		if (!draftmessage)
		{
			if (*ispreviewmsg())
			{
				p=cgi("draftmessage");
				if (*p)
				{
					CHECKFILENAME(p);
				}
				draftmessage=newmsg_createdraft(p);
			}
		}
	}

	draftmessagefilename= draftmessage ?
				 maildir_find(INBOX "." DRAFTS, draftmessage):0;

	if (*(p=cgi("previewmsg")))
	{
#ifdef	ISPELL
		if (strcmp(p, "SPELLCHK") == 0)
			printf("%s<br /><br />\n", checkspellingdone);
#endif
		printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"1\" class=\"box-small-outer\"><tr><td>\n");
		printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\" class=\"preview\"><tr><td>\n");

		if (draftmessagefilename)
		{
			const char *p=strrchr(draftmessagefilename, '/');

			if (p)
				++p;
			else
				p=draftmessagefilename;

			newmsg_preview(p);
		}
		printf("</td></tr></table>\n");
		printf("</td></tr></table>\n");

		printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"6\"><tr><td><hr width=\"80%%\" /></td></tr></table>\n");
	}

	printf("<input type=\"hidden\" name=\"form\" value=\"donewmsg\" />\n");
	newmsg_hiddenheader("pos", pos);
	newmsg_hiddenheader("focusto",
			    *cgi("newmsg") ? "headers":"text");

	/* Generate unique message token, to detect duplicate SUBMITs */

	tokennew();

	/* Display any error message */

	if (*cgi("foldermsg"))
	{
		printf("<p><span class=\"error\" style=\"color: #ff0000\">");
		output_attrencoded_nltobr(cgi("foldermsg"));
		printf("</span></p>");
	}

	if (strcmp(cgi("error"), "quota") == 0)
		printf("%s", quotaerr);

	/* Read message from the draft file */

	cursubj=0;
	curto=0;
	curfrom=0;
	curreplyto=0;
	curcc=0;
	curbcc=0;
	fp=0;

	if (draftmessagefilename)
	{
	int	x=maildir_safeopen(draftmessagefilename, O_RDONLY, 0);

		if (x >= 0)
			if ((fp=fdopen(x, "r")) == 0)
				close(x);
	}

	if (fp != 0)
	{
	char *header, *value;

		while ((header=maildir_readheader(fp, &value, 0)) != 0)
		{
		char	**rfchp=0;

			if (strcmp(header, "subject") == 0)
			{
				if (!cursubj && !(cursubj=strdup(value)))
					enomem();
				continue;
			}

			while (*value && isspace(*value))
				++value;

			if (strcmp(header, "from") == 0)
				rfchp= &curfrom;
			if (strcmp(header, "reply-to") == 0)
				rfchp= &curreplyto;
			if (strcmp(header, "to") == 0)
				rfchp= &curto;
			if (strcmp(header, "cc") == 0)
				rfchp= &curcc;
			if (strcmp(header, "bcc") == 0)
				rfchp= &curbcc;
			if (rfchp)
			{
			char	*newh=malloc ( (*rfchp ? strlen(*rfchp)+2:1)
					+strlen(value));

				if (!newh)	enomem();
				strcpy(newh, value);
				if (*rfchp)
					strcat(strcat(newh, ","), *rfchp);
				if (*rfchp)	free( *rfchp );
				*rfchp=newh;
			}
		}
	}

	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"1\" class=\"box-small-outer\"><tr><td>\n");
	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\" class=\"new-message-box\"><tr><td>\n");

	printf("<table border=\"0\" width=\"100%%\">\n");
	wbnochangingfrom=auth_getoptionenvint("wbnochangingfrom");
	if (wbnochangingfrom < 2)
		newmsg_header_rfc822(fromlab, "headerfrom", curfrom,
			*cgi("from") ? cgi("from"):
			pref_from && *pref_from ? pref_from:
			login_fromhdr(), wbnochangingfrom ? 1:0);

	printf("<tr valign=\"middle\"><th align=\"right\">"
	       "<p class=\"new-message-header\">"
	       "<span class=\"new-message-header-addressbook\">"
	       "%s</span></p></th><td width=\"6\">&nbsp;</td>",
	       addressbooklab);

	printf("<td valign=\"middle\">");
	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"4\">");
	printf("<tr valign=\"middle\"><td>%s\n", select1);
	ab_listselect();
	printf("%s</td><td width=\"100%%\">", select2);
	printf("<input type=\"submit\" name=\"addressbook_to\" value=\"%s\" />",
			tolab);
	printf("<input type=\"submit\" name=\"addressbook_cc\" value=\"%s\" />",
			cclab);
	printf("<input type=\"submit\" name=\"addressbook_bcc\" value=\"%s\" />",
			bcclab);
	printf("</td></tr></table>");

	printf("</td></tr>\n");

#if 0
			{
				FILE *fp;
				fp=fopen("/tmp/pid", "w");
				fprintf(fp, "%d", getpid());
				fclose(fp);
				sleep(10);
			}
#endif

	newmsg_header_rfc822(tolab, "headerto", curto, cgi("to"), 0);
	newmsg_header_rfc822(cclab, "headercc", curcc, cgi("cc"), 0);
	newmsg_header_rfc822(bcclab, "headerbcc", curbcc, cgi("bcc"), 0);
	newmsg_header_rfc822(replytolab, "headerreply-to",
			     curreplyto, cgi("replyto"), 0);
	newmsg_header(subjectlab, "headersubject",
		      cursubj, cgi("subject"));

	if (curto)	free(curto);
	if (curfrom)	free(curfrom);
	if (curreplyto)	free(curreplyto);
	if (curcc)	free(curcc);
	if (curbcc)	free(curbcc);
	if (cursubj)	free(cursubj);

	printf("<tr><td colspan=\"3\"><hr width=\"100%%\" /></td></tr>");
	printf("<tr>"

	       "<th valign=\"top\" align=\"right\">"
	       "<p class=\"new-message-header\">"
	       "<span class=\"new-message-header-message\">"
	       "%s</span></p></th><td width=\"6\">&nbsp;</td>"
	       "<td><select name=\"textformat\">"
	       "<option value=\"plain\" %s>%s</option>"
	       "<option value=\"wiki\" %s>%s</option></select>"

	       "<small>(<a href='%s/wikifmt.html' target='_blank'>%s</a>)</small>"
	       "</td></tr>"
	       "<tr><th valign=\"top\" align=\"right\">"
	       "<p class=\"new-message-header\">"
	       "<span class=\"new-message-header-message\">"
	       "%s</span></p></th><td width=\"6\">&nbsp;</td>",

	       getarg("FMTNAME"),
	       pref_wikifmt ? "":"selected='selected'", getarg("FMTTEXTPLAIN"),
	       pref_wikifmt ? "selected='selected'":"", getarg("FMTTEXTWIKI"),

	       get_imageurl(), getarg("FMTHELP"),
	       messagelab);

	printf("<td>%s\n", text1);

	if (fp)
	{
		newmsg_showfp(fp, &attachcnt);
	}
	else
	{
		printf("%s", cgi("body"));
	}
	printf("%s\n", text2);

	if (draftmessage && *draftmessage)
	{
		printf("<input type=\"hidden\" name=\"draftmessage\" value=\"");
		output_attrencoded(draftmessage);

		printf("\" />");
	}
	if (draftmessage)	free(draftmessage);
	printf("</td></tr>\n");

	printf("<tr><th valign=\"top\" align=\"right\">"
	       "<p class=\"new-message-header\">"
	       "<span class=\"new-message-header-message\">"
		"%s</span></p></th><td>&nbsp;</td>"
		"<td>%d&nbsp;&nbsp;<input type=\"submit\" name=\"doattachments\" value=\"%s\" /></td></tr>",
		attachmentslab, attachcnt, uploadlab);

	printf("<tr><td colspan=\"2\" align=\"right\"><input type=\"checkbox\" "
	       "name=\"fcc\" id=\"fcc\"%s /></td><td><label for=\"fcc\">%s</label></td></tr>\n",
	       pref_noarchive ? "":" checked=\"checked\"",
	       getarg("PRESERVELAB"));

	if (auth_getoptionenvint("wbnodsn") == 0)
		printf("<tr><td colspan=\"2\" align=\"right\"><input type=\"checkbox\" "
		       "name=\"dsn\" id=\"dsn\" /></td><td><label for=\"dsn\">%s</label></td></tr>\n",
		       getarg("DSN"));

	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		char *all_addr;

		printf("<tr><td colspan=\"2\" align=\"right\"><input type=\"checkbox\" "
		       "name=\"sign\" id=\"sign\" /></td><td><label for=\"sign\">%s</label><select name=\"signkey\">",
		       getarg("SIGNLAB"));
		gpgselectkey();
		printf("</select></td></tr>\n");

		all_addr=newmsg_alladdrs(fp);

		printf("<tr valign=\"middle\"><td colspan=\"2\" align=\"right\">"
		       "<input type=\"checkbox\" name=\"encrypt\" id=\"encrypt\" /></td>"
		       "<td><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr valign=\"middle\"><td><label for=\"encrypt\">%s</label></td><td><select size=\"4\" multiple=\"multiple\" name=\"encryptkey\">",
		       getarg("ENCRYPTLAB"));
		gpgencryptkeys(all_addr);
		printf("</select></td></tr>\n");
		printf("</table></td></tr>\n");

		if (ishttps())
			printf("<tr><td colspan=\"2\" align=\"left\">&nbsp;</td><td>%s<input type=\"password\" name=\"passphrase\" /></td></tr>\n",
			       getarg("PASSPHRASE"));

		if (all_addr)
			free(all_addr);
	}

	if (fp)
		fclose(fp);
	if (draftmessagefilename)
		free(draftmessagefilename);

	printf("<tr><td colspan=\"2\">&nbsp;</td><td>");
	printf("<input type=\"submit\" name=\"previewmsg\" value=\"%s\" />",
		previewlab);
	printf("<input type=\"submit\" name=\"sendmsg\" value=\"%s\" />",
		sendlab);
	printf("<input type=\"submit\" name=\"savedraft\" value=\"%s\" />",
		savedraft);
#ifdef	ISPELL
	printf("<input type=\"submit\" name=\"startspellchk\" value=\"%s\" />",
		checkspelling);
#endif
	printf("</td></tr>\n");
	printf("</table>\n");

	printf("</td></tr></table>\n");
	printf("</td></tr></table>\n");
}

static const char *geterrbuf(int fd)
{
static char errbuf[512];
char	*errbufptr=errbuf;
size_t	errbufleft=sizeof(errbuf)-1;

	while (errbufleft)
	{
	int	l=read(fd, errbufptr, errbufleft);

		if (l <= 0)	break;
		errbufptr += l;
		errbufleft -= l;
	}
	*errbufptr=0;
	return (errbuf);
}

static int waitfor(pid_t pid)
{
pid_t	childpid;
int	wait_stat;

	while ((childpid=wait(&wait_stat)) != pid)
		if (childpid == -1)	return (-1);

	return (wait_stat);
}

void sendmsg_done()
{
	if ( *cgi("pos"))
		http_redirect_argss("&form=readmsg&pos=%s", cgi("pos"), "");
	else if (*cgi("sendmsg"))
		http_redirect_argss("&form=folders&foldermsg=sent", "", "");
	else
		http_redirect_argss("&form=folders", "", "");
}

static int dosendmsg(const char *origdraft)
{
pid_t	pid;
const	char *returnaddr;
int	pipefd1[2];
char	*filename;
const char *line;
char	*draftmessage;
int	isgpgerr;
unsigned long filesize;
struct stat stat_buf;
int dsn;

	if (tokencheck()) /* Duplicate submission - message was already sent */
	{
		sendmsg_done();
		return (1);
	}

	if (strcmp(cgi("form"), "doattach") == 0)
	{
		/* When called from the attachment window, we do NOT create
		** a new draft message */

		draftmessage=strdup(origdraft);
	}
	else
		draftmessage=newmsg_createdraft(origdraft);
	if (!draftmessage)
		enomem();

	filename=newmsg_createsentmsg(draftmessage, &isgpgerr);

	if (!filename)
	{
		char *draftbase=maildir_basename(draftmessage);

		if (isgpgerr)
		{
			cgi_put("draftmessage", draftbase);
			output_form("gpgerr.html");
		}
		else
		{
			http_redirect_argss("&form=newmsg&pos=%s"
					    "&draft=%s&error=quota",
					    cgi("pos"), draftbase);
		}
		free(draftmessage);
		free(draftbase);
		return (1);
	}

	if (pipe(pipefd1) != 0)
	{
		cgi_put("foldermsg", "ERROR: pipe() failed.");
		maildir_msgpurgefile(INBOX "." SENT, filename);
		free(filename);
		free(draftmessage);
		return (0);
	}

	dsn= *cgi("dsn") != 0;

	pid=fork();
	if (pid < 0)
	{
		cgi_put("foldermsg", "ERROR: fork() failed.");
		close(pipefd1[0]);
		close(pipefd1[1]);
		maildir_msgpurgefile(INBOX "." SENT, filename);
		free(filename);
		free(draftmessage);
		return (0);
	}

	if (pid == 0)
	{
	static const char noexec[]="ERROR: Unable to execute sendit.sh.\n";
	static const char nofile[]="ERROR: Temp file not available - probably exceeded quota.\n";
	char	*tmpfile=maildir_find(INBOX "." SENT, filename);
	int	fd;

		if (!tmpfile)
		{
			if (fwrite((char*)nofile, 1, sizeof(nofile)-1, stderr))
				; /* ignore */
			_exit(1);
		}

		close(0);

		fd=maildir_safeopen(tmpfile, O_RDONLY, 0);
		dup2(pipefd1[1], 1);
		dup2(pipefd1[1], 2);
		close(pipefd1[0]);
		close(pipefd1[1]);

		if (dsn)
			putenv("DSN=-Nsuccess,delay,failure");
		else
			putenv("DSN=");

		if (fd == 0)
		{
			returnaddr=login_returnaddr();
			execl(SENDITSH, "sendit.sh", returnaddr,
				sqwebmail_mailboxid, NULL);
		}
			
		if (fwrite(noexec, 1, sizeof(noexec)-1, stderr))
			; /* ignore */
		_exit(1);
	}
	close(pipefd1[1]);

	line=geterrbuf(pipefd1[0]);
	close(pipefd1[0]);

	if (waitfor(pid))
	{
		if (!*line)
			line="Unable to send message.\n";
	}
	else
		line="";

	if (*line == 0)	/* Succesfully sent message */
	{
		if (*draftmessage)
		{
		char	*base=maildir_basename(draftmessage);
		char	*draftfile=maildir_find(INBOX "." DRAFTS, base);

			free(base);

			/* Remove draft file */

			if (draftfile)
			{
			char	*replytofolder=0, *replytomsg=0;
			char	*header, *value;
			FILE	*fp;
			int	x;

				fp=0;
				x=maildir_safeopen(draftfile, O_RDONLY, 0);
				if ( maildir_parsequota(draftfile, &filesize))
				{
					if (x < 0 || fstat(x, &stat_buf))
						stat_buf.st_size=0;
					filesize=stat_buf.st_size;
				}

				if (x >= 0)
					if ((fp=fdopen(x, "r")) == 0)
						close(x);

				/* First, look for a message that we should
				** mark as replied */

				while (fp && (header=maildir_readheader(fp,
						&value, 0)) != 0)
				{
					if (strcmp(header,"x-reply-to-folder")
						== 0 && !replytofolder)
					{
						replytofolder=strdup(value);
						if (!replytofolder)
							enomem();
					}
					if (strcmp(header,"x-reply-to-msg")
						== 0 && !replytomsg)
					{
						replytomsg=strdup(value);
						if (!replytomsg)
							enomem();
					}
					if (replytofolder && replytomsg)
						break;
				}
				if (fp)	fclose(fp);

				if (replytofolder && replytomsg)
					maildir_markreplied(replytofolder,
							replytomsg);
				if (replytofolder)	free(replytofolder);
				if (replytomsg)	free(replytomsg);
				
				maildir_quota_deleted(".",
						      -(long)filesize, -1);

				unlink(draftfile);
				free(draftfile);
			}
		}

		tokensave();

		if (*cgi("fcc") == 0)
		{
			unsigned long filesize=0;
			char	*tmpfile=maildir_find(INBOX "." SENT, filename);

			if (tmpfile)
			{
				maildir_parsequota(tmpfile, &filesize);
				unlink(tmpfile);
				maildir_quota_deleted(".", -(long)filesize,-1);
				free(tmpfile);
			}
		}

		free(filename);
		free(draftmessage);
		sendmsg_done();
		return (1);
	}

	if (stat(filename, &stat_buf) == 0)
		maildir_quota_deleted(".", -(long)stat_buf.st_size, -1);
	maildir_msgpurgefile(INBOX "." SENT, filename);
	free(filename);

	{
	char *draftbase=maildir_basename(draftmessage);

		http_redirect_argsss("&form=newmsg&pos=%s&draft=%s&foldermsg=%s",
			cgi("pos"), draftbase, line);
		free(draftmessage);
		free(draftbase);
	}
	return (1);
}

void newmsg_do(const char *folder)
{
const	char *draftmessage=cgi("draftmessage");

	if (*draftmessage)	/* It's ok if it's blank */
	{
		CHECKFILENAME(draftmessage);
	}

	if (*cgi("savedraft"))
	{
	char	*newdraft=newmsg_createdraft(draftmessage);

		if (!newdraft)	enomem();
		free(newdraft);
		sendmsg_done();
		return;
	}

	if (*cgi("sendmsg") && dosendmsg(draftmessage))
		return;

	if (*cgi("doattachments"))
	{
	char	*newdraft=newmsg_createdraft(draftmessage);
	char	*base;

		if (!newdraft)	enomem();
		if (*cgi("error"))
		{
			cgi_put("previewmsg", "1");
			output_form("newmsg.html");
			return;
		}

		base=maildir_basename(newdraft);
		http_redirect_argss("&form=attachments&pos=%s&draft=%s",
			cgi("pos"), base);
		free(base);
		free(newdraft);
		return;
	}
#ifdef	ISPELL
	if (*cgi("startspellchk"))
	{
	char	*newdraft=newmsg_createdraft(draftmessage);
	char	*base;

		if (!newdraft)	enomem();
		base=maildir_basename(newdraft);
		free(newdraft);
		if (spell_start(base) == 0)
		{
			cgi_put("draftmessage", base);
			output_form("spellchk.html");
		}
		else
		{
			http_redirect_argss("&form=newmsg&pos=%s&draft=%s&previewmsg=SPELLCHK",
				cgi("pos"), base);
		}
		free(base);
		return;
	}
#endif
	if (*ispreviewmsg())
	{
		output_form("newmsg.html");
		return;
	}
	http_redirect_argsss("&form=newmsg&pos=%s&draftmessage=%s&error=%s",
		cgi("pos"), draftmessage,
		cgi("error"));
}
