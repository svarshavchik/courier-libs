/*
** Copyright 1998 - 2013 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	<stdio.h>
#include	<string.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<stdlib.h>
#include	"sqwebmail.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirnewshared.h"
#include	"maildir/maildirinfo.h"
#include	"maildir/maildiraclt.h"

#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"md5/md5.h"
#include	"gpglib/gpglib.h"
#include	"maildir.h"
#include	"mailfilter.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirinfo.h"
#include	"numlib/numlib.h"
#include	"courierauth.h"
#include	"folder.h"
#include	"addressbook.h"
#include	"cgi/cgi.h"
#include	"pref.h"
#include	"token.h"
#include	"filter.h"
#include	"buf.h"
#include	"pref.h"
#include	"newmsg.h"
#include	"htmllibdir.h"
#include	"gpg.h"
#include	"acl.h"
#include	"auth.h"

#include	"msg2html.h"

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
#include	<locale.h>
#endif
#endif

#if	TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<errno.h>

#include	<courier-unicode.h>

#include	<unistd.h>
#if HAVE_WCHAR_H
#include	<wchar.h>
#endif

#include	"strftime.h"

extern FILE *open_langform(const char *lang, const char *formname,
			   int print_header);

extern const char *sqwebmail_content_language;
extern char sqwebmail_folder_rights[];
extern const char *sqwebmail_mailboxid;
extern char *get_imageurl();
extern const char *sqwebmail_content_locale;
extern void print_attrencodedlen(const char *, size_t, int, FILE *);

extern const char *showsize(unsigned long);
extern void maildir_cleanup();
extern const char *nonloginscriptptr();
extern int pref_flagpagesize;
extern int ishttps();
extern const char *sqwebmail_content_charset;
extern int verify_shared_index_file;

extern time_t rfc822_parsedt(const char *);
static time_t	current_time;

static const char *folder_err_msg=0;

extern const char *sqwebmail_folder;

extern void output_scriptptrget();
extern void output_scriptptr();
extern void output_scriptptrpostinfo();
extern void output_attrencoded(const char *);
extern void output_urlencoded(const char *);
extern char *scriptptrget();

void print_safe_len(const char *p, size_t n, void (*func)(const char *, size_t))
{
char	buf[10];
const	char *q=p;

	while (n)
	{
		--n;
		if (*p == '<')	strcpy(buf, "&lt;");
		else if (*p == '>') strcpy(buf, "&gt;");
		else if (*p == '&') strcpy(buf, "&amp;");
		else if (*p == ' ') strcpy(buf, "&nbsp;");
		else if (*p == '\n') strcpy(buf, "<br />");
		else if (ISCTRL(*p))
			sprintf(buf, "&#%d;", (int)(unsigned char)*p);
		else
		{
			p++;
			continue;
		}

		(*func)(q, p-q);
		(*func)(buf, strlen(buf));
		p++;
		q=p;
	}
	(*func)(q, p-q);
}

static void print_safe_to_stdout(const char *p, size_t cnt)
{
	if (cnt == 0)
		return;

	if (fwrite(p, cnt, 1, stdout) != 1)
		exit(1);
}

void print_safe(const char *p)
{
	print_safe_len(p, strlen(p), print_safe_to_stdout);
}

void call_print_safe_to_stdout(const char *p, size_t cnt)
{
	print_safe_len(p, cnt, print_safe_to_stdout);
}

void folder_contents_title()
{
const char *lab;
const char *f;
const char *inbox_lab, *drafts_lab, *trash_lab, *sent_lab;
int in_utf8;

	lab=getarg("FOLDERTITLE");

	if (*cgi("search"))
		lab=getarg("SEARCHRESULTSTITLE");

	inbox_lab=getarg("INBOX");
	drafts_lab=getarg("DRAFTS");
	trash_lab=getarg("TRASH");
	sent_lab=getarg("SENT");

	f=sqwebmail_folder;
	in_utf8=1;

	if (strcmp(f, INBOX) == 0)	f=inbox_lab;
	else if (strcmp(f, INBOX "." DRAFTS) == 0)	f=drafts_lab;
	else if (strcmp(f, INBOX "." SENT) == 0)	f=sent_lab;
	else if (strcmp(f, INBOX "." TRASH) == 0)	f=trash_lab;
	else in_utf8=0;

	if (lab)
	{
		char *ff, *origff;

		printf("%s", lab);

		origff=ff=in_utf8 ?
			unicode_convert_fromutf8(f,
						   sqwebmail_content_charset,
						   NULL)
			: folder_fromutf7(f);

		if (strcmp(ff, NEWSHAREDSP) == 0 ||
		    strncmp(ff, NEWSHAREDSP ".", sizeof(NEWSHAREDSP)) == 0)
		{
			printf("%s", getarg("PUBLICFOLDERS"));
			ff=strchr(ff, '.');
			if (!ff)
				ff="";
		}
		output_attrencoded(ff);
		free(origff);
	}
}

static int group_movedel(const char *folder,
			int (*func)(const char *, const char *, size_t))
{
struct cgi_arglist *arg;

	if (*cgi("SELECTALL"))	/* Everything is selected */
	{
		for (arg=cgi_arglist; arg; arg=arg->next)
		{
		const	char *f;

			if (strncmp(arg->argname, "MOVEFILE-", 9)) continue;
			f=cgi(arg->argname);
			CHECKFILENAME(f);
			if ((*func)(folder, f, atol(arg->argname+9)))
				return (-1);
		}
		return (0);
	}

	for (arg=cgi_arglist; arg; arg=arg->next)
	{
	unsigned long l;
	char	movedel[MAXLONGSIZE+10];
	const	char *f;

		if (strncmp(arg->argname, "MOVE-", 5))	continue;
		l=atol(arg->argname+5);
		sprintf(movedel, "MOVEFILE-%lu", l);
		f=cgi(movedel);
		CHECKFILENAME(f);
		if ((*func)(folder, f, l))
			return (-1);
	}
	return (0);
}

static int groupdel(const char *folder, const char *file, size_t pos)
{
	maildir_msgdeletefile(folder, file, pos);
	return (0);
}

static int groupmove(const char *folder, const char *file, size_t pos)
{
	return (maildir_msgmovefile(folder, file, cgi("moveto"), pos));
}

static const char *do_folder_delmsgs(const char *dir, size_t pos)
{
	int	rc=0;
	char	buf[2];
	char	*cur;

	strcpy(buf, ACL_DELETEMSGS);
	acl_computeRightsOnFolder(dir, buf);

#if 0
	{
		FILE *fp=fopen("/tmp/pid", "w");

		if (fp)
		{
			fprintf(fp, "%d", (int)getpid());
			fclose(fp);
			sleep(10);
		}
	}
#endif

	if (buf[0] == 0)
		return "nodel";

	if (*cgi("cmddel"))
	{
		rc=group_movedel( dir, &groupdel );
		maildir_savefoldermsgs(dir);
	}
	else if (*cgi("cmdpurgeall"))
	{
	    char *deldir;
	    struct maildir_info minfo;

	    if (maildir_info_imap_find(&minfo, dir, login_returnaddr())<0)
		    return "othererror";

	    if ((deldir=maildir_name2dir(minfo.homedir, minfo.maildir)) == NULL)
	    {
		maildir_info_destroy(&minfo);
		return "othererror";
	    }

	    cur = malloc(strlen(deldir)+5);
	    strcpy(cur, deldir);
	    strcat(cur, "/cur");

	    rc=maildir_del_content(cur);
	    maildir_quota_recalculate(".");

	    maildir_info_destroy(&minfo);
	    free(deldir);
	    free(cur);

	}
	else if (*cgi("cmdmove"))
	{
		const char *p=cgi("moveto");

		CHECKFILENAME(p);
		strcpy(buf, ACL_INSERT);
		acl_computeRightsOnFolder(p, buf);
		if (buf[0] == 0)
			return "noinsert";

		rc=group_movedel( dir, &groupmove );
		maildir_savefoldermsgs(dir);
	}

	maildir_cleanup();

	return rc ? "quota":"";
}

void folder_delmsgs(const char *dir, size_t pos)
{
	const char *status=do_folder_delmsgs(dir, pos);

	if (*cgi("search"))
		http_redirect_argsss("&error=%s&form=folder&pos=%s&search=1&"
				     SEARCHRESFILENAME "=%s", status,
				     cgi("pos"), cgi(SEARCHRESFILENAME));
	else
		http_redirect_argss("&error=%s&form=folder&pos=%s", status,
				    cgi("pos"));
}

static void savepath(const char *path, const char *maildir)
{
	char buf[BUFSIZ];
	FILE *ofp;
	FILE *fp;
	struct maildir_tmpcreate_info createInfo;

	maildir_tmpcreate_init(&createInfo);

	createInfo.maildir=".";
	createInfo.uniq="sharedpath";
	createInfo.doordie=1;

	ofp=maildir_tmpcreate_fp(&createInfo);

	fp=fopen(SHAREDPATHCACHE, "r");

	if (fp)
	{
		int cnt=0;

		while (cnt < 1)
		{
			char *p;

			if (fgets(buf, sizeof(buf), fp) == NULL)
				break;

			if ((p=strchr(buf, '\n')) != NULL) *p=0;

			if (strcmp(buf, maildir) == 0)
			{
				if (fgets(buf, sizeof(buf), fp) == NULL)
					break;
				continue;
			}

			fprintf(ofp, "%s\n", buf);

			if (fgets(buf, sizeof(buf), fp) == NULL)
				strcpy(buf, "");
			if ((p=strchr(buf, '\n')) != NULL) *p=0;
			fprintf(ofp, "%s\n", buf);

			++cnt;
		}
		fclose(fp);
	}

	fprintf(ofp, "%s\n%s\n", maildir, path);
	fclose(ofp);
	rename(createInfo.tmpname, SHAREDPATHCACHE);
	maildir_tmpcreate_free(&createInfo);
}

void folder_search(const char *dir, size_t pos)
{
	maildir_reload(dir);
	maildir_search(dir,
		       pos,
		       cgi("searchtxt"),
		       sqwebmail_content_charset,
		       pref_flagpagesize);

	http_redirect_argss("&search=1&form=folder&pos=%s&"
			    SEARCHRESFILENAME "=%s", cgi("pos"),
			    cgi(SEARCHRESFILENAME));
}

static void folder_msg_link(const char *, int, size_t, char);
static void folder_msg_unlink(const char *, int, size_t, char);

static char *truncate_at(const char *, const char *, size_t);

static void show_msg(const char *dir,
		     MSGINFO *msg,
		     MATCHEDSTR *matches,
		     int row,
		     const char *charset);

void folder_contents(const char *dir, size_t pos)
{
MSGINFO	**contents;
MATCHEDSTR **matches;
int	i, found;
int	morebefore, moreafter;
const char	*nomsg, *selectalllab;
 const char	*unselectalllab;
const char	*qerrmsg;
long highend;
unsigned long last_message_searched=0;
unsigned long *last_message_searched_ptr=NULL;

	qerrmsg=getarg("PERMERR");

	if (strcmp(cgi("error"), "quota") == 0)
		printf("%s", qerrmsg);

	if (strcmp(cgi("error"), "nodel") == 0)
		printf("%s", getarg("NODELPERM"));

	if (strcmp(cgi("error"), "noinsert") == 0)
		printf("%s", getarg("NOINSERTPERM"));

	if (strcmp(cgi("error"), "othererror") == 0)
		printf("%s", getarg("OTHERERROR"));

	if (strchr(sqwebmail_folder_rights, ACL_READ[0]) == NULL)
	{
		printf("%s", getarg("ACL"));
		return;
	}

	maildir_reload(dir);

	if (*cgi("search"))
	{
#if 0
	{
		FILE *fp=fopen("/tmp/pid", "w");

		if (fp)
		{
			fprintf(fp, "%d", (int)getpid());
			fclose(fp);
			sleep(10);
		}
	}
#endif

		morebefore=0;
		moreafter=0;
		maildir_loadsearch(pref_flagpagesize, &contents, &matches,
				   &last_message_searched);

		for (i=0; i<pref_flagpagesize; i++)
		{
			if (contents[i] == 0)	continue;

			last_message_searched_ptr= &last_message_searched;
			break;
		}
	}
	else
	{
		contents=maildir_read(dir, pref_flagpagesize, &pos,
				      &morebefore, &moreafter);
		matches=NULL;
	}

	time(&current_time);
	nomsg=getarg("NOMESSAGES");
	selectalllab=getarg("SELECTALL");
	unselectalllab=getarg("UNSELECTALL");

	if (maildir_countof(dir) <= pos + pref_flagpagesize - 1)
		highend = (long)(maildir_countof(dir) - 1);
	else
		highend = (long)(pos + pref_flagpagesize - 1);
	if (highend < 0) highend = 0;

	if (!qerrmsg)	qerrmsg="";

	folder_navigate(dir, pos, highend, morebefore, moreafter,
			last_message_searched_ptr);

	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\"><tr class=\"folder-index-header\"><th align=\"center\">%s</th><th>&nbsp;</th><th>%s</th><th>%s</th><th>%s</th><th>%s</th></tr>\n",
		getarg("NUM"),
		getarg("DATE"),
		(strncmp(dir, INBOX "." SENT, sizeof(INBOX)+sizeof(SENT)-1) &&
		 strncmp(dir, INBOX "." DRAFTS, sizeof(INBOX)+sizeof(DRAFTS)-1))
			? getarg("FROM") : getarg("TO"),
		getarg("SUBJECT"),
		getarg("SIZE"));

	found=0;
	for (i=0; i<pref_flagpagesize; i++)
	{
		if (contents[i] == 0)	continue;
		found=1;

		show_msg(dir, contents[i], matches ? matches[i]:NULL, i,
			 sqwebmail_content_charset);
	}

	if (found)
	{
		puts("<tr class=\"folder-index-bg-1\"><td colspan=\"6\"><hr /></td></tr>");
		puts("<tr class=\"folder-index-bg-2\"><td>&nbsp;</td>");
		puts("<td colspan=\"5\">");

		puts("<script type=\"text/javascript\">");
		puts("/* <![CDATA[ */");
		puts("function setAll(input, chk) {");
		printf("for (i = %ld; i <= %ld; i++) {\n",
			(long)pos, highend);
		puts("if (document.getElementById) e = document.getElementById('MOVE-' + i);");
		puts("else if (document.all) e = document['MOVE-' + i];");
		puts("if (e != null) { e.checked = chk; e.onchange(); }} }");
		puts("/* ]]> */");
		puts("</script>\n");

		puts("<script type=\"text/javascript\">");
		puts("/* <![CDATA[ */");
		printf("document.write('<button type=\"button\" onclick=\"setAll(this, true); return false;\">%s<\\/button>\\n&nbsp;');\n",
			selectalllab);
		printf("document.write('<button type=\"button\" onclick=\"setAll(this, false); return false;\">%s<\\/button>\\n');\n",
			unselectalllab);
		puts("/* ]]> */");
		puts("</script>\n");

		printf("<noscript><label><input type=\"checkbox\" name=\"SELECTALL\" />&nbsp;%s</label></noscript>\n",
			selectalllab);
		puts("</td></tr>");

		printf("</table>\n");
		folder_navigate(dir, pos, highend, morebefore, moreafter,
				last_message_searched_ptr);
	}
	if (!found && nomsg)
	{
		puts("<tr class=\"folder-index-bg-1\"><td colspan=\"6\" align=\"left\"><p>");
		puts(nomsg);
		puts("<br /></p></td></tr>");
		printf("</table>\n");
	}

	maildir_free(contents, pref_flagpagesize);
	if (matches)
		matches_free(matches, pref_flagpagesize);
}


static int folder_searchlink()
{
	if (*cgi("search") == 0)
		return 0;

	printf("&amp;search=1&amp;" SEARCHRESFILENAME "=");
	output_urlencoded(cgi(SEARCHRESFILENAME));
	return 1;
}

static void show_msg_match_str(const char *prefix,
			       const char *utf8match,
			       const char *suffix,
			       const char *classname);

static void show_msg(const char *dir,
		     MSGINFO *msg,
		     MATCHEDSTR *matches,
		     int row,
		     const char *charset)
{
	const char *date, *from, *subj, *size;
	char	*froms, *subjs;
	const char *p, *q;
	size_t l;
	char type[8];
	char *folder_index_entry_start, *folder_index_entry_end;

	size_t msgnum=msg->msgnum;

	date=MSGINFO_DATE(msg);
	from=MSGINFO_FROM(msg);
	subj=MSGINFO_SUBJECT(msg);
	size=MSGINFO_SIZE(msg);

	type[0]=maildirfile_type(MSGINFO_FILENAME(msg));
	type[1]='\0';
	if (type[0] == '\0')	strcpy(type, "&nbsp;");

	folder_index_entry_start="<span class=\"read-message\">";
	folder_index_entry_end="</span>";

	if (type[0] == MSGTYPE_NEW)
	{
		folder_index_entry_start="<strong class=\"unread-message\">";
		folder_index_entry_end="</strong>";
	}

	p=MSGINFO_FILENAME(msg);

	if ((q=strrchr(p, '/')) != 0)
		p=q+1;

	printf("<tr class=\"folder-index-bg-%d\" id=\"row%d\"><td align=\"right\" class=\"message-number\">%s%ld.%s</td><td class=\"message-status\"><input type=\"checkbox\" name=\"MOVE-%ld\" id=\"MOVE-%ld",
	       (row & 1)+1,
	       row,
	       folder_index_entry_start,
	       (long)(msgnum+1),
	       folder_index_entry_end,
	       (long) (msgnum),
	       (long) (msgnum));
	printf("\" onchange=\"setsel('MOVE-%ld', 'row%d', 'folder-index-bg-%d');\"%s /><input type=\"hidden\" name=\"MOVEFILE-%ld\" value=\"",
	       (long)(msgnum), row, (row & 1)+1,
	       (type[0] == MSGTYPE_DELETED ? " disabled=\"disabled\"":""),
	       (long)(msgnum));
	output_attrencoded(p);
	printf("\" />&nbsp;%s%s%s</td><td class=\"message-date\">%s",
	       folder_index_entry_start,
	       type,
	       folder_index_entry_end,

	       folder_index_entry_start
	       );
	if (!*date)	date=" ";
	folder_msg_link(dir, row, msgnum, type[0]);
	print_safe(date);
	folder_msg_unlink(dir, row, msgnum, type[0]);
	printf("%s</td><td class=\"message-from\">%s", folder_index_entry_end,
	       folder_index_entry_start);
	if (!*from)	from=" ";
	folder_msg_link(dir, row, msgnum, type[0]);


	froms=truncate_at(from, charset, 30);

	if (froms == 0)	enomem();

	print_safe(froms);
	free(froms);
	folder_msg_unlink(dir, row, msgnum, type[0]);
	printf("%s<br /></td><td class=\"message-subject\">%s", folder_index_entry_end,
	       folder_index_entry_start);

	folder_msg_link(dir, row, msgnum, type[0]);

#if 0
	{
		static int foo=0; if (foo++ == 0)
	{
		FILE *fp=fopen("/tmp/pid", "w");

		if (fp)
		{
			fprintf(fp, "%d", (int)getpid());
			fclose(fp);
			sleep(10);
		}
	}
	}
#endif

	subjs=truncate_at(subj, charset, 40);

	if (subjs == 0)	enomem();

	print_safe(subjs);
	l=strlen(subjs);
	while (l++ < 8)
		printf("&nbsp;");
	free(subjs);

	folder_msg_unlink(dir, row, msgnum, type[0]);
	printf("%s</td><td align=\"right\" class=\"message-size\">%s%s&nbsp;%s<br /></td></tr>\n", folder_index_entry_end, folder_index_entry_start, size, folder_index_entry_end);

	while (matches && matches->match)
	{
		printf("<tr class=\"folder-index-bg-%d\"><td align=\"right\" class=\"message-number\" colspan=\"3\">&nbsp;</td><td class=\"message-searchmatch\" colspan=\"3\">", (row & 1)+1);
		show_msg_match_str("...",
				   matches->prefix, "", "searchmatch-affix");
		show_msg_match_str("", matches->match, "", "searchmatch");
		show_msg_match_str("", matches->suffix,
				   "...", "searchmatch-affix");
		printf("</td></tr>\n");
		++matches;
	}
}

static void show_msg_match_str(const char *prefix,
			       const char *utf8match,
			       const char *suffix,
			       const char *classname)
{
	char *p;

	p=unicode_convert_fromutf8(utf8match, sqwebmail_content_charset,
				     NULL);

	if (p)
	{
		printf("<span class=\"%s\">%s", classname, prefix);
		output_attrencoded(p);
		free(p);
		printf("%s</span>", suffix);
	}
}

static void do_folder_navigate(const char *dir, size_t pos, long highend,
			       int morebefore, int moreafter)
{
	const char	*firstlab, *lastlab;
	const char	*beforelab, *afterlab;
	const char	*showncountlab, *jumplab, *golab;

	time(&current_time);
	showncountlab=getarg("SHOWNCOUNT");
	jumplab=getarg("JUMPTO");
	golab=getarg("GO");
	firstlab=getarg("FIRSTPAGE");
	beforelab=getarg("PREVPAGE");
	afterlab=getarg("NEXTPAGE");
	lastlab=getarg("LASTPAGE");

	printf("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" class=\"folder-nextprev-buttons\"><tr><td>");

	if (morebefore)
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=0\" style=\"text-decoration: none\">");
	}
	printf("%s", firstlab);
	if (morebefore)
		printf("</a>");

	puts("&nbsp;");

	if (morebefore)
	{
	size_t	beforepos;

		if (pos < pref_flagpagesize)	beforepos=0;
		else	beforepos=pos-pref_flagpagesize;

		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=%ld\" style=\"text-decoration: none\">",
			(long)beforepos);
	}
	printf("%s", beforelab);
	if (morebefore)
		printf("</a>");

	printf("</td></tr></table>\n");

	if (maildir_countof(dir) > 0) {
		puts("</td><td align=\"center\">\n");
		puts("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" class=\"folder-message-count\"><tr><td>\n");
		printf(showncountlab, (long)(pos+1), (long)(highend+1), (long)maildir_countof(dir));
		puts("</td></tr></table>");

		puts("<script type=\"text/javascript\">");
		puts("/* <![CDATA[ */");
		puts("document.write('<\\/td><td align=\"center\">' +");

		puts("'<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" class=\"folder-jumpto-field\"><tr><td>' +");
		printf("'%s <input type=\"text\" name=\"jumpto\" size=\"3\" value=\"%ld\" onchange=\"this.form.pos.value = this.value - 1;\" />' +\n",
			jumplab, (long)(pos+1));
		printf("'<button type=\"button\" onclick=\"this.form.submit();\">%s<\\/button>' +\n",
			golab);
		puts("'<\\/td><\\/tr><\\/table>');");
		puts("/* ]]> */");
		puts("</script>");
	}

	printf("</td><td align=\"right\">\n");

	printf("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" class=\"folder-nextprev-buttons\"><tr><td>");
	if (moreafter)
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=%ld\" style=\"text-decoration: none\">",
			(long)(pos+pref_flagpagesize));
	}
	printf("%s", afterlab);
	if (moreafter)
		printf("</a>");

	puts("&nbsp;");

	if (moreafter)
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=%ld\" style=\"text-decoration: none\">",
			(long)(maildir_countof(dir)-pref_flagpagesize));
	}
	printf("%s", lastlab);
	if (moreafter)
		printf("</a>");

	printf("</td></tr></table>\n");
}

void folder_navigate(const char *dir, size_t pos, long highend,
		     int morebefore, int moreafter,
		     unsigned long *last_message_searched_ptr)
{
	printf("<table width=\"100%%\" class=\"folder-nextprev-background\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr>");

	if (*cgi("search"))
	{
		printf("<td class=\"folder-return-link\">");
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=%ld\">%s</a>",
		       (long)pos, getarg("RETURNTOFOLDER"));
		printf("</td>");

		printf("<td class=\"folder-last-message-searched\">");
		if (last_message_searched_ptr)
		{
			printf(getarg("LASTMESSAGESEARCHED"),
			       *last_message_searched_ptr+1);
		}
		else
		{
			printf("&nbsp;");
		}
		printf("</td>");
	}
	else
	{
		printf("<td align=\"left\">");
		do_folder_navigate(dir, pos, highend, morebefore, moreafter);
		printf("</td>");
	}

	printf("</tr></table>");
}

static void folder_msg_link(const char *dir, int row, size_t pos, char t)
{
#if 0
	if (t == MSGTYPE_DELETED)
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folder&amp;pos=%s\">", cgi("pos"));
		return;
	}
#endif

	printf("<a href=\"");
	if (strcmp(dir, INBOX "." DRAFTS))
	{
		output_scriptptrget();
		if (folder_searchlink())
			printf("&amp;searchrow=%d", row);

		printf("&amp;form=readmsg&amp;pos=%ld\">", (long)pos);
	}
	else
	{
	size_t	mpos=pos;
	char	*filename=maildir_posfind(dir, &mpos);
	char	*basename=filename ? maildir_basename(filename):NULL;

		output_scriptptrget();
		printf("&amp;form=open-draft&amp;draft=");
		output_urlencoded(basename);
		printf("\">");
		if (basename)	free(basename);
		if (filename)	free(filename);
	}
}

static void folder_msg_unlink(const char *dir, int row, size_t pos, char t)
{
	printf("</a>");
}

size_t	msg_pos, msg_count;
static char	*msg_posfile;
static int	msg_hasprev, msg_hasnext;
static size_t	msg_searchpos;
static long	msg_prevpos, msg_prev_searchpos;
static long	msg_nextpos, msg_next_searchpos;

static const char	*msg_nextlab=0, *msg_prevlab=0, *msg_deletelab=0,
		*msg_purgelab=0, *msg_folderlab=0;
static char	msg_type;

static const char	*msg_replylab=0;
static const char	*msg_replyalllab=0;
static const char	*msg_replylistlab=0;
static const char	*msg_forwardlab=0;
static const char	*msg_forwardattlab=0;
static const char	*msg_fullheaderlab=0;
static const char	*msg_movetolab=0;
static const char	*msg_print=0;

static const char	*folder_inbox=0;
static const char	*folder_drafts=0;
static const char	*folder_trash=0;
static const char	*folder_sent=0;
static const char	*msg_golab=0;

static const char *msg_msglab;
static const char *msg_add=0;

static int	initnextprevcnt;

void folder_initnextprev(const char *dir, size_t pos)
{
	MSGINFO	**info;
	const	char *p;
	const	char *msg_numlab, *msg_numnewlab;
	static char *filename=0;
	int fd;

	MSGINFO *recp;
	MSGINFO **search_contents=NULL;
	MATCHEDSTR **search_matches=NULL;
	unsigned long last_message_searched=0;

	cgi_put(MIMEGPGFILENAME, "");

	if (filename)
		free(filename);
	filename=0;


	if (*cgi("mimegpg") && (filename=maildir_posfind(dir, &pos)) != 0)
	{
		char *tptr;
		int nfd;

		fd=maildir_semisafeopen(filename, O_RDONLY, 0);

		if (fd >= 0)
		{
			struct maildir_tmpcreate_info createInfo;

			maildir_purgemimegpg();

			maildir_tmpcreate_init(&createInfo);

			createInfo.uniq=":mimegpg:";
			createInfo.doordie=1;

			if ((nfd=maildir_tmpcreate_fd(&createInfo)) < 0)
			{
				free(filename);
				error("Can't create new file.");
			}

			tptr=createInfo.tmpname;
			createInfo.tmpname=NULL;
			maildir_tmpcreate_free(&createInfo);

			chmod(tptr, 0600);

			/*
			** Decrypt/check message into a temporary file
			** that's immediately marked as deleted, so that it
			** gets purged at the next sweep.
			*/

			if (gpgdecode(fd, nfd) < 0)
			{
				close(nfd);
				unlink(tptr);
				free(tptr);
			}
			else
			{
				close(fd);
				free(filename);
				filename=tptr;
				fd=nfd;

				cgi_put(MIMEGPGFILENAME,
					strrchr(filename, '/')+1);
			}
			close(fd);
		}
	}

	initnextprevcnt=0;
	msg_nextlab=getarg("NEXTLAB");
	msg_prevlab=getarg("PREVLAB");
	msg_deletelab=getarg("DELETELAB");
	msg_purgelab=getarg("PURGELAB");

	msg_folderlab=getarg("FOLDERLAB");

	msg_replylab=getarg("REPLY");
	msg_replyalllab=getarg("REPLYALL");
	msg_replylistlab=getarg("REPLYLIST");

	msg_forwardlab=getarg("FORWARD");
	msg_forwardattlab=getarg("FORWARDATT");

	msg_numlab=getarg("MSGNUM");
	msg_numnewlab=getarg("MSGNEWNUM");

	msg_fullheaderlab=getarg("FULLHDRS");

	msg_movetolab=getarg("MOVETO");
	msg_print=getarg("PRINT");

	folder_inbox=getarg("INBOX");
	folder_drafts=getarg("DRAFTS");
	folder_trash=getarg("TRASH");
	folder_sent=getarg("SENT");

	p=getarg("CREATEFAIL");
	if (strcmp(cgi("error"),"quota") == 0)
		printf("%s", p);

	msg_golab=getarg("GOLAB");
	msg_add=getarg("QUICKADD");

	msg_searchpos=atol(cgi("searchrow"));

	info=maildir_read(dir, 1, &pos, &msg_hasprev, &msg_hasnext);

	recp=info[0];
	msg_pos=pos;
	msg_prevpos=msg_pos-1;
	msg_nextpos=msg_pos+1;
	msg_prev_searchpos=msg_prevpos;
	msg_next_searchpos=msg_nextpos;

	if (*cgi("search"))
		maildir_loadsearch(pref_flagpagesize,
				   &search_contents,
				   &search_matches,
				   &last_message_searched);


	if (search_contents)
	{
		if (msg_searchpos < pref_flagpagesize &&
		    search_contents[msg_searchpos])
		{
			recp=search_contents[msg_searchpos];

			msg_pos=recp->msgnum;
			if ((msg_hasprev=msg_searchpos > 0 &&
			     search_contents[msg_searchpos-1]) != 0)
			{
				msg_prevpos=search_contents
					[msg_searchpos-1]->msgnum;
				msg_prev_searchpos=msg_searchpos-1;
			}

			if ((msg_hasnext=msg_searchpos + 1 <
			     pref_flagpagesize &&
			     search_contents[msg_searchpos+1]) != 0)
			{
				msg_nextpos=search_contents
					[msg_searchpos+1]->msgnum;
				msg_next_searchpos=msg_searchpos+1;
			}

		}
	}

	p=strrchr(MSGINFO_FILENAME(recp), '/');
	if (p)	p++;
	else	p=MSGINFO_FILENAME(recp);
	msg_posfile=strdup(p);
	if (!msg_posfile)	enomem();

	if ((msg_type=maildirfile_type(MSGINFO_FILENAME(recp)))
		== MSGTYPE_NEW) msg_numlab=msg_numnewlab;

	msg_msglab=msg_numlab;
	msg_count=maildir_countof(dir);
	maildir_free(info, 1);
	if (search_contents)
		maildir_free(search_contents, pref_flagpagesize);
	if (search_matches)
		matches_free(search_matches, pref_flagpagesize);
}

char *get_msgfilename(const char *folder, size_t *pos)
{
	char *filename;

	if (*cgi(MIMEGPGFILENAME))
	{
		const char *p=cgi(MIMEGPGFILENAME);

		CHECKFILENAME(p);

		filename=malloc(sizeof("tmp/")+strlen(p));
		if (!filename)
			enomem();
		strcat(strcpy(filename, "tmp/"), p);
	}
	else
		filename=maildir_posfind(folder, pos);

	if (!filename)	error("Message not found.");

	return (filename);
}

void output_mimegpgfilename()
{
	if (*cgi(MIMEGPGFILENAME))
	{
		printf("&amp;" MIMEGPGFILENAME "=");
		output_urlencoded(cgi(MIMEGPGFILENAME));
	}
}

void folder_nextprev()
{
	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\" class=\"message-menu-background\"><tr valign=\"middle\">");

	printf("<td align=\"left\"><table border=\"0\" cellspacing=\"4\" cellpadding=\"4\"><tr valign=\"top\">");

	/* PREV */

	printf("<td class=\"message-menu-button\">");

	if (msg_hasprev)
	{
		printf("<a href=\"");
		output_scriptptrget();

		if (folder_searchlink())
			printf("&searchrow=%ld", msg_prev_searchpos);

		printf("&amp;form=readmsg&amp;pos=%ld\">",
		       msg_prevpos);
	}

	printf("%s", msg_prevlab ? msg_prevlab:"");

	if (msg_hasprev)
	{
		printf("</a>");
	}
	printf("</td>");

	/* NEXT */

	printf("<td class=\"message-menu-button\">");

	if (msg_hasnext)
	{
		printf("<a href=\"");
		output_scriptptrget();

		if (folder_searchlink())
			printf("&searchrow=%ld", msg_next_searchpos);

		printf("&amp;form=readmsg&amp;pos=%ld\">",
		       msg_nextpos);
	}

	printf("%s", msg_nextlab ? msg_nextlab:"");

	if (msg_hasnext)
	{
		printf("</a>");
	}
	printf("</td>");

	/* DEL */

	printf("<td class=\"message-menu-button\">");
	if (msg_type != MSGTYPE_DELETED)
	{
		printf("<a href=\"");
		output_scriptptrget();
		tokennewget();
		folder_searchlink();
		printf("&amp;posfile=");
		output_urlencoded(msg_posfile);
		printf("&amp;form=delmsg&amp;pos=%ld\">",
			(long)msg_pos);
	}
	printf("%s", strcmp(sqwebmail_folder, INBOX "." TRASH) == 0
		? msg_purgelab : msg_deletelab);

	if (msg_type != MSGTYPE_DELETED)
		printf("</a>");

	printf("</td>\n");

	/* FOLDER */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	folder_searchlink();
	printf("&amp;pos=%ld&amp;form=folder\">%s</a></td>\n",
		(long)( (msg_pos/pref_flagpagesize)*pref_flagpagesize ),
		msg_folderlab);

	/* REPLY */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	printf("&amp;pos=%ld&amp;reply=1&amp;form=newmsg\">%s</a></td>\n",
		(long)msg_pos,
		msg_replylab);

	/* REPLY ALL */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	printf("&amp;pos=%ld&amp;replyall=1&amp;form=newmsg\">%s</a></td>\n",
		(long)msg_pos,
		msg_replyalllab);

	/* REPLY LIST */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	printf("&amp;pos=%ld&amp;replylist=1&amp;form=newmsg\">%s</a></td>\n",
		(long)msg_pos,
		msg_replylistlab);

	if (auth_getoptionenvint("wbnoimages"))
		printf("<td width=\"100%%\"></td></tr></table><table border=\"0\" cellspacing=\"4\" cellpadding=\"4\"><tr>");

	/* FORWARD */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	printf("&amp;pos=%ld&amp;forward=1&amp;form=newmsg\">%s</a></td>\n",
		(long)msg_pos,
		msg_forwardlab);

	/* FORWARD AS ATTACHMENT*/

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	printf("&amp;pos=%ld&amp;forwardatt=1&amp;form=newmsg\">%s</a></td>\n",
		(long)msg_pos,
		msg_forwardattlab);

	/* FULL HEADERS */

	if (!pref_flagfullheaders && !*cgi("fullheaders"))
	{
		printf("<td class=\"message-menu-button\"><a href=\"");
		output_scriptptrget();
		folder_searchlink();
		output_mimegpgfilename();
		printf("&amp;pos=%ld&amp;form=readmsg&amp;fullheaders=1\">%s</a></td>\n",
			(long)msg_pos, msg_fullheaderlab);
	}

	/* PRINT MESSAGE */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();

	printf("&amp;pos=%ld&amp;form=print&amp;setcookie=1%s\" target=\"_blank\">%s</a></td>\n",
		(long)msg_pos,
		((pref_flagfullheaders || *cgi("fullheaders")) ? "&amp;fullheaders=1" : ""),
		msg_print);

	/* SAVE MESSAGE */

	printf("<td class=\"message-menu-button\"><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();

	printf("&amp;pos=%ld&amp;form=fetch&amp;download=1\">%s</a></td>", (long)msg_pos,
	       getarg("SAVEMESSAGE"));

    printf("<td width=\"100%%\"></td></tr></table></td><td align=\"right\" valign=\"middle\">");

	printf("<table border=\"0\" cellspacing=\"4\"><tr><td class=\"message-x-of-y\">&nbsp;");
	printf(msg_msglab, (int)msg_pos+1, (int)msg_count);
	printf("&nbsp;</td></tr></table>");
    printf("</td></tr></table>\n");
}

void list_folder(const char *p)
{
	char *s=folder_fromutf7(p);
	print_safe(s);
	free(s);
}

void list_folder_xlate(const char *p,
		       const char *path,
		       const char *n_inbox,
		       const char *n_drafts,
		       const char *n_sent,
		       const char *n_trash)
{
	if (strcmp(p, INBOX) == 0)
		printf("%s", n_inbox);
	else if (strcmp(p, INBOX "." DRAFTS) == 0)
		printf("%s", n_drafts);
	else if (strcmp(p, INBOX "." TRASH) == 0)
		printf("%s", n_trash);
	else if (strcmp(p, INBOX "." SENT) == 0)
		printf("%s", n_sent);
	else
		list_folder(path);
}

static void parse_hierarchy(const char *hierarchy,
			    void (*maildir_hier_cb)
			    (const char *pfix, const char *homedir,
			     const char *path, const char *inbox_name),
			    void (*sharehier_cb)
			    (const char *sharedhier,
			     struct maildir_shindex_cache *cache));

static void show_transfer_dest_real(const char *, const char *,
				    const char *, const char *);
static void show_transfer_dest_fake(const char *,
				    struct maildir_shindex_cache *);

static void show_transfer_dest(const char *cur_folder)
{
	parse_hierarchy(cur_folder, show_transfer_dest_real,
			show_transfer_dest_fake);
}

static void show_transfer_dest_fake(const char *dummy1,
				    struct maildir_shindex_cache *dummy2)
{
}

static void show_transfer_dest_real1(const char *inbox_pfix,
				     const char *homedir,
				     const char *cur_folder,
				     const char *inbox_name);

static void show_transfer_dest_real(const char *inbox_pfix,
				    const char *homedir,
				    const char *cur_folder,
				    const char *inbox_name)
{
	FILE *fp;
	char buf1[BUFSIZ];
	char buf2[BUFSIZ];

	show_transfer_dest_real1(inbox_pfix, homedir, cur_folder, inbox_name);

	if ((fp=fopen(SHAREDPATHCACHE, "r")) != NULL)
	{
		while (fgets(buf1, sizeof(buf1), fp) &&
		       fgets(buf2, sizeof(buf2), fp))
		{
			char *p;

			p=strchr(buf1, '\n');
			if (p) *p=0;
			p=strchr(buf2, '\n');
			if (p) *p=0;

			if (homedir == NULL || strcmp(buf1, homedir))
			{
				show_transfer_dest_real1(buf2, buf1,
							 cur_folder,
							 inbox_name);
			}
		}
		fclose(fp);
	}
}

static void show_transfer_dest_real1(const char *inbox_pfix,
				     const char *homedir,
				     const char *cur_folder,
				     const char *inbox_name)
{
	char	**folders;
	size_t	i;
	const	char *p;
	int	has_shared=0;

	maildir_listfolders(inbox_pfix, homedir, &folders);
	for (i=0; folders[i]; i++)
	{
		char acl_buf[2];

		strcpy(acl_buf, ACL_INSERT);
		acl_computeRightsOnFolder(folders[i], acl_buf);

		if (acl_buf[0] == 0)
			continue;

		/* Transferring TO drafts is prohibited */

		if (cur_folder == NULL || strcmp(cur_folder,
						 INBOX "." DRAFTS))
		{
			if (strcmp(folders[i], INBOX "." DRAFTS) == 0)
				continue;
		}
		else
		{
			if (strncmp(folders[i], SHARED ".",
				    sizeof(SHARED)) &&
			    strcmp(folders[i], INBOX "." TRASH))
				continue;
		}

		if (cur_folder && strcmp(cur_folder, folders[i]) == 0)
			continue;

		p=folders[i];

		if (strcmp(p, INBOX) == 0)
			p=folder_inbox;
		else if (strcmp(p, INBOX "." DRAFTS) == 0)
			p=folder_drafts;
		else if (strcmp(p, INBOX "." TRASH) == 0)
			p=folder_trash;
		else if (strcmp(p, INBOX "." SENT) == 0)
			p=folder_sent;
		if (!p)	p=folders[i];

		if (strncmp(folders[i], SHARED ".", sizeof(SHARED)) == 0)
		{
		char	*d=maildir_shareddir(".", strchr(folders[i], '.')+1);
		struct	stat	stat_buf;

			if (!d)
			{
				maildir_freefolders(&folders);
				enomem();
			}
			if (stat(d, &stat_buf))	/* Not subscribed */
			{
				free(d);
				continue;
			}
			free(d);

			if (!has_shared)
			{
				printf("<option value=\"\"></option>\n");
				has_shared=1;
			}
		}

		printf("<option value=\"");
		output_attrencoded(folders[i]);
		printf("\">");

		if (strncmp(folders[i], NEWSHARED, sizeof(NEWSHARED)-1) == 0)
		{
			printf("%s.", getarg("PUBLICFOLDERS"));
		}

		p=strchr(folders[i], '.');

		list_folder(p ? p+1:folders[i]);
		printf("</option>\n");
	}
	maildir_freefolders(&folders);
}

void folder_msgmove()
{
	++initnextprevcnt;
	printf("<table border=\"0\" class=\"box-small-outer\"><tr><td>\n");
	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr><td class=\"folder-move-background\">&nbsp;%s&nbsp;<select name=\"list%d\">\n", msg_movetolab, initnextprevcnt);

	show_transfer_dest(sqwebmail_folder);

	printf("</select><input type=\"submit\"%s name=\"move%d\" value=\"%s\" /></td></tr></table>\n",
		(msg_type == MSGTYPE_DELETED ? " disabled":""),
		initnextprevcnt,
		msg_golab ? msg_golab:"");
	printf("<input type=\"hidden\" name=\"pos\" value=\"%s\" />", cgi("pos"));
	printf("<input type=\"hidden\" name=\"posfile\" value=\"");
	output_attrencoded(msg_posfile ? msg_posfile:"");
	printf("\" /></td></tr></table>\n");
}

void folder_delmsg(size_t pos)
{
MSGINFO	**info;
int	dummy;
const	char *f=cgi("posfile");
size_t	newpos;
int	rc=0;
char nbuf[MAXLONGSIZE+10];

	CHECKFILENAME(f);

	if (*cgi("move1"))
	{
		rc=maildir_msgmovefile(sqwebmail_folder, f, cgi("list1"), pos);
		maildir_savefoldermsgs(sqwebmail_folder);
	}
	else if (*cgi("move2"))
	{
		rc=maildir_msgmovefile(sqwebmail_folder, f, cgi("list2"), pos);
		maildir_savefoldermsgs(sqwebmail_folder);
	}
	else
	{
		maildir_msgdeletefile(sqwebmail_folder, f, pos);
		maildir_savefoldermsgs(sqwebmail_folder);
	}

	if (rc)
	{
		http_redirect_argu("&form=readmsg&pos=%s&error=quota",
			(unsigned long)pos);
		return;
	}

	newpos=pos+1;
	info=maildir_read(sqwebmail_folder, 1, &newpos, &dummy, &dummy);

	if (info[0] && newpos != pos)
	{
		sprintf(nbuf, "%lu", (unsigned long)newpos);
	}
	else
	{
		sprintf(nbuf, "%lu", (unsigned long)pos);
	}

	maildir_free(info, 1);

	if (*cgi("search"))
	{
		http_redirect_argss("&form=readmsg&pos=%s&search=1&"
				    SEARCHRESFILENAME "=%s", nbuf,
				    cgi(SEARCHRESFILENAME));
	}
	else
	{
		http_redirect_argss("&form=readmsg&pos=%s", nbuf, "");
	}
}

static int is_preview_mode()
{
	/* We're in new message window, and we're previewing a draft */

	return (*cgi("showdraft"));
}

static void dokeyimport(FILE *, struct rfc2045 *, int);

static void charset_warning(const char *mime_charset, void *arg)
{
	char charset_buf[32];
	char *p;

	charset_buf[0]=0;
	strncat(charset_buf, mime_charset, sizeof(charset_buf)-1);

	for (p=charset_buf; *p; p++)
		if (*p < ' ' || *p > 0x7f ||
		    *p == '<' || *p == '>' || *p == '&')
			*p=' ';

	printf(getarg("CHSET"), charset_buf, sqwebmail_content_charset);
}

static void html_warning()
{
	printf("%s", getarg("HTML"));
}

static void init_smileys(struct msg2html_info *info)
{
	FILE *fp=open_langform(sqwebmail_content_language, "smileys.txt", 0);
	char buf[1024];

	char imgbuf[1024];

	if (!fp)
		return;

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		char *p=strchr(buf, '#');
		char *code;
		char *img;
		char *attr;

		if (p) *p=0;

		code=buf;

		for (p=buf; *p && !isspace(*p); p++)
			;

		if (*p)
			*p++=0;

		while (*p && isspace(*p))
			p++;
		img=p;

		while (*p && !isspace(*p))
			p++;
		if (*p)
			*p++=0;

		while (*p && isspace(*p))
			p++;
		attr=p;
		p=strchr(p, '\n');
		if (p) *p=0;

		if (!*code || !*img)
			continue;

		snprintf(imgbuf, sizeof(imgbuf),
			 "<img src=\"%s/%s\" %s />",
			 get_imageurl(), img, attr);

		msg2html_add_smiley(info, code, imgbuf);
	}
	fclose(fp);
}

static void email_address_start(const char *name, const char *addr)
{
	if (is_preview_mode())
	    return;

	printf("<a href=\"");
	output_scriptptrget();
	printf("&amp;form=quickadd&amp;pos=%s&amp;newname=",
	       cgi("pos"));

	if (name)
		output_urlencoded(name);

	printf("&amp;newaddr=");
	if (addr)
		output_urlencoded(addr);

	printf("\" style=\"text-decoration: none\" "
	       "onmouseover=\"window.status='%s'; return true;\" "
	       "onmouseout=\"window.status=''; return true;\" >"
	       "<span class=\"message-rfc822-header-address\">",
	       msg_add ? msg_add:"");
}

static void email_address_end()
{
	if (is_preview_mode())
		return;

	printf("</a></span>");
}

static void email_header(const char *h,
			 void (*cb_func)(const char *))
{
	char *hdrname;
	char *p;
	const char *hdrvalue;

	if ((hdrname=malloc(sizeof("DSPHDR_")+strlen(h))) == NULL)
		enomem();

	strcpy (hdrname, "DSPHDR_");
	strcat (hdrname, h);

	for (p=hdrname; *p; p++)
		*p=toupper((int)(unsigned char)*p);

	hdrvalue = getarg (hdrname);

	(*cb_func)(hdrvalue && *hdrvalue ? hdrvalue:h);

	free(hdrname);
}

static const char *email_header_date_fmt(const char *def)
{
	const char *date_fmt = getarg ("DSPFMT_DATE");

	if (date_fmt && *date_fmt)
		def=date_fmt;
	return def;
}

static void buf_cat_esc_amp(struct buf *b, const char *url)
{
	for (; *url; ++url)
	{
		char	c[2];

		if (*url == '&')
		{
			buf_cat(b, "&amp;");
		}
		else if (*url == '<')
		{
			buf_cat(b, "&lt;");
		}
		else if (*url == '>')
		{
			buf_cat(b, "&gt;");
		}
		else if (*url == '"')
		{
			buf_cat(b, "&quot;");
		}
		else
		{
			c[0]=*url;
			c[1]=0;
			buf_cat(b, c);
		}
	}
}

extern const char *redirect_hash(const char *timestamp);

static char *get_textlink(const char *s, void *arg)
{
char	*t;
struct buf b;

	buf_init(&b);

	if (strncmp(s, "mailto:", 7) == 0)
	{
	int	i;

		buf_cat(&b, "<a href=\"");

		{
			char *p=scriptptrget();

			buf_cat(&b, p);
			free(p);
		}

		buf_cat(&b, "&amp;form=newmsg&amp;to=");

		for (i=7; s[i]; i++)
		{
		char	c[2];

			c[1]=0;
			if ((c[0]=s[i]) == '?')
				c[0]='&';
 			else if (c[0] == '&')
			{
				buf_cat(&b, "&amp;");
			}
			else if (c[0] == '<')
			{
				buf_cat(&b, "&lt;");
			}
			else if (c[0] == '>')
			{
				buf_cat(&b, "&gt;");
			}
			else if (c[0] == '"')
			{
				buf_cat(&b, "&quot;");
			}
			else
			{
				buf_cat(&b, c);
			}
		}
		buf_cat(&b, "\">"
			"<span class=\"message-text-plain-mailto-link\">");
		buf_cat_esc_amp(&b, s);
		buf_cat(&b, "</span></a>");
	}
	else if (strncmp(s, "http:", 5) == 0 || strncmp(s, "https:", 6) == 0)
	{
		char buffer[NUMBUFSIZE];
		time_t now;
		char *hash;
		const char *n;

		time(&now);
		libmail_str_time_t(now, buffer);

		hash=cgiurlencode(redirect_hash(buffer));

		t=cgiurlencode(s);
		buf_cat(&b, "<a href=\"");

		n=getenv("SCRIPT_NAME");
		if (!n || !*n) n="/";

		buf_cat(&b, n);
		buf_cat(&b, "?redirect=");
		buf_cat(&b, t);
		buf_cat(&b, "&amp;timestamp=");
		buf_cat(&b, buffer);
		buf_cat(&b, "&amp;md5=");
		if (hash)
		{
			buf_cat(&b, hash);
			free(hash);
		}
		buf_cat(&b, "\" target=\"_blank\">"
			"<span class=\"message-text-plain-http-link\">");
		buf_cat_esc_amp(&b, s);
		buf_cat(&b, "</span></a>");
		free(t);
	}
	t=strdup(b.ptr ? b.ptr:"");
	if (!t)	enomem();
	buf_free(&b);
	return (t);
}

static void message_rfc822_action(struct rfc2045id *idptr)
{
	if (is_preview_mode())
		return;

	printf("<tr valign=\"top\"><td>&nbsp;</td><td align=\"left\" valign=\"top\">");

	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr><td><a href=\"");
	output_scriptptrget();
	output_mimegpgfilename();
	msg2html_showmimeid(idptr, NULL);
	printf("&amp;pos=%ld&amp;reply=1&amp;form=newmsg\"><font size=\"-1\">%s</font></a>&nbsp;</td><td>&nbsp;<a href=\"",
		(long)msg_pos, msg_replylab);

	output_scriptptrget();
	output_mimegpgfilename();
	msg2html_showmimeid(idptr, NULL);
	printf("&amp;pos=%ld&amp;replyall=1&amp;form=newmsg\"><font size=\"-1\">%s</font></a>&nbsp;</td><td>&nbsp;<a href=\"",
		(long)msg_pos, msg_replyalllab);
	output_scriptptrget();
	output_mimegpgfilename();
	msg2html_showmimeid(idptr, NULL);
	printf("&amp;pos=%ld&amp;forward=1&amp;form=newmsg\"><font size=\"-1\">%s</font></a>&nbsp;</td><td>&nbsp;<a href=\"",
		(long)msg_pos, msg_forwardlab);

	output_scriptptrget();
	output_mimegpgfilename();
	msg2html_showmimeid(idptr, NULL);
	printf("&amp;pos=%ld&amp;forwardatt=1&amp;form=newmsg\"><font size=\"-1\">%s</font></a></td></tr></table>\n",
		(long)msg_pos, msg_forwardattlab);

	printf("</td></tr>\n");
}

static void output_mimeurl(struct rfc2045id *id, const char *form)
{
	output_scriptptrget();
	printf("&amp;form=%s&amp;pos=%ld", form, (long)msg_pos);
	msg2html_showmimeid(id, NULL);

	output_mimegpgfilename();
}

static void inline_image_action(struct rfc2045id *id, const char *content_type,
				void *arg)
{
	if (!is_preview_mode())
	{
		printf("<a href=\"");
		output_mimeurl(id, "fetch");
		printf("\" target=\"_blank\">");
	}
	printf("<img src=\"");
	output_mimeurl(id, "fetch");
	printf("\" alt=\"Inline picture: ");
	output_attrencoded(content_type);
	printf("\" />%s\n",
	       is_preview_mode() ? "":"</a>");
}


static void showattname(const char *fmt, const char *name,
	const char *content_type)
{
char	*t;

	if (!name || !*name)	name=content_type;
	if (!name)	name="";

	t=malloc(strlen(name)+strlen(fmt)+100);
	if (!t)
		return;

	sprintf(t, fmt, name);
	output_attrencoded(t);
	free(t);
}

static void unknown_attachment_action(struct rfc2045id *id,
				      const char *content_type,
				      const char *content_name,
				      off_t size,
				      void *arg)
{
	printf("<table border=\"0\" cellpadding=\"1\" cellspacing=\"0\" class=\"box-small-outer\"><tr><td>");
	printf("<table border=\"0\" cellpadding=\"4\" cellspacing=\"0\" class=\"message-download-attachment\"><tr><td>");

	if (strcmp(cgi("form"), "print") == 0)
	{
		showattname(getarg("ATTSTUB"), content_name, content_type);

		printf("&nbsp;(");
		output_attrencoded(content_type);
		printf(")");
	}
	else
	{
		printf("<div align=\"center\"><span class=\"message-attachment-header\">");
		showattname(getarg("ATTACHMENT"), content_name, content_type);

		printf("&nbsp;(");
		output_attrencoded(content_type);
		printf("; %s)</span></div>",
		       showsize(size));
		printf("<br /><div align=\"center\">");

		if (!is_preview_mode())
		{
			printf("<a href=\"");
			output_mimeurl(id, "fetch");
			printf("\" style=\"text-decoration: none\" target=\"_blank\">");
			printf("%s</a>&nbsp;/&nbsp;", getarg("DISPATT"));
			printf("<a href=\"");
			output_mimeurl(id, "fetch");
			printf("&amp;download=1\" style=\"text-decoration: none\">");
			printf("%s</a>", getarg("DOWNATT"));
		}

		printf("</div>\n");
	}

	printf("</td></tr></table>\n");
	printf("</td></tr></table>\n");
}

static int is_gpg_enabled()
{
	return *cgi(MIMEGPGFILENAME) && !is_preview_mode();
}

static void application_pgp_keys_action(struct rfc2045id *id)
{
	printf("<table border=\"0\" cellpadding=\"8\" cellspacing=\"1\" class=\"box-small-outer\"><tr><td>");
	printf("<table border=\"0\" cellpadding=\"4\" cellspacing=\"4\" class=\"message-application-pgpkeys\"><tr><td>");

	if (strcmp(cgi("form"), "print") == 0 || is_preview_mode())
	{
		printf("%s", getarg("KEY"));
	}
	else
	{
		printf("<div align=\"center\"><a href=\"");
		output_scriptptrget();
		printf("&amp;form=keyimport&amp;pos=%ld", (long)msg_pos);
		printf("&amp;pubkeyimport=1");
		output_mimegpgfilename();
		msg2html_showmimeid(id, "&amp;keymimeid=");
		printf("\" style=\"text-decoration: none\" class=\"message-application-pgpkeys\">");
		printf("%s", getarg("PUBKEY"));
		printf("</a></div>");

		printf("<hr width=\"100%%\" />\n");

		printf("<div align=\"center\"><a href=\"");
		output_scriptptrget();
		printf("&amp;form=keyimport&amp;pos=%ld", (long)msg_pos);
		printf("&amp;privkeyimport=1");
		output_mimegpgfilename();
		msg2html_showmimeid(id, "&amp;keymimeid=");
		printf("\" style=\"text-decoration: none\" class=\"message-application-pgpkeys\">");
		printf("%s", getarg("PRIVKEY"));
		printf("</a></div>");
	}

	printf("</td></tr></table>\n");
	printf("</td></tr></table>\n<br />\n");
}

static void gpg_message_action()
{
	printf("<form method=\"post\" action=\"");
	output_scriptptr();
	printf("\">");
	output_scriptptrpostinfo();
	printf("<input type=\"hidden\" name=\"form\" value=\"readmsg\" />");
	printf("<input type=\"hidden\" name=\"pos\" value=\"%s\" />",
	       cgi("pos"));
	printf("<input type=\"hidden\" name=\"mimegpg\" value=\"1\" />\n");

	printf("<table border=\"0\" cellpadding=\"1\""
	       " width=\"100%%\" class=\"box-outer\">"
	       "<tr><td><table width=\"100%%\" border=\"0\" cellspacing=\"0\""
	       " cellpadding=\"0\" class=\"box-white-outer\"><tr><td>");

	if ( *cgi(MIMEGPGFILENAME))
	{
		printf("%s", getarg("NOTCOMPACTGPG"));
	}
	else
	{
		printf("%s\n", getarg("MIMEGPGNOTICE"));

		if (ishttps())
			printf("%s\n", getarg("PASSPHRASE"));

		printf("%s", getarg("DECRYPT"));
	}
	printf("</td><tr></table></td></tr></table></form><br />\n");
}

const char *redirect_hash(const char *timestamp)
{
	struct stat stat_buf;

	char buffer[NUMBUFSIZE*2+10];
	const char *p=getenv("SQWEBMAIL_RANDSEED");

	if (strlen(timestamp) >= NUMBUFSIZE)
		return "";

	strcat(strcpy(buffer, timestamp), " ");

	if (p && *p)
		strncat(buffer, p, NUMBUFSIZE);
	else
	{
		if (stat(SENDITSH, &stat_buf) < 0)
			return "";

		libmail_str_ino_t(stat_buf.st_ino, buffer+strlen(buffer));
	}

	return md5_hash_courier(buffer);
}

static char *get_url_to_mime_part(const char *mimeid, void *arg)
{
	const char *mimegpgfilename=cgi(MIMEGPGFILENAME);
	const char *pos;
	char *p, *q;

	p=scriptptrget();
	pos=cgi("pos");

	q=malloc(strlen(p)+strlen(pos) +
		 strlen(mimegpgfilename)+
		 strlen(mimeid)+
		 sizeof("&mimeid=&pos=&form=fetch&mimegpgfilename="));
	if (!q)	enomem();
	strcpy(q, p);
	strcat(q, "&form=fetch&pos=");
	strcat(q, pos);
	strcat(q, "&mimeid=");
	strcat(q, mimeid);

	if (*mimegpgfilename)
		strcat(strcat(q, "&mimegpgfilename="), mimegpgfilename);

	return (q);
}

void folder_showmsg(const char *dir, size_t pos)
{
	char	*filename;
	FILE	*fp;
	struct	rfc2045 *rfc;
	char	buf[BUFSIZ];
	int	n;
	int	fd;

	struct msg2html_info *info;

	const char *script_name=nonloginscriptptr();


	if (*cgi("addnick"))
	{
		const char *name=cgi("newname");
		const char *addr=cgi("newaddr");

		const char *nick1=cgi("newnick1");
		const char *nick2=cgi("newnick2");

		while (*nick1 && isspace((int)(unsigned char)*nick1))
		       ++nick1;

		while (*nick2 && isspace((int)(unsigned char)*nick2))
		       ++nick2;

		if (*nick2)
			nick1=nick2;

		if (*nick1)
		{
			ab_add(name, addr, nick1);
		}
	}

	filename=get_msgfilename(dir, &pos);

	fp=0;
	fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	if (fd >= 0)
	{
		if ((fp=fdopen(fd, "r")) == 0)
			close(fd);
	}

	if (!fp)
	{
		free(filename);
		return;
	}

	msg_pos=pos;
	rfc=rfc2045_alloc();

	while ((n=fread(buf, 1, sizeof(buf), fp)) > 0)
		rfc2045_parse(rfc, buf, n);
	rfc2045_parse_partial(rfc);

	info=script_name ? msg2html_alloc(sqwebmail_content_charset):NULL;

	if (info)
	{
		char nowbuffer[NUMBUFSIZE];
		time_t now;
		char *hash;
		char *washpfix;
		char *washpfixmailto;
		char *scriptnameget=scriptptrget();
		static const char formbuf[]="&form=newmsg&to=";

		info->mimegpgfilename=cgi(MIMEGPGFILENAME);
		if (*info->mimegpgfilename)
			CHECKFILENAME(info->mimegpgfilename);

		info->gpgdir=GPGDIR;
		info->fullheaders=pref_flagfullheaders || *cgi("fullheaders");
		info->noflowedtext=pref_noflowedtext;
		info->showhtml=pref_showhtml;

		info->charset_warning=charset_warning;
		info->html_content_follows=html_warning;
		info->get_url_to_mime_part=get_url_to_mime_part;

		time(&now);
		libmail_str_time_t(now, nowbuffer);

		hash=cgiurlencode(redirect_hash(nowbuffer));

		washpfix=malloc(strlen(script_name)
				+ strlen(hash ? hash:"") + strlen(nowbuffer)
				+ 100);
		if (!washpfix)	enomem();

		strcat(strcat(strcat(strcat(strcat(strcpy(washpfix,
							  script_name),
						   "?timestamp="),
					    nowbuffer),
				     "&md5="),
			      (hash ? hash:"")),
		       "&redirect=");

		if (hash)
			free(hash);

		washpfixmailto=malloc(strlen(scriptnameget)+sizeof(formbuf));
		if (!washpfixmailto)	enomem();
		strcat(strcpy(washpfixmailto, scriptnameget), formbuf);
		free(scriptnameget);

		info->wash_http_prefix=washpfix;
		info->wash_mailto_prefix=washpfixmailto;

		init_smileys(info);

		info->email_address_start=email_address_start;
		info->email_address_end=email_address_end;
		info->email_header=email_header;
		info->email_header_date_fmt=email_header_date_fmt;
		info->get_textlink=get_textlink;
		info->message_rfc822_action=message_rfc822_action;
		info->inline_image_action=inline_image_action;
		info->unknown_attachment_action=unknown_attachment_action;
		info->application_pgp_keys_action=
			application_pgp_keys_action;
		info->gpg_message_action=gpg_message_action;

		info->is_gpg_enabled=is_gpg_enabled();
		info->is_preview_mode=is_preview_mode();

		msg2html(fp, rfc, info);
		msg2html_free(info);

		free(washpfix);
		free(washpfixmailto);
	}

	rfc2045_free(rfc);
	fclose(fp);
	if (*cgi(MIMEGPGFILENAME) == 0)
		maildir_markread(dir, pos);
	free(filename);
}

void folder_keyimport(const char *dir, size_t pos)
{
	char	*filename;
	FILE	*fp;
	struct	rfc2045 *rfc;
	int	fd;

	filename=get_msgfilename(dir, &pos);

	fp=0;
	fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	if (fd >= 0)
	{
		if ((fp=fdopen(fd, "r")) == 0)
			close(fd);
	}

	if (!fp)
	{
		free(filename);
		return;
	}

	rfc=rfc2045_fromfp(fp);


	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		struct rfc2045 *part;

		if (*cgi("pubkeyimport")
		    && (part=rfc2045_find(rfc, cgi("keymimeid"))) != 0)
		{
			dokeyimport(fp, part, 0);
		}
		else if (*cgi("privkeyimport")
		    && (part=rfc2045_find(rfc, cgi("keymimeid"))) != 0)
		{
			dokeyimport(fp, part, 1);
		}
	}
	rfc2045_free(rfc);
	fclose(fp);
	free(filename);

	printf("<p><a href=\"");
	output_scriptptrget();
	printf("&amp;form=readmsg&amp;pos=%s", cgi("pos"));
	printf("\">%s</a>", getarg("KEYIMPORT"));
}

static int importkey_func(const char *p, size_t cnt, void *voidptr);
static int importkeyin_func(const char *p, size_t cnt, void *voidptr);

static void dokeyimport(FILE *fp, struct rfc2045 *rfcp, int issecret)
{
	off_t	start_pos, end_pos, start_body, ldummy;
	char buf[BUFSIZ];
	int cnt;

	static const char start_str[]=
		"<table width=\"100%%\" border=\"0\" class=\"box-outer\"><tr><td>"
		"<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\""
		" class=\"box-white-outer\"><tr><td>%s<pre>\n";

	static const char end_str[]=
		"</pre></td></tr></table></td></tr></table><br />\n";

	if (libmail_gpg_import_start(GPGDIR, issecret))
		return;

	printf(start_str, getarg("IMPORTHDR"));

	rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body,
		&ldummy, &ldummy);
	if (fseek(fp, start_body, SEEK_SET) < 0)
	{
		error("Seek error.");
		libmail_gpg_import_finish(&importkey_func, NULL);
		printf("%s", end_str);
		return;
	}

	rfc2045_cdecode_start(rfcp, &importkeyin_func, 0);

	while (start_body < end_pos)
	{
		cnt=sizeof(buf);
		if (cnt > end_pos-start_body)
			cnt=end_pos-start_body;
		cnt=fread(buf, 1, cnt, fp);
		if (cnt <= 0)	break;
		start_body += cnt;
		if (rfc2045_cdecode(rfcp, buf, cnt))
		{
			rfc2045_cdecode_end(rfcp);
			printf("%s", end_str);
			return;
		}
	}

	if (rfc2045_cdecode_end(rfcp) == 0)
	{
		libmail_gpg_import_finish(&importkey_func, NULL);
	}

	printf("%s", end_str);
}

static int importkeyin_func(const char *p, size_t cnt, void *voidptr)
{
	return (libmail_gpg_import_do(p, cnt, &importkey_func, NULL));
}

static int importkey_func(const char *p, size_t cnt, void *voidptr)
{
	print_attrencodedlen(p, cnt, 1, stdout);
	return (0);
}


/*
** If we're currently showing (INBOX|shared|#shared).foo.bar hierarchy, return
** "x.foo".  If we're currently showing (INBOX|shared|#shared).foo, return
** an empty string.
*/
static char *get_parent_folder(const char *p)
{
	const char *q;

	q=strrchr(p, '.');

	if (q)
	{
		char	*s;

		s=malloc(q-p+1);
		if (!s)	enomem();
		memcpy(s, p, q-p);
		s[q-p]=0;
		return (s);
	}
	return (strdup(""));
}

static int checkrename(const char *origfolder,
		       const char *newfolder)
{
	char acl_buf[2];
	char *p, *q;

	strcpy(acl_buf, ACL_DELETEFOLDER);
	acl_computeRightsOnFolder(origfolder, acl_buf);
	if (acl_buf[0] == 0)
	{
		folder_err_msg=getarg("RENAME");
		return -1;
	}

	strcpy(acl_buf, ACL_CREATE);
	p=strdup(newfolder);

	if (!p || !(q=strrchr(p, '.')) ||
	    (*q=0,
	     acl_computeRightsOnFolder(p, acl_buf),
	     acl_buf[0]) == 0)
	{
		if (p)
			free(p);

		folder_err_msg=getarg("RENAME");
		return -1;
	}
	free(p);

	return 0;
}

static void dorename(const char *origfolder,
		     struct maildir_info *mifrom,
		     struct maildir_info *mito,
		     const char *err_invalid,
		     const char *err_cantdelete,
		     const char *err_exists)
{
	char *s;
	char *t;
	char *u;

	const char *p;
	struct	stat	stat_buf;

	if (mifrom->homedir == NULL ||
	    mifrom->maildir == NULL ||
	    mito->homedir == NULL ||
	    mito->maildir == NULL ||
	    strcmp(mifrom->homedir, mito->homedir))
	{
		folder_err_msg=err_invalid;
		return;
	}

	s=maildir_name2dir(".", mifrom->maildir);
	t=maildir_name2dir(".", mito->maildir);

	if (!s || !t)
	{
		if (s) free(s);
		if (t) free(t);
		folder_err_msg=err_invalid;
		return;
	}

	p=s;
	if (strncmp(p, "./", 2) == 0) p += 2;

	if (strcmp(p, ".") == 0 ||
	    strcmp(p, "." SENT) == 0 ||
	    strcmp(p, "." DRAFTS) == 0 ||
	    strcmp(p, "." TRASH) == 0)
	{
		free(s);
		free(t);
		folder_err_msg=err_invalid;
		return;
	}

	u=maildir_name2dir(mito->homedir, mito->maildir);
	if (!u)
	{
		free(s);
		free(t);
		folder_err_msg=err_invalid;
		return;
	}

	if (stat(u, &stat_buf) == 0)
	{
		free(s);
		free(t);
		folder_err_msg=err_exists;
		return;
	}
	free(u);

	if (mailfilter_folderused(origfolder))
	{
		free(s);
		free(t);
		folder_err_msg=err_cantdelete;
		return;
	}

	if (maildir_rename(mifrom->homedir,
			   strncmp(s, "./", 2) == 0 ? s+2:s,
			   strncmp(t, "./", 2) == 0 ? t+2:t,
			   MAILDIR_RENAME_FOLDER, NULL))
		folder_err_msg=err_cantdelete;

	free(s);
	free(t);
}

struct publicfolderlist_helper {
	char *name;
	char *homedir;
	char *maildir;
};

static void freeph(struct publicfolderlist_helper *ph)
{
	if (ph->name)
		free(ph->name);
	if (ph->homedir)
		free(ph->homedir);
	if (ph->maildir)
		free(ph->maildir);
	memset(ph, 0, sizeof(*ph));
}

static void do_folderlist(const char *pfix, const char *homedir,
			  const char *path, const char *inbox_name);
static void do_sharedhierlist(const char *sharedhier,
			      struct maildir_shindex_cache *cache);

static int checkcreate(const char *f, int isrec)
{
	char *s=strdup(f);
	char *q;
	char buf[2];
	struct maildir_info minfo;

	if (!s)
	{
		folder_err_msg=getarg("CREATEPERMS");
		return -1;
	}

	if (isrec)
	{
		if ((q=strrchr(s, '.')) == NULL ||
		    (*q=0, checkcreate(s, 0)) < 0)
		{
			free(s);
			folder_err_msg=getarg("CREATEPERMS");
			return -1;
		}
		*q='.';
	}

	if (maildir_info_imap_find(&minfo, s, login_returnaddr()) < 0)
	{
		free(s);
		folder_err_msg=getarg("CREATEPERMS");
		return -1;
	}

	if (strchr(minfo.maildir, '.') == NULL)
	{
		free(s);
		folder_err_msg=getarg("CREATEPERMS");
		return -1;
	}

	maildir_acl_delete(minfo.homedir,
			   strchr(minfo.maildir, '.'));

	maildir_info_destroy(&minfo);

	strcpy(buf, ACL_CREATE);

	if ((q=strrchr(s, '.')) == NULL ||
	    (*q=0,
	     acl_computeRightsOnFolder(s, buf),
	     buf[0]) == 0)
	{
		free(s);
		folder_err_msg=getarg("CREATEPERMS");
		return -1;
	}
	free(s);
	return 0;
}

void folder_list()
{
	const char	*err_invalid;
	const char	*err_exists;
	const char	*err_cantdelete;
	const char	*msg_hasbeensent;

	err_invalid=getarg("INVALID");
	err_exists=getarg("EXISTS");
	err_cantdelete=getarg("DELETE");
	msg_hasbeensent=getarg("WASSENT");

	folder_err_msg=0;

	if (strcmp(cgi("foldermsg"), "sent") == 0)
		folder_err_msg=msg_hasbeensent;

	if (*cgi("do.create"))
	{
		const char	*newfoldername=trim_spaces(cgi("foldername"));
		const char	*newdirname=trim_spaces(cgi("dirname"));
		const char	*folderdir=cgi("folderdir");
		char	*futf7;
		char	*dutf7;

		/*
		** New folder names cannot contain .s, and must be considered
		** as valid by maildir_folderpath.
		*/

		if (!*folderdir)
			folderdir=INBOX;

		futf7=folder_toutf7(newfoldername);
		dutf7=folder_toutf7(newdirname);;

		if (!*newfoldername ||
		    strchr(futf7, '.') ||
		    strchr(dutf7, '.'))
		{
			free(futf7);
			free(dutf7);
			folder_err_msg=err_invalid;
		}
		else
		{
			char	*p;
			struct maildir_info minfo;
			char *q;

			p=malloc(strlen(folderdir)+strlen(futf7)
				 +strlen(dutf7)+3);

			if (!p)	enomem();
			strcpy(p, folderdir);
			if (*dutf7)
			{
				if (*p)	strcat(p, ".");
				strcat(p, dutf7);
			}
			if (*p)	strcat(p, ".");
			strcat(p, futf7);

			free(futf7);
			free(dutf7);

			if (maildir_info_imap_find(&minfo, p,
						   login_returnaddr()) < 0)
			{
				folder_err_msg=err_invalid;
			}
			else if (minfo.homedir == NULL ||
				 minfo.maildir == NULL ||
				 (q=maildir_name2dir(minfo.homedir,
						     minfo.maildir)) == NULL)
			{
				maildir_info_destroy(&minfo);
				folder_err_msg=err_invalid;
			}
			else if (access(q, 0) == 0)
			{
				free(q);
				maildir_info_destroy(&minfo);
				folder_err_msg=err_exists;
			}
			else
			{
				if (checkcreate(p, *newdirname != 0) == 0)
				{
					if (maildir_make(q, 0700, 0700, 1))
						folder_err_msg=err_exists;
					else
					{
						char buf[1];

						buf[0]=0;
						acl_computeRightsOnFolder(p,
									  buf);
						/* Initialize ACLs correctly */
					}
				}
				free(q);
				maildir_info_destroy(&minfo);
			}
		}
	}

	if (*cgi("do.delete"))
	{
		const char *p=cgi("DELETE");
		char acl_buf[2];

		strcpy(acl_buf, ACL_DELETEFOLDER);
		acl_computeRightsOnFolder(p, acl_buf);
		if (acl_buf[0] == 0)
			folder_err_msg=getarg("DELETEPERMS");
		else if (mailfilter_folderused(p))
			folder_err_msg=err_cantdelete;
		else if (maildir_delete(p, *cgi("deletecontent")))
			folder_err_msg=err_cantdelete;
		else
			maildir_quota_recalculate(".");
	}

	if (*cgi("do.subunsub"))
	{
		const char *p=cgi("DELETE");
		char	*pp=strdup(p);
		char *d;

		if (pp && strncmp(pp, SHARED ".", sizeof(SHARED)) == 0 &&
		    (d=maildir_shareddir(".", pp+sizeof(SHARED))) != 0)
		{
		struct stat	stat_buf;

			if (stat(d, &stat_buf) == 0)
				maildir_shared_unsubscribe(".",
							   pp+sizeof(SHARED));
			else
				maildir_shared_subscribe(".",
							 pp+sizeof(SHARED));
			free(d);
		}

		if (pp)
			free(pp);
	}

	if (*cgi("do.rename"))
	{
		const char *p=cgi("DELETE");
		char	*pp=strdup(p);
		struct maildir_info mifrom, mito;
		const char *qutf7=cgi("renametofolder");
		const char *r=trim_spaces(cgi("renametoname"));
		char	*s;
		char	*rutf7;

		rutf7=folder_toutf7(r);

		s=malloc(strlen(qutf7)+strlen(rutf7)+1);

		if (!s)	enomem();

		strcat(strcpy(s, qutf7), rutf7);

		if (strchr(r, '.') == NULL
		    && maildir_info_imap_find(&mifrom, pp,
					      login_returnaddr()) == 0)
		{
			if (maildir_info_imap_find(&mito, s,
						   login_returnaddr()) == 0)
			{
				if (checkrename(pp, s) == 0)
					dorename(pp, &mifrom, &mito,
						 err_invalid,
						 err_cantdelete,
						 err_exists);
				maildir_info_destroy(&mifrom);
			}
			else
			{
				folder_err_msg=err_invalid;
			}
			maildir_info_destroy(&mito);
		}
		else
		{
			folder_err_msg=err_invalid;
		}
		free(rutf7);
		free(pp);
		free(s);
		maildir_quota_recalculate(".");
	}

	parse_hierarchy(cgi("folderdir"),
			    do_folderlist,
			    do_sharedhierlist);
}

static int do_publicfolderlist_cb(struct maildir_newshared_enum_cb *cb)
{
	struct publicfolderlist_helper *h=
		(struct publicfolderlist_helper *)cb->cb_arg;

	h->name=strdup(cb->name);
	if (cb->homedir)
		h->homedir=strdup(cb->homedir);
	h->maildir=strdup(cb->maildir);
	return 0;
}

static void parse_hierarchy(const char *folderdir,
			    void (*maildir_hier_cb)
			    (const char *pfix, const char *homedir,
			     const char *path, const char *inbox_name),
			    void (*sharedhier_cb)
			    (const char *sharedhier,
			     struct maildir_shindex_cache *cache))
{
	struct maildir_shindex_cache *index;
	const char *indexfile;
	const char *subhierarchy;
	const char *p;
	const char *q;
	size_t l;
	size_t n;
	struct publicfolderlist_helper ph;
	int eof;

	if (strchr(folderdir, '/'))
		enomem();

	if (strncmp(folderdir, NEWSHAREDSP, sizeof(NEWSHAREDSP)-1) == 0)
		switch (folderdir[sizeof(NEWSHAREDSP)-1]) {
		case 0:
			verify_shared_index_file=1;
			/* FALLTHRU */
		case '.':
			break;
		default:
			(*maildir_hier_cb)(INBOX, NULL, folderdir, INBOX);
			return;
		}
	else
	{
		(*maildir_hier_cb)(INBOX, NULL, folderdir, INBOX);
		return;
	}

	index=NULL;
	indexfile=NULL;
	subhierarchy=NULL;
	p=folderdir;

	memset(&ph, 0, sizeof(ph));

	while ((index=maildir_shared_cache_read(index, indexfile,
						subhierarchy)) != NULL)
	{
		q=strchr(p, '.');
		if (!q)
			break;

		p=q+1;

		if ((q=strchr(p, '.')) != NULL)
			l=q-p;
		else
			l=strlen(p);

		for (n=0; n<index->nrecords; n++)
		{
			char *m=maildir_info_imapmunge(index->records[n].name);

			if (!m)
				continue;

			if (strlen(m) == l &&
			    strncmp(m, p, l) == 0)
			{
				free(m);
				break;
			}
			free(m);
		}

		if (n >= index->nrecords)
		{
			index=NULL;
			break;
		}

		index->indexfile.startingpos=index->records[n].offset;
		freeph(&ph);
		if (maildir_newshared_nextAt(&index->indexfile, &eof,
					     &do_publicfolderlist_cb, &ph) ||
		    eof)
		{
			index=NULL;
			break;
		}

		if (ph.homedir)
		{
			char *loc=maildir_location(ph.homedir,
						   ph.maildir);
			char *m_path;
			char *m_inbox;

			if (loc)
			{
				while (*p)
				{
					if (*p == '.')
						break;
					++p;
				}

				m_path=malloc(p-folderdir+1);
				if (!m_path)
					enomem();
				memcpy(m_path, folderdir, p-folderdir);
				m_path[p-folderdir]=0;

				m_inbox=malloc(strlen(m_path)+1+strlen(p));
				if (!m_inbox)
					enomem();

				strcat(strcpy(m_inbox, m_path), p);

				savepath(m_path, loc);
				(*maildir_hier_cb)(m_path, loc, m_inbox,
						  m_path);
				free(loc);
				free(m_path);
				free(m_inbox);
			}
			freeph(&ph);
			return;
		}

		indexfile=ph.maildir;
		subhierarchy=index->records[n].name;
	}

	freeph(&ph);

	(*sharedhier_cb)(folderdir, index);
}

static void do_sharedhierlist(const char *folderdir,
			      struct maildir_shindex_cache *index)
{
	const char *p;
	const char *q;
	size_t n;
	struct publicfolderlist_helper ph;
	const char *folders_img;
	const char *name_inbox;
	int eof;
	char *url, *url2;

	p=strrchr(folderdir, '.');

	if (p)
		++p;
	else p=folderdir;

	folders_img=getarg("FOLDERSICON");
	name_inbox=getarg("INBOX");

	memset(&ph, 0, sizeof(ph));

       	printf("<table width=\"100%%\" border=\"0\" cellpadding=\"2\" cellspacing=\"0\" class=\"folderlist\">\n"
	       "<tr><td align=\"left\" "
	       "class=\"folderparentdir\">%s&lt;&lt;&lt;&nbsp;",
	       folders_img);

	if (strcmp(folderdir, NEWSHAREDSP) == 0)
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=folders&amp;folder=INBOX\">");
		print_safe(name_inbox);
		printf("</a>");
	}
	else
	{
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;folderdir=");
		output_urlencoded(NEWSHAREDSP);
		printf("&amp;form=folders&amp;folder=INBOX\">%s</a>",
		       getarg("PUBLICFOLDERS"));
	}

	for (q=folderdir; q<p; )
	{
		const char *r;
		char *s;

		if (*q == '.')
		{
			++q;
			continue;
		}

		for (r=q; *r; r++)
			if (*r == '.')
				break;

		if (q != folderdir)
		{
			printf(".<a href=\"");
			output_scriptptrget();
			printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir=");


			s=malloc(r-folderdir+1);
			if (!s)
				enomem();

			memcpy(s, folderdir, r-folderdir);
			s[r-folderdir]=0;

			output_urlencoded(s);
			printf("\">");
			free(s);

			s=malloc(r-q+1);
			if (!s)
				enomem();
			memcpy(s, q, r-q);
			s[r-q]=0;
			list_folder(s);
			free(s);
			printf("</a>");
		}
		q=r;
	}

	printf("</td></tr>\n");


	while (*q && *q != '.')
		++q;

	url=malloc(q-folderdir+1);
	if (!url)
		enomem();
	memcpy(url, folderdir, q-folderdir);
	url[q-folderdir]=0;

	for (n=0; index && n<index->nrecords; n++)
	{
		freeph(&ph);

		if (n == 0)
			index->indexfile.startingpos=0;

		if ((n == 0 ? &maildir_newshared_nextAt:
		     &maildir_newshared_next)(&index->indexfile, &eof,
					      &do_publicfolderlist_cb, &ph) ||
		    eof)
		{
			break;
		}

		if (ph.homedir)
		{
			char *d=maildir_location(ph.homedir, ph.maildir);

			if (d)
			{
				if (maildir_info_suppress(d))
				{
					free(d);
					continue;
				}
				free(d);
			}
		}

		printf("<tr class=\"foldersubdir\"><td align=\"left\">%s"
		       "&gt;&gt;&gt;&nbsp;<a href=\"", folders_img);
		output_scriptptrget();
		printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir=");

		output_urlencoded(url);

		url2=maildir_info_imapmunge(ph.name);

		if (!url2)
			enomem();
		printf(".");
		output_urlencoded(url2);

		printf("\">");
		list_folder(url2);
		free(url2);
		printf("</a></td></tr>\n");
	}
	free(url);
	freeph(&ph);
	printf("</table>\n");
}

static void do_folderlist(const char *inbox_pfix,
			  const char *homedir,
			  const char *folderdir,
			  const char *inbox_name)
{
	const char	*name_inbox;
	const char	*name_drafts;
	const char	*name_sent;
	const char	*name_trash;
	const char	*folder_img;
	const char	*folders_img;
	const char	*unread_label;
	const char	*acl_img;
	char acl_buf[4];
	char	**folders;
	size_t	i;
	unsigned nnew, nother;
	size_t folderdir_l;

	name_inbox=getarg("INBOX");
	name_drafts=getarg("DRAFTS");
	name_sent=getarg("SENT");
	name_trash=getarg("TRASH");
	folder_img=getarg("FOLDERICON");
	folders_img=getarg("FOLDERSICON");
	sqwebmail_folder=0;
	unread_label=getarg("UNREAD");
	acl_img=maildir_newshared_disabled ? NULL : getarg("ACLICON");

       	printf("<table width=\"100%%\" border=\"0\" cellpadding=\"2\" cellspacing=\"0\" class=\"folderlist\">\n");

	maildir_listfolders(inbox_pfix, homedir, &folders);

	if (*folderdir && strcmp(folderdir, INBOX))
	{
		char *parentfolder;
		size_t	i;
		char *q, *r;
		const char *c;

		if (strncmp(folderdir, SHARED ".", sizeof(SHARED)) == 0)
		{
			for (c=folderdir; *c; c++)
				if (*c == '.')
					break;

			r=malloc(strlen(inbox_pfix)+strlen(c)+1);
			if (!r)
				enomem();
			strcat(strcpy(r, inbox_pfix), c);

			parentfolder=get_parent_folder(r);
			free(r);
		}
		else
			parentfolder=get_parent_folder(folderdir);

		for (q=parentfolder; *q; q++)
			if (*q == '.')
				break;

		printf("<tr><td align=\"left\" colspan=\"2\" class=\"folderparentdir\">%s", folders_img);
		printf("&lt;&lt;&lt;&nbsp;");

#if 0
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;folderdir=");
		output_urlencoded(inbox_pfix);
		printf("&amp;form=folders&amp;folder=INBOX\">");
		print_safe(inbox_name);
		printf("</a>");
#endif

		i=0;
		while (parentfolder[i])
		{
		char	*p=strchr(parentfolder+i, '.');
		int	c;

			if (!p)	p=parentfolder+strlen(parentfolder);
			c= *p;
			*p=0;

			if (strchr(parentfolder, '.'))
				printf(".");
			printf("<a href=\"");
			output_scriptptrget();
			printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir=");
			output_urlencoded(parentfolder);
			printf("\">");
			if (strcmp(parentfolder, NEWSHAREDSP) == 0)
				printf("%s", getarg("PUBLICFOLDERS"));
			else
				list_folder_xlate(parentfolder,
						  parentfolder+i,
						  name_inbox,
						  name_drafts,
						  name_sent,
						  name_trash);
			printf("</a>");
			if ( (*p=c) != 0)	++p;
			i=p-parentfolder;
		}
		printf("</td></tr>\n");
		free(parentfolder);
	}
	else if (strcmp(inbox_pfix, INBOX))
	{
		size_t i;
		char *p;
		char *q;

		printf("<tr><td align=\"left\" colspan=\"2\" class=\"folderparentdir\">%s&lt;&lt;&lt;&nbsp;", folders_img);

		p=strdup(inbox_pfix);
		if (!p)
			enomem();

		if ((q=strrchr(p, '.')) != 0)
			*q=0;

		for (i=0; p[i]; )
		{
			size_t j;
			char save_ch;

			for (j=i; p[j]; j++)
				if (p[j] == '.')
					break;


			save_ch=p[j];
			p[j]=0;

			if (i)
				printf(".");

			printf("<a href=\"");
			output_scriptptrget();
			printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir=");
			output_urlencoded(p);
			printf("\">");

			if (strcmp(p, NEWSHAREDSP) == 0)
				printf("%s", getarg("PUBLICFOLDERS"));
			else
				list_folder(p+i);
			printf("</a>");

			p[j]=save_ch;

			if (save_ch)
				++j;
			i=j;
		}
		printf("</td></tr>\n");
		free(p);
	}


	if (!folderdir || strchr(folderdir, '.') == 0)
	{
		folderdir=inbox_pfix;
	}

	folderdir_l=strlen(folderdir);

	for (i=0; folders[i]; i++)
	{
		const	char *p;
		const	char *shortname=folders[i];

		size_t	j;
		const char *pfix;
		int isunsubscribed=0;
		const char	*img=folder_img;

		pfix="&gt;&gt;&gt;";

		if (strncmp(shortname, SHARED ".",
			    sizeof(SHARED)) == 0)
		{
			char	*dir;
			struct	stat	stat_buf;

			pfix="+++";

			dir=maildir_shareddir(".",
					      shortname+sizeof(SHARED));
			if (!dir)	continue;
			if (stat(dir, &stat_buf))
				isunsubscribed=1;
			free(dir);
		}

		if (strcmp(shortname, inbox_name) == 0 &&
		    strcmp(folderdir, inbox_name) == 0)
		{
			/* List INBOX at the top level */

			strcpy(acl_buf, ACL_LOOKUP ACL_ADMINISTER);
			acl_computeRightsOnFolder(shortname, acl_buf);
			if (acl_buf[0] == 0)
				continue;
		}
		else
		{
			if (strcmp(folderdir, INBOX) == 0 &&
			    strncmp(shortname, SHARED ".", sizeof(SHARED)) == 0)
			{
				shortname += sizeof(SHARED);
				strcpy(acl_buf, ACL_LOOKUP);
			}
			else
			{
				if (memcmp(shortname, folderdir, folderdir_l) ||
				    shortname[folderdir_l] != '.')
				{
					continue;
				}

				strcpy(acl_buf, ACL_LOOKUP ACL_ADMINISTER);
				acl_computeRightsOnFolder(shortname, acl_buf);
				if (acl_buf[0] == 0)
					continue;

				shortname += folderdir_l;
				++shortname;
			}

			if ((p=strchr(shortname, '.')) != 0)
			{
				char *s;
				char	*t;
				unsigned tot_nnew, tot_nother;

				s=malloc(p-folders[i]+1);
				if (!s)
					enomem();
				memcpy(s, folders[i], p-folders[i]);
				s[p-folders[i]]=0;

				printf("<tr class=\"foldersubdir\"><td align=\"left\">");
				if (acl_img && strchr(acl_buf, ACL_ADMINISTER[0]))
				{
					printf("<a href=\"");
					output_scriptptrget();
					printf("&amp;form=acl&amp;folder=");
					output_urlencoded(s);
					printf("\">%s</a>&nbsp;", acl_img);
				}

				printf("%s%s&nbsp;", folders_img, pfix);
				if (acl_buf[0])
				{
					printf("<a href=\"");
					output_scriptptrget();
					printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir=");
					output_urlencoded(s);
					printf("\">");
				}
				free(s);

				t=malloc(p-shortname+1);
				if (!t)	enomem();
				memcpy(t, shortname, p-shortname);
				t[p-shortname]=0;
				list_folder_xlate(folders[i],
						  t,
						  name_inbox,
						  name_drafts,
						  name_sent,
						  name_trash);
				free(t);
				if (strchr(acl_buf, ACL_LOOKUP[0]))
				{
					printf("</a>");
				}

				tot_nnew=0;
				tot_nother=0;

				j=i;
				while (folders[j] && memcmp(folders[j], folders[i],
							    p-folders[i]+1) == 0)
				{
					strcpy(acl_buf, ACL_LOOKUP ACL_READ);
					acl_computeRightsOnFolder(folders[j],
								  acl_buf);
					if (acl_buf[0] == 0)
					{
						++j;
						continue;
					}
					maildir_count(folders[j], &nnew, &nother);
					++j;
					tot_nnew += nnew;
					tot_nother += nother;
				}
				i=j-1;
				if (tot_nnew)
				{
					printf(" <span class=\"subfolderlistunread\">");
					printf(unread_label, tot_nnew);
					printf("</span>");
				}
				printf("</td><td align=\"right\" valign=\"top\"><span class=\"subfoldercnt\">%d</span>&nbsp;&nbsp;</td></tr>\n\n",
				       tot_nnew + tot_nother);
				continue;
			}
		}

		nnew=0;
		nother=0;

		if (strchr(acl_buf, ACL_LOOKUP[0]) == NULL)
			isunsubscribed=1;

		if (!isunsubscribed)
			maildir_count(folders[i], &nnew, &nother);

		printf("<tr%s><td align=\"left\" valign=\"top\">",
			isunsubscribed ? " class=\"folderunsubscribed\"":"");

		if (acl_img && strchr(acl_buf, ACL_ADMINISTER[0]))
		{
			printf("<a href=\"");
			output_scriptptrget();
			printf("&amp;form=acl&amp;folder=");
			output_urlencoded(folders[i]);
			printf("\">%s</a>&nbsp", acl_img);
		}

		printf("%s&nbsp;<input type=\"radio\" name=\"DELETE\" value=\"", img);
		output_attrencoded(folders[i]);
		printf("\" />&nbsp;");
		if (!isunsubscribed)
		{
			printf("<a class=\"folderlink\" href=\"");
			output_scriptptrget();
			printf("&amp;form=folder&amp;folder=");
			output_urlencoded(folders[i]);
			printf("\">");
		}

		list_folder_xlate(folders[i],
				  strcmp(folders[i], inbox_name) == 0
				  ? INBOX:shortname,
				  name_inbox,
				  name_drafts,
				  name_sent,
				  name_trash);
		if (!isunsubscribed)
			printf("</a>");
		if (nnew)
		{
			printf(" <span class=\"folderlistunread\">");
			printf(unread_label, nnew);
			printf("</span>");
		}
		printf("</td><td align=\"right\" valign=\"top\">");

		if (!isunsubscribed)
		{
			printf("<span class=\"foldercnt\">%d</span>&nbsp;&nbsp;",
			       nnew + nother);
		}
		else
		printf("&nbsp;\n");
		printf("</td></tr>\n\n");
	}
	maildir_freefolders(&folders);

	if (strcmp(folderdir, INBOX) == 0 && !maildir_newshared_disabled)
	{
		char *sp=cgiurlencode(NEWSHAREDSP);

		printf("<tr class=\"foldersubdir\"><td align=\"left\">%s&gt;&gt;&gt;&nbsp;<a href=\"", folders_img);
		output_scriptptrget();
		printf("&amp;form=folders&amp;folder=INBOX&amp;folderdir="
		       "%s\">%s</a>"
		       "</td><td>&nbsp;</td></tr>\n\n",
		       sp,
		       getarg("PUBLICFOLDERS"));
		free(sp);
	}
	printf("</table>\n");
}

void folder_list2()
{
	if (folder_err_msg)
	{
		printf("%s\n", folder_err_msg);
	}
}

static void folder_rename_dest_fake(const char *dummy1,
				    struct maildir_shindex_cache *dummy2);
static void folder_rename_dest_real(const char *inbox_pfix,
				    const char *homedir,
				    const char *cur_folder,
				    const char *inbox_name);

void folder_rename_list()
{
	parse_hierarchy(cgi("folderdir"), folder_rename_dest_real,
			folder_rename_dest_fake);
}

static void folder_rename_dest_fake(const char *dummy1,
				    struct maildir_shindex_cache *dummy2)
{
}

static void folder_rename_dest_real(const char *inbox_pfix,
				    const char *homedir,
				    const char *cur_folder,
				    const char *inbox_name)
{
	char	**folders;
	int	i;
	size_t pl=strlen(inbox_pfix);

	printf("<select name=\"renametofolder\">\n");
	printf("<option value=\"%s.\">", inbox_pfix);
	printf("( ... )");
	printf("</option>\n");

	maildir_listfolders(inbox_pfix, homedir, &folders);
	for (i=0; folders[i]; i++)
	{
		const char *p=folders[i];
		char	*q;
		size_t	ql;
		char acl_buf[2];

		if (strncmp(p, inbox_pfix, pl) == 0)
			switch (p[pl]) {
			case '.':
				break;
			default:
				continue;
			}
		else
			continue;

		p += pl+1;

		p=strrchr(p, '.');
		if (!p)	continue;
		q=malloc(p-folders[i]+1);
		if (!q)	enomem();
		memcpy(q, folders[i], p-folders[i]);
		q[p-folders[i]]=0;
		strcpy(acl_buf, ACL_CREATE);
		acl_computeRightsOnFolder(q, acl_buf);
		if (acl_buf[0])
		{
			printf("<option value=\"");
			output_attrencoded(q);
			printf(".\"%s>",
			       strcmp(q, cgi("folderdir")) == 0
			       ? " selected='selected'":"");
			list_folder(strchr(q, '.')+1);
			printf(".</option>\n");
		}
		ql=strlen(q);
		while (folders[++i])
		{
			if (memcmp(folders[i], q, ql) ||
				folders[i][ql] != '.' ||
				strchr(folders[i]+ql+1, '.'))	break;
		}
		--i;
		free(q);
	}
	maildir_freefolders(&folders);
	printf("</select>\n");
}

void folder_download(const char *folder, size_t pos, const char *mimeid)
{
	char	*filename;
	FILE *fp=NULL;
	int	fd;

	filename=get_msgfilename(folder, &pos);

	fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	if (fd >= 0)
	{
		if ((fp=fdopen(fd, "r")) == 0)
			close(fd);
	}

	if (!fp)
	{
		free(filename);
		error("Message not found.");
		return;
	}
	free(filename);

	cginocache();
	msg2html_download(fp, mimeid, *cgi("download") == '1',
			  sqwebmail_content_charset);

	fclose(fp);
}

void folder_showtransfer()
{
	const char	*deletelab, *purgelab, *movelab, *golab;

	deletelab=getarg("DELETE");
	purgelab=getarg("PURGE");
	movelab=getarg("ORMOVETO");
	golab=getarg("GO");
	folder_inbox=getarg("INBOX");
	folder_drafts=getarg("DRAFTS");
	folder_trash=getarg("TRASH");
	folder_sent=getarg("SENT");

	printf("<input type=\"hidden\" name=\"pos\" value=\"%s\" />", cgi("pos"));

	if (*cgi("search"))
	{
		printf("<input type=\"hidden\" name=\"search\" value=\"1\" />"
		       "<input type=\"hidden\" name=\"" SEARCHRESFILENAME
		       "\" value=\"");
		output_attrencoded(cgi(SEARCHRESFILENAME));
		printf("\" />");
	}

	if ((strcmp(sqwebmail_folder, INBOX "." TRASH) == 0) && (strlen(getarg("PURGEALL"))))
	    printf("<input type=\"submit\" name=\"cmdpurgeall\" value=\"%s\" onclick=\"javascript: return deleteAll();\" />",
		getarg("PURGEALL"));
	printf("<input type=\"submit\" name=\"cmddel\" value=\"%s\" />%s<select name=\"moveto\">",
		strcmp(sqwebmail_folder, INBOX "." TRASH) == 0
		? purgelab:deletelab,
		movelab);

	show_transfer_dest(sqwebmail_folder);
	printf("</select><input type=\"submit\" name=\"cmdmove\" value=\"%s\" />\n",
		golab);
}

void folder_showquota()
{
	const char	*quotamsg;
	struct maildirsize quotainfo;

	quotamsg=getarg("QUOTAUSAGE");

	if (maildir_openquotafile(&quotainfo, "."))
		return;

	if (quotainfo.quota.nmessages != 0 ||
	    quotainfo.quota.nbytes != 0)
		printf(quotamsg, maildir_readquota(&quotainfo));

	maildir_closequotafile(&quotainfo);
}

void
folder_cleanup()
{
	msg_purgelab=0;
	msg_folderlab=0;
	folder_drafts=0;
	folder_inbox=0;
	folder_sent=0;
	folder_trash=0;
	msg_forwardattlab=0;
	msg_forwardlab=0;
	msg_fullheaderlab=0;
	msg_golab=0;
	msg_movetolab=0;
	msg_nextlab=0;
	msg_prevlab=0;
	msg_deletelab=0;
	msg_posfile=0;
	msg_replyalllab=0;
	msg_replylistlab=0;
	msg_replylab=0;
	folder_err_msg=0;
	msg_msglab=0;
	msg_add=0;

	msg_type=0;
	initnextprevcnt=0;
	msg_hasprev=0;
	msg_hasnext=0;
	msg_pos=0;
	msg_count=0;
}


/*
** Unicode-aware truncation of text at a specified column, if text length
** exceeds the given # of characters.
*/

static char *truncate_at(const char *str,
			 const char *charset,
			 size_t ncols)
{
	char32_t *uc;
	size_t n;
	size_t cols, tp=0;
	char *retbuf;
	unicode_convert_handle_t h;
	int chopped=0;

	if (!str)
		return NULL;

	h=unicode_convert_tou_init("utf-8", &uc, &n, 1);

	if (h)
	{
		unicode_convert(h, str, strlen(str));
		unicode_convert_deinit(h, NULL);
	}
	else
	{
		uc=NULL;
	}

	if (!uc)
		return NULL;

	for (cols=0, n=0; uc[n]; n++) {

		cols += unicode_wcwidth(uc[n]);

		tp = n;
		if (cols > ncols-3 && n > 0 &&
		    unicode_grapheme_break(uc[n-1], uc[n]))
		{
			chopped=1;
			break;
		}
		++tp;
	}

	if (chopped)
	{
		uc = realloc(uc, sizeof(char32_t) * (tp+4));
		if (uc == 0) enomem();
		uc[tp]='.';
		uc[tp+1]='.';
		uc[tp+2]='.';
		tp += 3;
	}

	h=unicode_convert_fromu_init(charset, &retbuf, &cols, 1);

	if (h)
	{
		unicode_convert_uc(h, uc, tp);
		unicode_convert_deinit(h, NULL);
	}
	else
	{
		retbuf=NULL;
	}

	free(uc);
	return retbuf;
}
