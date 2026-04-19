/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
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
#include	"maildir/maildirmisc.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/encode.h"
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
#include	<sstream>
#include	<map>

/* Also in attachments.c */

int newdraftfd;
extern const char *sqwebmail_mailboxid;
extern void create_addrheader(std::string_view header,
			      std::string_view content_utf8);

const char mimemsg[]="This is a MIME-formatted message.  If you see this text it means that your\nmail software cannot handle MIME-formatted messages.\n\n";

std::string newmsg_createdraft_do(const char *, const char *, int);

/* Save message in a draft file */

std::string newmsg_createdraft(const char *curdraft)
{
	if (curdraft && *curdraft)
	{
		auto base=maildir_basename(curdraft);
		auto filename=maildir_find(INBOX "." DRAFTS, base.c_str());

		if (!filename.empty())
		{
			return newmsg_createdraft_do(
				filename.c_str(), cgi("message"), 0);
		}
	}
	return newmsg_createdraft_do(0, cgi("message"), 0);
}

static void create_draftheader_do(const char *hdrname, const char *p,
	bool isrfc822addr);

static void create_draftheader(const char *hdrname, const char *p,
			       const char *q, bool isrfc822addr)
{
	if (q && *q)	/* Add from address book */
	{
		auto nick=cgi_multiple("nick");

		size_t l=strlen(p);

		for (auto &n:nick)
			l += 1+n.size();

		std::string s;

		s.reserve(l);

		s += p;

		for (auto &n:nick)
		{
			s += ',';
			s += n;
		}

		create_draftheader_do(
			hdrname,
			s.c_str(),
			isrfc822addr // Better be true...
		);
		return;
	}
	create_draftheader_do(hdrname, p, isrfc822addr);
}

#define	ISLWS(c)	((c)=='\t' || (c)=='\r' || (c)=='\n' || (c)==' ')

static void header_wrap(const char *name, const char *hdr,
			std::string *outbuf, size_t *outcnt)
{
	char	*pfix;
	size_t	offset=strlen(name);

	*outcnt=0;

	static char empty[]="";
	pfix=empty;

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
			outbuf->append(pfix);
		}
		*outcnt += strlen(pfix);

		if (outbuf)
		{
		size_t j;
			for (j=0; j < i; j++)
			{
				if (ISLWS(hdr[j]))
				{
					outbuf->push_back(' ');
					while (ISLWS(hdr[j+1]))
						++j;
				}
				else
					outbuf->push_back(hdr[j]);
			}
		}
		*outcnt += i;
		static char fold[]="\n  ";
		pfix=fold;
		hdr += i;
		while (ISLWS(*hdr))
			++hdr;
	}
}

static void create_draftheader_do(const char *hdrname, const char *p,
	bool isrfc822addr)
{
	size_t	l;

	if (!*p)	return;

	if (isrfc822addr)
	{
		auto p_utf8=unicode::iconvert::convert(
			p,
			sqwebmail_content_charset,
			unicode::utf_8
		);
		create_addrheader(hdrname, p_utf8);
		return;
	}

	auto s=rfc2047::encode(p, sqwebmail_content_charset,
			     rfc2047_qp_allow_any).first;

	header_wrap(hdrname, s.c_str(), NULL, &l);

	std::string s2;
	s2.reserve(l);
	header_wrap(hdrname, s.c_str(), &s2, &l);

	maildir_writemsgstr(newdraftfd, hdrname);
	maildir_writemsgstr(newdraftfd, s2.c_str());
	maildir_writemsgstr(newdraftfd, "\n");
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


static std::string newmsg_multipart_boundary(rfc822::fdstreambuf &,
					     const char *);
static void newmsg_copy_attachments(rfc2045::entity &,
				    rfc822::fdstreambuf &,
				    const std::string &);

void newmsg_copy_nonmime_headers(const rfc2045::entity &message,
				 rfc822::fdstreambuf &fd)
{
	rfc2045::entity::line_iter<false>::headers headers{message, fd};

	headers.keep_eol=true;
	headers.name_lc=false;

	do
	{
		const auto &[name, content] = headers.name_content();

		if (name.empty())
			continue;
		std::string name_lc{name.begin(), name.end()};

		rfc2045::entity::tolowercase(name_lc);

		if (name_lc == "mime-version" ||
		    std::string_view{name_lc}.substr(0, 8) == "content-")
			continue;

		auto full_header=headers.current_header();

		maildir_writemsg(newdraftfd, full_header.data(),
				 full_header.size());
	} while(headers.next());
}

void newmsg_copy_content_headers(const rfc2045::entity &message,
				 rfc822::fdstreambuf &fd)
{
	rfc2045::entity::line_iter<false>::headers headers{message, fd};

	headers.keep_eol=true;
	headers.name_lc=false;

	do
	{
		const auto &[name, content] = headers.name_content();

		std::string name_lc{name.begin(), name.end()};

		rfc2045::entity::tolowercase(name_lc);

		if (std::string_view{name_lc}.substr(0, 8) != "content-")
			continue;

		auto full_header=headers.current_header();

		maildir_writemsg(newdraftfd, full_header.data(),
				 full_header.size());
	} while(headers.next());
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
				    UNICODE_LB_OPT_PRBREAK);
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

		h=unicode_convert_tou_init(sqwebmail_content_charset.c_str(),
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

static std::string mkurl(std::string_view url,
			 std::string_view dispurl)
{
	std::string buf;

	buf.reserve(url.size()+dispurl.size()+30);
	/* msg2html guarantees that the characters in url are "safe" */

	buf="<a href=\"";
	buf += url;
	buf += "\">";
	buf += dispurl;
	buf += "</a>";
	return buf;
}

std::string newmsg_createdraft_do(const char *curdraft, const char *newmsg,
				  int keepheader)
{
	unsigned long prev_size=0;
	off_t	transferencodingpos;
	off_t	transferencoding2pos;
	int is_newevent=strcmp(cgi("form"), "newevent") == 0;
	bool has_attachments=false;
	size_t newmsg_size;

/*
** Trim extra newlines.
*/
	newmsg_size=strlen(newmsg);

	while (newmsg_size && newmsg[newmsg_size-1] == '\n')
		--newmsg_size;

/* We're on the 'new event' screen */

	std::string draftfilename;

	if (curdraft && *curdraft)	/* Reuse a draft filename */
		newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, curdraft, draftfilename);
	else
		newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, draftfilename);
	if (newdraftfd < 0)	enomem();

	pref_wikifmt=0;
	if (strcmp(cgi("textformat"), "wiki") == 0)
		pref_wikifmt=1;
	pref_update();

	rfc822::fdstreambuf fp{curdraft && *curdraft
			       ? maildir_safeopen(curdraft, O_RDONLY, 0)
			       : -1};

	if (!fp.error())
	{
		struct	stat	stat_buf;

		if (fstat(fp.fileno(), &stat_buf))
		{
			enomem();
		}
		prev_size=stat_buf.st_size;

		rfc2045::entity::line_iter<false>::headers headers{fp};

		do
		{
			const auto &[header, value]=headers.name_content();

			if (header.empty())
				continue;

			if (keepheader == NEWMSG_SQISPELL)
			{
				if (header == "mime-version" ||
				    header.substr(0, 8) == "content-")
					continue;
			}
			else if (keepheader == NEWMSG_PCP)
			{
				if (header == "mime-version" ||
				    header.substr(0, 8) == "content-" ||
				    header == "date" ||
				    header == "from" ||
				    header == "subject")
					continue;
			}
			else
			{
				if (header != "in-reply-to" &&
				    header != "references" &&
				    header.substr(0, 2) != "x-")
					continue;

				/* Do not discard these headers */
			}

			if (header == "x-sqwebmail-wikifmt")
				continue;

			maildir_writemsg(newdraftfd, header.data(),
					 header.size());
			maildir_writemsgstr(newdraftfd, ": ");
			maildir_writemsg(newdraftfd, value.data(),
					 value.size());
			maildir_writemsgstr(newdraftfd, "\n");
		} while (headers.next());
		fp.pubseekpos(0);
	}
	else if (is_newevent)
		maildir_writemsgstr(newdraftfd, "X-Event: 1\n");

	if (!keepheader
	    || keepheader == NEWMSG_PCP)
	/* Coming back from msg edit, set headers */
	{
		std::string p=cgi("headerfrom");

		if (p.empty())	p=pref_from;
		if (p.empty() || auth_getoptionenvint("wbnochangingfrom"))
			p=login_fromhdr();

		create_draftheader("From: ", p.c_str(), 0, true);

		if (pref_from.empty() || pref_from != p)
			pref_setfrom(std::move(p));

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
					   cgi("addressbook_to"), true);
			create_draftheader("Cc: ", cgi("headercc"),
					   cgi("addressbook_cc"), true);
			create_draftheader("Bcc: ", cgi("headerbcc"),
					   cgi("addressbook_bcc"), true);
			create_draftheader("Reply-To: ", cgi("headerreply-to"), 0, true);
		}
	}

	if (pref_wikifmt)
		create_draftheader("x-sqwebmail-wikifmt: ", "1", 0, false);

	if (!keepheader || keepheader == NEWMSG_PCP)
	{
	time_t	t;

		create_draftheader("Subject: ", cgi("headersubject"), 0, false);

		time(&t);
		create_draftheader("Date: ", rfc822_mkdate(t), 0, false);
	}

	/* If the message has attachments, calculate multipart boundary */

	rfc2045::entity message;

	if (!fp.error())
	{
		std::istreambuf_iterator<char> b{&fp}, e;
		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	auto multipart_boundary=newmsg_multipart_boundary(fp, newmsg);

	if (!message.subentities.empty() &&
	    message.content_type.value == "multipart/mixed")
	{
		has_attachments=true;
		newmsg_create_multipart(newdraftfd,
			sqwebmail_content_charset.c_str(),
					multipart_boundary.c_str());

		maildir_writemsgstr(newdraftfd, "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd,"\n");
	}
	else
	{
		maildir_writemsgstr(newdraftfd, "Mime-Version: 1.0\n");
	}

	char last_boundary_char=multipart_boundary.back();

	if (pref_wikifmt)
	{
		if (++multipart_boundary.back() > '9') // Cheating
			multipart_boundary.back()='A';

		maildir_writemsgstr(newdraftfd,
				    "Content-Type: multipart/alternative;"
				    " boundary=\"");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd, "\"\n"
				    "\n"
				    "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd, "\n");
	}

	maildir_writemsgstr(newdraftfd,
			    "Content-Type: text/plain; format=flowed; delsp=yes;"
			    " charset=\"");
	maildir_writemsgstr(newdraftfd, sqwebmail_content_charset.c_str());
	maildir_writemsgstr(newdraftfd, "\"\n");

	maildir_writemsgstr(newdraftfd, "Content-Transfer-Encoding: ");
	transferencoding2pos=transferencodingpos=writebufpos;
	maildir_writemsgstr(newdraftfd, "7bit\n\n");

	/*	maildir_writemsgstr(newdraftfd, "\n"); */

	auto sig=pref_getsig();
	auto footer=pref_getfile(
		http11_open_langfile(
			get_templatedir(),
			sqwebmail_content_language.c_str(),
			"footer"
		)
	);

	while (newmsg_size &&
	       (newmsg[newmsg_size-1] == '\r' ||
		newmsg[newmsg_size-1] == '\n'))
		--newmsg_size;

	{
		struct wrap_info uw;

		wrap_text_init(&uw, sqwebmail_content_charset.c_str(),
			       save_textplain, NULL);

		wrap_text(&uw, newmsg, newmsg_size);

		if (sig.size() || footer.size())
		{
			static const char32_t sig_line[]={'-', '-', ' '};

			do_save_u_line(&uw, sig_line, 0, 0);
			do_save_u_line(&uw, sig_line, 3, 0);
		}

		if (sig.size())
			wrap_text(&uw, sig.data(), sig.size());

		if (footer.size())
		{
			do_save_u_line(&uw, NULL, 0, 0);
			maildir_writemsg(newdraftfd, footer.data(), footer.size());
		}

	}

	if (pref_wikifmt)
	{
		struct msg2html_textplain_info *info;

		maildir_writemsgstr(newdraftfd, "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd, "\n"
				    "Content-Type: text/html; charset=\"");
		maildir_writemsgstr(newdraftfd, sqwebmail_content_charset.c_str());
		maildir_writemsgstr(newdraftfd, "\"\n"
				    "Content-Transfer-Encoding: ");
		transferencoding2pos=writebufpos;
		maildir_writemsgstr(newdraftfd, "7bit\n\n");

		info=msg2html_textplain_start(sqwebmail_content_charset.c_str(),
					      sqwebmail_content_charset.c_str(),
					      true,
					      true,
					      mkurl,
					      NULL,
					      NULL,
					      1,
					      save_textplain,
					      NULL);

		if (info)
		{
			struct wrap_info uw;

			wrap_text_init(&uw, sqwebmail_content_charset.c_str(),
				       convert_text2html, info);

			wrap_text(&uw, newmsg, newmsg_size);
			msg2html_textplain_end(info);
		}

		if (sig.size() || footer.size())
			save_textplain("<hr />\n", 7, NULL);

		if (sig.size())
		{

			info=msg2html_textplain_start(sqwebmail_content_charset.c_str(),
						      sqwebmail_content_charset.c_str(),
						      true,
						      true,
						      mkurl,
						      NULL,
						      NULL,
						      1,
						      save_textplain,
						      NULL);

			if (info)
			{
				struct wrap_info uw;

				wrap_text_init(&uw, sqwebmail_content_charset.c_str(),
					       convert_text2html, info);

				wrap_text(&uw, sig.data(), sig.size());
				msg2html_textplain_end(info);
			}
		}

		if (footer.size())
		{
			save_textplain("<br />\n", 7, NULL);

			info=msg2html_textplain_start(sqwebmail_content_charset.c_str(),
						      sqwebmail_content_charset.c_str(),
						      true,
						      true,
						      mkurl,
						      NULL,
						      NULL,
						      1,
						      save_textplain,
						      NULL);

			if (info)
			{
				msg2html_textplain(info, footer.data(), footer.size());
				msg2html_textplain_end(info);
			}
		}

		maildir_writemsgstr(newdraftfd, "\n"
				    "--");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd, "--\n");
		multipart_boundary.back()=last_boundary_char;

	}

	if (has_attachments)
	{
		newmsg_copy_attachments(message, fp, multipart_boundary);
		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, multipart_boundary.c_str());
		maildir_writemsgstr(newdraftfd, "--\n");
	}

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

	if ( maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
			      draftfilename.c_str(), -1, prev_size))
		cgi_put("error", "quota");

	return(draftfilename);
}

static void sentmsg_copy(rfc822::fdstreambuf &sb, const rfc2045::entity &e)
{
	char buf[BUFSIZ];

	auto startpos=e.startpos;
	sb.pubseekpos(e.startpos);
	if (sb.error())
	{
		close(newdraftfd);
		enomem();
	}

        while (startpos < e.endbody)
        {
		size_t     cnt=sizeof(buf);

                if (cnt > e.endbody - startpos)
                        cnt=e.endbody - startpos;

		auto n=sb.sgetn(buf, cnt);

		if (n <= 0)
                {
                        enomem();
                }

                maildir_writemsg(newdraftfd, buf, n);
                startpos += n;
        }
}

/* Create message in the sent folder */

std::string newmsg_createsentmsg(const char *draftname, int *isgpgerr)
{
	auto filename=maildir_find(INBOX "." DRAFTS, draftname);

	*isgpgerr=0;

	if (filename.empty())	return "";

	rfc822::fdstreambuf fp{
		maildir_safeopen(filename.c_str(), O_RDONLY, 0)
	};

	if (fp.error())
	{
		enomem();
	}

	rfc2045::entity message;

	std::istreambuf_iterator<char> b{&fp}, e;
	rfc2045::entity::line_iter<false>::iter parser{b, e};

	message.parse(parser);

	std::string sentname;

	newdraftfd=maildir_createmsg(INBOX "." SENT, 0, sentname);
	if (newdraftfd < 0)
	{
		enomem();
	}

	/* First, copy all headers except X- headers */

	rfc2045::entity::line_iter<false>::headers headers{message, fp};

	headers.keep_eol=true;

	do
	{
		const auto &[header, value] = headers.name_content();

		if (header.substr(0, 2) == "x-")	continue;

		auto s=headers.current_header();

		if (s == "\n")
			continue;
		maildir_writemsg(newdraftfd, s.data(), s.size());
	} while (headers.next());
	if (auth_getoptionenvint("wbusexsender"))
	{
		maildir_writemsgstr(newdraftfd, "X-Sender: ");
		maildir_writemsgstr(newdraftfd, login_returnaddr());
		maildir_writemsgstr(newdraftfd, "\n");
	}

	maildir_writemsgstr(newdraftfd, "\n");

	{
		char buf[BUFSIZ];

		auto startbody=message.startbody;

		fp.pubseekpos(startbody);

		while (startbody < message.endbody)
		{
			size_t cnt=sizeof(buf);

			if (cnt > message.endbody-message.startbody)
				cnt=message.endbody-message.startbody;

			auto n=fp.sgetn(buf, cnt);

			if (n <= 0)
			{
				close(newdraftfd);
				enomem();
			}

			maildir_writemsg(newdraftfd, buf, n);
			startbody += n;
		}
	}

	if ( maildir_writemsg_flush(newdraftfd))
	{
		maildir_closemsg(newdraftfd, INBOX "." SENT,
				 sentname.c_str(), 0, 0);
		return "";
	}

	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		char dosign= *cgi("sign");
		char doencrypt= *cgi("encrypt");
		const char *signkey= cgi("signkey");
		std::string encryptkeys_s;

		{
			auto encryptkeys_v=cgi_multiple("encryptkey");

			if (!encryptkeys_v.empty())
			{
				size_t l=0;

				for (auto &e:encryptkeys_v)
					l += 1+e.size();

				encryptkeys_s.reserve(l-1);

				const char *sep="";

				for (auto &e:encryptkeys_v)
				{
					encryptkeys_s += sep;
					sep=" ";
					encryptkeys_s += e;
				}
			}
		}

		if (gpgbadarg(encryptkeys_s.c_str()))
		{
			encryptkeys_s.clear();
		}

		if (gpgbadarg(signkey) || !*signkey)
		{
			signkey=0;
		}

		if (encryptkeys_s.empty())
			doencrypt=0;

		if (!signkey)
			dosign=0;

		if (lseek(newdraftfd, 0L, SEEK_SET) < 0)
		{
			maildir_closemsg(newdraftfd, INBOX "." SENT,
					 sentname.c_str(), 0, 0);
			return "";
		}

		if (!dosign)
			signkey=0;
		if (!doencrypt)
			encryptkeys_s.clear();

		if (dosign || doencrypt)
		{
			/*
			** What we do is create another draft, then substitute
			** it for newdraftfd/sentname.  Sneaky.
			*/

			std::string newnewsentname;
			int newnewdraftfd=maildir_createmsg(INBOX "." SENT, 0,
							    newnewsentname);

			if (newnewdraftfd < 0)
			{
				maildir_closemsg(newdraftfd, INBOX "." SENT,
						 sentname.c_str(), 0, 0);
				return "";
			}

			if (gpgdomsg(newdraftfd, newnewdraftfd,
				     signkey, encryptkeys_s.c_str()))
			{
				maildir_closemsg(newnewdraftfd, INBOX "." SENT,
						 newnewsentname.c_str(), 0, 0);
				maildir_closemsg(newdraftfd, INBOX "." SENT,
						 sentname.c_str(), 0, 0);
				*isgpgerr=1;
				return "";
			}

			maildir_closemsg(newdraftfd, INBOX "." SENT,
					 sentname.c_str(), 0, 0);
			sentname=std::move(newnewsentname);
			newdraftfd=newnewdraftfd;

		}
	}

	if ( maildir_closemsg(newdraftfd, INBOX "." SENT,
			      sentname.c_str(), 1, 0))
	{
		return "";
	}

	return sentname;
}

/* ---------------------------------------------------------------------- */

/* Search for the boundary tag in a string buffer - this is the new message
** we're creating.  We should really look for the tag at the beginning of the
** line, however, the text is not yet linewrapped, besides, why make your
** life hard?
*/

/* Again, just look for it at the beginning of the line -- why make your
** life hard? */

bool multipart_boundary_checkf(std::string boundary, std::streambuf &f)
{
	char buffer[BUFSIZ];
	rfc2045::entity::boundary_detector detector{boundary};

	for (size_t n; (n=f.sgetn(buffer, BUFSIZ)) > 0; )
	{
		detector(buffer, n);

		if (detector)
			return false;
	}
	return true;
}

/* ---------------------------------------------------------------------- */

/* Copy existing attachments into the new draft message */

/* multipart_boundary - determine if current draft has attachments */

static std::string newmsg_multipart_boundary(rfc822::fdstreambuf &f,
					     const char *msg)
{
	unsigned counter=0;
	std::string boundary;

	while (1)
	{
		boundary=rfc2045::entity::new_boundary(counter);

		if (!f.error())
		{
			f.pubseekpos(0);
			if (!multipart_boundary_checkf(boundary, f))
				continue;
		}
		std::stringstream sb{msg};
		if (!multipart_boundary_checkf(boundary, *sb.rdbuf()))
			continue;
		break;
	}
	return (boundary);
}

static void newmsg_copy_attachments(rfc2045::entity &message,
				    rfc822::fdstreambuf &fd,
				    const std::string &boundary)
{
	bool foundtextplain=false;

	for (auto &subentity:message.subentities)
	{
		if (!foundtextplain &&
		    subentity.find_content_type("text/plain"))
		{	/* Previous version of this message */

			foundtextplain=true;
			continue;
		}
		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, boundary.c_str());
		maildir_writemsgstr(newdraftfd, "\n");
		sentmsg_copy(fd, subentity);
	}
}
