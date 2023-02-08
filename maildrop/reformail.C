/*
** Copyright 1998 - 2009 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"config.h"

#include	<stdio.h>
#include	<iostream>
#include	<iomanip>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<signal.h>
#include	<pwd.h>

#if	HAVE_LOCALE_H
#include	<locale.h>
#endif

#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#include	"mytime.h"
#include	<sys/types.h>
#include	"mywait.h"
#if HAVE_SYS_FILE_H
#include	<sys/file.h>
#endif
#include	"rfc822.h"
#include	"buffer.h"
#include	"liblock/config.h"
#include	"liblock/liblock.h"

#if	HAS_GETHOSTNAME
#else
extern "C" int gethostname(const char *, size_t);
#endif


static int inbody=0, addcrs=0, catenate=0;
static const char *(*append_more_headers)();
static Buffer	optx, optX, opta, optA, opti, optI, optu, optU, optubuf, optUbuf, optR;

static Buffer add_from_filter_buf;
static const char *add_from_filter_buf_ptr;
static const char *cache_maxlen="", *cache_name="";

static const char *( *from_filter)();
static Buffer	current_line;

void outofmem()
{
	std::cerr << "reformail: Out of memory." << std::endl;
	exit(1);
}

void help()
{
	std::cerr << "reformail: Invalid arguments." << std::endl;
	exit(1);
}

// Return next line from standard input.  Trailing CRs are stripped

const char *NextLine()
{
static Buffer buf;
int	c;

	buf.clear();
	while ((c=std::cin.get()) >= 0 && c != '\n')
		buf.push_back(c);
	if (c < 0 && buf.size() == 0)	return (0);
	if (buf.size())	// Strip CRs
	{
		c=buf.back();

		if (c == '\r')
			buf.pop_back();
	}
	buf.push_back('\n');
	buf.push_back_0();
	return (buf.c_str());
}

// from_filter is the initial filtering done on the message.
// from_filter adds or subtracts "From " quoting from the message.
// from_filter returns the next line from the message, after filtering.
// The line is always terminated by a newline character.
// When the header is being read, multiline headers are silently
// concatenated into a single return from from_filter() (separated by
// newlines, of course.
//
// from_filter is initialized to either from_filter(), add_from_filter()
// and rem_from_filter() respectively, to cause either addition, removal of,
// or no change to from quoting.  The pointer is automatically updated.
//
// Also, the from_filter silently discards empty lines at the beginning of
// the message.

// DO NOT CHANGE FROM QUOTING.

const char *no_from_filter_header();

const char *no_from_filter()
{
const	char *p;

	while ((p=NextLine()) && *p == '\n')
		;
	if (!p)	return (0);

	current_line=p;
	return ( no_from_filter_header() );
}

const char *read_blank();

const char *no_from_filter_header()
{
const	char *p;

static Buffer buf;

	from_filter= &no_from_filter_header;

	while ((p=NextLine()) && *p && *p != '\n' && isspace((unsigned char)*p))
		current_line += p;

	buf=current_line;
	buf.push_back_0();
	if (!p || *p == '\n')
	{
		from_filter= &read_blank;
		return (buf.c_str());
	}
	current_line=p;
	return (buf.c_str());
}

const char *read_blank()
{
	from_filter= &NextLine;
	return ("\n");
}

//////////////////////////////////////////////////////////////////////////
//
//  Add 'From ' quoting.  All headers are read into add_from_filter_buf,
//  and a suitable return address is located.  The 'From ' line is
//  generated, and return.  Subsequent calls fetch one header at a
//  time from add_from_filter_buf, then resume reading the body of the
//  message.
//
//////////////////////////////////////////////////////////////////////////

const char *add_from_filter_header();
const char *add_from_filter()
{
const	char *p;
int n;
	while ((p=NextLine()) && *p == '\n')
		;
	if (!p)	return (0);

	current_line=p;
	if (strncmp(p, "From ", 5) == 0)
		return ( no_from_filter_header() );
	add_from_filter_buf.clear();
	while (p && *p != '\n')
	{
		add_from_filter_buf += p;
		p=NextLine();
	}

	add_from_filter_buf.push_back_0();

static Buffer	return_path;
static Buffer	from_header;

	return_path.clear();
	from_header.clear();

	for (p=add_from_filter_buf.c_str(); *p; )
	{
	Buffer	header;

		while (*p && *p != ':' && *p != '\n')
		{
		int	c= (unsigned char)*p++;

			c=tolower(c);
			header.push_back(c);
		}
		for (;;)
		{
			while (*p && *p != '\n')
			{
				header.push_back(*p);
				p++;
			}
			if (!*p)	break;
			++p;
			header.push_back('\n');
			if (!*p || !isspace((unsigned char)*p))	break;
		}
		header.push_back_0();
		if (strncmp(header.c_str(), "return-path:", 12) == 0 ||
		    strncmp(header.c_str(), ">return-path:", 13) == 0 ||
		    strncmp(header.c_str(), "errors-to:", 10) == 0 ||
		    strncmp(header.c_str(), ">errors-to:", 11) == 0)
		{
			const char *p;

			for (p=header.c_str(); *p != ':'; p++)
				;
			return_path=p;
		}

		if (strncmp(header.c_str(), "from:", 5) == 0)
			from_header=header.c_str() + 5;
	}
	if (return_path.size() == 0)	return_path=from_header;
	return_path.push_back_0();

	struct rfc822t *rfc=rfc822t_alloc_new( return_path.c_str(),
					       NULL, NULL);

	if (!rfc)	outofmem();

	struct rfc822a *rfca=rfc822a_alloc( rfc);

	if (!rfca)	outofmem();

	from_header.clear();

	for (n=0; n<rfca->naddrs; ++n)
	{
		if (rfca->addrs[n].tokens)
		{
			char *p=rfc822_display_addr_tobuf(rfca, n, NULL);

			if (p)
			{
				try {
					from_header=p;
				} catch (...)
				{
					free(p);
					throw;
				}
				free(p);
				break;
			}
		}
	}

	rfc822a_free(rfca);
	rfc822t_free(rfc);

	if (from_header.size() == 0)	from_header="root";
	return_path="From ";
	return_path += from_header;
	return_path.push_back(' ');
time_t	t;

	time(&t);
	p=ctime(&t);
	while (*p && *p != '\n')
	{
		return_path.push_back(*p);
		p++;
	}
	return_path += "\n";
	return_path.push_back_0();
	from_filter=add_from_filter_header;
	add_from_filter_buf_ptr=add_from_filter_buf.c_str();
	return (return_path.c_str());
}

const char *add_from_filter_body();

const char *add_from_filter_header()
{
static Buffer buf;

	buf.clear();

	if (*add_from_filter_buf_ptr == '\0')
	{
		from_filter= &add_from_filter_body;
		return ("\n");
	}

	do
	{
		while (*add_from_filter_buf_ptr)
		{
			buf.push_back( (unsigned char)*add_from_filter_buf_ptr );
			if ( *add_from_filter_buf_ptr++ == '\n')	break;
		}
	} while ( *add_from_filter_buf_ptr && *add_from_filter_buf_ptr != '\n'
		&& isspace( (unsigned char)*add_from_filter_buf_ptr ));
	buf.push_back_0();
	return (buf.c_str());
}

const char *add_from_filter_body()
{
const char *p=NextLine();

	if (!p)	return (p);

const char *q;

	for (q=p; *q == '>'; q++)
		;
	if (strncmp(q, "From ", 5))	return (p);

static Buffer add_from_buf;

	add_from_buf=">";
	add_from_buf += p;
	add_from_buf.push_back_0();
	return (add_from_buf.c_str());
}

////////////////////////////////////////////////////////////////////////////
//
// Strip From quoting.
//
////////////////////////////////////////////////////////////////////////////

const char *rem_from_filter_header();

const char *(*rem_from_filter_header_ptr)();

const char *rem_from_filter()
{
const	char *p;

	while ((p=NextLine()) && *p == '\n')
		;
	if (!p)	return (0);

	if (strncmp(p, "From ", 5))
	{
		current_line=p;
		return ( no_from_filter_header() );
	}

	for (const char *q="Return-Path: <"; *q; ++q)
	{
		optI.push_back(*q);
	}
	for (p += 5; *p && *p != '\n' && isspace(*p); ++p)
		;
	if (*p == '<')
		++p;

	while (*p && *p != '\n' && *p != '>' && !isspace(*p))
	{
		optI.push_back(*p);
		++p;
	}
	optI.push_back('>');
	optI.push_back_0();

	p=NextLine();
	if (!p)	return (p);
	current_line=p;
	rem_from_filter_header_ptr= &no_from_filter_header;
	return ( rem_from_filter_header() );
}

const char *rem_from_filter_body();
const char *rem_from_filter_header()
{
const char *p=(*rem_from_filter_header_ptr)();

	rem_from_filter_header_ptr=from_filter;
	from_filter=rem_from_filter_header;
	if (!p || *p == '\n')
	{
		from_filter=&rem_from_filter_body;
		p="\n";
	}
	return (p);
}

const char *rem_from_filter_body()
{
const char *p=NextLine();

	if (!p)	return (p);

	if (*p == '>')
	{
	const char *q;

		for (q=p; *q == '>'; q++)
			;
		if (strncmp(q, "From ", 5) == 0)	++p;
	}
	return (p);
}

static const char *HostName()
{
static char hostname_buf[256];

	hostname_buf[0]=0;
	hostname_buf[sizeof(hostname_buf)-1]=0;
	gethostname(hostname_buf, sizeof(hostname_buf)-1);
	return (hostname_buf);
}

////////////////////////////////////////////////////////////////////////////
//
// Return TRUE if header is already in a list of headers.
//
// hdrs: null separated list of headers (and header contents)
// hdr - header to check (must be lowercase and terminated by a colon)
// pos - offset into hdrs where it's found.
//

static int has_hdr(const Buffer &hdrs, const char *hdr, unsigned &pos)
{
	const char *r=hdrs.c_str();
	auto l=hdrs.size();
	Buffer	buf2;
	unsigned pos2=0;

	while (l)
	{
		buf2.clear();
		pos=pos2;
		while (l)
		{
			--l;
			++pos2;
			buf2.push_back( tolower(*r));
			if (*r++ == 0)	break;
		}
		buf2.push_back_0();
		if (strncmp(hdr, buf2.c_str(), strlen(hdr)) == 0)
			return (1);
	}
	return (0);
}

static int has_hdr(const Buffer &hdrs, const char *hdr)
{
unsigned dummy;

	return (has_hdr(hdrs, hdr, dummy));
}

static void strip_empty_header(Buffer &buf)
{
	Buffer	newbuf;
	size_t l;
	const char *p;

	for (p=buf.c_str(), l=buf.size(); l; )
	{
		if (p[strlen(p)-1] == ':')
		{
			while (l)
			{
				--l;
				if (*p++ == '\0')	break;
			}
			continue;
		}
		while (l)
		{
			--l;
			newbuf.push_back( *p );
			if (*p++ == '\0')	break;
		}
	}
	buf=newbuf;
}

static void strip_header(Buffer &header, unsigned offset)
{
	Buffer	buf1;
	const char *p=header.c_str();
	auto l=header.size();

	while (l)
	{
		if (!offset)
		{
			while (l)
			{
				--l;
				if (*p++ == '\0')	break;
			}
			break;
		}
		buf1.push_back( *p++ );
		--l;
		--offset;
	}
	while (l--)
		buf1.push_back( *p++ );
	header=buf1;
}

const char *ReadLineAddNewHeader();

const char *ReadLineAddHeader()
{
Buffer	buf1;
const char *q;
const char *p;
unsigned pos;
static Buffer oldbuf;

	for (;;)
	{
		p= (*from_filter)();

		if (!p)	return p;
		if (*p == '\n')
		{
			strip_empty_header(opti);
			strip_empty_header(optI);
			return ( ReadLineAddNewHeader());
		}
		buf1.clear();
		for (q=p; *q && *q != '\n'; q++)
		{
			buf1.push_back( tolower(*q) );
			if (*q == ':')	break;
		}
		buf1.push_back_0();

		if (has_hdr(opti, buf1.c_str()))
		{
			oldbuf="old-";
			oldbuf += buf1;
			buf1=oldbuf;

		Buffer	tbuf;

			tbuf="Old-";
			tbuf += p;
			oldbuf=tbuf;
			oldbuf.push_back_0();
			p=oldbuf.c_str();
		}
		if (has_hdr(optR, buf1.c_str(), pos))
		{
			Buffer	tbuf;

			q=optR.c_str();
			q += pos + strlen(buf1.c_str());
			tbuf=q;

			p += strlen(buf1.c_str());
			tbuf += p;
			oldbuf=tbuf;
			oldbuf.push_back_0();
			p=oldbuf.c_str();
		}

		if (has_hdr(optI, buf1.c_str()))
			continue;
		if (has_hdr(optu, buf1.c_str()))
		{
			if (!has_hdr(optubuf, buf1.c_str()))
			{
				q=p;
				do
				{
					optubuf.push_back( *q );
				} while (*q++);
				break;
			}
			continue;
		}

		if (has_hdr(optU, buf1.c_str()))
		{
			if (has_hdr(optUbuf, buf1.c_str(), pos))
				strip_header(optUbuf, pos);
			while (*p)
			{
				optUbuf.push_back( *p );
				p++;
			}
			optUbuf.pop_back();
			optUbuf.push_back_0();
			continue;
		}
		break;
	}

	unsigned offset;

	if (has_hdr(opta, buf1.c_str(), offset))
		strip_header(opta, offset);
	return (p);
}

const char *ReadLineAddNewHeaderDone();

const char *ReadLineAddNewHeader()
{
	append_more_headers= &ReadLineAddNewHeader;

Buffer	*bufptr;

	if (opta.size())	bufptr= &opta;
	else if (optA.size())	bufptr= &optA;
	else if (opti.size())	bufptr= &opti;
	else if (optI.size())	bufptr= &optI;
	else if (optUbuf.size())	bufptr= &optUbuf;
	else
	{
		append_more_headers=&ReadLineAddNewHeaderDone;
		return ("\n");
	}

static Buffer buf1;
Buffer	buf2;

	buf1.clear();

	const char *p= bufptr->c_str();
	auto l= bufptr->size();

	while (l)
	{
		if ( !*p )
		{
			p++;
			l--;
			break;
		}
		buf1.push_back( *p );
		p++;
		l--;
	}
	buf1.push_back('\n');
	buf1.push_back_0();

	while (l--)
		buf2.push_back(*p++);
	*bufptr=buf2;
	return (buf1.c_str());
}

const char *ReadLineAddNewHeaderDone()
{
	return ( (*from_filter)() );
}

////////////////////////////////////////////////////////////////////////////
const char *ReadLine()
{
const char *p=(*append_more_headers)();

	if (!p)	return (p);

static Buffer	buf;

	if (*p == '\n')
		inbody=1;

	if (catenate && !inbody)
	{
	const char *q;

		buf.clear();
		for (q=p; *q; q++)
		{
			if (*q != '\n')
			{
				buf.push_back(*q);
				continue;
			}
			do
			{
				++q;
			} while (*q && isspace(*q));
			if (*q)
				buf.push_back(' ');
			--q;
		}
		if (addcrs)	buf.push_back('\r');
		buf.push_back('\n');
		buf.push_back_0();
		return (buf.c_str());
	}

	if (addcrs)
	{
		buf=p;
		buf.pop_back();
		buf += "\r\n";
		buf.push_back_0();
		return (buf.c_str());
	}
	return (p);
}

/////////////////////////////////////////////////////////////////////////
//
// Default activity: just copy the message (let the low-level format
// filters do their job.
//
/////////////////////////////////////////////////////////////////////////

void copy(int, char *[], int)
{
const char *p;

	while ((p= ReadLine()) != 0)
		std::cout << p;
}

void cache(int, char *[], int)
{
const char *p;
Buffer	buf;
int found=0;

	addcrs=0;
	while ((p= ReadLine()) != 0)
	{
	int	c;

		if (inbody)	break;
		buf.clear();
		while (*p && *p != '\n')
		{
			c= (unsigned char)*p;
			c=tolower(c);
			buf.push_back(c);
			if (*p++ == ':')	break;
		}
		if (!(buf == "message-id:"))	continue;
		buf.push_back_0();
		while (*p && isspace( (unsigned char)*p))	p++;
		buf.clear();
		while (*p)
		{
			buf.push_back(*p);
			++p;
		}

		while (buf.size())
		{
			auto c=buf.back();

			if (!isspace(c))
				break;
			buf.pop_back();
		}

		if (buf.size() == 0)	break;
		buf.push_back_0();

	int	fd=open(cache_name, O_RDWR | O_CREAT, 0600);

		if (fd < 0)
		{
			perror("open");
			exit(75);
		}

		if (ll_lock_ex(fd) < 0)
		{
			perror("lock");
			exit(75);
		}

	off_t	pos=0;

		if (lseek(fd, 0L, SEEK_END) == -1 ||
			(pos=lseek(fd, 0L, SEEK_CUR)) == -1 ||
			lseek(fd, 0L, SEEK_SET) == -1)
		{
			perror("seek");
			exit(75);
		}

	off_t	maxlen_n=atol(cache_maxlen);
	char	*charbuf;
	off_t	newpos=maxlen_n;

		if (newpos < pos)	newpos=pos;

		if ((charbuf=new char[newpos+buf.size()+1]) == NULL)
			outofmem();

	off_t	readcnt=read(fd, charbuf, newpos);

		if (readcnt < 0)	perror("read");

	char *q, *r;

		for (q=r=charbuf; q<charbuf+readcnt; )
		{
			if (*q == '\0')	break;	// Double null terminator
			if (strcmp( buf.c_str(), q) == 0)
			{
				found=1;
				while (q < charbuf+readcnt)
					if (*q++ == '\0')	break;
			}
			else while (q < charbuf+readcnt)
				if ( (*r++=*q++) == '\0') break;
		}
		memcpy(r, buf.c_str(), buf.size());
		r += buf.size();
		for (q=charbuf; q<r; )
		{
			if (r - q < maxlen_n)
				break;
			while (q < r)
				if (*q++ == '\0')	break;
		}
		if (q == r)	q=charbuf;
		*r++ = '\0';
		if (lseek(fd, 0L, SEEK_SET) == -1)
		{
			perror("lseek");
			exit(1);
		}
		while (q < r)
		{
			readcnt=write(fd, q, r-q);
			if (readcnt == -1)
			{
				perror("write");
				exit(1);
			}
			q += readcnt;
		}
		close(fd);
		delete[] charbuf;
		break;
	}
	while ((p= ReadLine()) != 0)
		;
	exit(found ? 0:1);
}

//////////////////////////////////////////////////////////////////////////
//
// Extract headers

void extract_headers(int, char *[], int)
{
const char *p, *q;
Buffer	b;

	catenate=1;
	while ((p=ReadLine()) && !inbody)
	{
		b.clear();
		for (q=p; *q && *q != '\n'; )
		{
		int	c= (unsigned char)*q;

			b.push_back( tolower(c) );
			if ( *q++ == ':')	break;
		}
		b.push_back_0();

		if (has_hdr(optx, b.c_str()))
		{
			while (*q && *q != '\n' && isspace(*q))
				q++;
			std::cout << q;
			continue;
		}

		if (has_hdr(optX, b.c_str()))
		{
			std::cout << p;
			continue;
		}
	}
	if (!std::cin.seekg(0, std::ios::end).fail())
		return;
	std::cin.clear();

	while ( ReadLine() )
		;
}
//////////////////////////////////////////////////////////////////////////
//
// Split mbox file into messages.

void split(int argc, char *argv[], int argn)
{
const char *p;
Buffer	buf;
int	l;
int	do_environ=1;
unsigned long	environ=0;
unsigned	environ_len=3;
const	char *env;

	if (argn >= argc)	help();

	while ( (p=NextLine()) && *p == '\n')
		;

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);
	env=getenv("FILENO");
	if (env)
	{
	const char *q;

		for (q=env; *q; q++)
			if (!isdigit(*q))	break;
		if (*q)	do_environ=0;
		else
		{
			environ_len=strlen(env);
			environ=atol(env);
		}
	}

	while (p)
	{
	int	fds[2];

		if (pipe(fds) < 0)
		{
			std::cerr << "reformail: pipe() failed." << std::endl;
			exit(1);
		}

	pid_t	pid=fork();

		if (pid == -1)
		{
			std::cerr << "reformail: fork() failed." << std::endl;
			exit(1);
		}

		if (pid == 0)
		{
			dup2(fds[0], 0);
			close(fds[0]);
			close(fds[1]);

		Buffer	buf, buf2;

			if (do_environ)
			{
			char	*s;

				while (environ || environ_len)
				{
					buf.push_back( "0123456789"[environ % 10]);
					environ /= 10;
					if (environ_len)	--environ_len;
				}

				buf2="FILENO=";
				while (buf.size())
				{
					buf2.push_back(buf.back());
					buf.pop_back();
				}
				buf2.push_back_0();
				s=strdup(buf2.c_str());
				if (!s)
				{
					perror("strdup");
					exit (1);
				}
				putenv(s);
			}

			execvp( argv[argn], argv+argn);
			std::cerr << "reformail: exec() failed." << std::endl;
			exit(1);
		}
		close(fds[0]);
		environ++;

		do
		{
			buf=p;
			p=ReadLine();
			if (!p || strncmp(p, "From ", 5) == 0)
				buf.pop_back();	// Drop trailing newline
			else
			{
				if (addcrs)
				{
					buf.pop_back();
					buf.push_back('\r');
					buf.push_back('\n');
				}
			}

			const char *q=buf.c_str();

			l=buf.size();
			while (l)
			{
			int	n= ::write( fds[1], q, l);
				if (n <= 0)
				{
					std::cerr
					  << "reformail: write() failed."
					  << std::endl;
					exit(1);
				}
				q += n;
				l -= n;
			}
		} while (p && strncmp(p, "From ", 5));
		close(fds[1]);

	int	wait_stat;

		while ( wait(&wait_stat) != pid )
			;
		if (!WIFEXITED(wait_stat) || WEXITSTATUS(wait_stat))
			break;	// Rely on diagnostic from child
	}
}

//////////////////////////////////////////////////////////////////////////////

static void add_bin64(Buffer &buf, unsigned long n)
{
int	i;

	for (i=0; i<16; i++)
	{
		buf.push_back( "0123456789ABCDEF"[n % 16] );
		n /= 16;
	}
}

static void add_messageid(Buffer &buf)
{
time_t	t;

	buf.push_back('<');
	time(&t);
	add_bin64(buf,t);
	buf.push_back('.');
	add_bin64(buf, getpid() );
	buf += ".reformail@";
	buf += HostName();
	buf.push_back('>');
}

static void add_opta(Buffer &buf, const char *optarg)
{
Buffer	chk_buf;
const char *c;

	for (c=optarg; *c; c++)
		chk_buf.push_back( tolower( (unsigned char)*c ));
	if (chk_buf == "message-id:" || chk_buf == "resent_message_id:")
	{
		chk_buf=optarg;
		chk_buf += " ";
		add_messageid(chk_buf);
		chk_buf.push_back_0();
		optarg=chk_buf.c_str();
	}

	do
	{
		buf.push_back( *optarg );
	} while (*optarg++);
}

int main(int argc, char *argv[])
{
int	argn, done;
const char	*optarg;
void	(*function)(int, char *[], int)=0;

#if HAVE_SETLOCALE
	setlocale(LC_ALL, "C");
#endif

	from_filter= &no_from_filter;
	append_more_headers=&ReadLineAddHeader;
	done=0;
	for (argn=1; argn<argc; argn++)
	{
		if (strcmp(argv[argn], "--") == 0 || strcmp(argv[argn],"-")==0)
		{
			++argn;
			break;
		}
		if (argv[argn][0] != '-')	break;
		optarg=argv[argn]+2;
		if (!*optarg)	optarg=0;
		switch ( argv[argn][1] )	{
		case 'd':
			if (!optarg || !*optarg)	optarg="1";
			addcrs=atoi(optarg);
			break;
		case 'c':
			catenate=1;
			break;
		case 'f':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || *optarg == '0')
				from_filter=&rem_from_filter;
			else
				from_filter=&add_from_filter;
			break;
		case 'D':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || argn+1 >= argc)	help();
			if (function)	help();
			function=cache;
			cache_maxlen=optarg;
			cache_name=argv[++argn];
			break;
		case 'a':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			add_opta(opta, optarg);
			break;
		case 'A':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			add_opta(optA, optarg);
			break;
		case 'i':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			do
			{
				opti.push_back( *optarg );
			} while (*optarg++);
			break;
		case 'I':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			do
			{
				optI.push_back( *optarg );
			} while (*optarg++);
			break;
		case 'R':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			while (*optarg)
				optR.push_back(*optarg++);
			if (argn+1 >= argc)	help();
			optarg=argv[++argn];
			while (*optarg)
				optR.push_back(*optarg++);
			optR.push_back_0();
			break;
		case 'u':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			while (*optarg)
				optu.push_back(*optarg++);
			optu.push_back_0();
			break;
		case 'U':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			while (*optarg)
				optU.push_back(*optarg++);
			optU.push_back_0();
			break;
		case 'x':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			while (*optarg)
				optx.push_back(*optarg++);
			optx.push_back_0();
			break;
		case 'X':
			if (!optarg && argn+1 < argc)	optarg=argv[++argn];
			if (!optarg || !*optarg)	help();
			if (function)	help();
			while (*optarg)
				optX.push_back(*optarg++);
			optX.push_back_0();
			break;
		case 's':
			if (function)	help();
			function= &split;
			++argn;
			done=1;
			break;
		default:
			help();
		}
		if (done)	break;
	}
	if (optx.size() || optX.size())
	{
		if (function)	help();
		function=extract_headers;
	}

	if (!function)	function=copy;
	(*function)(argc, argv, argn);
	std::cout.flush();
	if (std::cout.fail())
	{
		std::cerr << "reformail: error writing output." << std::endl;
		exit(1);
	}
	exit(0);
}
