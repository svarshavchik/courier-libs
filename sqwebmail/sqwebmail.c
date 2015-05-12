/*
** Copyright 1998 - 2009 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
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
#include	"rfc822/rfc822hdr.h"
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

#include	"strftime.h"

extern void spell_show();
extern void spell_check_continue();
extern void print_safe(const char *);
extern void ldaplist();
extern int ldapsearch();
extern void doldapsearch();

extern void sent_gpgerrtxt();
extern void sent_gpgerrresume();
extern const char *redirect_hash(const char *);

const char *sqwebmail_mailboxid=0;
const char *sqwebmail_folder=0;

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

const char *sqwebmail_sessiontoken=0;

const char *sqwebmail_content_language=0;
const char *sqwebmail_content_locale;
const char *sqwebmail_system_charset=0;
static char *sys_locale_charset=0;

const char *sqwebmail_content_ispelldict;
const char *sqwebmail_content_charset;

dev_t sqwebmail_homedir_dev;
ino_t sqwebmail_homedir_ino;

static int noimages=0;

time_t	login_time;

extern int nochangepass();

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

struct template_stack {
	struct template_stack *next;
	FILE *fp;
} ;

static struct template_stack *template_stack=NULL;

char *trim_spaces(const char *s);

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

/* enomem() used to be just an out-of-memory handler.  Now, I use it as a
** generic failure type of a deal.
*/

void rfc2045_error(const char *p)
{
	error(p);
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

void output_attrencoded(const char *p)
{
	output_attrencoded_fp(p, stdout);
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
char	*q=cgiurlencode(p);

	printf("%s", q);
	free(q);
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
	if (sqwebmail_mailboxid)
	{
	char	*q=cgiurlencode(sqwebmail_mailboxid);
	char	buf[NUMBUFSIZE];

		printf("/login/%s/%s/%s", q,
			sqwebmail_sessiontoken ?  sqwebmail_sessiontoken:" ",
			libmail_str_time_t(login_time, buf));
		free(q);
	}
}

void output_loginscriptptr_get()
{
	output_loginscriptptr();
	if (sqwebmail_mailboxid)
	{
	char	*q=cgiurlencode(sqwebmail_mailboxid);
	char	buf[NUMBUFSIZE];

		printf("/login/%s/%s/%s", q,
			sqwebmail_sessiontoken ?  sqwebmail_sessiontoken:" ",
			libmail_str_time_t(login_time, buf));
		free(q);
	}
}

char *scriptptrget()
{
char	*q=0;
size_t	l=0;
int	i;
char	buf[NUMBUFSIZE];

#define	ADD(s) {const char *zz=(s); if (i) strcat(q, zz); l += strlen(zz);}
#define ADDE(ue) { char *yy=cgiurlencode(ue); ADD(yy); free(yy); }

	for (i=0; i<2; i++)
	{
		if (i && (q=malloc(l+1)) == 0)	enomem();
		if (i)	*q=0;
		ADD( nonloginscriptptr() );
		if (!sqwebmail_mailboxid)
		{
			ADD("?");
			continue;
		}

		ADD("/login/");
		ADDE(sqwebmail_mailboxid);
		ADD("/");
		ADD(sqwebmail_sessiontoken ? sqwebmail_sessiontoken:" ");
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
char	*p=scriptptrget();

	printf("%s", p);
	free(p);
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

void error(const char *errmsg)
{
	cginocache();
	printf("Content-Type: text/html; charset=us-ascii\n\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1></body></html>\n",
		errmsg, errmsg);
	cleanup();
	fake_exit(1);
}

void error2(const char *file, int line)
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

void error3(const char *file, int line, const char *msg1, const char *msg2, int err)
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


char *get_templatedir()
{
char	*templatedir=getenv("SQWEBMAIL_TEMPLATEDIR");
	
	if (!templatedir || !*templatedir)	templatedir=HTMLLIBDIR;

	return templatedir;
}


char *get_imageurl()
{
char	*imageurl=getenv("SQWEBMAIL_IMAGEURL");
	
	if (!imageurl || !*imageurl)	imageurl=IMGPATH;

	return imageurl;
}


FILE *open_langform(const char *lang, const char *formname,
		    int print_header)
{
char	*formpath;
FILE	*f;
char	*templatedir=get_templatedir();
	
	/* templatedir/lang/formname */

	if (!(formpath=malloc(strlen(templatedir)+3+
		strlen(lang)+strlen(formname))))
		error("Out of memory.");

	strcat(strcat(strcat(strcat(strcpy(formpath, templatedir), "/"),
		lang), "/"), formname);

	f=fopen(formpath, "r");

	free(formpath);
	
	if (f && print_header)
		printf("Content-Language: %s\n", lang);
	if (f)
		fcntl(fileno(f), F_SETFD, FD_CLOEXEC);
	return (f);
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

static void output_image( FILE *f,
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
		while ((c=getc(f)) >= 0
		       && c != '@')
			;
		while ((c=getc(f)) >= 0
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
		while ((c=getc(f)) >= 0
		       && c != '@' && c != ',')
			MKIMG(c);
		MKIMG('"');
		MKIMG(' ');
		if (c == ',')
			c=getc(f);
		while (c >= 0 && c != '@')
		{
			MKIMG(c);
			c=getc(f);
		}
		while ((c=getc(f)) >= 0 && c != '@')
			;
		MKIMG(' ');
		MKIMG('/');
		MKIMG('>');
	}
}

/* ---- time zone list ---- */

static int timezonefile( int (*callback_func)(const char *, const char *,
					      void *), void *callback_arg)
{
	FILE *f=NULL;
	char buffer[BUFSIZ];

	if (sqwebmail_content_language)
		f=open_langform(sqwebmail_content_language, "TIMEZONELIST", 0);

	if (!f)	f=open_langform(HTTP11_DEFAULTLANG, "TIMEZONELIST", 0);

	if (!f)
		return (0);

	while (fgets(buffer, sizeof(buffer), f) != NULL)
	{
		char *p=strchr(buffer, '\n');
		char *tz;
		int rc;

		if (p) *p=0;

		p=strchr(buffer, '#');
		if (p) *p=0;

		for (p=buffer; *p; p++)
			if (!isspace((int)(unsigned char)*p))
				break;

		if (!*p)
			continue;

		tz=p;
		while (*p)
		{
			if (isspace((int)(unsigned char)*p))
				break;
			++p;
		}
		if (*p) *p++=0;
		while (*p && isspace((int)(unsigned char)*p))
			++p;

		if (strcmp(p, "*") == 0)
			p="";
		if (strcmp(tz, "*") == 0)
			tz="";

		rc= (*callback_func)(tz, p, callback_arg);

		if (rc)
		{
			fclose(f);
			return (rc);
		}
	}
	fclose(f);
	return (0);
}

static int callback_timezonelist(const char *, const char *, void *);

static void timezonelist()
{
	printf("<select name=\"timezonelist\" class=\"timezonelist\">");
	timezonefile(callback_timezonelist, NULL);
	printf("</select>\n");
}

static int callback_timezonelist(const char *tz, const char *n, void *dummy)
{
	printf("<option value=\"%s\">", tz);
	output_attrencoded(n);
	printf("</option>\n");
	return (0);
}

static int set_timezone(const char *p)
{
	static char *s_buffer=0;
	char *buffer;

	if (!p || !*p || strcmp(p, "*") == 0)
		return (0);

	buffer=malloc(strlen(p)+10);
	if (!buffer)
		return (0);
	strcat(strcpy(buffer, "TZ="), p);

	putenv(buffer);

	if (s_buffer)
		free(buffer);
	s_buffer=buffer;

	return (0);
}

static int callback_get_timezone(const char *, const char *, void *);

/* Return TZ selected from login dropdown */

static char *get_timezone()
{
	char *langptr=0;

	timezonefile(callback_get_timezone, &langptr);

	if (!langptr)
	{
		langptr=strdup("");
		if (!langptr)
			enomem();
	}

	if (*langptr == 0)
	{
		free(langptr);
		langptr=strdup("*");
		if (!langptr)
			enomem();
	}

	return(langptr);
}

static int callback_get_timezone(const char *tz, const char *n, void *dummy)
{
	if (strcmp(tz, cgi("timezonelist")) == 0)
	{
		char **p=(char **)dummy;

		if (*p)
			free(*p);

		*p=strdup(tz);
	}
	return (0);
}

/* ------------------------ */

static FILE *do_open_form(const char *formname, int flag)
{
	struct template_stack *ts;
	FILE	*f=NULL;

	if ((ts=(struct template_stack *)malloc(sizeof(struct template_stack)))
	    == NULL)
		return (NULL);

	if (sqwebmail_content_language)
		f=open_langform(sqwebmail_content_language, formname, flag);
	if (!f)	f=open_langform(HTTP11_DEFAULTLANG, formname, flag);

	if (!f)
	{
		free(ts);
		return (NULL);
	}

	ts->next=template_stack;
	template_stack=ts;
	ts->fp=f;
	return (f);
}

static void do_close_form()
{
	struct template_stack *ts=template_stack;

	if (!ts)
		enomem();

	fclose(ts->fp);
	template_stack=ts->next;
	free(ts);
}

static void do_output_form_loop(FILE *);

static void fix_xml_header(FILE *f)
{
	char linebuf[80];

	/*
	** Some templates now have an <?xml > header.  Adjust the
	** encoding to match the selected default.  Yes, it's a dirty hack,
	** and I'm proud of it, since it allows me to continue editing the
	** HTML templates in Amaya.
	*/

	if (fgets(linebuf, sizeof(linebuf), f) == NULL)
		return;

	if (strncasecmp(linebuf, "<?xml version=", 14) == 0)
		sprintf(linebuf, "<?xml version=\"1.0\" encoding=\"%s\"?>\n",
			sqwebmail_content_charset);

	printf("%s", linebuf);
}

void output_form(const char *formname)
{
	FILE	*f;

#ifdef	GZIP
	int	dogzip;
	int	pipefd[2];
	pid_t	pid= -1;
#endif

	noimages= auth_getoptionenvint("wbnoimages");

	f=do_open_form(formname, 1);

	sqwebmail_formname=formname;

	if (!f)	error("Can't open form template.");

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

	if (sqwebmail_content_charset)
		printf("; charset=%s", sqwebmail_content_charset);

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

static FILE *openinclude(const char *);


void insert_include(const char *inc_name)
{
	FILE *ff=openinclude(inc_name);
	do_output_form_loop(ff);
	do_close_form();
}

static void do_output_form_loop(FILE *f)
{
	int	c, c2, c3;

	while ((c=getc(f)) >= 0)
	{
	char	kw[64];

		if (c != '[')
		{
			putchar(c);
			continue;
		}
		c=getc(f);
		if (c != '#')
		{
			putchar('[');
			ungetc(c,f);
			continue;
		}
		c=getc(f);
		if (c == '?')
		{
			c=getc(f);
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
			    (c == '4' && maildir_filter_hasmaildirfilter(".")) ||
			    (c == '5' && libmail_gpg_has_gpg(GPGDIR)) ||
			    (c == '6' && !ishttps()) ||
			    (c == '7' && !sqpcp_has_calendar()) ||
			    (c == '8' && !sqpcp_has_groupware())
			    )
			{
				while ((c=getc(f)) != EOF)
				{
					if (c != '[')	continue;
					if ( getc(f) != '#')	continue;
					if ( getc(f) != '?')	continue;
					if ( getc(f) != '#')	continue;
					if ( getc(f) == ']')	break;
				}
			}
			continue;
		}

		if (c == '$')
		{
			struct var_put_buf buf;

			buf.argp=buf.argbuf;
			buf.argn=sizeof(buf.argbuf)-1;

			while ((c=getc(f)) >= 0 && c != '\n')
			{
				if (c == '#')
				{
					c=getc(f);
					if (c == ']')	break;
					ungetc(c, f);
					c='#';
				}

				if (c == '@')
				{
					c=getc(f);
					if (c == '@')
					{
						output_image(f,
							     var_put_buf_func,
							     &buf);
						continue;
					}
					ungetc(c, f);
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
			c=getc(f);
			if (c == '#')
			{
				c=getc(f);
				if (c == ']')
					continue;
			}
			if (c != EOF)
				ungetc(c, f);
			continue;
		}

		if (!isalnum(c) && c != ':')
		{
			putchar('[');
			putchar('#');
			ungetc(c, f);
			continue;
		}
		c2=0;
		while (c != EOF && (isalnum(c) || c == ':' || c == '_'))
		{
			if (c2 < sizeof(kw)-1)
				kw[c2++]=c;
			c=getc(f);
		}
		kw[c2]=0;
		c2=c;

		if (c2 != '#')
		{
			putchar('[');
			putchar('#');
			printf("%s", kw);
			ungetc(c2, f);
			continue;
		}

		if ((c3=getc(f)) != ']')
		{
			putchar('[');
			putchar('#');
			printf("%s", kw);
			putchar(c2);
			ungetc(c3, f);
			continue;
		}

		if (strcmp(kw, "a") == 0)
		{
			addressbook();
		}
		else if (strcmp(kw, "d") == 0)
		{
			const char *f=cgi("folderdir");
			char *origc, *c;
			const char *sep="";

			origc=c=folder_fromutf7(f);

			if (*c && strcmp(c, INBOX))
			{
				printf(" - ");

				if (strcmp(c, NEWSHAREDSP) == 0 ||
				    strncmp(c, NEWSHAREDSP ".",
					    sizeof(NEWSHAREDSP)) == 0)
				{
					printf("%s", getarg("PUBLICFOLDERS"));
					sep=".";
				}

				c=strchr(c, '.');
				if (c)
				{
					printf("%s", sep);
					print_safe(c+1);
				}
			}
			free(origc);
		}
		else if (strcmp(kw, "D") == 0)
		{
			const char *p=cgi("folder");
			const char *q=strrchr(p, '.');

				if (q)
				{
				char	*r=malloc(q-p+1);

					if (!r)	enomem();
					memcpy(r, p, q-p);
					r[q-p]=0;
					output_urlencoded(r);
					free(r);
				}
		}
		else if (strcmp(kw, "G") == 0)
		{
			output_attrencoded(login_returnaddr());
		}
		else if (strcmp(kw, "r") == 0)
		{
			output_attrencoded(cgi("redirect"));
		}
		else if (strcmp(kw, "s") == 0)
		{
			output_scriptptrget();
		}
		else if (strcmp(kw, "S") == 0)
		{
			output_loginscriptptr();
		}
		else if (strcmp(kw, "R") == 0)
		{
			output_loginscriptptr_get();
		}
		else if (strcmp(kw, "p") == 0)
		{
			output_scriptptr();
		}
		else if (strcmp(kw, "P") == 0)
		{
			output_scriptptrpostinfo();
		}
		else if (strcmp(kw, "f") == 0)
		{
			folder_contents_title();
		}
		else if (strcmp(kw, "F") == 0)
		{
			folder_contents(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (strcmp(kw, "n") == 0)
		{
			folder_initnextprev(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (strcmp(kw, "N") == 0)
		{
			folder_nextprev();
		}
		else if (strcmp(kw, "m") == 0)
		{
			folder_msgmove();
		}
		else if (strcmp(kw, "M") == 0)
		{
			folder_showmsg(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (strcmp(kw, "T") == 0)
		{
			folder_showtransfer();
		}
		else if (strcmp(kw, "L") == 0)
		{
			folder_list();
		}
		else if (strcmp(kw, "l") == 0)
		{
			folder_list2();
		}
		else if (strcmp(kw, "E") == 0)
		{
			folder_rename_list();
		}
		else if (strcmp(kw, "W") == 0)
		{
			newmsg_init(sqwebmail_folder, cgi("pos"));
		}
		else if (strcmp(kw, "z") == 0)
		{
			pref_isdisplayfullmsg();
		}
		else if (strcmp(kw, "y") == 0)
		{
			pref_isoldest1st();
		}
		else if (strcmp(kw, "H") == 0)
		{
			pref_displayhtml();
		}
		else if (strcmp(kw, "FLOWEDTEXT") == 0)
		{
			pref_displayflowedtext();
		}
		else if (strcmp(kw, "NOARCHIVE") == 0)
		{
			pref_displaynoarchive();
		}
		else if (strcmp(kw, "NOAUTORENAMESENT") == 0)
		{
			pref_displaynoautorenamesent();
		}
		else if (strcmp(kw, "x") == 0)
		{
			pref_setprefs();
		}
		else if (strcmp(kw, "w") == 0)
		{
			pref_sortorder();
		}
		else if (strcmp(kw, "t") == 0)
		{
			pref_signature();
		}
		else if (strcmp(kw, "u") == 0)
		{
			pref_pagesize();
		}
		else if (strcmp(kw, "v") == 0)
		{
			pref_displayautopurge();
		}
		else if (strcmp(kw, "A") == 0)
		{
			attachments_head(sqwebmail_folder, cgi("pos"),
				cgi("draft"));
		}
		else if (strcmp(kw, "ATTACHOPTS") == 0)
		{
			attachments_opts(cgi("draft"));
		}
		else if (strcmp(kw, "GPGERR") == 0)
		{
			sent_gpgerrtxt();
		}
		else if (strcmp(kw, "GPGERRRESUME") == 0)
		{
			sent_gpgerrresume();
		}
#ifdef	ISPELL
		else if (strcmp(kw, "K") == 0)
		{
			spell_show();
		}
#endif
#ifdef	BANNERPROG
		else if (strcmp(kw, "B") == 0)
		{
			char banargbuf[31];
			int	i=0;
			int	wait_stat;
			pid_t	p, p2;

				if ((c=getc(f)) != '{')
					ungetc(c, f);
				else	while ((c=getc(f)), isalnum(c))
						if (i < sizeof(banargbuf)-1)
							banargbuf[i++]=c;
				banargbuf[i]=0;
				fflush(stdout);

				if ( (p=fork()) == 0 )
				{
					execl(BANNERPROG, BANNERPROG,
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
#endif
		else if (strcmp(kw, "h") == 0)
		{
			FILE *fp=fopen(LOGINDOMAINLIST, "r");

			if (fp) {
				/* parse LOGINDOMAINLIST and print proper output */
				print_logindomainlist(fp);
				fclose(fp);
			}
		}
		else if (strcmp(kw, "o") == 0)
		{
			ldaplist();
		}
		else if (strcmp(kw, "O") == 0)
		{
			doldapsearch();
		}
		else if (strcmp(kw, "IMAGEURL") == 0)
		{
			printf("%s", get_imageurl());
		}			
		else if (strcmp(kw, "LOADMAILFILTER") == 0)
		{
			mailfilter_init();
		}
		else if (strcmp(kw, "MAILFILTERLIST") == 0)
		{
			mailfilter_list();
		}
		else if (strcmp(kw, "MAILFILTERLISTFOLDERS") == 0)
		{
			mailfilter_listfolders();
		}
		else if (strcmp(kw, "QUOTA") == 0)
		{
			folder_showquota();
		}
		else if (strcmp(kw, "NICKLIST") == 0)
		{
		        ab_listselect();
		}
		else if (strcmp(kw, "LISTPUB") == 0)
		{
			gpglistpub();
		}
		else if (strcmp(kw, "LISTSEC") == 0)
		{
			gpglistsec();
		}
		else if (strcmp(kw, "KEYIMPORT") == 0)
		{
			folder_keyimport(sqwebmail_folder, atol(cgi("pos")));
		}
		else if (strcmp(kw, "GPGCREATE") == 0)
		{
			gpgcreate();
		}
		else if (strcmp(kw, "DOGPG") == 0)
		{
			gpgdo();
		}
		else if (strcmp(kw, "ATTACHPUB") == 0)
		{
			gpgselectpubkey();
		}
		else if (strcmp(kw, "ATTACHSEC") == 0)
		{
			gpgselectprivkey();
		}
		else if (strcmp(kw, "MAILINGLISTS") == 0)
		{
			char *p=getmailinglists();

			/* <sigh> amaya inserts a bunch of spaces that mess
			** things up in Netscape.
			*/

			output_attrencoded(p ? p:"");
			if (p)
				free(p);
		}
		else if (strcmp(kw, "AUTORESPONSE") == 0)
		{
			autoresponse();
		}
		else if (strcmp(kw, "AUTORESPONSE_LIST") == 0)
		{
			autoresponselist();
		}
		else if (strcmp(kw, "AUTORESPONSE_PICK") == 0)
		{
			autoresponsepick();
		}
		else if (strcmp(kw, "AUTORESPONSE_DELETE") == 0)
		{
			autoresponsedelete();
		}
		else if (strcmp(kw, "SQWEBMAILCSS") == 0)
		{
			printf("%s/sqwebmail.css", get_imageurl());
		}
		else if (strcmp(kw, "timezonelist") == 0)
		{
			timezonelist();
		}
		else if (strcmp(kw, "PREFWEEK") == 0)
		{
			pref_displayweekstart();
		}
		else if (strcmp(kw, "NEWEVENT") == 0)
		{
			sqpcp_newevent();
		}
		else if (strcmp(kw, "RECURRING") == 0)
		{
			printf("%s", getarg("RECURRING"));
		}
		else if (strcmp(kw, "EVENTSTART") == 0)
		{
			sqpcp_eventstart();
		}
		else if (strcmp(kw, "EVENTEND") == 0)
		{
			sqpcp_eventend();
		}
		else if (strcmp(kw, "EVENTFROM") == 0)
		{
			sqpcp_eventfrom();
		}
		else if (strcmp(kw, "EVENTTIMES") == 0)
		{
			sqpcp_eventtimes();
		}
		else if (strcmp(kw, "EVENTPARTICIPANTS") == 0)
		{
			sqpcp_eventparticipants();
		}
		else if (strcmp(kw, "EVENTTEXT") == 0)
		{
			sqpcp_eventtext();
		}
		else if (strcmp(kw, "EVENTATTACH") == 0)
		{
			sqpcp_eventattach();
		}
		else if (strcmp(kw, "EVENTSUMMARY") == 0)
		{
			sqpcp_summary();
		}
		else if (strcmp(kw, "CALENDARTODAY") == 0)
		{
			sqpcp_todays_date();
		}
		else if (strcmp(kw, "CALENDARWEEKLYLINK") == 0)
		{
			sqpcp_weeklylink();
		}
		else if (strcmp(kw, "CALENDARMONTHLYLINK") == 0)
		{
			sqpcp_monthlylink();
		}
		else if (strcmp(kw, "CALENDARTODAYV") == 0)
		{
			sqpcp_todays_date_verbose();
		}
		else if (strcmp(kw, "CALENDARDAYVIEW") == 0)
		{
			sqpcp_daily_view();
		}
		else if (strcmp(kw, "CALENDARPREVDAY") == 0)
		{
			sqpcp_prevday();
		}
		else if (strcmp(kw, "CALENDARNEXTDAY") == 0)
		{
			sqpcp_nextday();
		}
		else if (strcmp(kw, "CALENDARWEEK") == 0)
		{
			sqpcp_show_cal_week();
		}
		else if (strcmp(kw, "CALENDARNEXTWEEK") == 0)
		{
			sqpcp_show_cal_nextweek();
		}
		else if (strcmp(kw, "CALENDARPREVWEEK") == 0)
		{
			sqpcp_show_cal_prevweek();
		}
		else if (strcmp(kw, "CALENDARWEEKVIEW") == 0)
		{
			sqpcp_displayweek();
		}
		else if (strcmp(kw, "CALENDARMONTH") == 0)
		{
			sqpcp_show_cal_month();
		}
		else if (strcmp(kw, "CALENDARNEXTMONTH") == 0)
		{
			sqpcp_show_cal_nextmonth();
		}
		else if (strcmp(kw, "CALENDARPREVMONTH") == 0)
		{
			sqpcp_show_cal_prevmonth();
		}
		else if (strcmp(kw, "CALENDARMONTHVIEW") == 0)
		{
			sqpcp_displaymonth();
		}
		else if (strcmp(kw, "EVENTDISPLAYINIT") == 0)
		{
			sqpcp_displayeventinit();
		}
		else if (strcmp(kw, "EVENTDELETEINIT") == 0)
		{
			sqpcp_deleteeventinit();
		}
		else if (strcmp(kw, "EVENTDISPLAY") == 0)
		{
			sqpcp_displayevent();
		}
		else if (strcmp(kw, "EVENTBACKLINK") == 0)
		{
			sqpcp_eventbacklink();
		}
		else if (strcmp(kw, "EVENTEDITLINK") == 0)
		{
			sqpcp_eventeditlink();
		}
		else if (strcmp(kw, "EVENTCANCELUNCANCELLINK") == 0)
		{
			sqpcp_eventcanceluncancellink();
		}
		else if (strcmp(kw, "EVENTCANCELUNCANCELLINK") == 0)
		{
			sqpcp_eventcanceluncancellink();
		}
		else if (strcmp(kw, "EVENTCANCELUNCANCELIMAGE") == 0)
		{
			sqpcp_eventcanceluncancelimage();
		}
		else if (strcmp(kw, "EVENTCANCELUNCANCELTEXT") == 0)
		{
			sqpcp_eventcanceluncanceltext();
		}
		else if (strcmp(kw, "EVENTDELETELINK") == 0)
		{
			sqpcp_eventdeletelink();
		}
		else if (strcmp(kw, "EVENTACL") == 0)
		{
			sqpcp_eventacl();
		}
		else if (strcmp(kw, "ABOOKNAMELIST") == 0)
		{
			ab_addrselect();
		}
		else if (strcmp(kw, "LISTRIGHTS") == 0)
			listrights();
		else if (strcmp(kw, "GETACL") == 0)
			getacl();
		else if (strcmp(kw, "MSGPOS") == 0)
		{
			printf("%ld", atol(cgi("pos"))+1);
		}
		else if (strncmp(kw, "radio:", 6) == 0)
		{
		const char *name=strtok(kw+6, ":");
		const char *value=strtok(0, ":");

			if (name && value)
			{
				printf("<input type=\"radio\" name=\"%s\""
					" value=\"%s\"",
					name, value);
				if ( strcmp(cgi(name), value) == 0)
					printf(" checked=\"checked\"");
				printf(" />");
			}
		}
		else if (strncmp(kw, "checkbox:", 9) == 0)
		{
		const char *name=strtok(kw+9, ":");
		const char *cgivar=strtok(0, ":");

			if (name && cgivar)
			{
				printf("<input type=\"checkbox\" name=\"%s\""
					"%s />",
					name,
					*cgi(cgivar) ? " checked=\"checked\"":"");
			}
		}
		else if (strncmp(kw, "input:", 6) == 0)
		{
			output_attrencoded(cgi(kw+6));
		}
		else if (strncmp(kw, "select:", 7) == 0)
		{
		const char *name=strtok(kw+7, ":");
		const char *class=strtok(0, ":");
		const char *size=strtok(0, ":");

			printf("<select name=\"%s\"", name ? name:"");
			if (class)	printf(" class=\"%s\"", class);
			if (size)	printf(" size=\"%s\"", size);
			printf(">");
		}
		else if (strncmp(kw, "option:", 7) == 0)
		{
		const char *name=strtok(kw+7, ":");
		const char *cgivar=strtok(0, ":");
		const char *cgival=strtok(0, ":");

			printf("<option value=\"%s\"", name ? name:"");
			if (cgivar && cgival &&
				strcmp(cgi(cgivar), cgival) == 0)
				printf(" selected='selected'");
			printf(">");
		}
		else if (strcmp(kw, "endoption") == 0)
			printf("</option>");
		else if (strcmp(kw, "endselect") == 0)
			printf("</select>");
		else if (strncmp(kw, "env:", 4) == 0) {
			const char *val = getenv(kw+4);
			if (val) output_attrencoded(val);
		}
		else if (strncmp(kw, "include:", 8) == 0)
		{
			insert_include(kw+8);
		}
		else if (strcmp(kw, "endinclude") == 0)
		{
			break;
		}
	}
}

/* Include another template file */

static FILE *openinclude(const char *p)
{
	char buffer[BUFSIZ];
	FILE *f;

	buffer[0]=0;
	strncat(buffer, p, 100);
	strcat(buffer, ".inc.html");

	f=do_open_form(buffer, 0);

	if (!f)
		error("Can't open form template.");

	while (fgets(buffer, sizeof(buffer), f))
	{
		const char *p=strchr(buffer, '[');

		if (!p)
			continue;

		if (strncmp(p, "[#begininclude#]", 16) == 0)
		{
			break;
		}
	}
	return (f);
}


/* Top level HTTP redirect without referencing a particular mailbox */

static void http_redirect_top(const char *app)
{
const	char *p=nonloginscriptptr();
char	*buf=malloc(strlen(p)+strlen(app)+2);

	if (!buf)	enomem();
	strcat(strcpy(buf, p), app);
	cgiredirect(buf);
	free(buf);
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
char *base=scriptptrget();
char *arg1s=cgiurlencode(arg1);
char *arg2s=cgiurlencode(arg2);
char *arg3s=cgiurlencode(arg3);
char *q;

	/* We generate a Location: redirected_url header.  The actual
	** header is generated in cgiredirect, we just build it here */

	q=malloc(strlen(base)+strlen(fmt)+strlen(arg1s)+strlen(arg2s)+
			strlen(arg3s)+1);
	if (!q)	enomem();
	strcpy(q, base);
	sprintf(q+strlen(q), fmt, arg1s, arg2s, arg3s);
	cgiredirect(q);
	free(q);
	free(arg1s);
	free(arg2s);
	free(arg3s);
	free(base);
}

void output_user_form(const char *formname)
{
char	*p;

	if (!*formname || strchr(formname, '.') || strchr(formname, '/'))
		error("Invalid request.");

	if ((strcmp(formname, "filter") == 0
	     || strcmp(formname, "autoresponse") == 0)
	    && maildir_filter_hasmaildirfilter("."))
		/* Script kiddies... */
		formname="nofilter";

	if (strcmp(formname, "filter") == 0 && *cgi("do.submitfilter"))
		mailfilter_submit();

	if (strcmp(formname, "gpg") == 0 && libmail_gpg_has_gpg(GPGDIR))
		error("Invalid request.");

	if (strcmp(formname, "gpgcreate") == 0 && libmail_gpg_has_gpg(GPGDIR))
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

	if (strcmp(formname, "print") == 0 && *cgi("setcookie") == '1')
	{
	const char *qs=getenv("QUERY_STRING");
	const char *pi=getenv("PATH_INFO");
	const char *nl;
	char	*buf;

		if (!pi)	pi="";
		if (!pi)	pi="";

		nl=nonloginscriptptr();

		buf=malloc(strlen(nl) + sizeof("/printmsg/print?")+strlen(qs));
		if (!buf)	enomem();
		strcat(strcat(strcpy(buf, nl), "/printmsg/print?"), qs);
		cginocache();
		cgi_setcookie("sqwebmail-pi", pi);
		printf("Refresh: 0; URL=%s\n", buf);
		free(buf);
		output_form("printredirect.html");
		return;
	}

	if (strcmp(cgi("fromscreen"), "mailfilter") == 0)
		maildir_filter_endmaildirfilter(".");	/* Remove the temp file */

	if (strcmp(formname, "logout") == 0)
	{
		unlink(IPFILE);
		http_redirect_top("");
		return;
	}

	if (strcmp(formname, "fetch") == 0)
	{
		folder_download( sqwebmail_folder, atol(cgi("pos")),
			cgi("mimeid") );
		return;
	}

	if (strcmp(formname, "delmsg") == 0)
	{
		folder_delmsg( atol(cgi("pos")));
		return;
	}

	if (strcmp(formname, "donewmsg") == 0)
	{
		newmsg_do(sqwebmail_folder);
		return;
	}

	if (strcmp(formname, "doattach") == 0)
	{
		doattach(sqwebmail_folder, cgi("draft"));
		return;
	}

	if (strcmp(formname, "folderdel") == 0)
	{
		folder_delmsgs(sqwebmail_folder, atol(cgi("pos")));
		return;
	}
	if (strcmp(formname, "spellchk") == 0)
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

	if (strcmp(formname, "event-edit") == 0)
	{
		formname="folders";
		if (sqpcp_loggedin())
		{
			formname="eventshow";	/* default */
			if (sqpcp_eventedit() == 0)
				formname="newevent";
		}
	}


	if (strcmp(formname, "open-draft") == 0)
	{
		formname="newmsg";
		if (sqpcp_has_calendar())
			/* DRAFTS may contain event files */
		{
			const char *n=cgi("draft");
			char *filename;
			FILE *fp;

			CHECKFILENAME(n);

			filename=maildir_find(INBOX "." DRAFTS, n);

			if (filename)
			{
				if ((fp=fopen(filename, "r")) != NULL)
				{
					struct rfc822hdr h;

					rfc822hdr_init(&h, 8192);

					while (rfc822hdr_read(&h, fp, NULL, 0)
					       == 0)
					{
						if (strcasecmp(h.header,
							       "X-Event") == 0)
						{
							formname="newevent";
							cgi_put("draftmessage",
								cgi("draft"));
							break;
						}
					}
					rfc822hdr_free(&h);
					fclose(fp);
				}
				free(filename);
			}
		}
	}

	if (strcmp(formname, "newevent") == 0 ||
	    strcmp(formname, "eventdaily") == 0 ||
	    strcmp(formname, "eventweekly") == 0 ||
	    strcmp(formname, "eventmonthly") == 0 ||
	    strcmp(formname, "eventshow") == 0 ||
	    strcmp(formname, "eventacl") == 0)
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
	p=malloc(strlen(formname)+6);
	if (!p)	enomem();

	strcat(strcpy(p, formname),".html");
	output_form(p);
	free(p);
}


extern void folder_cleanup();
extern void maildir_cleanup();
extern void mailfilter_cleanup();

#ifdef	ISPELL
extern void ispell_cleanup();
#endif

void cleanup()
{
	sqwebmail_formname = NULL;
	sqwebmail_mailboxid=0;
	sqwebmail_folder=0;
	sqwebmail_sessiontoken=0;
	sqwebmail_content_language=0;
	sqwebmail_content_locale=0;
	sqwebmail_system_charset=0;
	if (sys_locale_charset)
		free(sys_locale_charset);
	sys_locale_charset=0;
	sqwebmail_content_ispelldict=0;
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



static RETSIGTYPE catch_sig(int n)
{
	n=n;
	cleanup();
	maildir_cache_cancel();
	exit(0);
}

static void setlang()
{
	static char *lang_buf=0;
	char *p;

	if (sqwebmail_content_locale && *sqwebmail_content_locale
	    && (p=malloc(sizeof("LANG=")+strlen(sqwebmail_content_locale)))!=0)
	{
		strcat(strcpy(p, "LANG="), sqwebmail_content_locale);
		putenv(p);
		if (lang_buf)
			free(lang_buf);
		lang_buf=p;
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
	setlocale(LC_ALL, sqwebmail_content_locale);
#if	USE_LIBCHARSET
	setlocale(LC_CTYPE, sqwebmail_content_locale);
	sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
	setlocale(LC_CTYPE, sqwebmail_content_locale);
	sqwebmail_system_charset = sys_locale_charset=strdup(nl_langinfo(CODESET));
#else
	sqwebmail_system_charset = NULL;
#endif	/* USE_LIBCHARSET */
	setlocale(LC_CTYPE, "C");
	setlang();
#endif
#endif
}

void rename_sent_folder(int really)
{
	char buf[128];
	char yyyymm[128];
	const char *yyyymmp;

	time_t t;
	struct tm *tm;
	char *pp;

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

	if ((yyyymmp=read_sqconfig(".", SENTSTAMP, NULL)) != NULL &&
	    strcmp(yyyymm, yyyymmp) == 0)
		return;

	if (strftime (buf, sizeof(buf), "." SENT ".%Y.%m-%b", tm) == 0)
		return;

	pp=folder_toutf7(buf);

	if (really)
		rename("." SENT, pp);
	free(pp);
	if (really)
		(void)maildir_create(INBOX "." SENT);

	write_sqconfig(".", SENTSTAMP, yyyymm);
}

static int valid_redirect();

static void redirect(const char *url)
{
	if (valid_redirect())
	{
		printf("Refresh: 0; URL=%s\n", url);
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

	if (now < timestamp_t || now > timestamp_t + get_timeouthard())
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

#if 0
	if (getenv("SQWEBMAIL_DEBUG"))
	{
		main2();
		return (0);
	}
#endif

	courier_authdebug_login_init();

	if (argc > 1)
		usage();

	prefork=getenv("SQWEBMAIL_PREFORK");

	n=prefork ? atoi(prefork):5;

	cgi_daemon(n, SOCKFILENAME, NULL, call_main2, NULL);

	return (0);
}

static int setuidgid(uid_t u, gid_t g, const char *dir, void *dummy)
{
	if (setgid(g) < 0 || setuid(u) < 0)
	{
		fprintf(stderr,
			"CRIT: Cache - can't setuid/setgid to %u/%u\n",
		       (unsigned)u, (unsigned)g);
		return (-1);
	}

	if (chdir(dir))
	{
		fprintf(stderr,
			"CRIT: Cache - can't chdir to %s: %s\n", dir, strerror(errno));
		return (-1);
	}
	return (0);
}

static void main2()
{
const char	*u;
const char	*ip_addr;
char	*pi;
char	*pi_malloced;
int	reset_cookie=0;
time_t	timeouthard=get_timeouthard();


#ifdef	GZIP
	gzip_save_fd= -1;
#endif
	u=ip_addr=pi=NULL;

	ip_addr=getenv("REMOTE_ADDR");

#if 0
	{
		FILE *f;

		f=fopen("/tmp/pid", "w");
		fprintf(f, "%d\n", (int)getpid());
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

	{
		timeouthard=get_timeouthard();
	}

	if (maildir_cache_init(timeouthard, CACHEDIR, CACHEOWNER, authvars))
	{
		printf("Content-Type: text/plain\n\nmaildir_cache_init() failed\n");
		fake_exit(0);
	}

	pi=getenv("PATH_INFO");

	pi_malloced=0;
	sqpcp_init();

	if (pi && strncmp(pi, "/printmsg/", 10) == 0)
	{
		/* See comment in output_user_form */

		pi_malloced=pi=cgi_cookie("sqwebmail-pi");
		if (*pi_malloced == 0)
		{
			free(pi_malloced);
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

	if (pi && strncmp(pi, "/login/", 7) == 0)
	{
	const char	*p;
	time_t	last_time, current_time;
	char	*q;
	time_t	timeoutsoft=get_timeoutsoft();

		/* Logging into the mailbox */

		pi=strdup(pi);
		if (pi_malloced)	free(pi_malloced);

		if (!pi)	enomem();

		(void)strtok(pi, "/");	/* Skip login */
		u=strtok(NULL, "/");	/* mailboxid */
		sqwebmail_sessiontoken=strtok(NULL, "/"); /* sessiontoken */
		q=strtok(NULL, "/");	/* login time */
		login_time=0;
		while (q && *q >= '0' && *q <= '9')
			login_time=login_time * 10 + (*q++ - '0');

		if (maildir_cache_search(u, login_time, setuidgid, NULL)
		    && prelogin(u))
		{
			free(pi);
			error("Unable to access your mailbox, sqwebmail permissions may be wrong.");
		}

		time(&current_time);

		/* Ok, boys and girls, time to validate the connection as
		** follows */

		if (	!sqwebmail_sessiontoken

		/* 1. Read IPFILE.  Check that it's timestamp is current enough,
		** and the session hasn't timed out.
		*/

			|| !(p=read_sqconfig(".", IPFILE, &last_time))

/*			|| last_time > current_time	*/

			|| last_time + timeouthard < current_time

		/* 2. IPFILE will contain seven words - IP address, session
		** token, language, locale, ispell dictionary,
		** timezone, charset.  Validate both.
		*/
			|| !(q=strdup(p))
			|| !(p=strtok(q, " "))
			|| (strcmp(p, ip_addr) && strcmp(p, "none"))
			|| !(p=strtok(NULL, " "))
			|| strcmp(p, sqwebmail_sessiontoken)
			|| !(p=strtok(NULL, " "))
			|| !(sqwebmail_content_language=strdup(p))
			|| !(p=strtok(NULL, " "))
			|| !(sqwebmail_content_locale=strdup(p))
			|| !(p=strtok(NULL, " "))
			|| !(sqwebmail_content_ispelldict=strdup(p))
			|| !(p=strtok(NULL, " "))
			|| set_timezone(p)
			|| !(p=strtok(NULL, " "))
			|| !(sqwebmail_content_charset=strdup(p))

		/* 3. Check the timestamp on the TIMESTAMP file.  See if the
		** session has reached its soft timeout.
		*/

			|| !read_sqconfig(".", TIMESTAMP, &last_time)

/*			|| last_time > current_time	*/

			|| last_time + timeoutsoft < current_time)
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
			free(pi);

			u=getenv("SQWEBMAIL_SHAREDMUNGENAMES");

			maildir_info_munge_complex(u && *u);

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
		free(q);
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
			free(pi);
			return;
		}

		sqwebmail_mailboxid=u;

		{
			struct stat stat_buf;

			if (stat(".", &stat_buf) < 0)
			{
				output_form("expired.html");
				free(pi);
				return;
			}

			sqwebmail_homedir_dev=stat_buf.st_dev;
			sqwebmail_homedir_ino=stat_buf.st_ino;
		}

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
		setlocale(LC_ALL, sqwebmail_content_locale);
#if	USE_LIBCHARSET
		setlocale(LC_CTYPE, sqwebmail_content_locale);
		sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
		setlocale(LC_CTYPE, sqwebmail_content_locale);
		sqwebmail_system_charset = sys_locale_charset
			= strdup(nl_langinfo(CODESET));
#else
		sqwebmail_system_charset = NULL;
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
		free(pi);
	}
	else
		/* Must be one of those special forms */
	{
	char	*rm;
	long	n;

		if (pi_malloced)	free(pi_malloced);

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

		if (*(u=trim_spaces(cgi("username"))))
			/* Request to log in */
		{
		const char *p=cgi("password");
		const char *mailboxid;
		const char *u2=cgi("logindomain");
		char	*ubuf=malloc(strlen(u)+strlen(u2)+2);

			if (ubuf == NULL) enomem();
			strcpy(ubuf, u);
			if (*u2)
				strcat(strcat(ubuf, "@"), u2);

			maildir_cache_start();

			if (*p && (mailboxid=do_login(ubuf, p, ip_addr))
			    != 0)
			{
				char	*q;
				const	char *saveip=ip_addr;
				char	*tz;

				sqwebmail_mailboxid=mailboxid;
				sqwebmail_folder="INBOX";
				sqwebmail_sessiontoken=random128();

				tz=get_timezone();
				if (*cgi("sameip") == 0)
					saveip="none";

				q=malloc(strlen(saveip)
					 +strlen(sqwebmail_sessiontoken)
					 +strlen(sqwebmail_content_language)
					 +strlen(sqwebmail_content_ispelldict)
					 +strlen(sqwebmail_content_charset)
					 +strlen(tz)
					 +strlen(sqwebmail_content_locale)+7);
				if (!q)	enomem();
				sprintf(q, "%s %s %s %s %s %s %s", saveip,
					sqwebmail_sessiontoken,
					sqwebmail_content_language,
					sqwebmail_content_locale,
					sqwebmail_content_ispelldict,
					tz,
					sqwebmail_content_charset);
				write_sqconfig(".", IPFILE, q);
				free(q);
				free(tz);
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
				setlocale(LC_ALL, sqwebmail_content_locale);
#if	USE_LIBCHARSET
				setlocale(LC_CTYPE, sqwebmail_content_locale);
				sqwebmail_system_charset = locale_charset();
#elif	HAVE_LANGINFO_CODESET
				setlocale(LC_CTYPE, sqwebmail_content_locale);

				sqwebmail_system_charset = sys_locale_charset
					= strdup(nl_langinfo(CODESET));
#else
				sqwebmail_system_charset = NULL;
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

				sqpcp_login(ubuf, p);
				maildir_acl_reset(".");

				http_redirect_argss(*cgi("inpublic") ?
						    "&form=folders":
						    "&form=refreshfr", "", "");
				free(ubuf);
				return;
			}
			maildir_cache_cancel();

			free(ubuf);
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

		if ( *(u=cgi("redirect")))
			/* Redirection request to hide the referral tag */
		{
			redirect(u);
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

#ifdef	malloc

#undef	malloc
#undef	realloc
#undef	free
#undef	strdup
#undef	calloc

static void *allocp[1000];

extern void *malloc(size_t), *realloc(void *, size_t), free(void *),
	*calloc(size_t, size_t);
extern char *strdup(const char *);

char *my_strdup(const char *c)
{
size_t	i;

	for (i=0; i<sizeof(allocp)/sizeof(allocp[0]); i++)
		if (!allocp[i])
			return (allocp[i]=strdup(c));
	abort();
	return (0);
}

void *my_malloc(size_t n)
{
size_t	i;

	for (i=0; i<sizeof(allocp)/sizeof(allocp[0]); i++)
		if (!allocp[i])
			return (allocp[i]=malloc(n));
	abort();
	return (0);
}

void *my_calloc(size_t a, size_t b)
{
size_t	i;

	for (i=0; i<sizeof(allocp)/sizeof(allocp[0]); i++)
		if (!allocp[i])
			return (allocp[i]=calloc(a,b));
	abort();
	return (0);
}

void *my_realloc(void *p, size_t s)
{
size_t	i;

	for (i=0; i<sizeof(allocp)/sizeof(allocp[0]); i++)
		if (p && allocp[i] == p)
		{
		void	*q=realloc(p, s);

			if (q)	allocp[i]=q;
			return (q);
		}
	abort();
}

void my_free(void *p)
{
size_t i;

	for (i=0; i<sizeof(allocp)/sizeof(allocp[0]); i++)
		if (p && allocp[i] == p)
		{
			free(p);
			allocp[i]=0;
			return;
		}
	abort();
}
#endif

/* Trim leading and trailing white spaces from string */

char *trim_spaces(const char *s)
{
	char *p, *q;

	p=strdup(s);
	if (!p)
		enomem();

	if (*p)
	{
		for (q=p+strlen(p)-1; q >= p && isspace(*q); q--)
			*q=0;

		for (q=p; *q && isspace(*q); q++)
			;
		if (p != q)
			p=q;
	}

	return (p);
}

