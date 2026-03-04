/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
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

extern "C" const char *sqwebmail_content_charset;
extern int spell_start(const char *);
extern "C" const char *sqwebmail_mailboxid;
extern "C" const char *sqwebmail_folder;
extern "C" void print_safe_len(const char *, size_t, void (*)(const char *, size_t));
extern void call_print_safe_to_stdout(const char *, size_t);
extern "C" void print_attrencodedlen(const char *, size_t, int, FILE *);
extern "C" void output_attrencoded_nltobr(const char *);
extern "C" void output_attrencoded_oknl(const char *);
extern "C" void output_attrencoded(const char *);
extern "C" void output_scriptptrget();
extern "C" void output_form(const char *);
extern "C" void output_urlencoded(const char *);

extern std::string newmsg_newdraft(const char *, const char *, const char *,
				   const char *);
extern char *newmsg_createdraft(const char *);
extern std::string newmsg_createsentmsg(const char *draftname, int *isgpgerr);
extern "C" int ishttps();

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
		std::string s;

		rfc822::display_header(
			"subject",
			encoded,
			sqwebmail_content_charset,
			std::back_inserter(s)
		);

		output_attrencoded(s.c_str());
	}
	else if (val)
		output_attrencoded(val);
	printf("\" /></td></tr>\n");
}

static void newmsg_header_rfc822(const char *label, const char *field,
				 const std::string &encoded, const char *val,
				 int is_readonly)
{
int		hdrmaxlen=2048;
const char	*p=getarg("HDRMAXLEN");

	if (p && (atoi(p) > hdrmaxlen))
		hdrmaxlen=atoi(p);

	printf("<tr><th align=\"right\"><p class=\"new-message-header\">"
	       "<span class=\"new-message-header-%s\">%s</span></p></th>"
	       "<td width=\"6\">&nbsp;</td>",
	       field, label);

	printf("<td><input type=\"text\" name=\"%s\" size=\"80\" maxlength=\"%d\""
	       " class=\"new-message-header-text\" value=\"",
		field, hdrmaxlen);
	if (!encoded.empty())
	{
		rfc822::tokens t{encoded};
		rfc822::addresses a{t};

		auto addresses=a.wrap_display(-1, sqwebmail_content_charset);

		for (auto &a:addresses)
			output_attrencoded(a.c_str());
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

std::string newmsg_alladdrs(rfc822::fdstreambuf &fp)
{
	std::string headers;

	if (!fp.error())
	{
		fp.pubseekpos(0);

		rfc2045::entity::line_iter<false>::headers read_headers{fp};

		/* First, combine all the headers into one header. */

		do
		{
			const auto &[header, value]=read_headers.name_content();

			if (header != "from" &&
			    header != "to" &&
			    header != "cc" &&
			    header != "bcc")
				continue;

			if (headers.size())
				headers += ",";

			headers += value;
		} while (read_headers.next());
	}

	/* Now, parse the header, and extract the addresses */

	rfc822::tokens t{headers};
	rfc822::addresses a{t};

	std::string s;

	for (auto &address:a)
	{
		if (address.address.empty())
			continue;

		address.address.display_address(
			sqwebmail_content_charset,
			std::back_inserter(s)
		);
		s += "\n";
	}

	return (s);
}

void newmsg_showfp(rfc822::fdstreambuf &fp, int *attachcnt)
{
	rfc2045::entity message;
	std::istreambuf_iterator<char> b{&fp}, e;
	rfc2045::entity::line_iter<false>::iter parser{b, e};

	message.parse(parser);

	/* Here's a nice opportunity to count all attachments */

	*attachcnt=message.subentities.size();

	if (*attachcnt)	--*attachcnt;
	/* Not counting the 1st MIME part */

	if (message.content_type.value == "multipart/alternative")
		*attachcnt=0;

	auto q=message.find_content_type("text/plain");

	if (q)
	{
		struct show_textarea_info info;

		show_textarea_init(&info, 1);

		rfc822::mime_decoder decoder{
			[&]
			(const char *ptr, size_t n)
			{
				show_textarea(&info, ptr, n);
			},
			fp, sqwebmail_content_charset
		};

		decoder.decode_header=false;
		decoder.decode(*q);
		show_textarea(&info, "\n", 1);
	}
}

extern "C" void newmsg_preview(const char *p)
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
#ifdef ISPELL
	const char	*checkspellingdone=getarg("SPELLCHECKDONE");
	const char	*checkspelling=getarg("CHECKSPELLING");
#endif
	const char	*quotaerr=getarg("QUOTAERR");
	const char	*fromlab=getarg("FROMLAB");
	const char	*replytolab=getarg("REPLYTOLAB");
	const char	*addressbooklab=getarg("ADDRESSBOOK");
	const char	*select1=getarg("SELECT1");
	const char	*select2=getarg("SELECT2");
	const char	*text1=getarg("TEXT1");
	const char	*text2=getarg("TEXT2");
	std::string draftmessage;
	const	char *p;
	int	attachcnt=0;
	std::string cursubj, curto, curcc, curbcc, curfrom, curreplyto;
	int wbnochangingfrom;

	/* Picking up an existing draft? */

	p=cgi("draft");
	if (*p)
	{
		CHECKFILENAME(p);
	}

	if (*p)
	{
		draftmessage=p;
		p="";
	}
	else
	{
		draftmessage=newmsg_newdraft(folder, pos,
			forwardsep, replysalutation);

		if (draftmessage.empty())
		{
			if (*ispreviewmsg())
			{
				p=cgi("draftmessage");
				if (*p)
				{
					CHECKFILENAME(p);
				}
				char *ptr=newmsg_createdraft(p);
				draftmessage=ptr;
				free(ptr);
			}
		}
	}


	auto draftmessagefilename= !draftmessage.empty() ?
		maildir_find(INBOX "." DRAFTS, draftmessage.c_str()):"";

	if (*(p=cgi("previewmsg")))
	{
#ifdef	ISPELL
		if (strcmp(p, "SPELLCHK") == 0)
			printf("%s<br /><br />\n", checkspellingdone);
#endif
		printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"1\" class=\"box-small-outer\"><tr><td>\n");
		printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\" class=\"preview\"><tr><td>\n");

		if (!draftmessagefilename.empty())
		{
			const char *p=
				strrchr(draftmessagefilename.c_str(), '/');

			if (p)
				++p;
			else
				p=draftmessagefilename.c_str();

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

	rfc822::fdstreambuf fp{
		!draftmessagefilename.empty()
		? maildir_safeopen(draftmessagefilename.c_str(),
				   O_RDONLY, 0)
		: -1
	};

	if (!fp.error())
	{
		rfc2045::entity::line_iter<false>::headers headers{fp};

		do
		{
			const auto &[header, value]=headers.name_content();

			std::string *rfchp=0;

			if (header == "subject")
			{
				if (cursubj.empty())
					cursubj=value;
				continue;
			}

			if (header == "from")
				rfchp= &curfrom;
			if (header == "reply-to")
				rfchp= &curreplyto;
			if (header == "to")
				rfchp= &curto;
			if (header == "cc")
				rfchp= &curcc;
			if (header == "bcc")
				rfchp= &curbcc;

			if (rfchp)
			{
				if (rfchp->size())
					(*rfchp) += ", ";
				(*rfchp) += value;
			}
		} while (headers.next());
		fp.pubseekpos(0);
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
		      cursubj.c_str(), cgi("subject"));

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

	if (!fp.error())
	{
		newmsg_showfp(fp, &attachcnt);
	}
	else
	{
		printf("%s", cgi("body"));
	}
	printf("%s\n", text2);

	if (!draftmessage.empty())
	{
		printf("<input type=\"hidden\" name=\"draftmessage\" value=\"");
		output_attrencoded(draftmessage.c_str());

		printf("\" />");
	}
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
		printf("<tr><td colspan=\"2\" align=\"right\"><input type=\"checkbox\" "
		       "name=\"sign\" id=\"sign\" /></td><td><label for=\"sign\">%s</label><select name=\"signkey\">",
		       getarg("SIGNLAB"));
		gpgselectkey();
		printf("</select></td></tr>\n");

		auto all_addr=newmsg_alladdrs(fp);

		printf("<tr valign=\"middle\"><td colspan=\"2\" align=\"right\">"
		       "<input type=\"checkbox\" name=\"encrypt\" id=\"encrypt\" /></td>"
		       "<td><table border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr valign=\"middle\"><td><label for=\"encrypt\">%s</label></td><td><select size=\"4\" multiple=\"multiple\" name=\"encryptkey\">",
		       getarg("ENCRYPTLAB"));
		gpgencryptkeys(all_addr.c_str());
		printf("</select></td></tr>\n");
		printf("</table></td></tr>\n");

		if (ishttps())
			printf("<tr><td colspan=\"2\" align=\"left\">&nbsp;</td><td>%s<input type=\"password\" name=\"passphrase\" /></td></tr>\n",
			       getarg("PASSPHRASE"));
	}

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

	auto filename=newmsg_createsentmsg(draftmessage, &isgpgerr);

	if (filename.empty())
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
		maildir_msgpurgefile(INBOX "." SENT, filename.c_str());
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
		maildir_msgpurgefile(INBOX "." SENT, filename.c_str());
		free(draftmessage);
		return (0);
	}

	if (pid == 0)
	{
	static const char noexec[]="ERROR: Unable to execute sendit.sh.\n";
	static const char nofile[]="ERROR: Temp file not available - probably exceeded quota.\n";
	auto tmpfile=maildir_find(INBOX "." SENT, filename.c_str());
	int	fd;

		if (tmpfile.empty())
		{
			if (fwrite((char*)nofile, 1, sizeof(nofile)-1, stderr))
				; /* ignore */
			_exit(1);
		}

		close(0);

		fd=maildir_safeopen(tmpfile.c_str(), O_RDONLY, 0);
		dup2(pipefd1[1], 1);
		dup2(pipefd1[1], 2);
		close(pipefd1[0]);
		close(pipefd1[1]);

		static char dsnopt[]="DSN=-Nsuccess,delay,failure";
		static char nodsnopt[]="DSN=";
		if (dsn)
			putenv(dsnopt);
		else
			putenv(nodsnopt);

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
		auto draftfile=maildir_find(INBOX "." DRAFTS, base);

			free(base);

			/* Remove draft file */

			if (!draftfile.empty())
			{
				std::string replytofolder, replytomsg;
				int	x;

				x=maildir_safeopen(draftfile.c_str(),
						   O_RDONLY, 0);
				if ( maildir_parsequota(draftfile.c_str(),
							&filesize))
				{
					if (x < 0 || fstat(x, &stat_buf))
						stat_buf.st_size=0;
					filesize=stat_buf.st_size;
				}

				rfc822::fdstreambuf fp{x};

				rfc2045::entity::line_iter<false>
					::headers headers{fp};

				headers.name_lc=true;

				/* First, look for a message that we should
				** mark as replied */

				do
				{
					const auto &[header, value]=headers.name_content();

					if (header.empty())
						break;

					if (header == "x-reply-to-folder")
					{
						replytofolder=value;
					}
					if (header == "x-reply-to-msg")
					{
						replytomsg=value;
					}
					if (!replytofolder.empty() &&
					    !replytomsg.empty())
						break;
				} while (headers.next());

				if (!replytofolder.empty() &&
				    !replytomsg.empty())
					maildir_markreplied(
						replytofolder.c_str(),
						replytomsg.c_str()
					);

				maildir_quota_deleted(".", -(long)filesize, -1);

				unlink(draftfile.c_str());
			}
		}

		tokensave();

		if (*cgi("fcc") == 0)
		{
			unsigned long filesize=0;
			auto tmpfile=maildir_find(INBOX "." SENT,
						  filename.c_str());

			if (!tmpfile.empty())
			{
				maildir_parsequota(tmpfile.c_str(), &filesize);
				unlink(tmpfile.c_str());
				maildir_quota_deleted(".", -(long)filesize,-1);
			}
		}

		free(draftmessage);
		sendmsg_done();
		return (1);
	}

	if (stat(filename.c_str(), &stat_buf) == 0)
		maildir_quota_deleted(".", -(long)stat_buf.st_size, -1);
	maildir_msgpurgefile(INBOX "." SENT, filename.c_str());

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
