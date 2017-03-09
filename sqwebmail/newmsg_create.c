/*
** Copyright 1998 - 2009 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	"cgi/cgi.h"
#include	"sqconfig.h"
#include	"sqwebmail.h"
#include	"auth.h"
#include	"maildir.h"
#include	"newmsg.h"
#include	"folder.h"
#include	"filter.h"
#include	"pref.h"
#include	"gpg.h"
#include	"addressbook.h"
#include	"maildir/maildirmisc.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"
#include	"rfc822/rfc2047.h"
#include	"rfc822/encode.h"
#include	"rfc822/rfc822hdr.h"
#include	"msg2html.h"
#include	"gpglib/gpglib.h"
#include	"http11/http11.h"
#include	"htmllibdir.h"
#include	<courier-unicode.h>
#include	"courierauth.h"

#include	<stdlib.h>
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<ctype.h>
#include	<fcntl.h>

#define HASTEXTPLAIN(q) (rfc2045_searchcontenttype((q), "text/plain") != NULL)
/* Also in attachments.c */

extern const char *rfc822_mkdt(time_t);

extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_content_language;

int newdraftfd;
extern const char *sqwebmail_mailboxid;

const char mimemsg[]="This is a MIME-formatted message.  If you see this text it means that your\nmail software cannot handle MIME-formatted messages.\n\n";

char *newmsg_createdraft_do(const char *, const char *, int);

/* Save message in a draft file */

char *newmsg_createdraft(const char *curdraft)
{
	if (curdraft && *curdraft)
	{
	char	*base=maildir_basename(curdraft);
	char	*filename=maildir_find(INBOX "." DRAFTS, base);

		if (filename)
		{
		char	*p=newmsg_createdraft_do(filename, cgi("message"), 0);

			free(filename);
			free(base);
			return (p);
		}
		free(base);
	}
	return (newmsg_createdraft_do(0, cgi("message"), 0));
}

static void create_draftheader_do(const char *hdrname, const char *p,
	int isrfc822addr);

static void create_draftheader(const char *hdrname, const char *p,
			       const char *q, int isrfc822addr)
{
	if (q && *q)	/* Add from address book */
	{
	char	*nick=cgi_multiple("nick", ",");
	char	*s;

		if (nick)
		{
			s=malloc(strlen(p)+strlen(nick)+2);

			if (s)
			{
				strcpy(s, p);
				if (*s && *nick)	strcat(s, ",");
				strcat(s, nick);
				create_draftheader_do(hdrname, s, isrfc822addr);
				free(s);
				free(nick);
				return;
			}
			free(nick);
		}

	}
	create_draftheader_do(hdrname, p, isrfc822addr);
}

#define	ISLWS(c)	((c)=='\t' || (c)=='\r' || (c)=='\n' || (c)==' ')

static void header_wrap(const char *name, const char *hdr,
			char *outbuf, size_t *outcnt)
{
char	*pfix;
size_t	offset=strlen(name);

	*outcnt=0;
	pfix="";

	while (*hdr)
	{
	size_t i;
	size_t spc;

		for (spc=0, i=0; hdr[i]; i++)
		{
			if (i + offset >= 75 && spc)
			{
			        i = spc;
				offset = 0;
				break;
			}

			if (ISLWS(hdr[i]))
			{
				spc = i;
				while (ISLWS(hdr[i+1]))
					++i;
			}
		}

		if (outbuf)
		{
			strcpy(outbuf, pfix);
			outbuf += strlen(pfix);
		}
		*outcnt += strlen(pfix);

		if (outbuf)
		{
		size_t j;
			for (j=0; j < i; j++)
			{
				if (ISLWS(hdr[j]))
				{
					*(outbuf++) = ' ';
					while (ISLWS(hdr[j+1]))
						++j;
				}
				else
					*(outbuf++) = hdr[j];
			}
		}
		*outcnt += i;
		pfix="\n  ";
		hdr += i;
		while (ISLWS(*hdr))
			++hdr;
	}
	if (outbuf)
		*outbuf=0;
	++*outcnt;
}

static void create_draftheader_do(const char *hdrname, const char *p,
	int isrfc822addr)
{
char	*s, *t;
size_t	l;

	if (!*p)	return;

	if (!isrfc822addr)
	{
		s=rfc2047_encode_str(p, sqwebmail_content_charset,
				     rfc2047_qp_allow_any);
	}
	else
	{
		s=rfc2047_encode_header_tobuf("to", p,
					      sqwebmail_content_charset);
	}

	header_wrap(hdrname, s, NULL, &l);
	if (l)
	{
		if (!(t=malloc(l))) enomem();
		header_wrap(hdrname, s, t, &l);
		if (*t)
		{
			free(s);
			s = t;
		}
		else
			free(t);
	}

	if (!s)
	{
		close(newdraftfd);
		enomem();
	}
	maildir_writemsgstr(newdraftfd, hdrname);
	maildir_writemsgstr(newdraftfd, s);
	maildir_writemsgstr(newdraftfd, "\n");
	free(s);
}

void newmsg_create_multipart(int newdraftfd, const char *charset,
			const char *multipart_boundary)
{
	maildir_writemsgstr(newdraftfd,
		"Mime-version: 1.0\n"
		"Content-Type: multipart/mixed; boundary=\"");
	maildir_writemsgstr(newdraftfd, multipart_boundary);
	maildir_writemsgstr(newdraftfd, "\"; charset=\"");
	maildir_writemsgstr(newdraftfd, charset);
	maildir_writemsgstr(newdraftfd,
					"\"\n\n");

	maildir_writemsgstr(newdraftfd, mimemsg);
}


static char	*newmsg_multipart_boundary(FILE *, const char *);
static void newmsg_copy_attachments(struct rfc2045 *, FILE *, const char *);

void newmsg_copy_nonmime_headers(FILE *fp)
{
char	*header, *value;
char	*q;

	while ((header=maildir_readheader(fp, &value, 1)) != NULL)
	{
		if (strcmp(header, "mime-version") == 0 ||
			strncmp(header, "content-", 8) == 0)	continue;

		/* Fluff - capitalize header names */

		for (q=header; *q; q++)
		{
			for (*q=toupper(*q); *q; q++)
				if (*q == '-')	break;
			if (!*q)
				break;
		}

		maildir_writemsgstr(newdraftfd, header);
		maildir_writemsgstr(newdraftfd, ": ");
		maildir_writemsgstr(newdraftfd, value);
		maildir_writemsgstr(newdraftfd, "\n");
	}
}

void newmsg_copy_content_headers(FILE *fp)
{
char	*header, *value;
char	*q;

	while ((header=maildir_readheader(fp, &value, 1)) != NULL)
	{
		if (strncmp(header, "content-", 8)) continue;

		for (q=header; *q; q++)
		{
			for (*q=toupper(*q); *q; q++)
				if (*q == '-')	break;
			if (!*q)
				break;
		}

		maildir_writemsgstr(newdraftfd, header);
		maildir_writemsgstr(newdraftfd, ": ");
		maildir_writemsgstr(newdraftfd, value);
		maildir_writemsgstr(newdraftfd, "\n");
	}
}

void wrap_text_init(struct wrap_info *uw,
		    const char *output_chset,
		    void (*output_func)(const char *p, size_t l, void *arg),
		    void *arg)
{
	memset(uw, 0, sizeof(*uw));
	uw->output_func=output_func;
	uw->output_chset=output_chset;
	uw->arg=arg;
}

static void do_save_u_line(struct wrap_info *uw,
			   const char32_t *uc,
			   size_t ucsize,
			   int flowed)
{
	char *cbuf;
	size_t csize;

	unicode_convert_handle_t h=
		unicode_convert_fromu_init(uw->output_chset,
					     &cbuf,
					     &csize,
					     0);

	if (h)
	{
		if (ucsize)
		{
			if (uc[0] == ' ')
				unicode_convert_uc(h, uc, 1);
			/* Space stuff */

			unicode_convert_uc(h, uc, ucsize);
		}
		if (flowed)
		{
			char32_t spc=' ';
			unicode_convert_uc(h, &spc, 1);
		}

		{
			char32_t nl='\n';
			unicode_convert_uc(h, &nl, 1);
		}

		if (unicode_convert_deinit(h, NULL))
			cbuf=NULL;
	}
	else
		cbuf=NULL;

	if (cbuf)
	{
		(*uw->output_func)(cbuf, csize, uw->arg);
		free(cbuf);
	}
}

static void flush_line(struct wrap_info *uw, int flowed)
{
	do_save_u_line(uw, uw->uc + uw->line_start,
		       uw->word_start - uw->line_start, flowed);

	uw->line_start=uw->word_start;
	uw->line_width=0;
}

static void add_word(struct wrap_info *uw)
{
	if (uw->line_start < uw->word_start &&
	    uw->line_width + uw->word_width > MYLINESIZE)
		flush_line(uw, 1);

	uw->line_width += uw->word_width;

	uw->word_start=uw->cur_index;
	uw->word_width=0;
}

static int do_save_u_process_lb(int type, void *arg)
{
	struct wrap_info *uw=(struct wrap_info *)arg;

	if (uw->cur_index >= uw->ucsize)
		enomem();

	if (type != UNICODE_LB_NONE)
	{
		add_word(uw);
		if (type == UNICODE_LB_MANDATORY)
			flush_line(uw, 0);
	}


	if (uw->word_width >= MYLINESIZE &&
	    uw->cur_index > 0 &&
	    unicode_grapheme_break(uw->uc[uw->cur_index-1],
				   uw->uc[uw->cur_index]))
		add_word(uw);

	uw->word_width += unicode_wcwidth(uw->uc[uw->cur_index]);
	++uw->cur_index;
	return 0;
}

static void do_wrap_u_line(struct wrap_info *uw,
			   const char32_t *uc,
			   size_t ucsize)
{
	unicode_lb_info_t lb;

	while (ucsize && uc[ucsize-1] == ' ')
		--ucsize;

	uw->uc=uc;
	uw->ucsize=ucsize;
	uw->cur_index=0;
	uw->word_start=0;
	uw->word_width=0;

	uw->line_start=0;
	uw->line_width=0;
	if ((lb=unicode_lb_init(do_save_u_process_lb, uw)) != NULL)
	{
		unicode_lb_set_opts(lb,
				    UNICODE_LB_OPT_PRBREAK
				    | UNICODE_LB_OPT_SYBREAK);
		unicode_lb_next_cnt(lb, uc, ucsize);
		unicode_lb_end(lb);
		add_word(uw);
		flush_line(uw, 0);
	}
}

static void save_textplain(const char *p, size_t l, void *dummy)
{
	maildir_writemsg(newdraftfd, p, l);
}

void wrap_text(struct wrap_info *uw,
	       const char *newmsg,
	       size_t newmsg_size)
{
	size_t i=0, j;

	while (i < newmsg_size)
	{
		char32_t *uc;
		size_t ucsize;
		unicode_convert_handle_t h;

		j=i;

		while (i<newmsg_size && newmsg[i] != '\n')
			++i;

		h=unicode_convert_tou_init(sqwebmail_content_charset,
					     &uc, &ucsize, 0);

		if (h)
		{
			unicode_convert(h, newmsg+j, i-j);

			if (unicode_convert_deinit(h, NULL))
				uc=NULL;
		}
		else
		{
			uc=NULL;
		}

		if (uc)
		{
			size_t i, j;

			/* Get rid of any CRs that sneak in */

			for (i=j=0; i<ucsize; ++i)
			{
				if (uc[i] == '\r')
					continue;

				uc[j]=uc[i];
				++j;
			}

			if (j && *uc == '>')
				do_save_u_line(uw, uc, j, 0);
			else
				do_wrap_u_line(uw, uc, j);

			free(uc);
		}

		if (i < newmsg_size)
			++i;
	}
}

static void convert_text2html(const char *p, size_t l, void *arg)
{
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;

	msg2html_textplain(info, p, l);
}

static char *mkurl(const char *url, void *dummy)
{
	char *buf=malloc(strlen(url)*2+100);

	if (!buf)
		return NULL;

	/* msg2html guarantees that the characters in url are "safe" */

	sprintf(buf, "<a href=\"%s\">%s</a>", url, url);
	return buf;
}

char *newmsg_createdraft_do(const char *curdraft, const char *newmsg,
	int keepheader)
{
char	*draftfilename;
FILE	*fp=0;
char	*multipart_boundary;
const char *content_type;
const char *content_transfer_encoding;
const char *charset;
unsigned long prev_size=0;
off_t	transferencodingpos;
off_t	transferencoding2pos;
int is_newevent=strcmp(cgi("form"), "newevent") == 0;
struct	rfc2045	*rfcp;
int has_attachments=0;
size_t newmsg_size;
char *sig, *footer;

/*
** Trim extra newlines.
*/
	newmsg_size=strlen(newmsg);

	while (newmsg_size && newmsg[newmsg_size-1] == '\n')
		--newmsg_size;

/* We're on the 'new event' screen */

 	if (curdraft)	/* Reuse a draft filename */
		newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, curdraft, &draftfilename);
	else
		newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, &draftfilename);
	if (newdraftfd < 0)	enomem();

	pref_wikifmt=0;
	if (strcmp(cgi("textformat"), "wiki") == 0)
		pref_wikifmt=1;
	pref_update();

	fp=NULL;
	if (curdraft)
	{
	int	x=maildir_safeopen(curdraft, O_RDONLY, 0);

		if (x >= 0)
			if ((fp=fdopen(x, "r")) == 0)
				close(x);
	}

	if (fp)
	{
	char	*header, *value;
	struct	stat	stat_buf;

		if (fstat(fileno(fp), &stat_buf))
		{
			fclose(fp);
			enomem();
		}
		prev_size=stat_buf.st_size;

		while ((header=maildir_readheader(fp, &value, 1)) != NULL)
		{
			if (keepheader == NEWMSG_SQISPELL)
			{
				if (strcasecmp(header, "mime-version") == 0 ||
				    strncasecmp(header, "content-", 8) == 0)
					continue;
			}
			else if (keepheader == NEWMSG_PCP)
			{
				if (strcasecmp(header, "mime-version") == 0 ||
				    strncasecmp(header, "content-", 8) == 0 ||
				    strcasecmp(header, "date") == 0 ||
				    strcasecmp(header, "from") == 0 ||
				    strcasecmp(header, "subject") == 0)
					continue;
			}
			else
			{
				if (strcmp(header, "in-reply-to") &&
					strcmp(header, "references") &&
					strncmp(header, "x-", 2))	continue;
				/* Do not discard these headers */
			}

			if (strcasecmp(header, "x-sqwebmail-wikifmt") == 0)
				continue;

			maildir_writemsgstr(newdraftfd, header);
			maildir_writemsgstr(newdraftfd, ": ");
			maildir_writemsgstr(newdraftfd, value);
			maildir_writemsgstr(newdraftfd, "\n");
		}
	}
	else if (is_newevent)
		maildir_writemsgstr(newdraftfd, "X-Event: 1\n");

	if (!keepheader
	    || keepheader == NEWMSG_PCP)
	/* Coming back from msg edit, set headers */
	{
	const	char *p=cgi("headerfrom");

		if (!*p)	p=pref_from;
		if (!p || !*p || auth_getoptionenvint("wbnochangingfrom"))
			p=login_fromhdr();

		create_draftheader("From: ", p, 0, 1);

		if (!pref_from || strcmp(p, pref_from))
			pref_setfrom(p);

/* sam ????
	create_draftheader("In-Reply-To: ", cgi("headerin-reply-to"));
*/
		if (!is_newevent)
		{
#if 0
			{
				FILE *fp;
				fp=fopen("/tmp/pid", "w");
				fprintf(fp, "%d", getpid());
				fclose(fp);
				sleep(10);
			}
#endif

			create_draftheader("To: ", cgi("headerto"),
					   cgi("addressbook_to"), 1);
			create_draftheader("Cc: ", cgi("headercc"),
					   cgi("addressbook_cc"), 1);
			create_draftheader("Bcc: ", cgi("headerbcc"),
					   cgi("addressbook_bcc"), 1);
			create_draftheader("Reply-To: ", cgi("headerreply-to"), 0, 1);
		}
	}

	if (pref_wikifmt)
		create_draftheader("x-sqwebmail-wikifmt: ", "1", 0, 0);

	if (!keepheader || keepheader == NEWMSG_PCP)
	{
	time_t	t;

		create_draftheader("Subject: ", cgi("headersubject"), 0, 0);

		time(&t);
		create_draftheader("Date: ", rfc822_mkdate(t), 0, 0);
	}

	/* If the message has attachments, calculate multipart boundary */

	rfcp=NULL;

	if (fp)
	{
		rfcp=rfc2045_fromfp(fp);
		if (!rfcp)
		{
			close(newdraftfd);
			fclose(fp);
			enomem();
		}
	}

	multipart_boundary=newmsg_multipart_boundary(fp, newmsg);

	if (rfcp && rfcp->firstpart &&
	    strcmp((rfc2045_mimeinfo(rfcp, &content_type,
			      &content_transfer_encoding, &charset),
		    content_type), "multipart/mixed") == 0)
	{
		has_attachments=1;
		newmsg_create_multipart(newdraftfd,
			sqwebmail_content_charset, multipart_boundary);

		maildir_writemsgstr(newdraftfd, "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd,"\n");
	}
	else
	{
		maildir_writemsgstr(newdraftfd, "Mime-Version: 1.0\n");
	}

	if (pref_wikifmt)
	{
		++multipart_boundary[strlen(multipart_boundary)-1];

		maildir_writemsgstr(newdraftfd,
				    "Content-Type: multipart/alternative;"
				    " boundary=\"");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "\"\n"
				    "\n"
				    "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "\n");
	}

	maildir_writemsgstr(newdraftfd,
			    "Content-Type: text/plain; format=flowed; delsp=yes;"
			    " charset=\"");
	maildir_writemsgstr(newdraftfd, sqwebmail_content_charset);
	maildir_writemsgstr(newdraftfd, "\"\n");

	maildir_writemsgstr(newdraftfd, "Content-Transfer-Encoding: ");
	transferencoding2pos=transferencodingpos=writebufpos;
	maildir_writemsgstr(newdraftfd, "7bit\n\n");

	/*	maildir_writemsgstr(newdraftfd, "\n"); */

	sig=pref_getsig();
	footer=pref_getfile(http11_open_langfile(get_templatedir(),
						 sqwebmail_content_language,
						 "footer"));

	while (newmsg_size &&
	       (newmsg[newmsg_size-1] == '\r' ||
		newmsg[newmsg_size-1] == '\n'))
		--newmsg_size;

	{
		struct wrap_info uw;

		wrap_text_init(&uw, sqwebmail_content_charset,
			       save_textplain, NULL);

		wrap_text(&uw, newmsg, newmsg_size);

		if ((sig && *sig) || (footer && *footer))
		{
			static const char32_t sig_line[]={'-', '-', ' '};

			do_save_u_line(&uw, sig_line, 0, 0);
			do_save_u_line(&uw, sig_line, 3, 0);
		}

		if (sig && *sig)
			wrap_text(&uw, sig, strlen(sig));

		if (footer && *footer)
		{
			do_save_u_line(&uw, NULL, 0, 0);
			maildir_writemsg(newdraftfd, footer, strlen(footer));
		}

	}

	if (pref_wikifmt)
	{
		struct msg2html_textplain_info *info;

		maildir_writemsgstr(newdraftfd, "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "\n"
				    "Content-Type: text/html; charset=\"");
		maildir_writemsgstr(newdraftfd, sqwebmail_content_charset);
		maildir_writemsgstr(newdraftfd, "\"\n"
				    "Content-Transfer-Encoding: ");
		transferencoding2pos=writebufpos;
		maildir_writemsgstr(newdraftfd, "7bit\n\n");

		info=msg2html_textplain_start(sqwebmail_content_charset,
					      sqwebmail_content_charset,
					      1,
					      1,
					      0,
					      mkurl, NULL,
					      NULL,
					      NULL,
					      1,
					      save_textplain,
					      NULL);

		if (info)
		{
			struct wrap_info uw;

			wrap_text_init(&uw, sqwebmail_content_charset,
				       convert_text2html, info);

			wrap_text(&uw, newmsg, newmsg_size);
			msg2html_textplain_end(info);
		}

		if ((sig && *sig) || (footer && *footer))
			save_textplain("<hr />\n", 7, NULL);

		if (sig && *sig)
		{

			info=msg2html_textplain_start(sqwebmail_content_charset,
						      sqwebmail_content_charset,
						      1,
						      1,
						      0,
						      mkurl, NULL,
						      NULL,
						      NULL,
						      1,
						      save_textplain,
						      NULL);

			if (info)
			{
				struct wrap_info uw;

				wrap_text_init(&uw, sqwebmail_content_charset,
					       convert_text2html, info);

				wrap_text(&uw, sig, strlen(sig));
				msg2html_textplain_end(info);
			}
		}

		if (footer && *footer)
		{
			save_textplain("<br />\n", 7, NULL);

			info=msg2html_textplain_start(sqwebmail_content_charset,
						      sqwebmail_content_charset,
						      1,
						      1,
						      0,
						      mkurl, NULL,
						      NULL,
						      NULL,
						      1,
						      save_textplain,
						      NULL);

			if (info)
			{
				msg2html_textplain(info, footer,
						   strlen(footer));
				msg2html_textplain_end(info);
			}
		}

		maildir_writemsgstr(newdraftfd, "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "--\n");
		--multipart_boundary[strlen(multipart_boundary)-1];

	}

	if (sig)
		free(sig);

	if (footer)
		free(footer);

	if ( multipart_boundary && rfcp && has_attachments)
	{
		newmsg_copy_attachments(rfcp, fp, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "--\n");
		free(multipart_boundary);
	}
	if (fp)	fclose(fp);
	if (rfcp)
		rfc2045_free(rfcp);

	if ( maildir_writemsg_flush(newdraftfd) == 0 && writebuf8bit)
	{
		if (lseek(newdraftfd, transferencodingpos, SEEK_SET) < 0 ||
			write(newdraftfd, "8", 1) != 1 ||
		    lseek(newdraftfd, transferencoding2pos, SEEK_SET) < 0 ||
			write(newdraftfd, "8", 1) != 1)
		{
			close(newdraftfd);
			enomem();
		}
	}

	if ( maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, -1, prev_size))
		cgi_put("error", "quota");

	return(draftfilename);
}

static void sentmsg_copy(FILE *f, struct rfc2045 *p)
{
	off_t start_pos, end_pos, start_body;
	char buf[BUFSIZ];
	int n;
	off_t   dummy;

        rfc2045_mimepos(p, &start_pos, &end_pos, &start_body, &dummy, &dummy);
        if (fseek(f, start_pos, SEEK_SET) == -1)
        {
                fclose(f);
                close(newdraftfd);
                enomem();
        }

        while (start_pos < end_pos)
        {
        int     cnt=sizeof(buf);

                if (cnt > end_pos - start_pos)
                        cnt=end_pos - start_pos;

                if ((n=fread(buf, 1, cnt, f)) <= 0)
                {
                        fclose(f);
                        close(newdraftfd);
                        enomem();
                }

                maildir_writemsg(newdraftfd, buf, n);
                start_pos += n;
        }
}


/* Create message in the sent folder */

static void header_uc(char *h)
{
	while (*h)
	{
		*h=toupper( (int)(unsigned char) *h);
		while (*h)
		{
			if (*h++ == '-')	break;
		}
	}
}

struct lookup_buffers {
	struct lookup_buffers *next;
	char *buf;
	char *buf2;
	} ;

static int lookup_addressbook_do(const char *header, const char *value,
	struct lookup_buffers **lookup_buffer_list)
{
	struct	rfc822t *t;
	struct	rfc822a *a;
	int	i;
	char	*newbuf;
	struct lookup_buffers *ptr;
	int	expanded=0;

	t=rfc822t_alloc_new(value, NULL, NULL);
	if (!t)	enomem();
	a=rfc822a_alloc(t);
	if (!a)
	{
		rfc822t_free(t);
		enomem();
	}

	for (i=0; i<a->naddrs; i++)
	{
		char	*p;
		char	*s;
		const	char *q;
		struct lookup_buffers *r;

		if (a->addrs[i].tokens == 0)
			continue;
		if (a->addrs[i].name)
			continue;	/* Can't be a nickname */

		p=rfc822_getaddr(a, i);
		if (!p)
		{
			rfc822a_free(a);
			rfc822t_free(t);
			free(p);
			return (-1);
		}

		for (ptr= *lookup_buffer_list; ptr; ptr=ptr->next)
			if (strcmp(ptr->buf2, p) == 0)
				break;

		if (ptr)	/* Address book loop */
		{
		int	j;

			for (j=i+1; j<a->naddrs; j++)
				a->addrs[j-1]=a->addrs[j];
			--a->naddrs;
			--i;
			free(p);
			continue;
		}

		s=rfc822_display_addr_str_tobuf(p, "utf-8");

		if (s == NULL || (q=ab_find(s)) == 0)
		{
			if (s)
				free(s);
			free(p);
			continue;
		}
		free(s);

		r=malloc(sizeof(struct lookup_buffers));
		if (r)	r->buf=r->buf2=0;

		if (!r || !(r->buf=strdup(q)) || !(r->buf2=strdup(p)))
		{
			free(p);
			if (r && r->buf)	free(r->buf);
			if (r)	free(r);
			rfc822a_free(a);
			rfc822t_free(t);
			return (-1);
		}
		free(p);
		r->next= *lookup_buffer_list;
		*lookup_buffer_list=r;
		a->addrs[i].tokens->next=0;
		a->addrs[i].tokens->token=0;
		a->addrs[i].tokens->ptr=r->buf;
		a->addrs[i].tokens->len=strlen(r->buf);
		expanded=1;
	}

	newbuf=rfc822_getaddrs_wrap(a, 70);
	rfc822a_free(a);
	rfc822t_free(t);
	if (!newbuf)	return (-1);

	if (expanded)	/* Look through the address book again */
	{
	int	rc=lookup_addressbook_do(header, newbuf, lookup_buffer_list);

		free(newbuf);
		return (rc);
	}

	create_draftheader_do(header, newbuf, 1);
	free(newbuf);
	return (0);
}

static void lookup_addressbook(const char *header, const char *value)
{
	struct lookup_buffers *lookup_buffer_list=0;
	int	rc;
	char *header_cpy;
	char *value_cpy;
	/*
	** header & value may be pointing to buffer allocated by
	** maildir_readheader.
	** lookup_addressbook_do may call it again.
	*/

	header_cpy=strdup(header);
	if (!header_cpy)
		enomem();

	value_cpy=strdup(value);
	if (!value_cpy)
	{
		free(header_cpy);
		enomem();
	}

	rc=lookup_addressbook_do(header_cpy, value_cpy, &lookup_buffer_list);
	free(header_cpy);

	while (lookup_buffer_list)
	{
	struct lookup_buffers *p=lookup_buffer_list;

		lookup_buffer_list=p->next;
		free(p->buf);
		free(p->buf2);
		free(p);
	}
	if (rc)	enomem();
}

char *newmsg_createsentmsg(const char *draftname, int *isgpgerr)
{
char	*filename=maildir_find(INBOX "." DRAFTS, draftname);
FILE	*fp;
char	*sentname;
char	*header, *value;
struct	rfc2045 *rfcp;
int	x;

	*isgpgerr=0;

	if (!filename)	return (0);

	fp=0;

	x=maildir_safeopen(filename, O_RDONLY, 0);
	if (x >= 0)
		if ((fp=fdopen(x, "r")) == 0)
			close(x);

	if (fp == 0)
	{
		free(filename);
		enomem();
	}

	rfcp=rfc2045_fromfp(fp);
	if (!rfcp || fseek(fp, 0L, SEEK_SET) < 0)
	{
		fclose(fp);
		close(newdraftfd);
		enomem();
	}

	newdraftfd=maildir_createmsg(INBOX "." SENT, 0, &sentname);
	if (newdraftfd < 0)
	{
		rfc2045_free(rfcp);
		free(filename);
		fclose(fp);
		enomem();
	}
	/* First, copy all headers except X- headers */

	while ((header=maildir_readheader(fp, &value, 1)) != 0)
	{
		if (strncmp(header, "x-", 2) == 0)	continue;
		header_uc(header);
		if (rfc822hdr_namecmp(header, "To") == 0)
		{
			lookup_addressbook("To: ", value);
			continue;
		}

		if (rfc822hdr_namecmp(header, "Cc") == 0)
		{
			lookup_addressbook("Cc: ", value);
			continue;
		}

		if (rfc822hdr_namecmp(header, "Bcc") == 0)
		{
			lookup_addressbook("Bcc: ", value);
			continue;
		}

		maildir_writemsgstr(newdraftfd, header);
		maildir_writemsgstr(newdraftfd, ": ");
		maildir_writemsgstr(newdraftfd, value);
		maildir_writemsgstr(newdraftfd, "\n");
	}
	if (auth_getoptionenvint("wbusexsender"))
	{
		maildir_writemsgstr(newdraftfd, "X-Sender: ");
		maildir_writemsgstr(newdraftfd, login_returnaddr());
		maildir_writemsgstr(newdraftfd, "\n");
	}

	maildir_writemsgstr(newdraftfd, "\n");

	{
		off_t start_pos, end_pos, start_body;
		char buf[BUFSIZ];
		int n;
		off_t   dummy;

		rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body,
				&dummy, &dummy);

		if (fseek(fp, start_body, SEEK_SET) == -1)
		{
			fclose(fp);
			close(newdraftfd);
			enomem();
		}

		while (start_body < end_pos)
		{
			int     cnt=sizeof(buf);

			if (cnt > end_pos - start_pos)
				cnt=end_pos - start_pos;

			if ((n=fread(buf, 1, cnt, fp)) <= 0)
			{
				fclose(fp);
				close(newdraftfd);
				enomem();
			}

			maildir_writemsg(newdraftfd, buf, n);
			start_body += n;
		}
	}


	if ( maildir_writemsg_flush(newdraftfd))
	{
		free(sentname);
		return (0);
	}

#if 0
	if (writebuf8bit)
	{
		if (lseek(newdraftfd, transferencodingpos, SEEK_SET) < 0 ||
			write(newdraftfd, "8", 1) != 1)
		{
			free(sentname);
			return (0);
		}
	}
#endif

	if ( maildir_writemsg_flush(newdraftfd))
	{
		maildir_closemsg(newdraftfd, INBOX "." SENT, sentname, 0, 0);
		free(sentname);
		return (0);
	}

	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		char dosign= *cgi("sign");
		char doencrypt= *cgi("encrypt");
		const char *signkey= cgi("signkey");
		char *encryptkeys=cgi_multiple("encryptkey", " ");

		if (!encryptkeys)
			enomem();

		if (gpgbadarg(encryptkeys) || !*encryptkeys)
		{
			free(encryptkeys);
			encryptkeys=0;
		}

		if (gpgbadarg(signkey) || !*signkey)
		{
			signkey=0;
		}

		if (!encryptkeys)
			doencrypt=0;

		if (!signkey)
			dosign=0;

		if (lseek(newdraftfd, 0L, SEEK_SET) < 0)
		{
			maildir_closemsg(newdraftfd, INBOX "." SENT,
					 sentname, 0, 0);
			free(sentname);
			return (0);
		}

		if (!dosign)
			signkey=0;
		if (!doencrypt)
			encryptkeys=0;

		if (dosign || doencrypt)
		{
			/*
			** What we do is create another draft, then substitute
			** it for newdraftfd/sentname.  Sneaky.
			*/

			char *newnewsentname;
			int newnewdraftfd=maildir_createmsg(INBOX "." SENT, 0,
							    &newnewsentname);

			if (newnewdraftfd < 0)
			{
				maildir_closemsg(newdraftfd, INBOX "." SENT,
						 sentname, 0, 0);
				free(sentname);
				free(encryptkeys);
				return (0);
			}

			if (gpgdomsg(newdraftfd, newnewdraftfd,
				     signkey, encryptkeys))
			{
				maildir_closemsg(newnewdraftfd, INBOX "." SENT,
						 newnewsentname, 0, 0);
				free(newnewsentname);
				maildir_closemsg(newdraftfd, INBOX "." SENT,
						 sentname, 0, 0);
				free(sentname);
				free(encryptkeys);
				*isgpgerr=1;
				return (0);
			}

			maildir_closemsg(newdraftfd, INBOX "." SENT, sentname, 0, 0);
			free(sentname);
			sentname=newnewsentname;
			newdraftfd=newnewdraftfd;

		}
		free(encryptkeys);
	}

	if ( maildir_closemsg(newdraftfd, INBOX "." SENT, sentname, 1, 0))
	{
		free(sentname);
		return (0);
	}
	return (sentname);
}

/* ---------------------------------------------------------------------- */

/* Create a potential multipart boundary separator tag */

char *multipart_boundary_create()
{
char	pidbuf[MAXLONGSIZE];
char	timebuf[MAXLONGSIZE];
time_t	t;
char	cntbuf[MAXLONGSIZE];
static unsigned long cnt=0;
char	*p;

	sprintf(pidbuf, "%lu", (unsigned long)getpid());
	time(&t);
	sprintf(timebuf, "%lu", (unsigned long)t);
	sprintf(cntbuf, "%lu", cnt++);
	p=malloc(strlen(pidbuf)+strlen(timebuf) +strlen(cntbuf)+20);
	sprintf(p, "=_%s_%s_%s_000", cntbuf, pidbuf, timebuf);
	return (p);
}

/* Search for the boundary tag in a string buffer - this is the new message
** we're creating.  We should really look for the tag at the beginning of the
** line, however, the text is not yet linewrapped, besides, why make your
** life hard?
*/

int multipart_boundary_checks(const char *boundary, const char *msg)
{
size_t	boundarylen=strlen(boundary);

	while (*msg)
	{
		if (msg[0] == '-' && msg[1] == '-' && msg[2] != '-' &&
			strncasecmp(msg+2, boundary, boundarylen) == 0)
				return (-1);
		++msg;
	}
	return (0);
}

/* Again, just look for it at the beginning of the line -- why make your
** life hard? */

int multipart_boundary_checkf(const char *boundary, FILE *f)
{
size_t	boundarylen=strlen(boundary);
const char *line;

	if (fseek(f, 0L, SEEK_SET) == -1)
	{
		fclose(f);
		close(newdraftfd);
		enomem();
	}

	while ((line=maildir_readline(f)) != 0)
		if (line[0] == '-' && line[1] == '-' &&
			strncasecmp(line+2, boundary, boundarylen) == 0)
				return (-1);
	return (0);
}

/* ---------------------------------------------------------------------- */

/* Copy existing attachments into the new draft message */

/* multipart_boundary - determine if current draft has attachments */

static char	*newmsg_multipart_boundary(FILE *f, const char *msg)
{
	char	*p=0;

	do
	{
		if (p)	free(p);
		p=multipart_boundary_create();
	} while (multipart_boundary_checks(p, msg)
		 || (f && multipart_boundary_checkf(p, f)));
	return (p);
}

static void	newmsg_copy_attachments(struct rfc2045 *rfcp,
					FILE *f, const char *boundary)
{
struct	rfc2045 *p;
int	foundtextplain=0;

	for (p=rfcp->firstpart; p; p=p->next)
	{
		if (p->isdummy)	continue;
		if (!foundtextplain && HASTEXTPLAIN(p))
		{	/* Previous version of this message */

			foundtextplain=1;
			continue;
		}
		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, boundary);
		maildir_writemsgstr(newdraftfd, "\n");
		sentmsg_copy(f, p);	/* Reuse some code */
	}
}
