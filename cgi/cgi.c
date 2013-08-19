/*
** Copyright 1998 - 2012 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>

#if	HAVE_UNISTD_H
#include	<unistd.h>
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

#ifndef	CGIMAXARG
#define	CGIMAXARG	500000
#endif

#ifndef	CGIMAXFORMDATAARG
#define	CGIMAXFORMDATAARG	2000000
#endif

#if CGIMAXARG < 256
#error CGIMAXARG too small
#endif

#if CGIMAXFORMDATAARG < 1024
#error CGIMAXFORMDATAARG too small
#endif

#if	CGIFORMDATA

#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"rfc2045/rfc2045.h"

static void cgi_formdata(unsigned long);

#ifndef	HAVE_STRNCASECMP
extern int strncasecmp(const char *, const char *, size_t);
#endif

static int cgiformfd;
static char hascgiformfd=0;
static struct rfc2045 *rfc2045p=0;

#endif

extern void error(const char *);

static void enomem()
{
	error("Out of memory.");
}

static char *cgi_args=0;

struct cgi_arglist *cgi_arglist=0;

static size_t cgi_maxarg()
{
	const char *p=getenv("SQWEBMAIL_MAXARGSIZE");
	size_t n=0;

	if (p)
		n=atoi(p);

	if (n < CGIMAXARG)
		n=CGIMAXARG;
	return n;
}

static size_t cgi_maxformarg()
{
	const char *p=getenv("SQWEBMAIL_MAXATTSIZE");
	size_t n=0;

	if (p)
		n=atoi(p);

	if (n < CGIMAXFORMDATAARG)
		n=CGIMAXFORMDATAARG;
	return n;
}

/*
**	Set up CGI arguments.  Initializes cgi_arglist link list.
**
**	arg1<NUL>value1<NUL>arg2<NUL>value2<NUL> ... argn<NUL>valuen<NUL><NUL>
*/

static void cgi_setup_1();

void cgi_setup()
{
struct cgi_arglist *p;

	cgi_setup_1();

	if (cgi_arglist)
		cgi_arglist->prev=0;

	/* Initialize the prev pointer */

	for (p=cgi_arglist; p; p=p->next)
		if (p->next)
			p->next->prev=p;
}


static void cgi_setup_1()
{
char	*p=getenv("REQUEST_METHOD"), *q, *r;
char	*args;
unsigned long cl;
int	c;
struct cgi_arglist *argp;

	if (p && strcmp(p, "GET") == 0)	/* This is a GET post */
	{
		args=getenv("QUERY_STRING");
		if (!args)	return;
		if (strlen(args) > cgi_maxarg())	enomem();
		cgi_args=malloc(strlen(args)+1);	/* Extra insurance */
		if (!cgi_args)	return;
		strcpy(cgi_args,args);
		args=cgi_args;
	}
	else if (p && strcmp(p, "POST") == 0)
	{
		args=getenv("CONTENT_TYPE");
		if (!args)	return;

#if	CGIFORMDATA

		if (strncasecmp(args,"multipart/form-data;", 20) == 0)
		{
			args=getenv("CONTENT_LENGTH");
			if (!args)	return;
			cl=atol(args);
			if (cl > cgi_maxformarg())
			{
				printf("Content-Type: text/html\n\n");
				printf("<html><body><h1>Attachment size (%ld MB) exceeds limit set by system administrator (%ld MB)</h1></body></html>\n",
					(long)(cl / (1024 * 1024)),
					(long)(cgi_maxformarg() / (1024 * 1024)));
				fake_exit(1);
			}
			cgi_formdata(cl);
			return;
		}
#endif

		if (strncmp(args, "application/x-www-form-urlencoded", 33))
			return;
		args=getenv("CONTENT_LENGTH");
		if (!args)	return;
		cl=atol(args);
		if (cl > cgi_maxarg())
		{
			printf("Content-Type: text/html\n\n");
 			printf("<html><body><h1>Message size (%ld MB) exceeds limit set by system administrator (%ld MB)</h1></body></html>\n",
 			       (long)(cl / (1024 * 1024)),
 			       (long)(cgi_maxarg() / (1024 * 1024)));
			fake_exit(1);
		}
	cgi_args=malloc(cl+1);	/* Extra insurance */
		if (!cgi_args)	return;
		q=cgi_args;
		while (cl)
		{
			c=getchar();
			if (c < 0)
			{
				free(cgi_args);
				cgi_args=0;
				return;
			}
			*q++=c;
			--cl;
		}
		*q=0;
		args=cgi_args;
	}
	else	return;

	q=args;
	while (*q)
	{
		argp=malloc(sizeof(*cgi_arglist));
		if (!argp)	enomem();
		argp->next=cgi_arglist;
		cgi_arglist=argp;
		argp->argname=q;
		argp->argvalue="";
		p=q;
		while (*q && *q != '&')
			q++;
		if (*q)	*q++=0;
		if ((r=strchr(p, '=')) != 0)
		{
			*r++='\0';
			argp->argvalue=r;
			cgiurldecode(r);
		}
		cgiurldecode(p);
	}
}

static char *cgiurlencode_common(const char *buf, const char *punct)
{
char	*newbuf=0;
size_t	cnt=0;
int	pass;
const char *p;
static const char hex[]="0123456789ABCDEF";

	for (pass=0; pass<2; pass++)
	{
		if (pass && (newbuf=malloc(cnt+1)) == 0)	enomem();
		cnt=0;
		for (p=buf; *p; p++)
		{
			if (strchr(punct, *p) || *p < 32 || *p >= 127)
			{
				if (pass)
				{
					newbuf[cnt]='%';
					newbuf[cnt+1]=hex[
						((int)(unsigned char)*p) / 16];
					newbuf[cnt+2]=hex[ *p & 15 ];
				}
				cnt += 3;
				continue;
			}
			if (pass)
				newbuf[cnt]= *p == ' ' ? '+':*p;
			++cnt;
		}
	}
	newbuf[cnt]=0;
	return (newbuf);
}

char *cgiurlencode(const char *buf)
{
	return (cgiurlencode_common(buf, "\"?;<>&=/:%@+#"));
}

char *cgiurlencode_noamp(const char *buf)
{
	return (cgiurlencode_common(buf, "\"?<>=/:%@+#"));
}

char *cgiurlencode_noeq(const char *buf)
{
	return (cgiurlencode_common(buf, "\"?;<>&/:%@+#"));
}

void cgi_cleanup()
{
#if	CGIFORMDATA

	if (hascgiformfd)
	{
		close(cgiformfd);
		hascgiformfd=0;
	}
#endif

}

const char *cgi(const char *arg)
{
struct cgi_arglist *argp;

	for (argp=cgi_arglist; argp; argp=argp->next)
		if (strcmp(argp->argname, arg) == 0)
			return (argp->argvalue);
	return ("");
}

char *cgi_multiple(const char *arg, const char *sep)
{
struct cgi_arglist *argp;
size_t	l=1;
char	*buf;

	for (argp=cgi_arglist; argp; argp=argp->next)
		if (strcmp(argp->argname, arg) == 0)
			l += strlen(argp->argvalue)+strlen(sep);

	buf=malloc(l);
	if (!buf)	return(0);
	*buf=0;

	/*
	** Because the cgi list is build from the tail end up, we go backwards
	** now, so that we return options in the same order they were selected.
	*/

	argp=cgi_arglist;
	while (argp && argp->next)
		argp=argp->next;

	for (; argp; argp=argp->prev)
		if (strcmp(argp->argname, arg) == 0)
		{
			if (*buf)	strcat(buf, sep);
			strcat(buf, argp->argvalue);
		}
	return (buf);
}

static char *nybble(char *p, int *n)
{
	if ( *p >= '0' && *p <= '9')
		(*n) = (*n) * 16 + (*p++ - '0');
	else if ( *p >= 'A' && *p <= 'F')
		(*n) = (*n) * 16 + (*p++ - 'A' + 10);
	else if ( *p >= 'a' && *p <= 'f')
		(*n) = (*n) * 16 + (*p++ - 'a' + 10);
	return (p);
}

void cgiurldecode(char *q)
{
char	*p=q;
int	c;

	while (*q)
	{
		if (*q == '+')
		{
			*p++=' ';
			q++;
			continue;
		}
		if (*q != '%')
		{
			*p++=*q++;
			continue;
		}
		++q;
		c=0;
		q=nybble(q, &c);
		q=nybble(q, &c);

		if (c && c != '\r')
			/* Ignore CRs we get in TEXTAREAS */
			*p++=c;
	}
	*p++=0;
}

void cgi_put(const char *cginame, const char *cgivalue)
{
struct cgi_arglist *argp;

	for (argp=cgi_arglist; argp; argp=argp->next)
		if (strcmp(argp->argname, cginame) == 0)
		{
			argp->argvalue=cgivalue;
			return;
		}

	argp=malloc(sizeof(*cgi_arglist));
	if (!argp)	enomem();
	argp->next=cgi_arglist;
	argp->prev=0;
	if (argp->next)
		argp->next->prev=argp;
	cgi_arglist=argp;
	argp->argname=cginame;
	argp->argvalue=cgivalue;
}

#if	CGIFORMDATA

/**************************************************************************/

/* multipart/formdata decoding */

static char *disposition_name=NULL, *disposition_filename=NULL;

static char *formargbuf;
static char *formargptr;

static int save_formdata(const char *p, size_t l, void *miscptr)
{
	memcpy(formargptr, p, l);
	formargptr += l;
	return (0);
}

static void cgiformdecode(struct rfc2045 *p, struct rfc2045id *a, void *b)
{
off_t start_pos, end_pos, start_body;
char	buf[512];
int	n;
off_t	dummy;

	a=a;
	b=b;

	if (disposition_name)
		free(disposition_name);
	if (disposition_filename)
		free(disposition_filename);

	if (rfc2231_udecodeDisposition(p, "name", NULL, &disposition_name) < 0)
		disposition_name=NULL;

	if (rfc2231_udecodeDisposition(p, "filename", NULL,
				       &disposition_filename) < 0)
		disposition_filename=NULL;

	if (!p->content_disposition
	    || strcmp(p->content_disposition, "form-data"))	return;

	if (!disposition_name || !*disposition_name)	return;

	if (!disposition_filename || !*disposition_filename)
	{
		rfc2045_mimepos(p, &start_pos, &end_pos, &start_body,
			&dummy, &dummy);

		if (lseek(cgiformfd, start_body, SEEK_SET) == -1)
			enomem();

		formargbuf=malloc(end_pos - start_body+1);
		if (!formargbuf)	enomem();
		formargptr=formargbuf;

		rfc2045_cdecode_start(p, &save_formdata, 0);
		while (start_body < end_pos)
		{
			n=sizeof(buf);
			if (n > end_pos - start_body)
				n=end_pos-start_body;
			n=read(cgiformfd, buf, n);
			if (n <= 0)	enomem();
			rfc2045_cdecode(p, buf, n);
			start_body += n;
		}
		rfc2045_cdecode_end(p);

		*formargptr=0;
		{
			char	*name=strdup(disposition_name);
			char	*value=strdup(formargbuf);
			char	*p, *q;

			/* Just like for GET/POSTs, strip CRs. */

			for (p=q=value; *p; p++)
			{
				if (*p == '\r')	continue;
				*q++ = *p;
			}
			*q++='\0';
			cgi_put(name, value);
		}
		free(formargbuf);
	}
}

static const char *cgitempdir="/tmp";

void cgiformdatatempdir(const char *p)
{
	cgitempdir=p;
}

static void cgiformfdw(const char *p, size_t n)
{
	while (n)
	{
	int	k=write(cgiformfd, p, n);

		if (k <= 0)	enomem();
		p += k;
		n -= k;
	}
}

static void cgi_formdata(unsigned long contentlength)
{
char	pidbuf[MAXLONGSIZE];
char	timebuf[MAXLONGSIZE];
char	cntbuf[MAXLONGSIZE];
time_t	t;
unsigned long cnt;
int	n;
char	*filename, *p;

static const char fakeheader[]="MIME-Version: 1.0\nContent-Type: ";
char	buf[BUFSIZ];

	sprintf(pidbuf, "%lu", (unsigned long)getpid());
	time(&t);
	sprintf(timebuf, "%lu", (unsigned long)t);
	cnt=0;

	buf[sizeof(buf)-1]=0;
	if (gethostname(buf, sizeof(buf)-1) != 0)
		buf[0]='\0';

	do
	{
		sprintf(cntbuf, "%lu", (unsigned long)cnt);
		filename=malloc(strlen(pidbuf)+strlen(timebuf)+strlen(cntbuf)
				+strlen(cgitempdir)+strlen(buf)+10);
		if (!filename)	enomem();
		sprintf(filename, "%s/%s.%s_%s.%s", cgitempdir,
				timebuf, pidbuf, cntbuf, buf);
		cgiformfd=open(filename, O_RDWR | O_CREAT | O_EXCL, 0644);
	} while (cgiformfd < 0);
	unlink(filename);	/* !!!MUST WORK!!! */
	hascgiformfd=1;
	p=getenv("CONTENT_TYPE");
	free(filename);
	cgiformfdw(fakeheader, strlen(fakeheader));
	cgiformfdw(p, strlen(p));
	cgiformfdw("\n\n", 2);

	clearerr(stdin);

	while (contentlength)
	{
		n=sizeof(buf);
		if (n > contentlength)	n=contentlength;

		n=fread(buf, 1, n, stdin);
		if (n <= 0)
			enomem();
		cgiformfdw(buf, n);
		contentlength -= n;
	}

	rfc2045p=rfc2045_alloc();
	lseek(cgiformfd, 0L, SEEK_SET);
	while ((n=read(cgiformfd, buf, sizeof(buf))) > 0)
		rfc2045_parse(rfc2045p, buf, n);
	rfc2045_parse_partial(rfc2045p);
	rfc2045_decode(rfc2045p, &cgiformdecode, 0);

}

struct cgigetfileinfo {
	int (*start_file)(const char *, const char *, void *);
	int (*file)(const char *, size_t, void *);
	void (*end_file)(void *);
	size_t filenum;
	void *voidarg;
	} ;


static void cgifiledecode(struct rfc2045 *p, struct rfc2045id *a, void *b)
{
off_t start_pos, end_pos, start_body;
char	buf[512];
int	n;
struct cgigetfileinfo *c;
off_t	dummy;

	a=a;
	c=(struct cgigetfileinfo *)b;

	if (c->filenum == 0)	return;	/* Already retrieved this one. */

	if (disposition_name)
		free(disposition_name);
	if (disposition_filename)
		free(disposition_filename);

	if (rfc2231_udecodeDisposition(p, "name", NULL, &disposition_name) < 0
	    ||
	    rfc2231_udecodeDisposition(p, "filename", NULL,
				       &disposition_filename) < 0)
	{
		disposition_name=disposition_filename=NULL;
		enomem();
	}

	if (!p->content_disposition
	    || strcmp(p->content_disposition, "form-data"))	return;

	if (!*disposition_name)	return;

	if (!*disposition_filename)	return;

	rfc2045_mimepos(p, &start_pos, &end_pos, &start_body,
			&dummy, &dummy);

	if (start_body == end_pos)	/* NULL FILE */
			return;

	if ( --c->filenum )	return;	/* Not this one */

	if ( (*c->start_file)(disposition_name, disposition_filename,
			      c->voidarg) )
		return;

	if (lseek(cgiformfd, start_body, SEEK_SET) == -1)
		enomem();

	rfc2045_cdecode_start(p, c->file, c->voidarg);
	while (start_body < end_pos)
	{
		n=sizeof(buf);
		if (n > end_pos - start_body)
			n=end_pos-start_body;
		n=read(cgiformfd, buf, n);
		if (n <= 0)	enomem();
		rfc2045_cdecode(p, buf, n);
		start_body += n;
	}
	rfc2045_cdecode_end(p);
	(*c->end_file)(c->voidarg);
}

int cgi_getfiles( int (*start_file)(const char *, const char *, void *),
		int (*file)(const char *, size_t, void *),
		void (*end_file)(void *), size_t filenum, void *voidarg)
{
	struct cgigetfileinfo gfi;

	gfi.start_file=start_file;
	gfi.file=file;
	gfi.end_file=end_file;
	gfi.filenum=filenum;
	gfi.voidarg=voidarg;

	if (rfc2045p) rfc2045_decode(rfc2045p, &cgifiledecode, &gfi);
	if (gfi.filenum)	return (-1);
	return (0);
}

#endif

/* cookies */

int cgi_set_cookie_url(struct cgi_set_cookie_info *cookie_info,
		       const char *url)
{
	const char *p;

	if (cookie_info->domain)
		free(cookie_info->domain);
	if (cookie_info->path)
		free(cookie_info->path);

	cookie_info->secure=0;

	if (strncmp(url, "https://", 8) == 0)
		cookie_info->secure=1;

	for (p=url; *p; p++)
	{
		if (*p == ':')
		{
			url= ++p;
			break;
		}

		if (*p == '/')
			break;
	}

	if (strncmp(url, "//", 2) == 0)
	{
		p= url += 2;

		while (*url)
		{
			if (*url == '/')
				break;
			++url;
		}

		if ((cookie_info->domain=malloc(url-p+1)) == NULL)
			return -1;

		memcpy(cookie_info->domain, p, url-p);
		cookie_info->domain[url-p]=0;
	}

	if ((cookie_info->path=strdup(url)) == NULL)
		return -1;
	return 0;
}

void cgi_set_cookies(struct cgi_set_cookie_info *cookies,
		     size_t n_cookies)
{
	size_t i;
	const char *sep="";

	printf("Set-Cookie: ");

	for (i=0; i<n_cookies; i++, cookies++)
	{
		printf("%s%s=\"%s\"; ", sep, cookies->name, cookies->value);
		sep="; ";

		if (cookies->path)
			printf("Path=\"%s\"; ", cookies->path);

		if (cookies->secure)
			printf("Secure; ");

		if (cookies->age >= 0)
			printf("Max-Age=%d; ", cookies->age);
		printf("Version=1");
	}

	printf("\n");
	fflush(stdout);
}

/*
** Parse Cookie: header
**
** get_cookie_value() skips over a single cookie name=value, returning # of
** bytes, excluding quotes (if any), plus one more (for the trailing \0).
**
** out_ptr, if not NULL, receives ptr to the next byte after name=value
**
** out_value, if not NULL receives the cookie's value, excluding any quotes.
*/

static size_t get_cookie_value(const char *ptr, const char **out_ptr,
			       char *out_value)
{
	int in_quote=0;
	size_t cnt=1;

	while (*ptr)
	{
		if (!in_quote)
		{
			if (*ptr == ';' || *ptr == ',' ||
			    isspace((int)(unsigned char)*ptr))
				break;
		}

		if (*ptr == '"')
		{
			in_quote= ~in_quote;
			++ptr;
			continue;
		}

		if (out_value)
			*out_value++ = *ptr;
		++cnt;
		++ptr;
	}

	if (out_value)
		*out_value=0;

	if (out_ptr)
		*out_ptr=ptr;
	return cnt;
}

/*
** Search for a cookie.
**
** Returns NULL and sets errno=ENOENT, if cookie not found.
**
** Returns malloc-ed buffer that holds the cookie's value (or NULL if
** malloc fails).
*/

char *cgi_get_cookie(const char *cookie_name)
{
	size_t cookie_name_len=strlen(cookie_name);
	const char *cookie=getenv("HTTP_COOKIE");

	if (!cookie)
		cookie="";

	while (*cookie)
	{
		if (isspace((int)(unsigned char)*cookie) ||
		    *cookie == ';' || *cookie == ',')
		{
			++cookie;
			continue;
		}

		if (strncmp(cookie, cookie_name, cookie_name_len) == 0 &&
		    cookie[cookie_name_len] == '=')
		{
			char *buf;

			cookie += cookie_name_len;
			++cookie;

			if ((buf=malloc(get_cookie_value(cookie, NULL, NULL)))
			    == NULL)
			{
				return NULL;
			}

			get_cookie_value(cookie, NULL, buf);

			if (*buf == 0) /* Pretend not found */
			{
				free(buf);
				errno=ENOENT;
				return NULL;
			}

			return buf;
		}

		get_cookie_value(cookie, &cookie, NULL);
	}

	errno=ENOENT;
	return NULL;
}
