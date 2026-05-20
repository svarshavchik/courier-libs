/*
** Copyright 1998 - 2026. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"sqwebmail.h"
#include	"sqconfig.h"
#include	"auth.h"
#include	"folder.h"
#include	"pref.h"
#include	"maildir.h"
#include	"cgi/cgi.h"
#include	"pref.h"
#include	"mailinglist.h"
#include	"newmsg.h"
#include	"pcp.h"
#include	"acl.h"
#include	"addressbook.h"
#include	"autoresponse.h"
#include	"http11/http11.h"
#include	"random128/random128.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirinfo.h"
#include	"maildir/maildiraclt.h"
#include	"liblock/config.h"
#include	"liblock/liblock.h"
#include	"courierauth.h"
#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<signal.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include        <sys/socket.h>
#if	HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
#include	<locale.h>
#if	USE_LIBCHARSET
#if	HAVE_LOCALCHARSET_H
#include	<localcharset.h>
#elif	HAVE_LIBCHARSET_H
#include	<libcharset.h>
#endif	/* HAVE_LOCALCHARSET_H */
#elif	HAVE_LANGINFO_CODESET
#include	<langinfo.h>
#endif	/* USE_LIBCHARSET */
#endif	/* HAVE_SETLOCALE */
#endif	/* HAVE_LOCALE_H */
#include	<ctype.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if	HAVE_SYS_SELECT_H
#include	<sys/select.h>
#endif
#if	HAVE_SYS_UIO_H
#include	<sys/uio.h>
#endif
#define	MD5_INTERNAL
#include	"md5/md5.h"

#include	<courierauthdebug.h>
#include	"rfc2045/rfc2045.h"
#include	"maildir/maildircache.h"
#include	"maildir/maildiraclt.h"
#include	"maildir/maildirnewshared.h"
#include	"mailfilter.h"
#include	"numlib/numlib.h"
#include	"gpglib/gpglib.h"
#include	"gpg.h"
#if	HAVE_CRYPT_H
#include	<crypt.h>
#endif
#if     NEED_CRYPT_PROTOTYPE
extern char *crypt(const char *, const char *);
#endif
#include	"htmllibdir.h"

#include	"logindomainlist.h"
#include	"rfc822/rfc822.h"
#include	<fstream>
#include	<vector>
#include	<string>
#include	<string_view>
#include	<memory>
extern void print_safe(const char *);

extern void sent_gpgerrtxt();
extern void sent_gpgerrresume();
std::string sqwebmail_mailboxid;
const char *sqwebmail_folder=0;

extern void spell_show();
extern void spell_check_continue();
extern void ldaplist();
extern int ldapsearch();
extern void doldapsearch();

#define ALL_RIGHTS \
	ACL_ADMINISTER \
	ACL_CREATE \
	ACL_EXPUNGE \
	ACL_INSERT \
	ACL_LOOKUP \
	ACL_READ \
	ACL_SEEN \
	ACL_DELETEMSGS \
	ACL_WRITE \
	ACL_DELETEFOLDER
char sqwebmail_folder_rights[sizeof(ALL_RIGHTS)];

std::string sqwebmail_sessiontoken;

std::string sqwebmail_content_language;
std::string sqwebmail_content_locale;
std::string sqwebmail_system_charset;
std::string sys_locale_charset;

std::string sqwebmail_content_ispelldict;
std::string sqwebmail_content_charset;

dev_t sqwebmail_homedir_dev;
ino_t sqwebmail_homedir_ino;

static int noimages=0;

time_t	login_time;

/* Need to cache the following environment variables */
static const char * const authvars[] = { "AUTHADDR", "AUTHFULLNAME",
					 "OPTIONS", "AUTHENTICATED", 0 };

#ifdef	GZIP
static int gzip_save_fd;

#endif

static const char *sqwebmail_formname;

extern void attachments_head(const char *, const char *, const char *);
extern void attachments_opts(const char *);
extern void doattach(const char *, const char *);

static void timezonelist();

static std::vector<std::unique_ptr<rfc822::fdstreambuf>> template_stack;

std::string trim_spaces(const char *s);

size_t get_timeoutsoft()
{
	time_t n=TIMEOUTSOFT;
	const char *p;

	p=getenv("SQWEBMAIL_TIMEOUTSOFT");

	if (p && *p)
		n=atoi(p);

	return n;
}

size_t get_timeouthard()
{
	time_t n=TIMEOUTHARD;
	const char *p;

	p=getenv("SQWEBMAIL_TIMEOUTHARD");

	if (p && *p)
		n=atoi(p);

	return n;
}

void fake_exit(int n)
{
	maildir_cache_cancel();
	exit(n);
}


/* Stub to catch aborts from authlib */

void authexit(int n)
{
	fake_exit(n);
}

void print_attrencodedlen(const char *p, size_t len, int oknl, FILE *fp)
{
	for (; len; p++, --len)
	{
		switch (*p)	{
		case '<':
			fprintf(fp, "&lt;");
			continue;
		case '>':
			fprintf(fp, "&gt;");
			continue;
		case '&':
			fprintf(fp, "&amp;");
			continue;
		case '"':
			fprintf(fp, "&quot;");
			continue;
		case '\n':
			if (oknl)
			{
				if (oknl == 2)
				{
					fprintf(fp, "<br />");
					continue;
				}
				putc('\n', fp);
				continue;
			}
		default:
			if (!ISCTRL(*p))
			{
				putc(*p, fp);
				continue;
			}
		}
		fprintf(fp, "&#%d;", (int)(unsigned char)*p);
	}
}

void output_attrencoded_fp(const char *p, FILE *fp)
{
	print_attrencodedlen(p, strlen(p), 0, fp);
}

void output_attrencoded_fplen(const char *p, size_t len, FILE *fp)
{
	print_attrencodedlen(p, len, 0, fp);
}

void output_attrencoded(const char *p)
{
	output_attrencoded_fp(p, stdout);
}

void output_attrencoded(std::string_view p)
{
	print_attrencodedlen(p.data(), p.size(), 0, stdout);
}

void output_attrencoded_oknl_fp(const char *p, FILE *fp)
{
	print_attrencodedlen(p, strlen(p), 1, fp);
}

void output_attrencoded_oknl(const char *p)
{
	output_attrencoded_oknl_fp(p, stdout);
}

void output_attrencoded_nltobr(const char *p)
{
	print_attrencodedlen(p, strlen(p), 2, stdout);
}

void output_urlencoded(const char *p)
{
	std::string q;

	q.reserve(cgi_encode::estimate(p));
	cgi_encode::encode(std::back_inserter(q), p);

	printf("%s", q.c_str());
}

void output_loginscriptptr()
{
#if	USE_HTTPS_LOGIN
const	char *p=cgihttpsscriptptr();
#elif	USE_RELATIVE_URL
const	char *p=cgirelscriptptr();
#else
const	char *p=cgihttpscriptptr();
#endif

	printf("%s", p);
}

const char *nonloginscriptptr()
{
#if	USE_HTTPS
	return (cgihttpsscriptptr());
#elif	USE_RELATIVE_URL
	return (cgirelscriptptr());
#else
	return (cgihttpscriptptr());
#endif
}

void output_scriptptr()
{
const	char *p=nonloginscriptptr();

	printf("%s", p);
	if (!sqwebmail_mailboxid.empty())
	{
		std::string q;

		q.reserve(cgi_encode::estimate(sqwebmail_mailboxid));
		cgi_encode::encode(std::back_inserter(q), sqwebmail_mailboxid);
		char	buf[NUMBUFSIZE];

		printf("/login/%s/%s/%s", q.c_str(),
			!sqwebmail_sessiontoken.empty() ?
				sqwebmail_sessiontoken.c_str():" ",
			libmail_str_time_t(login_time, buf));
	}
}

void output_loginscriptptr_get()
{
	output_loginscriptptr();
	if (!sqwebmail_mailboxid.empty())
	{
		std::string q;

		q.reserve(cgi_encode::estimate(sqwebmail_mailboxid));
		cgi_encode::encode(std::back_inserter(q), sqwebmail_mailboxid);

		char	buf[NUMBUFSIZE];

		printf("/login/%s/%s/%s", q.c_str(),
			!sqwebmail_sessiontoken.empty() ?
				sqwebmail_sessiontoken.c_str():" ",
			libmail_str_time_t(login_time, buf));
	}
}

std::string scriptptrget()
{
	std::string q;
	size_t	l=0;
	int	i;
	char	buf[NUMBUFSIZE];

#define	ADD(s) {const char *zz=(s); if (i) q += zz; l += strlen(zz);}
#define ADDE(ue) { \
	std::string yy; \
	yy.reserve(cgi_encode::estimate(ue)); \
	cgi_encode::encode(std::back_inserter(yy), ue); \
	ADD(yy.c_str()); \
}

	for (i=0; i<2; i++)
	{
		if (i) q.reserve(l);
		ADD( nonloginscriptptr() );
		if (sqwebmail_mailboxid.empty())
		{
			ADD("?");
			continue;
		}

		ADD("/login/");
		ADDE(sqwebmail_mailboxid.c_str());
		ADD("/");
		ADD(!sqwebmail_sessiontoken.empty() ?
			sqwebmail_sessiontoken.c_str():" ");
		ADD("/");
		ADD(libmail_str_time_t(login_time, buf));

		ADD( "?" );
		if (sqwebmail_folder)
		{
			ADD("folder=");
			ADDE(sqwebmail_folder);
		}
	}
#undef	ADD
#undef	ADDE
	return (q);
}

void output_scriptptrget()
{
	std::string p=scriptptrget();

	printf("%s", p.c_str());
	return;
}

void output_scriptptrpostinfo()
{
	if (sqwebmail_folder)
	{
		printf("<input type=\"hidden\" name=\"folder\" value=\"");
		output_attrencoded(sqwebmail_folder);
		printf("\" />");
	}

	if (*cgi("folderdir"))	/* In folders.html */
	{
		printf("<input type=\"hidden\" name=\"folderdir\" value=\"");
		output_attrencoded(cgi("folderdir"));
		printf("\" />");
	}
}

extern "C" void error(const char *errmsg)
{
	cginocache();
	printf("Content-Type: text/html; charset=us-ascii\n\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1></body></html>\n",
		errmsg, errmsg);
	cleanup();
	fake_exit(1);
}

extern "C" void error2(const char *file, int line)
{
	cginocache();
	printf("Content-Type: text/html; charset=us-ascii\n\n"
		"<html><head><title>Internal error</title></head><body>"
		"<h1>Internal error (module %s, line %d) - contact system administrator</h1>"
		"</body></html>\n",
		file, line);
	cleanup();
	fake_exit(1);
}

extern "C" void error3(const char *file, int line, const char *msg1, const char *msg2, int err)
{
	cginocache();
	if (err == -1) err = errno;
	printf("Content-Type: text/html; charset=us-ascii\n\n"
		"<html><head><title>Internal error</title></head><body>"
		"<h1>Internal error (module %s, line %d) - contact system administrator</h1>"
		"<pre>%s\n%s\n%s</pre>"
		"</body></html>\n",
		file, line, msg1?msg1:"", msg2?msg2:"", err?strerror(err):"");
	cleanup();
	fake_exit(1);
}


extern "C" char *get_templatedir()
{
	char	*templatedir=getenv("SQWEBMAIL_TEMPLATEDIR");

	static char def_templatedir[]=HTMLLIBDIR;
	if (!templatedir || !*templatedir)	templatedir=def_templatedir;

	return templatedir;
}


extern "C" char *get_imageurl()
{
	char	*imageurl=getenv("SQWEBMAIL_IMAGEURL");

	static char imgpath_str[]=IMGPATH;
	if (!imageurl || !*imageurl)	imageurl=imgpath_str;

	return imageurl;
}

void open_langform(
	rfc822::fdstreambuf &fbuf,
	std::string_view lang,
	std::string_view formname,
	bool print_header
)
{
	std::string formpath;
	char	*templatedir=get_templatedir();

	/* templatedir/lang/formname */

	formpath.reserve(strlen(templatedir)+2+lang.size()+formname.size());

	formpath += templatedir;
	formpath += "/";
	formpath += lang;
	formpath += "/";
	formpath += formname;

	fbuf=rfc822::fdstreambuf{open(formpath.c_str(), O_RDONLY|O_CLOEXEC)};

	if (!fbuf.error() && print_header)
	{
		printf("Content-Language: ");
		fwrite(lang.data(), lang.size(), 1, stdout);
		printf("\n");
	}
	if (!fbuf.error())
		fcntl(fbuf.fileno(), F_SETFD, FD_CLOEXEC);
}

int ishttps()
{
	const char *p=getenv("HTTPS");

	return (p && strcasecmp(p, "on") == 0);
}

struct var_put_buf {
	char argbuf[3072];
	char *argp;
	size_t argn;
} ;

static void var_put_buf_func(int c, void *p)
{
	struct var_put_buf *pp=(struct var_put_buf *)p;

	if (pp->argn)
	{
		*pp->argp++=c;
		--pp->argn;
	}
}

static void pass_image_through(int c, void *p)
{
	putchar(c);
}

static void output_image( std::istream &f,
			  void (*output_func)(int, void *), void *void_arg)
{
	int c;

	/*
	  Conditional image.  It's formatted as follows:

	  @@filename,width=x height=y@text@
            ^
            |
	    ----- we're at this point now.

	  If images are enabled, we replace that with an IMG tag we build from
	  filename,width=x, height=y.
	  If images are disabled, we replace all of this with text.

	*/

#define	MKIMG(c)	(*output_func)((c), void_arg)

	if (noimages)
	{
		while ((c=f.get()) >= 0
		       && c != '@')
			;
		while ((c=f.get()) >= 0
		       && c != '@')
			MKIMG(c);
	}
	else
	{
		char	*p;

		MKIMG('<');
		MKIMG('i');
		MKIMG('m');
		MKIMG('g');
		MKIMG(' ');
		MKIMG('s');
		MKIMG('r');
		MKIMG('c');
		MKIMG('=');
		MKIMG('"');
		for (p=get_imageurl(); *p; p++)
			MKIMG(*p);

		MKIMG('/');
		while ((c=f.get()) >= 0
		       && c != '@' && c != ',')
			MKIMG(c);
		MKIMG('"');
		MKIMG(' ');
		if (c == ',')
			c=f.get();
		while (c >= 0 && c != '@')
		{
			MKIMG(c);
			c=f.get();
		}
		while ((c=f.get()) >= 0 && c != '@')
			;
		MKIMG(' ');
		MKIMG('/');
		MKIMG('>');
	}
}

/* ---- time zone list ---- */

static bool timezonefile(
	std::function<bool(std::string_view, std::string_view)> callback_func
)
{
	rfc822::fdstreambuf fbuf;
	std::string buffer;

	if (!sqwebmail_content_language.empty())
		open_langform(fbuf,
				sqwebmail_content_language.c_str(),
				"TIMEZONELIST", false);

	if (fbuf.error())
		open_langform(fbuf,
				HTTP11_DEFAULTLANG, "TIMEZONELIST", false);

	if (fbuf.error())
		return (0);

	std::istream f{&fbuf};
	while (std::getline(f, buffer))
	{
		size_t n=buffer.find('#');
		if (n != buffer.npos)
			buffer.resize(n);

		n=buffer.find_first_not_of(" \t");
		if (n == buffer.npos)
			continue;

		auto p=std::string_view{buffer}.substr(n);

		size_t e=p.find_first_of(" \t");
		if (e == p.npos)
			continue;

		std::string_view tz{p.data(), e};

		p.remove_prefix(e+1);
		e=p.find_first_not_of(" \t");
		if (e == p.npos)
			continue;

		p.remove_prefix(e);

		e=p.find_last_not_of(" \t");

		if (!callback_func(tz, p.substr(0, ++e)))
			return (false);
	}
	return (true);
}

static bool callback_timezonelist(std::string_view tz, std::string_view n);

static void timezonelist()
{
	printf("<select name=\"timezonelist\" class=\"timezonelist\">");
	timezonefile(callback_timezonelist);
	printf("</select>\n");
}

static bool callback_timezonelist(std::string_view tz, std::string_view n)
{
	printf("<option value=\"");
	fwrite(tz.data(), tz.size(), 1, stdout);
	printf("\">");
	output_attrencoded(n);
	printf("</option>\n");
	return (true);
}

static void set_timezone(std::string_view p)
{
	if (p.empty() || p == "*")
		return;

	std::string sv{p};
	setenv("TZ", sv.c_str(), 1);
}

/* Return TZ selected from login dropdown */

static std::string get_timezone()
{
	std::string langptr;

	timezonefile(
		[&langptr] (
			std::string_view tz,
			std::string_view n
		) -> bool
		{
			if (tz == cgi("timezonelist"))
				langptr=tz;
			return (true);
		});

	return(langptr);
}

/* ------------------------ */

static bool do_open_form(std::string_view formname, bool print_header)
{
	auto f=std::make_unique<rfc822::fdstreambuf>();

	if (!sqwebmail_content_language.empty())
		open_langform(*f, sqwebmail_content_language.c_str(), formname, print_header);
	if (f->error())
		open_langform(*f, HTTP11_DEFAULTLANG, formname, print_header);

	if (f->error())
		return (false);

	template_stack.push_back(std::move(f));
	return (true);
}

static void do_close_form()
{
	if (template_stack.empty())
		enomem();

	template_stack.pop_back();
}

static void do_output_form_loop(std::istream &);

static void fix_xml_header(std::istream &f)
{
	std::string linebuf;
	/*
	** Some templates now have an <?xml > header.  Adjust the
	** encoding to match the selected default.  Yes, it's a dirty hack,
	** and I'm proud of it, since it allows me to continue editing the
	** HTML templates in Amaya.
	*/

	if (!std::getline(f, linebuf))
		return;

	if (std::string_view{linebuf}.substr(0, 14) == "<?xml version=")
		printf("<?xml version=\"1.0\" encoding=\"%s\"?>\n",
			sqwebmail_content_charset.c_str());
	else
		printf("%s", linebuf.c_str());
}

void output_form(const char *formname)
{
#ifdef	GZIP
	int	dogzip;
	int	pipefd[2];
	pid_t	pid= -1;
#endif

	noimages= auth_getoptionenvint("wbnoimages");

	if (!do_open_form(formname, true))
		error("Can't open form template.");

	sqwebmail_formname=formname;

	/*
	** Except for the dummy frame window (and the tiny empty frame),
	** and the window containing the print preview of the message,
	** expire everything.
	*/

	if (strcmp(formname, "index.html") && strcmp(formname, "empty.html") &&
		strcmp(formname, "print.html"))
		cginocache();

#ifdef	GZIP

	dogzip=0;
	if (strcmp(formname, "readmsg.html") == 0 ||
	    strcmp(formname, "folder.html") == 0 ||
	    strcmp(formname, "folders.html") == 0 ||
	    strcmp(formname, "gpg.html") == 0)
	{
	const char *p=getenv("HTTP_ACCEPT_ENCODING");

		if (p)
		{
		char	*q=strdup(p), *r;

			if (!q)	enomem();
			for (r=q; *r; r++)
				*r= tolower((int)(unsigned char)*r);
			for (r=q; (r=strtok(r, ", ")) != 0; r=0)
				if (strcmp(r, "gzip") == 0)
				{
					dogzip=1;
					if (pipe(pipefd))
						enomem();
				}
			free(q);
		}
	}
#endif

	/* Do not send a Vary header for attachment downloads */

	if (*cgi("download") == 0)
		printf("Vary: Accept-Language\n");

#ifdef	GZIP
	if (dogzip)
		printf("Content-Encoding: gzip\n");
#endif

	printf("Content-Type: text/html");

	if (!sqwebmail_content_charset.empty())
		printf("; charset=%s", sqwebmail_content_charset.c_str());

	printf("\n\n");

#ifdef	GZIP
	if (dogzip)
	{
		fflush(stdout);
		while ((pid=fork()) == -1)
			sleep(5);
		if (pid == 0)
		{
			dup2(pipefd[0], 0);
			close(pipefd[0]);
			close(pipefd[1]);
			execl(GZIP, "gzip", "-c", (char *)0);
			fprintf(stderr,
			       "ERR: Cannot execute " GZIP ": %s\n",
			       strerror(errno));
			exit(1);
		}

		gzip_save_fd=dup(1);
		dup2(pipefd[1], 1);
		close(pipefd[1]);
		close(pipefd[0]);
	}
#endif
	std::istream f{&*template_stack.back()};
	fix_xml_header(f);
	do_output_form_loop(f);
	do_close_form();

#ifdef	GZIP
	if (pid > 0)
	{
	int	waitstat;
	pid_t	p2;

		/* Restore original stdout */

		fflush(stdout);
		dup2(gzip_save_fd, 1);
		close(gzip_save_fd);
		gzip_save_fd= -1;
		while ((p2=wait(&waitstat)) >= 0 && p2 != pid)
			;
	}
#endif
}

static void openinclude(std::string_view p);


void insert_include(std::string_view inc_name)
{
	openinclude(inc_name);

	std::istream ff{&*template_stack.back()};
	do_output_form_loop(ff);
	do_close_form();
}

static void do_output_form_loop(std::istream &f)
{
	int	c, c2, c3;

	while ((c=f.get()) >= 0)
	{
	char	kwbuf[64];

		if (c != '[')
		{
			putchar(c);
			continue;
		}
		c=f.get();
		if (c != '#')
		{
			putchar('[');
			f.unget();
			continue;
		}
		c=f.get();
		if (c == '?')
		{
			c=f.get();
			if (c < '0' || c > '9')
			{
				putchar('[');
				putchar('#');
				putchar('?');
				putchar(c);
				continue;
			}
			if (
			    ( c == '0' && nochangepass()) ||
			    (c == '1' && strncmp(cgi("folderdir"),
						 SHARED ".",
						 sizeof(SHARED)) == 0) ||
			    (c == '2' && strncmp(cgi("folderdir"),
						 SHARED ".",
						 sizeof(SHARED))) ||
			    (c == '4' && !maildir::filter::has(".")) ||
			    (c == '5' && libmail_gpg_has_gpg(GPGDIR)) ||
			    (c == '6' && !ishttps()) ||
			    (c == '7' && !sqpcp_has_calendar()) ||
			    (c == '8' && !sqpcp_has_groupware())
			    )
			{
				while ((c=f.get()) != EOF)
				{
					if (c != '[')	continue;
					if ( f.get() != '#')	continue;
					if ( f.get() != '?')	continue;
					if ( f.get() != '#')	continue;
					if ( f.get() == ']')	break;
				}
			}
			continue;
		}

		if (c == '$')
		{
			struct var_put_buf buf;

			buf.argp=buf.argbuf;
			buf.argn=sizeof(buf.argbuf)-1;

			while ((c=f.get()) >= 0 && c != '\n')
			{
				if (c == '#')
				{
					c=f.get();
					if (c == ']')	break;
					f.unget();
					c='#';
				}

				if (c == '@')
				{
					c=f.get();
					if (c == '@')
					{
						output_image(f,
							     var_put_buf_func,
							     &buf);
						continue;
					}
					f.unget();
					c='@';
				}
				var_put_buf_func(c, &buf);
			}
			*buf.argp=0;
			addarg(buf.argbuf);
			continue;
		}

		if (c == '@')
		{
			output_image(f, pass_image_through, NULL);
			c=f.get();
			if (c == '#')
			{
				c=f.get();
				if (c == ']')
					continue;
			}
			if (c != EOF)
				f.unget();
			continue;
		}

		if (!isalnum(c) && c != ':')
		{
			putchar('[');
			putchar('#');
			putchar(c);
			continue;
		}
		c2=0;
		while (c != EOF && (isalnum(c) || c == ':' || c == '_'))
		{
			if ((size_t)c2 < sizeof(kwbuf)-1)
				kwbuf[c2++]=c;
			c=f.get();
		}
		kwbuf[c2]=0;
		c2=c;

		if (c2 != '#')
		{
			putchar('[');
			putchar('#');
			printf("%s", kwbuf);
			f.unget();
			continue;
		}

		if ((c3=f.get()) != ']')
		{
			putchar('[');
			putchar('#');
			printf("%s", kwbuf);
			putchar(c2);
			f.unget();
			continue;
		}

		std::string_view kw{kwbuf};
		if (kw == "a")
		{
			addressbook();
		}
		else if (kw == "d")
		{
			std::string c=folder_fromutf8(cgi("folderdir"));
			const char *sep="";

			if (!c.empty() && c != INBOX)
			{
				printf(" - ");

				if (c == NEWSHAREDSP ||
				    std::string_view{c}.substr(
					0, sizeof(NEWSHAREDSP)-1
				    ) == NEWSHAREDSP ".")
				{
					printf("%s", getarg("PUBLICFOLDERS"));
					sep=".";
				}

				auto p=c.find('.');
				if (p != std::string::npos)
					c=c.substr(p+1);
				if (!c.empty())
				{
					printf("%s", sep);
					print_safe(c.c_str());
				}
			}
		}
		else if (kw == "D")
		{
			const char *p=cgi("folder");
			const char *q=strrchr(p, '.');

			if (q)
			{
				std::string r{p, (size_t)(q-p)};
				output_urlencoded(r.c_str());
			}
		}
		else if (kw == "G")
		{
			output_attrencoded(login_returnaddr());
		}
		else if (kw == "r")
		{
			output_attrencoded(cgi("redirect"));
		}
		else if (kw == "s")
		{
			output_scriptptrget();
		}
		else if (kw == "S")
		{
			output_loginscriptptr();
		}
		else if (kw == "R")
		{
			output_loginscriptptr_get();
		}
		else if (kw == "p")
		{
			output_scriptptr();
		}
		else if (kw == "P")
		{
			output_scriptptrpostinfo();
		}
		else if (kw == "f")
		{
			folder_contents_title();
		}
		else if (kw == "F")
		{
			folder_contents(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (kw == "n")
		{
			folder_initnextprev(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (kw == "N")
		{
			folder_nextprev();
		}
		else if (kw == "m")
		{
			folder_msgmove();
		}
		else if (kw == "M")
		{
			folder_showmsg(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (kw == "T")
		{
			folder_showtransfer();
		}
		else if (kw == "L")
		{
			folder_list();
		}
		else if (kw == "l")
		{
			folder_list2();
		}
		else if (kw == "E")
		{
			folder_rename_list();
		}
		else if (kw == "W")
		{
			newmsg_init(sqwebmail_folder, cgi("pos"));
		}
		else if (kw == "z")
		{
			pref_isdisplayfullmsg();
		}
		else if (kw == "y")
		{
			pref_isoldest1st();
		}
		else if (kw == "H")
		{
			pref_displayhtml();
		}
		else if (kw == "FLOWEDTEXT")
		{
			pref_displayflowedtext();
		}
		else if (kw == "NOARCHIVE")
		{
			pref_displaynoarchive();
		}
		else if (kw == "NOAUTORENAMESENT")
		{
			pref_displaynoautorenamesent();
		}
		else if (kw == "x")
		{
			pref_setprefs();
		}
		else if (kw == "w")
		{
			pref_sortorder();
		}
		else if (kw == "t")
		{
			pref_signature();
		}
		else if (kw == "u")
		{
			pref_pagesize();
		}
		else if (kw == "v")
		{
			pref_displayautopurge();
		}
		else if (kw == "A")
		{
			attachments_head(sqwebmail_folder, cgi("pos"),
				cgi("draft"));
		}
		else if (kw == "ATTACHOPTS")
		{
			attachments_opts(cgi("draft"));
		}
		else if (kw == "GPGERR")
		{
			sent_gpgerrtxt();
		}
		else if (kw == "GPGERRRESUME")
		{
			sent_gpgerrresume();
		}
#ifdef	ISPELL
		else if (kw == "K")
		{
			spell_show();
		}
#endif
		else if (kw == "B")
		{
			char banargbuf[31];
			size_t	i=0;
			int	wait_stat;
			pid_t	p, p2;

			if ((c=f.get()) != '{')
				f.unget();
			else	while ((c=f.get()), isalnum(c))
					if (i < sizeof(banargbuf)-1)
						banargbuf[i++]=c;
			banargbuf[i]=0;
			fflush(stdout);

			const char *bannerprog=getenv("SQWEBMAIL_BANNERPROG");

			if (bannerprog && *bannerprog)
			{
				if ( (p=fork()) == 0 )
				{
					execl(bannerprog, bannerprog,
							sqwebmail_formname,
							banargbuf, (char *)0);
					_exit(0);
				}
				if (p > 0)
				{
					while ((p2=wait(&wait_stat)) > 0 &&
						p2 != p)
						;
				}
			}
		}
		else if (kw == "h")
		{
			FILE *fp=fopen(LOGINDOMAINLIST, "r");

			if (fp)
			{
				/* parse LOGINDOMAINLIST and print proper output */
				print_logindomainlist(fp);
				fclose(fp);
			}
		}
		else if (kw == "o")
		{
			ldaplist();
		}
		else if (kw == "O")
		{
			doldapsearch();
		}
		else if (kw == "IMAGEURL")
		{
			printf("%s", get_imageurl());
		}
		else if (kw == "LOADMAILFILTER")
		{
			mailfilter_init();
		}
		else if (kw == "MAILFILTERLIST")
		{
			mailfilter_list();
		}
		else if (kw == "MAILFILTERLISTFOLDERS")
		{
			mailfilter_listfolders();
		}
		else if (kw == "QUOTA")
		{
			folder_showquota();
		}
		else if (kw == "NICKLIST")
		{
		        ab_listselect();
		}
		else if (kw == "LISTPUB")
		{
			gpglistpub();
		}
		else if (kw == "LISTSEC")
		{
			gpglistsec();
		}
		else if (kw == "KEYIMPORT")
		{
			folder_keyimport(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (kw == "GPGCREATE")
		{
			gpgcreate();
		}
		else if (kw == "DOGPG")
		{
			gpgdo();
		}
		else if (kw == "ATTACHPUB")
		{
			gpgselectpubkey();
		}
		else if (kw == "ATTACHSEC")
		{
			gpgselectprivkey();
		}
		else if (kw == "MAILINGLISTS")
		{
			char *p=getmailinglists();

			/* <sigh> amaya inserts a bunch of spaces that mess
			** things up in Netscape.
			*/

			output_attrencoded(p ? p:"");
			if (p)
				free(p);
		}
		else if (kw == "AUTORESPONSE")
		{
			autoresponse();
		}
		else if (kw == "AUTORESPONSE_LIST")
		{
			autoresponselist();
		}
		else if (kw == "AUTORESPONSE_PICK")
		{
			autoresponsepick();
		}
		else if (kw == "AUTORESPONSE_DELETE")
		{
			autoresponsedelete();
		}
		else if (kw == "SQWEBMAILCSS")
		{
			printf("%s/sqwebmail.css", get_imageurl());
		}
		else if (kw == "timezonelist")
		{
			timezonelist();
		}
		else if (kw == "PREFWEEK")
		{
			pref_displayweekstart();
		}
		else if (kw == "NEWEVENT")
		{
			sqpcp_newevent();
		}
		else if (kw == "RECURRING")
		{
			printf("%s", getarg("RECURRING"));
		}
		else if (kw == "EVENTSTART")
		{
			sqpcp_eventstart();
		}
		else if (kw == "EVENTEND")
		{
			sqpcp_eventend();
		}
		else if (kw == "EVENTFROM")
		{
			sqpcp_eventfrom();
		}
		else if (kw == "EVENTTIMES")
		{
			sqpcp_eventtimes();
		}
		else if (kw == "EVENTPARTICIPANTS")
		{
			sqpcp_eventparticipants();
		}
		else if (kw == "EVENTTEXT")
		{
			sqpcp_eventtext();
		}
		else if (kw == "EVENTATTACH")
		{
			sqpcp_eventattach();
		}
		else if (kw == "EVENTSUMMARY")
		{
			sqpcp_summary();
		}
		else if (kw == "CALENDARTODAY")
		{
			sqpcp_todays_date();
		}
		else if (kw == "CALENDARWEEKLYLINK")
		{
			sqpcp_weeklylink();
		}
		else if (kw == "CALENDARMONTHLYLINK")
		{
			sqpcp_monthlylink();
		}
		else if (kw == "CALENDARTODAYV")
		{
			sqpcp_todays_date_verbose();
		}
		else if (kw == "CALENDARDAYVIEW")
		{
			sqpcp_daily_view();
		}
		else if (kw == "CALENDARPREVDAY")
		{
			sqpcp_prevday();
		}
		else if (kw == "CALENDARNEXTDAY")
		{
			sqpcp_nextday();
		}
		else if (kw == "CALENDARWEEK")
		{
			sqpcp_show_cal_week();
		}
		else if (kw == "CALENDARNEXTWEEK")
		{
			sqpcp_show_cal_nextweek();
		}
		else if (kw == "CALENDARPREVWEEK")
		{
			sqpcp_show_cal_prevweek();
		}
		else if (kw == "CALENDARWEEKVIEW")
		{
			sqpcp_displayweek();
		}
		else if (kw == "CALENDARMONTH")
		{
			sqpcp_show_cal_month();
		}
		else if (kw == "CALENDARNEXTMONTH")
		{
			sqpcp_show_cal_nextmonth();
		}
		else if (kw == "CALENDARPREVMONTH")
		{
			sqpcp_show_cal_prevmonth();
		}
		else if (kw == "CALENDARMONTHVIEW")
		{
			sqpcp_displaymonth();
		}
		else if (kw == "EVENTDISPLAYINIT")
		{
			sqpcp_displayeventinit();
		}
		else if (kw == "EVENTDELETEINIT")
		{
			sqpcp_deleteeventinit();
		}
		else if (kw == "EVENTDISPLAY")
		{
			sqpcp_displayevent();
		}
		else if (kw == "EVENTBACKLINK")
		{
			sqpcp_eventbacklink();
		}
		else if (kw == "EVENTEDITLINK")
		{
			sqpcp_eventeditlink();
		}
		else if (kw == "EVENTCANCELUNCANCELLINK")
		{
			sqpcp_eventcanceluncancellink();
		}
		else if (kw == "EVENTCANCELUNCANCELLINK")
		{
			sqpcp_eventcanceluncancellink();
		}
		else if (kw == "EVENTCANCELUNCANCELIMAGE")
		{
			sqpcp_eventcanceluncancelimage();
		}
		else if (kw == "EVENTCANCELUNCANCELTEXT")
		{
			sqpcp_eventcanceluncanceltext();
		}
		else if (kw == "EVENTDELETELINK")
		{
			sqpcp_eventdeletelink();
		}
		else if (kw == "EVENTACL")
		{
			sqpcp_eventacl();
		}
		else if (kw == "ABOOKNAMELIST")
		{
			ab_addrselect();
		}
		else if (kw == "LISTRIGHTS")
			listrights();
		else if (kw == "GETACL")
			getacl();
		else if (kw == "MSGPOS")
		{
			printf("%ld", atol(cgi("pos"))+1);
		}
		else if (kw.substr(0, 6) == "radio:")
		{
			std::string_view name=kw.substr(6);
			size_t split_pos=name.find(':');
			std::string_view value=name.substr(split_pos+1);
			name=name.substr(0, split_pos);

			printf("<input type=\"radio\" name=\"");
			fwrite(name.data(), name.size(), 1, stdout);
			printf("\" value=\"");
			fwrite(value.data(), value.size(), 1, stdout);
			printf("\"");
			if ( cgi(std::string{name}.c_str()) == value)
				printf(" checked=\"checked\"");
			printf(" />");
			fflush(stdout);
		}
		else if (kw.substr(0, 9) == "checkbox:")
		{
			std::string_view name=kw.substr(9);
			size_t split_pos=name.find(':');
			std::string_view cgivar=name.substr(split_pos+1);
			name=name.substr(0, split_pos);

			printf("<input type=\"checkbox\" name=\"");
			fwrite(name.data(), name.size(), 1, stdout);
			printf("\"%s \"",
				*cgi(std::string{cgivar}.c_str())
				? " checked=\"checked\"":"");
			printf(" />");
		}
		else if (kw.substr(0, 6) == "input:")
			output_attrencoded(cgi(kwbuf+6));
		else if (kw.substr(0, 7) == "select:")
		{
			std::string_view name=kw.substr(7);
			std::string_view class_str, size;
			size_t split_pos1=name.find(':');

			if (split_pos1 != name.npos)
			{
				class_str=name.substr(split_pos1+1);
				size_t split_pos2=class_str.find(':');
				if (split_pos2 != class_str.npos)
				{
					size=class_str.substr(split_pos2+1);
					class_str=class_str.substr(
						0,
						split_pos2
					);
				}
				name=name.substr(0, split_pos1);
			}

			printf("<select name=\"");
			fwrite(name.data(), name.size(), 1, stdout);
			printf("\"");
			if (!class_str.empty())
			{
				printf(" class=\"");
				fwrite(class_str.data(), class_str.size(), 1, stdout);
				printf("\"");
			}
			if (!size.empty())
			{
				printf(" size=\"");
				fwrite(size.data(), size.size(), 1, stdout);
				printf("\"");
			}
			printf(">");
		}
		else if (kw.substr(0, 7) == "option:")
		{
			std::string_view name=kw.substr(7);
			std::string_view cgivar, cgival;
			size_t split_pos1=name.find(':');
			if (split_pos1 != name.npos)
			{
				cgivar=name.substr(split_pos1+1);
				size_t split_pos2=cgivar.find(':');
				if (split_pos2 != cgivar.npos)
				{
					cgival=cgivar.substr(split_pos2+1);
					cgivar=cgivar.substr(
						0,
						split_pos2
					);
				}
				name=name.substr(0, split_pos1);
			}

			printf("<option value=\"");
			fwrite(name.data(), name.size(), 1, stdout);
			printf("\"");
			if (!cgivar.empty() && !cgival.empty() &&
				std::string_view{
					cgi(std::string{cgivar}.c_str())
				} == cgival)
			{
				printf(" selected='selected'");
			}
			printf(">");
		}
		else if (kw == "endoption")
		{
			printf("</option>");
		}
		else if (kw == "endselect")
		{
			printf("</select>");
		}
		else if (kw.substr(0, 4) == "env:")
		{
			const char *val = getenv(kwbuf+4);
			if (val) output_attrencoded(val);
		}
		else if (kw.substr(0, 8) == "include:")
		{
			insert_include(kw.substr(8));
		}
		else if (kw == "endinclude")
		{
			break;
		}
	}
}

/* Include another template file */

static void openinclude(std::string_view p)
{
	std::string buffer;

	buffer.reserve(p.size()+10);
	buffer.append(p);
	buffer.append(".inc.html");

	if (!do_open_form(buffer, false))
		error("Can't open form template.");

	std::istream f{&*template_stack.back()};

	while (std::getline(f, buffer))
	{
		std::string_view p=buffer;

		size_t i=p.find('[');

		if (i == std::string_view::npos)
			continue;

		if (p.substr(i, 16) == "[#begininclude#]")
			break;
	}
}


/* Top level HTTP redirect without referencing a particular mailbox */

static void http_redirect_top(const char *app)
{
	const	char *p=nonloginscriptptr();

	std::string buf;
	buf.reserve(strlen(p) + strlen(app));
	buf += p;
	buf += app;
	cgiredirect(buf.c_str());
}

/* HTTP redirects within a given mailbox, various formats */

void http_redirect_argu(const char *fmt, unsigned long un)
{
char	buf[MAXLONGSIZE];

	sprintf(buf, "%lu", un);
	http_redirect_argss(fmt, buf, "");
}

void http_redirect_argss(const char *fmt, const char *arg1, const char *arg2)
{
	http_redirect_argsss(fmt, arg1, arg2, "");
}

void http_redirect_argsss(const char *fmt, const char *arg1, const char *arg2,
	const char *arg3)
{
	std::string base=scriptptrget();

	std::string args[3];

	args[0].reserve(cgi_encode::estimate(arg1));
	cgi_encode::encode(std::back_inserter(args[0]), arg1);
	args[1].reserve(cgi_encode::estimate(arg2));
	cgi_encode::encode(std::back_inserter(args[1]), arg2);
	args[2].reserve(cgi_encode::estimate(arg3));
	cgi_encode::encode(std::back_inserter(args[2]), arg3);

	/* We generate a Location: redirected_url header.  The actual
	** header is generated in cgiredirect, we just build it here */

	std::string q;
	q.reserve(base.size()+strlen(fmt)+args[0].size()+args[1].size()+
		       args[2].size());
	q=base;

	size_t i=0;

	for (const char *p=fmt; *p; p++)
	{
		if (*p == '@')
		{
			if (i < 3)
			{
				q+=args[i];
				i++;
			}
		}
		else
			q+=*p;
	}
	cgiredirect(q.c_str());
}

void output_user_form(std::string_view formname)
{
	if (formname.empty() || formname.find('.') != std::string_view::npos ||
	    formname.find('/') != std::string_view::npos)
		error("Invalid request.");

	if ((formname == "filter" || formname == "autoresponse") &&
	    !maildir::filter::has("."))
		/* Script kiddies... */
		formname="nofilter";

	if (formname == "filter" && *cgi("do.submitfilter"))
		mailfilter_submit();

	if (formname == "gpg" && libmail_gpg_has_gpg(GPGDIR))
		error("Invalid request.");

	if (formname == "gpgcreate" && libmail_gpg_has_gpg(GPGDIR))
		error("Invalid request.");

	if (*cgi("ldapsearch"))	/* Special voodoo for LDAP address book stuff */
	{
		if (ldapsearch() == 0)
		{
			output_form("ldapsearch.html");
			return;
		}
	}

	/*
	** In order to hide the session ID in the URL of the message what
	** we do is that the initial URL, that contains setcookie=1, results
	** in us setting a temporary cookie that contains the session ID,
	** then we return a redirect to a url which has /printmsg/ in the
	** PATH_INFO, instead of the session ID.  The code in main()
	** traps /printmsg/ PATH_INFO, fetches the path info from the
	** cookie, and punts after resetting setcookie to 0.
	*/

	if (formname == "print" && *cgi("setcookie") == '1')
	{
		const char *qs=getenv("QUERY_STRING");
		const char *pi=getenv("PATH_INFO");
		const char *nl;
		std::string buf;

		if (!pi)	pi="";
		if (!pi)	pi="";

		nl=nonloginscriptptr();

		buf.resize(strlen(nl) + sizeof("/printmsg/print?")+
			       strlen(qs));
		buf=nl;
		buf += "/printmsg/print?";
		buf += qs;
		cginocache();
		cgi_setcookie("sqwebmail-pi", pi);
		printf("Refresh: 0; URL=%s\n", buf.c_str());
		output_form("printredirect.html");
		return;
	}

	if (strcmp(cgi("fromscreen"), "mailfilter") == 0)
		maildir::filter::cancel(".");	/* Remove the temp file */

	if (formname == "logout")
	{
		unlink(IPFILE);
		http_redirect_top("");
		return;
	}

	if (formname == "fetch")
	{
		folder_download( sqwebmail_folder, atol(cgi("pos")),
			cgi("mimeid") );
		return;
	}

	if (formname == "delmsg")
	{
		folder_delmsg( atol(cgi("pos")));
		return;
	}

	if (formname == "donewmsg")
	{
		newmsg_do(sqwebmail_folder);
		return;
	}

	if (formname == "doattach")
	{
		doattach(sqwebmail_folder, cgi("draft"));
		return;
	}

	if (formname == "folderdel")
	{
		folder_delmsgs(sqwebmail_folder, atol(cgi("pos")));
		return;
	}
	if (formname == "spellchk")
	{
#ifdef	ISPELL
		spell_check_continue();
#else
		printf("Status: 404");
#endif
		return;
	}

	if (sqpcp_loggedin())
	{
		if (*cgi("do.neweventpreview"))
		{
			sqpcp_preview();
			return;
		}

		if (*cgi("do.neweventsave"))
		{
			sqpcp_save();
			return;
		}

		if (*cgi("do.neweventpostpone"))
		{
			sqpcp_postpone();
			return;
		}

		if (*cgi("do.neweventdeleteattach"))
		{
			sqpcp_deleteattach();
			return;
		}

		if (*cgi("do.neweventupload"))
		{
			sqpcp_uploadattach();
			return;
		}

		if (*cgi("do.neweventuppubkey"))
		{
			sqpcp_attachpubkey();
			return;
		}

		if (*cgi("do.neweventupprivkey"))
		{
			sqpcp_attachprivkey();
			return;
		}
		if (*cgi("do.eventdelete"))
		{
			sqpcp_dodelete();
			return;
		}
	}

	if (formname == "event-edit")
	{
		formname="folders";
		if (sqpcp_loggedin())
		{
			formname="eventshow";	/* default */
			if (sqpcp_eventedit() == 0)
				formname="newevent";
		}
	}


	if (formname == "open-draft")
	{
		formname="newmsg";
		if (sqpcp_has_calendar())
			/* DRAFTS may contain event files */
		{
			const char *n=cgi("draft");

			CHECKFILENAME(n);

			auto filename=maildir_find(INBOX "." DRAFTS, n);

			if (!filename.empty())
			{
				rfc822::fdstreambuf sb{
					open(filename.c_str(), O_RDONLY)
				};

				if (!sb.error())
				{
					rfc2045::entity::line_iter<false>
						::headers headers{sb};

					do
					{
						const auto &[header,v] =
							headers.name_content();

						if (header == "x-event")
						{
							formname="newevent";
							cgi_put("draftmessage",
								cgi("draft"));
							break;
						}
					} while (headers.next());
				}
			}
		}
	}

	if (formname == "newevent" ||
	    formname == "eventdaily" ||
	    formname == "eventweekly" ||
	    formname == "eventmonthly" ||
	    formname == "eventshow" ||
	    formname == "eventacl")
	{
		if (!sqpcp_has_calendar() ||
		    !sqpcp_loggedin())
			formname="folders";	/* Naughty boy */
	}

	if (*cgi("do.search"))
	{
		folder_search(sqwebmail_folder, atol(cgi("pos")));
		return;
	}
	std::string filename{formname};
	filename += ".html";
	output_form(filename.c_str());
}

#ifdef	ISPELL
extern void ispell_cleanup();
#endif

void cleanup()
{
	sqwebmail_formname = NULL;
	sqwebmail_mailboxid.clear();
	sqwebmail_folder=0;
	sqwebmail_sessiontoken.clear();
	sqwebmail_content_language.clear();
	sqwebmail_content_charset.clear();
	sqwebmail_content_locale.clear();
	sqwebmail_system_charset.clear();
	sys_locale_charset.clear();
	sqwebmail_content_ispelldict.clear();
	folder_cleanup();
	maildir_cleanup();
	mailfilter_cleanup();
#ifdef ISPELL
	ispell_cleanup();
#endif

#ifdef GZIP
	if (gzip_save_fd >= 0)	/* Restore original stdout */
	{
		dup2(gzip_save_fd, 1);
		close(gzip_save_fd);
		gzip_save_fd= -1;
	}
#endif

	libmail_gpg_cleanup();
	freeargs();
	sqpcp_close();
}



static void catch_sig(int n)
{
	n=n;
	cleanup();
	maildir_cache_cancel();
	exit(0);
}

static void setlang()
{
	if (!sqwebmail_content_locale.empty())
	{
		setenv("LANG", sqwebmail_content_locale.c_str(), 1);
	}
}

static void init_default_locale()
{
char	*templatedir=get_templatedir();
char	*cl=http11_best_content_language(templatedir,
			getenv("HTTP_ACCEPT_LANGUAGE"));

	sqwebmail_content_language=
				http11_content_language(templatedir, cl);
	sqwebmail_content_locale=
			http11_content_locale(templatedir, cl);
	sqwebmail_content_ispelldict=
			http11_content_ispelldict(templatedir, cl);
	sqwebmail_content_charset=
			http11_content_charset(templatedir, cl);

	free(cl);
#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
	setlocale(LC_ALL, sqwebmail_content_locale.c_str());
#if	USE_LIBCHARSET
	setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());
	sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
	setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());
	sqwebmail_system_charset = sys_locale_charset=nl_langinfo(CODESET);
#else
	sqwebmail_system_charset.clear();
#endif	/* USE_LIBCHARSET */
	setlocale(LC_CTYPE, "C");
	setlang();
#endif
#endif
}

void rename_sent_folder(int really)
{
	char buf[128];
	char buf2[256];
	char yyyymm[128];

	time_t t;
	struct tm *tm;

	if (really)
		(void)maildir_create(INBOX "." SENT); /* No matter what */

	time(&t);
	tm=localtime(&t);
	if (!tm)
		return;

	if (tm->tm_mon == 0)
	{
		tm->tm_mon=11;
		--tm->tm_year;
	}
	else
		--tm->tm_mon;

	if (strftime (yyyymm, sizeof(yyyymm), "%Y%m", tm) == 0)
		return;

	auto yyyymmp=read_sqconfig(".", SENTSTAMP, NULL);

	if (yyyymmp &&
	    *yyyymmp == yyyymm)
		return;

	if (strftime (buf, sizeof(buf), "%m-%b", tm) == 0)
		return;

	auto pp=folder_toutf8(buf);

	if (strftime (buf2, sizeof(buf), "." SENT ".%Y.", tm) == 0)
		return;

	strcat(buf2, pp.c_str());

	if (really)
		rename("." SENT, buf2);

	if (really)
		(void)maildir_create(INBOX "." SENT);

	write_sqconfig(".", SENTSTAMP, yyyymm);
}

static int valid_redirect();

static void redirect(const char *url)
{
	if (valid_redirect())
	{
		std::string p=url;
		for (size_t i=0; i<p.size(); ++i)
		{
			if (p[i] == '\r' || p[i] == '\n')
				p[i]=' ';
		}
		printf("Refresh: 0; URL=%s\n", p.c_str());
		output_form("redirect.html");
		return;
	}

	printf("Content-Type: text/plain\n\n"
	       "The URL you clicked on is no longer valid.\n");
	return;
}

static int valid_redirect()
{
	const char *timestamp=cgi("timestamp"), *p;
	unsigned long timestamp_n;
	time_t timestamp_t;
	time_t now;

	if (sscanf(timestamp, "%lu", &timestamp_n) != 1)
		return 0;

	timestamp_t=(time_t)timestamp_n;
	time(&now);

	if (now < timestamp_t ||
	    now > (time_t)(timestamp_t + get_timeouthard()))
		return 0;

	p=redirect_hash(timestamp);

	if (*p == 0 || strcmp(cgi("md5"), p))
		return 0;
	return 1;
}

static void main2();

static void usage()
{
	fprintf(stderr, "sqwebmaild does not accept command arguments.\n"
		"Use sqwebmaild.rc script to start sqwebmaild as a daemon.\n");
	exit(1);
}

static void call_main2(void *dummy)
{
	main2();
	cleanup();
}

int main(int argc, char **argv)
{
	const char *prefork;
	int n;

	if (argc == 3 && strcmp(argv[1], "form") == 0)
	{
		init_default_locale();
		cgi_setup();
		output_form(argv[2]);
		return 0;
	}

	courier_authdebug_login_init();

	if (argc > 1)
		usage();

	prefork=getenv("SQWEBMAIL_PREFORK");

	n=prefork ? atoi(prefork):5;

	cgi_daemon(n, SOCKFILENAME, NULL, call_main2, NULL);

	return (0);
}

static bool setuidgid(uid_t u, gid_t g, const char *dir)
{
	if (setgid(g) < 0 || setuid(u) < 0)
	{
		fprintf(stderr,
			"CRIT: Cache - can't setuid/setgid to %u/%u\n",
		       (unsigned)u, (unsigned)g);
		return (false);
	}

	if (chdir(dir))
	{
		fprintf(stderr,
			"CRIT: Cache - can't chdir to %s: %s\n", dir, strerror(errno));
		return (false);
	}
	return (true);
}

static bool validate_request(
	const char *ip_addr, time_t current_time,
	time_t timeoutsoft)
{
	time_t last_time;

	std::optional<std::string> s;
	/* Ok, boys and girls, time to validate the connection as
	** follows */

	if (sqwebmail_sessiontoken.empty()

		/* 1. Read IPFILE.  Check that it's timestamp is current enough,
		** a	nd the session hasn't timed out.
		*/

		|| !(s=read_sqconfig(".", IPFILE, &last_time)))

/*			|| last_time > current_time	*/

		return false;

	/* 2. IPFILE will contain seven words - IP address, session
	** token, language, locale, ispell dictionary,
	** timezone, charset.  Validate both.
	*/
	std::string_view sv{*s};

	size_t word=0;

	while (!sv.empty())
	{
		size_t n=sv.find_first_not_of(' ');
		if (n == std::string_view::npos)
			break;

		sv.remove_prefix(n);

		n=sv.find_first_of(' ');
		if (n == std::string_view::npos)
			n=sv.size();

		std::string_view token=sv.substr(0, n);

		sv.remove_prefix(n);

		switch (word++)
		{
			case 0:
				if (token != ip_addr && token != "none")
					return false;
				break;
			case 1:
				if (token != sqwebmail_sessiontoken)
					return false;
				break;
			case 2:
				sqwebmail_content_language=std::string{token};
				break;
			case 3:
				sqwebmail_content_locale=std::string{token};
				break;
			case 4:
				sqwebmail_content_ispelldict=std::string{token};
				break;
			case 5:
				set_timezone(token);
				break;
			case 6:
				sqwebmail_content_charset=std::string{token};
				break;
		}
	}

	if (word != 7)
		return false;

	/* 3. Check the timestamp on the TIMESTAMP file.  See if the
	** session has reached its soft timeout.
	*/

	if (!read_sqconfig(".", TIMESTAMP, &last_time))
		return false;

	if (last_time + timeoutsoft < current_time)
		return false;

	return true;
}

static void main2()
{
	const char	*ip_addr;
	std::string	pi;
	int	reset_cookie=0;
	time_t	timeouthard=get_timeouthard();


#ifdef	GZIP
	gzip_save_fd= -1;
#endif
	ip_addr=getenv("REMOTE_ADDR");

#if 0
	{
		FILE *f;

		f=fopen("/tmp/pid", "w");
		fprintf(f, "gdb /proc/%d/exe %d\n", (int)getpid(),
			(int)getpid());
		fclose(f);
		sleep(10);
	}
#endif

	/*
	 * Note: if we get a signal during FastCGI processing, this means
	 * means we need to terminate so that the webserver can respawn us.
	 * Exception is SIGPIPE which we just ignore (this is what we get
	 * if we try to write data to a client which goes away before
	 * we finished sending them the reply)
	 */

	signal(SIGHUP, catch_sig);
	signal(SIGINT, catch_sig);
	signal(SIGPIPE, catch_sig);
	signal(SIGTERM, catch_sig);

	if (!ip_addr)	ip_addr="127.0.0.1";

	umask(0077);

	maildir_cache_init(timeouthard, CACHEDIR, CACHEOWNER, authvars);

	auto pi_env=getenv("PATH_INFO");

	if (pi_env)
		pi=pi_env;

	sqpcp_init();

	if (!pi.empty() && std::string_view{pi}.substr(0, 10) == "/printmsg/")
	{
		/* See comment in output_user_form */

		pi=cgi_cookie("sqwebmail-pi");
		if (pi.empty())
		{
			if (setgid(getgid()) < 0 ||
			    setuid(getuid()) < 0)
			{
				perror("setuid/setgid");
				exit(1);
			}
			output_form("printnocookie.html");
			return;
		}
		reset_cookie=1;
		cgi_setcookie("sqwebmail-pi", "DELETED");
	}

	if (!pi.empty() && std::string_view{pi}.substr(0, 7) == "/login/")
	{
		time_t	current_time;
		time_t	timeoutsoft=get_timeoutsoft();

		/* Logging into the mailbox */

		std::string_view pi_sv{pi};
		pi_sv.remove_prefix(7);
		std::string_view u=pi_sv.substr(0, pi_sv.find('/'));
		pi_sv.remove_prefix(u.size()+1);
		sqwebmail_sessiontoken=pi_sv.substr(0, pi_sv.find('/'));
		pi_sv.remove_prefix(sqwebmail_sessiontoken.size()+1);
		std::string_view login_time_sv=pi_sv.substr(0, pi_sv.find('/'));
		login_time=0;
		while (!login_time_sv.empty() && login_time_sv[0] >= '0' && login_time_sv[0] <= '9')
		{
			login_time=login_time * 10 + (login_time_sv[0] - '0');
			login_time_sv.remove_prefix(1);
		}

		time(&current_time);

		if (!maildir_cache_search(
				u, login_time,
				[&](uid_t uid, gid_t gid, const std::string &homedir)
				{
					return setuidgid(uid, gid, homedir.c_str());
				}) ||
		    !validate_request(ip_addr, current_time, timeoutsoft) ||
		    login_time + timeouthard < current_time)
		{
			if (setgid(getgid()) < 0 ||
			    setuid(getuid()) < 0)	/* Drop root prevs */
			{
				perror("setuid/setgid");
				exit(1);
			}
			if (chdir("/") < 0)
			{
				output_form("expired.html");
				return;
			}
			cgi_setup();
			init_default_locale();

			auto shared_mungenames=
				getenv("SQWEBMAIL_SHAREDMUNGENAMES");

			maildir_info_munge_complex(
				shared_mungenames && *shared_mungenames
			);

			if (strcmp(cgi("form"), "logout") == 0)
				/* Already logged out, and the link
				** had target=_parent tag.
				*/
			{
				http_redirect_top("");
				return;
			}
			output_form("expired.html");
			return;
		}
		cgiformdatatempdir("tmp");
		cgi_setup();	/* Read CGI environment */
		if (reset_cookie)
			cgi_put("setcookie", "0");

		/* Update soft timeout stamp */

		write_sqconfig(".", TIMESTAMP, "");

		/* We must always have the folder CGI arg */

		if (!*(sqwebmail_folder=cgi("folder")))
		{
			init_default_locale();
			output_form("expired.html");
			return;
		}

		sqwebmail_mailboxid=std::string{u};

		{
			struct stat stat_buf;

			if (stat(".", &stat_buf) < 0)
			{
				output_form("expired.html");
				return;
			}

			sqwebmail_homedir_dev=stat_buf.st_dev;
			sqwebmail_homedir_ino=stat_buf.st_ino;
		}

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
		setlocale(LC_ALL, sqwebmail_content_locale.c_str());
#if	USE_LIBCHARSET
		setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());
		sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
		setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());
		sqwebmail_system_charset = sys_locale_charset
			= nl_langinfo(CODESET);
#else
		sqwebmail_system_charset.clear();
#endif  /* USE_LIBCHARSET */
		setlocale(LC_CTYPE, "C");
		setlang();
#endif
#endif
		CHECKFILENAME(sqwebmail_folder);

		strcpy(sqwebmail_folder_rights, ALL_RIGHTS);
		acl_computeRightsOnFolder(sqwebmail_folder,
					  sqwebmail_folder_rights);

		pref_init();
		(void)sqpcp_loggedin();
		if (auth_getoptionenvint("disableshared"))
		{
			maildir_acl_disabled=1;
			maildir_newshared_disabled=1;
		}

		if (strcmp(cgi("form"), "empty"))
		{
			if (*cgi("refresh"))
			{
				printf("Refresh: %ld; URL=",
				       (long)get_timeoutsoft()/2);
				output_scriptptrget();
				printf("&empty=1&refresh=1\n");
			}
		}

		output_user_form(cgi("form"));
	}
	else
		/* Must be one of those special forms */
	{
	char	*rm;
	long	n;

		if ((rm=getenv("REQUEST_METHOD")) == 0 ||
			(strcmp(rm, "POST") == 0 &&
				((rm=getenv("CONTENT_TYPE")) != 0 &&
				strncasecmp(rm,"multipart/form-data;", 20)
					== 0)))
			emsg("multipart/formdata posts not allowed","");

		/* Some additional safety checks */

		rm=getenv("CONTENT_LENGTH");
		n= rm ? atol(rm):0;
		if (n < 0 || n > 256)	enomem();

		cgi_setup();
		init_default_locale();

		auto u=trim_spaces(cgi("username"));

		if (!u.empty())
			/* Request to log in */
		{
			const char *p=cgi("password");
			const char *mailboxid;
			const char *u2=cgi("logindomain");

			std::string ubuf;

			ubuf.reserve(
				u.size()+strlen(u2)+2
			);

			ubuf=u;
			if (*u2)
			{
				ubuf+="@";
				ubuf+=u2;
			}

			maildir_cache_start();

			if (*p && (mailboxid=do_login(ubuf.c_str(), p, ip_addr))
			    != 0)
			{
				std::string q;
				const	char *saveip=ip_addr;

				sqwebmail_mailboxid=std::string{mailboxid};
				sqwebmail_folder="INBOX";
				sqwebmail_sessiontoken=random128();

				auto tz=get_timezone();
				if (*cgi("sameip") == 0)
					saveip="none";

				q.reserve(
					strlen(saveip)
					+sqwebmail_sessiontoken.size()
					+sqwebmail_content_language.size()
					+sqwebmail_content_ispelldict.size()
					+sqwebmail_content_charset.size()
					+tz.size()
					+sqwebmail_content_locale.size()+6);

				q.append(saveip);
				q.append(" ");
				q.append(sqwebmail_sessiontoken);
				q.append(" ");
				q.append(sqwebmail_content_language);
				q.append(" ");
				q.append(sqwebmail_content_locale);
				q.append(" ");
				q.append(sqwebmail_content_ispelldict);
				q.append(" ");
				q.append(tz);
				q.append(" ");
				q.append(sqwebmail_content_charset);
				write_sqconfig(".", IPFILE, q.c_str());
				time(&login_time);
				{
					char buf[1024];

					buf[sizeof(buf)-1]=0;
					if (getcwd(buf, sizeof(buf)-1) == 0)
					{
						fprintf(stderr,
						       "CRIT: getcwd() failed: %s\n",strerror(errno));
						fake_exit(1);
					} /* oops */

					maildir_cache_save(mailboxid,
							   login_time,
							   buf,
							   geteuid(), getegid()
							   );

				}
				write_sqconfig(".", TIMESTAMP, "");
#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
				setlocale(LC_ALL, sqwebmail_content_locale.c_str());
#if	USE_LIBCHARSET
				setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());
				sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
				setlocale(LC_CTYPE, sqwebmail_content_locale.c_str());

				sqwebmail_system_charset = sys_locale_charset
					= nl_langinfo(CODESET);
#else
				sqwebmail_system_charset.clear();
#endif	/* USE_LIBCHARSET */
				setlocale(LC_CTYPE, "C");
				setlang();
#endif
#endif
				pref_init();
				(void)maildir_create(INBOX "." DRAFTS);

				if (!pref_noautorenamesent)
					(void)rename_sent_folder(1);
				(void)maildir_create(INBOX "." SENT);
				(void)maildir_create(INBOX "." TRASH);
				maildir_autopurge();
				unlink(SHAREDPATHCACHE);

				sqpcp_login(ubuf.c_str(), p);
				maildir_acl_reset(".");

				http_redirect_argss(*cgi("inpublic") ?
						    "&form=folders":
						    "&form=refreshfr", "", "");
				return;
			}
			maildir_cache_cancel();

			if (setgid(getgid()) < 0 ||
			    setuid(getuid()) < 0)	/* Drop root prevs */
			{
				perror("setuid/setgid");
				exit(1);
			}
			output_form("invalid.html");	/* Invalid login */
			return;
		}

		if (setgid(getgid()) < 0 ||
		    setuid(getuid()) < 0)	/* Drop root prevs */
		{
			perror("setuid/setgid");
			exit(1);
		}

		const char *url;

		if ( *(url=cgi("redirect")))
			/* Redirection request to hide the referral tag */
		{
			redirect(url);
		}
		else if ( *cgi("noframes") == '1')
			output_form("login.html");	/* Main frame */
		else
		if ( *cgi("empty") == '1')
		{
			output_form("empty.html");	/* Minor frameset */
		}

/*
** Apparently we can't show just SCRIPT NAME as our frameset due to some
** weird bug in Communicator which, under certain conditions, will get
** confused figuring out which page views have expired.  POSTs with URLs
** referring to SCRIPT_NAME will be replied with an expiration header, and
** Communicator will assume that index.html also has expired, forcing a
** frameset reload the next time the Communicator window is resized,
** essentially logging the user off.
*/

		else if (*cgi("index") == '1')
			output_form("index.html");	/* Frameset Window */
		else
		{
			http_redirect_top("?index=1");
		}

		return;
	}
	return;
}

/* Trim leading and trailing white spaces from string */

std::string trim_spaces(const char *s)
{
	while (*s && isspace(*s))
		++s;

	std::string p=s;
	while (!p.empty() && isspace(p.back()))
		p.pop_back();

	return p;
}
