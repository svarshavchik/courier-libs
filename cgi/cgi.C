/*
** Copyright 1998 - 2026 S. Varshavchik.
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

#include	<time.h>
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif

#ifndef	CGIMAXARG
#define	CGIMAXARG	500000
#endif

#ifndef	CGIMAXFORMDATAARG
#define	CGIMAXFORMDATAARG	10000000
#endif

#if CGIMAXARG < 256
#error CGIMAXARG too small
#endif

#if CGIMAXFORMDATAARG < 1024
#error CGIMAXFORMDATAARG too small
#endif

#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"rfc2045/rfc2045.h"
#include	<utility>
#include	<algorithm>
#include	<string_view>
#include	<vector>
#include	<unordered_map>
#include	<charconv>
#include	<iostream>
#include	<tuple>

static void cgi_formdata(unsigned long);

static rfc822::fdstreambuf cgiformfd;
static rfc2045::entity message;

extern "C" void error(const char *);

static void enomem()
{
	error("Out of memory.");
}

std::unordered_map<std::string, std::vector<std::string>> cgi_arglist;

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
**	Set up CGI arguments.  Initializes cgi_arglist.
*/

void cgi_setup()
{
	const char	*p=getenv("REQUEST_METHOD");
	const char	*args;
	unsigned long cl;
	std::string cgi_args;

	if (p && strcmp(p, "GET") == 0)	/* This is a GET post */
	{
		args=getenv("QUERY_STRING");
		if (!args)	return;
		if (strlen(args) > cgi_maxarg())	enomem();
	}
	else if (p && strcmp(p, "POST") == 0)
	{
		args=getenv("CONTENT_TYPE");
		if (!args)	return;

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
		cgi_args.reserve(cl);
		while (cl)
		{
			int c=getchar();
			if (c < 0)
			{
				cgi_args.clear();
				return;
			}
			cgi_args += c;
			--cl;
		}
		args=cgi_args.c_str();
	}
	else	return;

	for (const char *q=args; *q; )
	{
		const char *p=q;
		while (*q && *q != '&')
			q++;

		std::string_view name{p, (size_t)(q-p)};
		std::string value;

		auto r=name.find('=');
		if (r != std::string_view::npos)
		{
			value=std::string{name.substr(r+1)};
			name=name.substr(0, r);
		}
		value.resize(cgiurldecode(value.data()));
		cgi_arglist[std::string{name}].push_back(std::string{value});
		if (*q)	q++;
	}
}

void cgi_cleanup()
{
	cgiformfd = rfc822::fdstreambuf{};
	message = rfc2045::entity{};
	cgi_arglist.clear();
}

const char *cgi(const char *arg)
{
	auto iter=cgi_arglist.find(arg);

	if (iter != cgi_arglist.end())
		return (iter->second[0].c_str());
	return ("");
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

size_t cgiurldecode(char *q)
{
char	*p=q, *orig=q;
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
	*p=0;
	return (p - orig);
}

void cgi_put(std::string_view name_sv, std::string_view value_sv)
{
	cgi_arglist[std::string{name_sv}]= {std::string{value_sv}};
}

/**************************************************************************/

/* multipart/formdata decoding */

static std::tuple<std::string, std::string, std::string> parse_disposition(
	const rfc2045::entity &field)
{
	std::string name, filename;

	rfc2045::entity::rfc2231_header header{
		field.content_disposition, true
	};

	auto iter=header.parameters.find("name");

	if (iter != header.parameters.end())
		name=iter->second.value;

	iter=header.parameters.find("filename");

	if (iter != header.parameters.end())
		filename=iter->second.value;

	return {header.value, name, filename};
}

static void cgiformdecode(const rfc2045::entity &field,
	rfc822::fdstreambuf &sb)
{
	auto &&[disposition, name, filename]=parse_disposition(field);
	if (disposition != "form-data" || !filename.empty())
		return;

	std::string formargbuf;

	rfc822::mime_decoder decoder{
		[&](const char *p, size_t n){
			formargbuf.append(p, n);
		},
		sb
	};
	decoder.decode_header=false;
	decoder.decode(field);

	formargbuf.erase(
		std::remove(
			formargbuf.begin(),
			formargbuf.end(),
			'\r'),
		formargbuf.end()
	);
	formargbuf.resize(cgiurldecode(formargbuf.data()));
	cgi_arglist[std::move(name)].push_back(std::move(formargbuf));
}

static const char *cgitempdir="/tmp";

void cgiformdatatempdir(const char *p)
{
	cgitempdir=p;
}

static void cgi_formdata(unsigned long contentlength)
{
	char	pidbuf[MAXLONGSIZE+1];
	char	timebuf[MAXLONGSIZE+1];
	char	cntbuf[MAXLONGSIZE+1];
	time_t	t;
	unsigned long cnt;
	std::string filename;
	char	*p;

	static const char fakeheader[]="MIME-Version: 1.0\r\nContent-Type: ";
	char	buf[BUFSIZ];

	*std::to_chars(
		pidbuf,
		pidbuf+MAXLONGSIZE,
		getpid()).ptr=0;
	time(&t);
	*std::to_chars(
		timebuf,
		timebuf+MAXLONGSIZE,
		t).ptr=0;
	cnt=0;

	buf[sizeof(buf)-1]=0;
	if (gethostname(buf, sizeof(buf)-1) != 0)
		buf[0]='\0';

	do
	{
		if (cnt > 1000)
		{
			perror("cgi_formdata: cannot create tempfile");
			fake_exit(1);
		}

		*std::to_chars(
			cntbuf,
			cntbuf+MAXLONGSIZE,
			cnt++).ptr=0;
		filename.clear();
		filename.reserve(strlen(pidbuf)+strlen(timebuf)+strlen(cntbuf)
			+strlen(cgitempdir)+strlen(buf)+10
		);
		filename += cgitempdir;
		filename += "/";
		filename += timebuf;
		filename += ".";
		filename += pidbuf;
		filename += "_";
		filename += cntbuf;
		filename += ".";
		filename += buf;
		cgiformfd=rfc822::fdstreambuf{
			open(filename.c_str(),
				O_RDWR | O_CREAT | O_EXCL, 0600)
		};
	} while (cgiformfd.error());
	unlink(filename.c_str());	/* !!!MUST WORK!!! */
	p=getenv("CONTENT_TYPE");
	cgiformfd.sputn(fakeheader, sizeof(fakeheader)-1);
	cgiformfd.sputn(p, strlen(p));
	cgiformfd.sputn("\r\n\r\n", 4);

	while (contentlength)
	{
		size_t n=sizeof(buf);
		if (n > contentlength)	n=contentlength;
		n=read(STDIN_FILENO, buf, n);
		if (n <= 0)
			enomem();
		cgiformfd.sputn(buf, n);
		contentlength -= n;
	}

	if (cgiformfd.pubseekpos(0) != 0)
		enomem();

	{
		std::istreambuf_iterator<char> b{&cgiformfd}, e;
		rfc2045::entity::line_iter<true>::iter parser{b, e};

		message.parse(parser);
	}

	for (auto &e:message.subentities)
		cgiformdecode(e, cgiformfd);
}

int cgi_getfiles( int (*start_file)(const char *, const char *, void *),
		int (*file)(const char *, size_t, void *),
		void (*end_file)(void *), size_t filenum, void *voidarg)
{
	size_t n=0;

	for (auto &e:message.subentities)
	{
		const auto &[disposition, name, filename]=parse_disposition(e);
		if (disposition != "form-data" || filename.empty())
			continue;

		if (n++ != filenum)
			continue;

		if ((*start_file)(name.c_str(), filename.c_str(), voidarg))
			continue;

		bool error=false;

		rfc822::mime_decoder decoder{
			[&](const char *p, size_t n){
				if (error)
					return;
				if ((*file)(p, n, voidarg))
					error=true;
			},
			cgiformfd
		};

		decoder.decode_header=false;
		decoder.decode(e);

		(*end_file)(voidarg);

		return 0;
	}

	return -1;
}

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

		if ((cookie_info->domain=reinterpret_cast<char *>
		     (malloc(url-p+1))) == NULL)
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

			if ((buf=reinterpret_cast<char *>(
				malloc(get_cookie_value(cookie, NULL, NULL))
			    )) == NULL)
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
