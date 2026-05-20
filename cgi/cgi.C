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

	rfc2231::header header{field.content_disposition, true};

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

	rfc2045::mime_decoder decoder{
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

		rfc2045::mime_decoder decoder{
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

void cgi_set_cookie_info::set_from_url(std::string_view url)
{
	domain.clear();
	path.clear();
	secure=false;

	if (url.substr(0, 8) == "https://")
		secure=true;

	for (const char &c:url)
	{
		if (c == ':')
		{
			url.remove_prefix(&c-url.data()+1);
			break;
		}

		if (c == '/')
			break;
	}

	if (url.substr(0, 2) == "//")
	{
		url.remove_prefix(2);

		auto slash=url.find('/');

		if (slash != url.npos)
		{
			domain=static_cast<std::string>(
				url.substr(1, slash-1)
			);

			url.remove_prefix(slash);
		}
	}

	path=static_cast<std::string>(url);
}

void cgi_set_cookies(const std::vector<cgi_set_cookie_info> &cookies)
{
	const char *sep="";

	printf("Set-Cookie: ");

	for (auto &cookie:cookies)
	{
		printf("%s%s=\"%s\"; ", sep, cookie.name.c_str(),
		       cookie.value.c_str());
		sep="; ";

		if (!cookie.path.empty())
			printf("Path=\"%s\"; ", cookie.path.c_str());

		if (cookie.secure)
			printf("Secure; ");

		if (cookie.age >= 0)
			printf("Max-Age=%d; ", cookie.age);
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

static size_t get_cookie_value_len(std::string_view cookie,
	size_t *size_ret, std::string *ret)
{
	bool in_quote=false;
	size_t cnt=0;
	size_t dummy=0;

	if (!size_ret)
		size_ret=&dummy;

	for (size_t i=0; i<cookie.size(); ++i)
	{
		const char c=cookie[i];

		if (!in_quote)
		{
			switch (c)
			{
				case ' ': case '\t': case '\n': case '\r':
				case ';': case ',':
					*size_ret=i;
					return cnt;
			}
		}

		if (c == '"')
			in_quote= !in_quote;
		else
		{
			if (ret)
				ret->push_back(c);
			++cnt;
		}
	}

	*size_ret=cookie.size();
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

std::string cgi_get_cookie(std::string_view cookie_name)
{
	size_t cookie_name_len=cookie_name.size();
	const char *cookie_cstr=getenv("HTTP_COOKIE");

	if (!cookie_cstr)
		return "";

	std::string_view cookie{cookie_cstr};

	while (!cookie.empty())
	{
		switch (cookie.front())
		{
			case ' ': case '\t': case '\n': case '\r':
			case ';': case ',':
				cookie.remove_prefix(1);
				continue;
		}

		if (cookie.size() > cookie_name_len &&
		    cookie.substr(0, cookie_name_len) == cookie_name &&
		    cookie[cookie_name_len] == '=')
		{
			cookie.remove_prefix(cookie_name_len + 1);

			std::string value;

			value.reserve(get_cookie_value_len(
				cookie,
				nullptr,
				nullptr
			));

			get_cookie_value_len(cookie, nullptr, &value);
			return value;
		}

		size_t skip=0;

		get_cookie_value_len(cookie, &skip, nullptr);
		cookie.remove_prefix(skip);
	}

	return "";
}
