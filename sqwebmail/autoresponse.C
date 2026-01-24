/*
*/

/*
** Copyright 2001-2011 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"config.h"
#include	"autoresponse.h"
#include	"maildir/autoresponse.h"
#include	"mailfilter.h"
#include	<courier-unicode.h>
#include	"sqwebmail.h"
#include	"htmllibdir.h"
#include	"maildir.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirfilter.h"
#include	"rfc2045/rfc2045.h"
#include	"newmsg.h"
#include	"cgi/cgi.h"
#include	"numlib/numlib.h"
#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<signal.h>
#include	<ctype.h>
#include	<errno.h>
#include	<string>
#include	<string_view>
#include	<algorithm>
#include	<sstream>

#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

extern const char *sqwebmail_content_charset;
extern "C" void output_attrencoded(const char *);
extern const char *calc_mime_type(const char *filename);

extern void charset_warning(const char *);

static void save_autoresponse(const char *p, size_t l, void *vp)
{
	std::ostream *o=reinterpret_cast<std::ostream *>(vp);

	(*o) << std::string_view{p, l};
}

static bool read_headers(std::istream &i);

static int show_autoresponse_trampoline(const char *ptr, size_t cnt, void *arg)
{
	show_textarea((struct show_textarea_info *)arg, ptr, cnt);
	return 0;
}

void autoresponse()
{
const char	*autoresp_title1=getarg("TITLE1");
const char	*autoresp_title2=getarg("TITLE2");
const char	*autoresp_text1=getarg("TEXT1");
const char	*autoresp_text2=getarg("TEXT2");

	if ( *cgi("do.newautoresp"))
	{
		const char *name=cgi("newname");
		char *p;

		p=folder_toutf8(name);

		if (!p || !mail::autoresponse::validate("", p))
		{
			free(p);
			printf("%s", getarg("BADNAME"));
			return;
		}

		std::ifstream i;

		mail::autoresponse::open(i, "", p);

		if (i)
		{
			free(p);
			printf("%s", getarg("EEXIST"));
			return;
		}

		printf("%s%s%s\n", autoresp_title1, name, autoresp_title2);
		printf("<input type=\"hidden\" name=\"autoresponse\" value=\"");
		output_attrencoded(p);
		printf("\" />\n");
		free(p);

		printf("%s%s\n", autoresp_text1, autoresp_text2);
		printf("%s<input type=\"file\" size=\"20\" name=\"uploadfile\" /><br />",
		       getarg("UPLOAD"));
		printf("<input type=\"submit\" name=\"do.autorespcreate\""
		       " value=\"%s\" />", getarg("SAVE"));
		return;
	}

	if ( *cgi("do.autorespedit"))
	{
		const char *autorespname=cgi("autoresponse_choose");
		char *s=folder_fromutf8(autorespname);
		const char *pp;

		if (!s)
		{
			printf(getarg("ERROR"), strerror(errno));
			return;
		}

		pp=cgi("replytext");

		std::ifstream fp;

		mail::autoresponse::open(fp, "", autorespname);

		if (!fp && !*pp)
		{
			free(s);
			return;
		}

		printf("%s%s%s\n", autoresp_title1, s, autoresp_title2);

		if (fp && !read_headers(fp))
		{
			free(s);
			return;
		}

		printf("<input type=\"hidden\" name=\"autoresponse\" value=\"");
		output_attrencoded(autorespname);
		printf("\" />\n");
		free(s);

		printf("%s", autoresp_text1);

		if (pp && *pp)
			output_attrencoded(pp);
		else
		{
			struct show_textarea_info info;
			unicode_convert_handle_t h;

			show_textarea_init(&info, 0);

			h=unicode_convert_init("utf-8",
						 sqwebmail_content_charset,
						 show_autoresponse_trampoline,
						 &info);

			if (h)
			{
				std::string s;

				while (std::getline(fp, s))
				{
					s += "\n";
					unicode_convert(h, s.c_str(), s.size());
				}
				unicode_convert_deinit(h, NULL);
			}
		}

		printf("%s\n", autoresp_text2);
		printf("%s<input type=\"file\" size=\"20\" name=\"uploadfile\" /><br />",
		       getarg("UPLOAD"));
		printf("<input type=\"submit\" name=\"do.autorespcreate\""
		       " value=\"%s\" />", getarg("SAVE"));
		return;
	}
}

/*
** Read the MIME headers in the autoresponse file, to make sure that we
** can show it.
*/

static bool read_headers(std::istream &i)
{
	rfc2045::entity message;

	std::stringstream ss;

	ss << "Mime-Version: 1.0\n";

	std::string line;

	while (std::getline(i, line))
	{
		line += "\n";

		ss << line;
		if (line == "\n" || line == "\r\n")
			break;
	}

	ss.seekg(0);

	{
		std::istreambuf_iterator<char> b{ss.rdbuf()}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	if (message.content_type.value != "text/plain" ||
	    !message.content_type.format_flowed())
	{
		printf(getarg("ATT"), message.content_type.value.c_str());
		return false;
	}

	return true;
}

static bool upload_attachment(std::ostream &);

void autoresponsedelete()
{
	if ( *cgi("do.autorespcreate"))
	{
		const char *autorespname=cgi("autoresponse");
		const char *autoresptxt=cgi("text");
		size_t l;

		if (!mail::autoresponse::create(
			    "", autorespname,
			    [&]
			    (std::ostream &o)
			    {
				    if (upload_attachment(o))
					    return;

				    wrap_info uw;

				    l=strlen(autoresptxt);
				    while (l && (autoresptxt[l-1] == '\r' ||
						 autoresptxt[l-1] == '\n'))
					    --l;

				    o << "Content-Type: text/plain"
					    "; format=flowed; delsp=yes"
					    "; charset=\"utf-8\"\n"
					    "Content-Transfer-Encoding: "
					    "8bit\n\n";

				    wrap_text_init(&uw, "utf-8",
						   save_autoresponse, &o);
				    wrap_text(&uw, autoresptxt, l);
			    }))
		{
			if (errno == ENOSPC)
			{
				cgi_put("do.autorespedit", "1");
				cgi_put("autoresponse_choose", autorespname);
				cgi_put("replytext", cgi("text"));
				printf(getarg("QUOTA"), strerror(errno));
			}
			else
				printf(getarg("SAVEFAILED"), strerror(errno));
		}
		return;
	}

	if ( *cgi("do.autorespdelete"))
	{
		const char *autorespname=cgi("autoresponse_choose");

		if (mailfilter_autoreplyused(autorespname))
		{
			char *s=folder_fromutf8(autorespname);
			printf(getarg("INUSE"), s ? s:"");
			if (s)
				free(s);
		}
		else
			mail::autoresponse::remove("", autorespname);
		return;
	}
}

struct upload_attach_info {
	std::ostream &attachment;
	rfc822::fdstreambuf tmpfile=rfc822::fdstreambuf::tmpfile();
	bool first=true;

	std::string filename;
} ;

static int start_upload(const char *, const char *, void *);
static int upload(const char *, size_t, void *);
static void end_upload(void *);

static bool upload_attachment(std::ostream &o)
{
	upload_attach_info uai{o};

	if (cgi_getfiles( &start_upload, &upload, &end_upload, 1, &uai ))
		return false;

	if (uai.first)
		return false; // No attachment?

	if (uai.tmpfile.error() || !o)
		return false;

	return true;
}

static int start_upload(const char *name, const char *filename, void *vp)
{
	struct upload_attach_info *uai=(struct upload_attach_info *)vp;
	const char *p;

	if (!uai->first)
		return (0);

	p=strrchr(filename, '/');
	if (p)	filename=p+1;

	p=strrchr(filename, '\\');
	if (p)	filename=p+1;

	if (*filename)
	{
		uai->filename=filename;
	}
	else
	{
		p=strrchr(name, '/');
		if (p)	name=p+1;

		p=strrchr(name, '\\');
		if (p)	name=p+1;
		uai->filename=p;
	}

	return (0);
}

static int upload(const char *c, size_t n, void *vp)
{
	struct upload_attach_info *uai=(struct upload_attach_info *)vp;

	if (uai->first)
		uai->tmpfile.sputn(c, n);

	return (0);
}

static void end_upload(void *vp)
{
	struct upload_attach_info *uai=(struct upload_attach_info *)vp;
	const char *mimetype;
	char *argvec[10];
	int n;
	pid_t pid1, pid2;
	int waitstat;

	if (!uai->first)
		return;
	uai->first=false;

	uai->tmpfile.pubseekpos(0);

	if (uai->tmpfile.error())
		return;

	mimetype=calc_mime_type(uai->filename.c_str());

	/*
	** If we get something that's MIMEed as message/rfc822, read it, strip
	** its headers except for the MIME content- headers, then save what's
	** left as our autoreply.  This allows for a convenient way to upload
	** multipart/alternative content.
	*/

	if (rfc2045_message_content_type(mimetype))
	{
		/* Magic */

		std::istream itmpfile{&uai->tmpfile};

		std::string s;

		bool is_content_header=false;

		while (std::getline(itmpfile, s))
		{
			s += "\n";
			if (s == "\n")
			{
				uai->attachment << s;
				break;
			}

			switch (*s.c_str()) {
			case ' ':
			case '\t':
				break;
			default:

				std::string headercpy{
					s.begin(),
						std::find(s.begin(), s.end(),
							  ':')};

				rfc2045::entity::tolowercase(
					headercpy.begin(),
					headercpy.end()
				);

				is_content_header=
					std::string_view{headercpy}.substr(0, 8)
					== "content-";
			}

			if (is_content_header)
				uai->attachment << s;
		}

		while (std::getline(itmpfile, s))
		{
			s += "\n";

			uai->attachment << s;
		}
		return;
	}

	static char makemime_str[]="makemime";
	static char copt_str[]="-c";
	argvec[0]=makemime_str;
	argvec[1]=copt_str;
	argvec[2]=(char *)mimetype;

	n=3;
	if (strncasecmp(argvec[2], "text/", 5) == 0 ||
	    strcasecmp(argvec[2], "auto") == 0)
	{
		static char copt_str[]="-C";
		argvec[3]=copt_str;
		argvec[4]=(char *)sqwebmail_content_charset;
		n=5;
	}

	static char dash[]="-";
	argvec[n++]=dash;
	argvec[n]=0;

	signal(SIGCHLD, SIG_DFL);

	auto tmpfile2=rfc822::fdstreambuf::tmpfile();

	pid1=fork();

	if (pid1 < 0)
	{
		enomem();
	}


	if (pid1 == 0)
	{
		dup2(uai->tmpfile.fileno(), 0);
		dup2(tmpfile2.fileno(), 1);

		uai->tmpfile = {};
		tmpfile2 = {};

		execv(MAKEMIME, argvec);
		fprintf(stderr,
		       "CRIT: exec %s: %s\n", MAKEMIME, strerror(errno));
		exit(1);
	}

	for (;;)
	{
		pid2=wait(&waitstat);

		if (pid2 == pid1)
		{
			waitstat= WIFEXITED(waitstat) ? WEXITSTATUS(waitstat)
				: 1;
			break;
		}

		if (pid2 == -1)
		{
			waitstat=1;
			break;
		}
	}

	tmpfile2.pubseekpos(0);
	if (waitstat || tmpfile2.error())
	{
		enomem();
	}

	uai->attachment << &tmpfile2;
}

void autoresponselist()
{
	auto list=mail::autoresponse::list("");

	std::sort(list.begin(), list.end());

	for (auto &f:list)
	{
		char *s;

		printf("<option value=\"");
		output_attrencoded(f.c_str());
		printf("\">");

		s=folder_fromutf8(f.c_str());
		output_attrencoded(s);
		printf("</option>");
	}
}

void autoresponsepick()
{
	auto list=mail::autoresponse::list("");

	const char *choice=cgi("autoresponse_choose");

	std::sort(list.begin(), list.end());

	for (auto &f:list)
	{
		char *s;

		printf("<option%s value=\"",
		       strcmp(choice, f.c_str()) ? "":" selected='selected'");
		output_attrencoded(f.c_str());
		printf("\">");

		s=folder_fromutf8(f.c_str());
		output_attrencoded(s);
		printf("</option>");
		free(s);
	}
}
