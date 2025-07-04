/*
** Copyright 2000-2025 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#if	HAVE_CONFIG_H
#include "rfc2045_config.h"
#endif
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<time.h>
#include	<stdio.h>
#include	<errno.h>
#include	<string.h>
#include	<signal.h>
#if	HAVE_STRINGS_H
#include	<strings.h>
#endif
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<stdlib.h>
#include	<ctype.h>
#include	<pwd.h>
#include	<fcntl.h>
#include	<signal.h>

#include	"rfc822/rfc822.h"
#include	"rfc822/encode.h"
#include	"rfc2045.h"
#include	"rfc2045charset.h"
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	"numlib/numlib.h"

#include	<iostream>
#include	<vector>
#include	<string>
#include	<string_view>
#include	<algorithm>
#include	<memory>
#include	<courier-unicode.h>

#if     HAS_GETHOSTNAME
#else
int gethostname(const char *, size_t);
#endif

/******************************************************************************

Check if a file is a regular file.

******************************************************************************/

static int isreg(int fd)
{
	struct stat st;
	if (fstat(fd, &st))
	{
		perror("fstat");
		exit(1);
	}
	return S_ISREG(st.st_mode);
}

/******************************************************************************

Determine the file descriptor wanted, if any.

******************************************************************************/

static int fd_wanted(std::string_view filename, std::string_view mode)
{
	if (filename == "-")		/* stdin or stdout */
		return mode != "r" ? 1:0;
	if (filename.size() && filename[0] == '&')
	{
		int fd=0;

		for (char c:filename)
			if (c >= '0' && c <= '9')
			{
				fd=fd*10 + (c-'0');
			}
		return fd;	/* file descriptor */
	}
	return -1;		/* or a file */
}

/******************************************************************************

Open some file or a pipe for reading and writing.

******************************************************************************/

static rfc822::fdstreambuf openfile_or_pipe(std::string_view filename,
					    std::string_view mode)
{
	int	fd, fd_to_dup = fd_wanted(filename, mode);

	if (fd_to_dup >= 0)
		fd = dup(fd_to_dup);
	else
	{
		std::string s{filename.begin(), filename.end()};
		fd=open(s.c_str(), (mode != "r") ?
			O_WRONLY|O_CREAT|O_TRUNC:O_RDONLY, 0666);
	}
	if (fd < 0)
	{
		std::string s{filename.begin(), filename.end()};
		perror(s.c_str());
		exit(1);
	}

	return rfc822::fdstreambuf{fd};
}

/******************************************************************************

Open some file.  If we get a pipe, open a temporary file, and drain pipe's
contents into it.

******************************************************************************/

static rfc822::fdstreambuf openfile(std::string_view filename)
{
	rfc822::fdstreambuf fp=openfile_or_pipe(filename, "r");
	int	fd=fp.fileno();;

	if (!isreg(fd))	/* Must be a pipe */
	{
		FILE *t=tmpfile();
		int tfd=dup(fileno(t));

		if (tfd < 0)
		{
			perror("dup");
			exit(1);
		}

		rfc822::fdstreambuf tfile{tfd};
		fclose(t);

		char buf[BUFSIZ];

		if (!t)
		{
			perror("tmpfile");
			exit(1);
		}

		while (1)
		{
			auto n=fp.sgetn(buf, BUFSIZ);

			if (n < 0)
			{
				perror("read");
				exit(1);
			}

			if (n == 0)
				break;

			if (tfile.sputn(buf, n) != n)
			{
				perror("write");
				exit(1);
			}
		}

		if (tfile.pubseekpos(0) != 0)
		{
			perror("seek");
			exit(1);
		}

		fp=std::move(tfile);
	}
	else
	{
		if (fp.tell() < 0)
		{
			perror("seek");
			exit(1);
		}
	}
	return (fp);
}

/******************************************************************************

Build argv/argc from a file.

******************************************************************************/

static void read_args(std::vector<std::string> &args, std::string file)
{
	auto streambuf=openfile_or_pipe(file, "r");
	std::istream i{&streambuf};
	std::string buffer;

	args.clear();

	while (std::getline(i, buffer))
	{
		/* Skip the filler. */

		auto b=buffer.begin(), e=buffer.end();

		b=std::find_if(b, e, [](char c){
			return !isspace((int)(unsigned char)c);
		});

		if (b == e || *b == '#') continue;

		while (e > b && isspace(e[-1]))
			--e;

		if (std::string_view{&*b,
				     static_cast<size_t>(e-b)} == "-")
		{
			if (isreg(streambuf.fileno()))
			{
				auto orig_pos=streambuf.pubseekoff(
					0, std::ios_base::cur
				);
				if (orig_pos == -1 ||
				    lseek(streambuf.fileno(), orig_pos,
					  SEEK_SET) == -1)
				{
					perror("seek");
					exit(1);
				}
			}
			break;
		}

		args.push_back(std::string(b, e));
	}
}

static void usage()
{
	fprintf(stderr,
"Usage:\n"
"  makemime -c type [-o file] [-e encoding] [-C charset] [-N name] \\\n"
"                   [-a \"Header: Contents\"] file\n"
"           -m [ type ] [-o file] [-e encoding] [-a \"Header: Contents\"] file\n"
"           -j [-o file] file1 file2\n"
"           @file\n"
"\n"
"   file:  filename    - read or write from filename\n"
"          -           - read or write from stdin or stdout\n"
"          &n          - read or write from file descriptor n\n"
"          \\( opts \\)  - read from child process, that generates [ opts ]\n"
		"\n");

	fprintf(stderr,
"Options:\n"
"\n"
"  -c type         - create a new MIME section from \"file\" with this\n"
"                    Content-Type: (default is application/octet-stream).\n"
"  -C charset      - MIME charset of a new text/plain section.\n"
		"  -N name         - MIME content name of the new mime section.\n");

	fprintf(stderr,
"  -m [ type ]     - create a multipart mime section from \"file\" of this\n"
"                    Content-Type: (default is multipart/mixed).\n"
"  -e encoding     - use the given encoding (7bit, 8bit, quoted-printable,\n"
"                    or base64), instead of guessing.  Omit \"-e\" and use\n"
"                    -c auto to set Content-Type: to text/plain or\n"
		"                    application/octet-stream based on picked encoding.\n");

	fprintf(stderr,
"  -j file1 file2  - join mime section file2 to multipart section file1.\n"
"  -o file         - write ther result to file, instead of stdout (not\n"
"                    allowed in child processes).\n"
"  -a header       - prepend an additional header to the output.\n"
"\n"
"  @file - read all of the above options from file, one option or\n"
"          value on each line.\n"
	);
	exit (0);
}

/******************************************************************************

The arguments are parsed into the following structure, as a tree.

******************************************************************************/
struct mimestruct {

	/*
	** One or two input files.  We initialize either file or child,
	** depending on the source being a file, or a child process.
	** Later, we open a file pointer in either case.
	*/

	std::string_view inputfile1, inputfile2;

	// Child mimestructs
	std::unique_ptr<mimestruct> inputchild1, inputchild2;

	rfc822::fdstreambuf inputfilebuf1, inputfilebuf2;
	std::istream inputfp1, inputfp2;

	pid_t	child1=0, child2=0;

	/* Output file.  Defaults to "-", stdout */

	std::string_view outputfile{"-"};
	rfc822::fdstreambuf outputfilebuf;
	std::ostream outputfp;

		/* The handler and open functions */

	void (mimestruct::*handler_func)()=nullptr;
	void (mimestruct::*open_func)()=nullptr;

	void createsimplemime();
	void createmultipartmime();
	void joinmultipart();

	void opencreatesimplemime();
	void opencreatemultipartmime();
	void openjoinmultipart();
	void copyfilebuf1();
	void copyfilebuf2();
	void copyfilebuf(rfc822::fdstreambuf &);
	bool tryboundary(rfc822::fdstreambuf &sb, std::string_view bbuf);
	std::string mkboundary(rfc822::fdstreambuf &sb);

	rfc822::fdstreambuf openchild(mimestruct &child,
				      pid_t	*pidptr,
				      int usescratch);

	void openoutput();
		/* The new mime type, and encoding (-e) */
	const char *mimetype=nullptr;
	const char *mimeencoding=nullptr;
	const char *textplaincharset=nullptr;
	const char *contentname=nullptr;

		/* A list of -a headers */
	std::vector<std::string> aheaders;

	void goodexit(int exitcode);

	mimestruct(std::vector<std::string>::const_iterator &argsb,
		   std::vector<std::string>::const_iterator &argse);

	mimestruct(const mimestruct &)=delete;
	mimestruct &operator=(const mimestruct &)=delete;
} ;

/******************************************************************************

Recursively build the mimestruct tree.

******************************************************************************/

mimestruct::mimestruct(std::vector<std::string>::const_iterator &argsb,
		       std::vector<std::string>::const_iterator &argse)
	: inputfp1{&inputfilebuf1},
	  inputfp2{&inputfilebuf2},
	  outputfp{&outputfilebuf}
{
	if (argsb == argse)	usage();

	if (argsb->compare(0, 2, "-c") == 0)
	{
		handler_func= &mimestruct::createsimplemime;
		open_func= &mimestruct::opencreatesimplemime;
		if (argsb->size() > 2)
		{
			mimetype=argsb->c_str()+2;
			++argsb;
		}
		else
		{
			++argsb;
			if (argsb != argse &&
			    *argsb->c_str() != '-' && *argsb->c_str() != ')')
			{
				mimetype=argsb->c_str();
				++argsb;
			}
			else
				mimetype="application/octet-stream";
		}

		while (isspace((int)(unsigned char)*mimetype))
			++mimetype;
	}
	else if (argsb->compare(0, 2, "-m") == 0)
	{
		handler_func= &mimestruct::createmultipartmime;
		open_func= &mimestruct::opencreatemultipartmime;
		if (argsb->size() > 2)
		{
			mimetype=argsb->c_str()+2;
			++argsb;
		}
		else
		{
			++argsb;
			if (argsb != argse && *argsb->c_str() != '-' &&
			    *argsb->c_str() != ')')
			{
				mimetype=argsb->c_str();
				++argsb;
			}
			else
				mimetype="multipart/mixed";
		}
		while (isspace((int)(unsigned char)*mimetype))
			++mimetype;
	}
	else if (argsb->compare(0, 2, "-j") == 0)
	{
		const char *filename;

		handler_func= &mimestruct::joinmultipart;
		open_func= &mimestruct::openjoinmultipart;
		if (argsb->size() > 2)
		{
			filename=argsb->c_str()+2;
			++argsb;
		}
		else
		{
			++argsb;
			if (argsb == argse)	usage();
			filename=argsb->c_str();
			++argsb;
		}

		while (isspace((int)(unsigned char)*filename))
			++filename;

		if (strcmp(filename, "(") == 0)
		{
			inputchild2=std::make_unique<mimestruct>(argsb, argse);
			if (argsb == argse || *argsb != ")")
				usage();
			++argsb;
		}
		else
			inputfile2=filename;
	}
	else
		usage();

	/* Handle common options */

	while (argsb != argse)
	{
		if (argsb->compare(0, 2, "-o") == 0)
		{
			const char *f=argsb->c_str()+2;

			++argsb;
			if (*f == 0)
			{
				if (argsb == argse)	usage();
				f=argsb->c_str();
				++argsb;
			}
			while (isspace((int)(unsigned char)*f))
				++f;
			outputfile=f;
			continue;
		}

		if (argsb->compare(0, 2, "-C") == 0)
		{
			const char *f=argsb->c_str()+2;

			++argsb;

			if (*f == 0)
			{
				if (argsb == argse)	usage();
				f=argsb->c_str();
				++argsb;
			}
			while (isspace((int)(unsigned char)*f))
				++f;
			textplaincharset=f;
			continue;
		}

		if (argsb->compare(0, 2, "-N") == 0)
		{
			const char *f=argsb->c_str()+2;

			++argsb;

			if (*f == 0)
			{
				if (argsb == argse)	usage();
				f=argsb->c_str();
				++argsb;
			}
			while (isspace((int)(unsigned char)*f))
				++f;
			contentname=f;
			continue;
		}

		if (argsb->compare(0, 2, "-e") == 0)
		{
			const char *f=argsb->c_str()+2;

			++argsb;

			if (*f == 0)
			{
				if (argsb == argse)	usage();
				f=argsb->c_str();
				++argsb;
			}

			while (isspace((int)(unsigned char)*f))
				++f;

			std::string buf{f};

			std::for_each(buf.begin(), buf.end(),
				      []
				      (auto &c)
				      {
					      c=tolower(c);
				      });

			if (buf == "7bit")
				f="7bit";
			else if (buf == "8bit")
				f="8bit";
			else if (buf == "quoted-printable")
				f="quoted_printable";
			else if (buf == "base64")
				f="base64";
			else
				usage();

			mimeencoding=f;
			continue;
		}

		if (argsb->compare(0, 2, "-a") == 0)
		{
			const char *f=argsb->c_str()+2;

			++argsb;

			if (*f == 0)
			{
				if (argsb == argse)	usage();
				f=argsb->c_str();
				++argsb;
			}

			while (isspace((int)(unsigned char)*f))
				++f;

			aheaders.push_back(f);
			continue;
		}
		break;
	}

	/* We must now have the input file argument */

	if (argsb == argse)	usage();

	if (*argsb == "(")
	{
		++argsb;
		inputchild1=std::make_unique<mimestruct>(argsb, argse);
		if (argsb == argse || *argsb != ")")
			usage();
		++argsb;
	}
	else
	{
		inputfile1=argsb->c_str();
		++argsb;
	}
}

/******************************************************************************

After we're done, terminate with a zero exit code if all child processes also
terminated with a zero exit code.  Otherwise, terminate with a non-zero exit
code thus propagating any child's non-zero exit code to parent.

******************************************************************************/

void mimestruct::goodexit(int exitcode)
{
	if (outputfilebuf.pubsync() < 0 || !outputfp)
	{
		perror("makemime");
		exit(1);
	}

	/*
	** Drain any leftover input, so that the child doesn't get
	** a SIGPIPE.
	*/

	{
		char buf[BUFSIZ];

		while (inputfilebuf1.sgetn(buf, BUFSIZ) > 0)
			;

		while (inputfilebuf2.sgetn(buf, BUFSIZ) > 0)
			;
	}

	if (!inputfp1 || !inputfp2)
	{
		perror("makemime");
		exitcode=1;
	}

	inputfilebuf1=rfc822::fdstreambuf{};
	inputfilebuf2=rfc822::fdstreambuf{};
	outputfilebuf=rfc822::fdstreambuf{};

	while (child1 > 0 && child2 > 0)
	{
	int	waitstat;
	pid_t	p=wait(&waitstat);

		if (p <= 0 && errno == ECHILD)	break;

		if (p == child1)
			child1=0;
		else if (p == child2)
			child2=0;
		else	continue;
		if (waitstat)	exitcode=1;
	}
	exit(exitcode);
}

int main(int argc, char **argv)
{
	signal(SIGCHLD, SIG_DFL);

	if (argc)
	{
		--argc;
		++argv;
	}

	std::vector<std::string> args{argv, argv+argc};

	if (!args.empty() && *args[0].c_str() == '@')
		read_args(args, args[0].substr(1));

	auto b=args.cbegin(), e=args.cend();
	mimestruct m{b, e};
	if (b != e)	usage();	/* Some arguments left */

	(m.*m.open_func)();
	(m.*m.handler_func)();
	m.goodexit(0);
	return (0);
}

void mimestruct::createsimplemime()
{
	const char *orig_charset=textplaincharset;

	/*
	** Determine encoding by reading the file, running it through
	** encodeautodetect
	*/

	if (mimeencoding == 0)
	{
		auto	orig_pos=inputfilebuf1.tell();
		bool	binaryflag;

		if (orig_pos == -1)
		{
			perror("ftell");
			goodexit(1);
		}

		std::tie(mimeencoding, binaryflag)=
			 rfc822::libmail_encode_autodetect(
				 inputfilebuf1, false
			 );

		if (inputfilebuf1.pubseekpos(orig_pos) < 0)
		{
			perror("fseek");
			goodexit(1);
		}

		if (strcmp(mimetype, "auto") == 0)
			mimetype=binaryflag
				? (orig_charset=0,
				   "application/octet-stream"):"text/plain";
	}

	for (auto &a:aheaders)
		outputfp << a << "\n";

	outputfp << "Content-Type: " << mimetype;

	if (orig_charset && *orig_charset)
	{
		const char *c;

		outputfp << "; charset=\"";
		for (c=orig_charset; *c; c++)
		{
			if (*c != '"' && *c != '\\')
				outputfp << *c;
		}
		outputfp << "\"";
	}

	if (contentname && *contentname)
	{
		rfc2231_attr_encode(
			"name",
			contentname,
			unicode_default_chset(),
			"",
			[this]
			(const char *param,
			 const char *value)
			{
				outputfp << ";\n  " << param << "=" << value;
			});
	}

	outputfp << "\nContent-Transfer-Encoding: "
		   << mimeencoding << "\n\n";

	rfc822::encode encoder{
		[this]
		(const char *p, size_t n)
		{
			if (outputfilebuf.sputn(p, n) < 0)
			{
				perror("write");
				exit(1);
			}
		},
		mimeencoding
	};

	{
		char input_buf[BUFSIZ];

		while (1)
		{
			auto n=inputfilebuf1.sgetn(
				input_buf, sizeof(input_buf)
			);

			if (n < 0)
			{
				perror("read");
				exit(1);
			}

			if (n == 0)
				break;

			encoder(input_buf, n);
		}
	}
}

/******************************************************************************

Satisfy paranoia by making sure that the MIME boundary we picked does not
appear in the contents of the bounded section.

******************************************************************************/

bool mimestruct::tryboundary(rfc822::fdstreambuf &sb, std::string_view bbuf)
{
	std::string s;
	auto	orig_pos=sb.tell();

	if (orig_pos == -1)
	{
		perror("tell");
		goodexit(1);
	}

	bool tryagain=false;

	std::istream i{&sb};
	while (std::getline(i, s))
	{
		const char *p=s.c_str();
		if (p[0] == '-' && p[1] == '-' &&
		    strncmp(p+2, bbuf.data(), bbuf.size()) == 0)
		{
			tryagain=true;
			break;
		}
	}

	if (!i.eof() || sb.pubseekpos(orig_pos) < 0)
	{
		perror("fseek");
		goodexit(1);
	}

	return tryagain;
}

/******************************************************************************

Create a MIME boundary for some content.

******************************************************************************/

std::string mimestruct::mkboundary(rfc822::fdstreambuf &sb)
{
	pid_t	pid=getpid();
	time_t	t;
	static unsigned n=0;
	char bbuf[NUMBUFSIZE*4];
	char	buf[NUMBUFSIZE];

	time(&t);

	do
	{
		strcpy(bbuf, "=_");
		strcat(bbuf, libmail_str_size_t(++n, buf));
		strcat(bbuf, "_");
		strcat(bbuf, libmail_str_time_t(t, buf));
		strcat(bbuf, "_");
		strcat(bbuf, libmail_str_pid_t(pid, buf));
	} while (tryboundary(sb, bbuf));
	return (bbuf);
}

void mimestruct::createmultipartmime()
{
	auto b=mkboundary(inputfilebuf1);

	if (mimeencoding == 0)
		mimeencoding="8bit";

	for (auto &a:aheaders)
		outputfp << a << "\n";
	outputfp <<"Content-Type: "
		 << mimetype
		 << "; boundary=\""
		 << b
		 << "\"\n"
		"Content-Transfer-Encoding: "
		 << mimeencoding
		 << "\n\n"
		RFC2045MIMEMSG
		"\n--"
		 << b
		 << "\n";

	copyfilebuf1();

	outputfp << "\n--" << b << "--\n";
}

void mimestruct::copyfilebuf1()
{
	copyfilebuf(inputfilebuf1);
}

void mimestruct::copyfilebuf2()
{
	copyfilebuf(inputfilebuf2);
}

void mimestruct::copyfilebuf(rfc822::fdstreambuf &sb)
{

	char buf[BUFSIZ];

	while (1)
	{
		auto n=sb.sgetn(buf, BUFSIZ);
		if (n < 0)
		{
			perror("read");
			goodexit(1);
		}

		if (n == 0)
			break;

		if (outputfilebuf.sputn(buf, n) != n)
		{
			perror("write");
			goodexit(1);
		}
	}
}

void mimestruct::joinmultipart()
{
	std::string new_boundary;
	std::string old_boundary;
	std::string s;

	do
	{
		new_boundary=mkboundary(inputfilebuf1);
	} while (tryboundary(inputfilebuf2, new_boundary));

	/* Copy the header */

	std::string headername;

	for (;;)
	{
		if (!std::getline(inputfp2, s))
		{
			s.clear();
			break;
		}

		if (s == "\r" || s == "" || strncmp(s.c_str(), "--", 2) == 0)
			break;

		headername.clear();
		headername.insert(headername.end(),
				  s.begin(),
				  std::find(s.begin(), s.end(), ':'));

		for (auto &c:headername)
			if (c >= 'A' && c <= 'Z')
				c += 'a'-'A';

		if (headername != "content-type")
		{
			outputfp << s << "\n";
			continue;
		}

		auto p=std::find(s.begin(), s.end(), ';');

		outputfp << std::string_view{s.c_str(),
				static_cast<size_t>(p-s.begin())}
			<<"; boundary=\""
			 << new_boundary << "\"\n";

		while (1)
		{
			auto c=inputfilebuf2.sgetc();

			if (c < 0)
				break;

			if (c == '\n' || (c != ' '))
				break;

			std::getline(inputfp2, s);
		}
	}

	std::string boundary_candidate;

	do
	{
		if (strncmp(s.c_str(), "--", 2) == 0)
		{
			if (old_boundary.empty())
			{
				old_boundary=s;
				if (old_boundary.back() == '\r')
					old_boundary.pop_back();

				auto ptr=old_boundary.end();

				if (ptr[-1] == '-' && ptr[-2] == '-')
				{
					old_boundary.pop_back();
					old_boundary.pop_back();
				}
				for (auto &c:old_boundary)
					if (c >= 'A' && c <= 'Z')
						c += 'a'-'A';
			}

			boundary_candidate=s;

			for (auto &c:boundary_candidate)
				if (c >= 'A' && c <= 'Z')
					c += 'a'-'A';

			if (s.size() >= old_boundary.size() &&
			    std::equal(old_boundary.begin(), old_boundary.end(),
				       s.begin()))
			{
				if (s.back() == '\r')
					s.pop_back();

				if (s.size() >= 4 && std::string_view{
						s.c_str()+s.size()-2, 2}
					== "--")
					break;
				outputfp << "--" << new_boundary << "\n";
				continue;
			}
		}
		outputfp << s << "\n";;
	} while (std::getline(inputfp2, s));

	outputfp << "--" << new_boundary << "\n";

	copyfilebuf1();

	outputfp << "\n--" << new_boundary << "--\n";
	goodexit(0);
}

/******************************************************************************

Open input from a child process

******************************************************************************/

rfc822::fdstreambuf mimestruct::openchild(mimestruct &child,
					  pid_t	*pidptr,
					  int usescratch)
{
int	pipefd[2];
char	buf[NUMBUFSIZE+1];
char	buf2[NUMBUFSIZE+1];

	if (pipe(pipefd) < 0)
	{
		perror("pipe");
		exit(1);
	}

	*pidptr=fork();

	if (*pidptr < 0)
	{
		perror("fork");
		exit(1);
	}

	if (*pidptr == 0)
	{
		/* Duplicate pipe on stdout */

		close(pipefd[0]);
		dup2(pipefd[1], 1);
		close(pipefd[1]);

		/* Close any input files opened by parent */

		inputfilebuf1=rfc822::fdstreambuf{};
		inputfilebuf2=rfc822::fdstreambuf{};

		/* Open, then execute the child process */

		(child.*(child.open_func))();
		(child.*(child.handler_func))();
		child.goodexit(0);
	}
	close(pipefd[1]);

	/*
	** Open the pipe by calling openfile(), automatically creating
	** the scratch file, if necessary.
	*/

	buf[0]='&';
	strcpy(buf+1, libmail_str_size_t(pipefd[0], buf2));

	rfc822::fdstreambuf fp=
		usescratch ? openfile(buf)
		:openfile_or_pipe(buf, "r");
	close(pipefd[0]);	/* fd was duped by openfile */
	return (fp);
}

void mimestruct::openoutput()
{
	outputfilebuf=openfile_or_pipe(outputfile, std::string_view{"w"});
}

void mimestruct::openjoinmultipart()
{
	/* number two is the multipart section */
	if (inputchild2)
		inputfilebuf2=openchild(*inputchild2, &child2, 1);
	else
		inputfilebuf2=openfile(inputfile2);


	if (inputchild1)
		inputfilebuf1=openchild(*inputchild1, &child1, 1);
	else
		inputfilebuf1=openfile(inputfile1);
	openoutput();
}

void mimestruct::opencreatesimplemime()
{
	if (inputchild1)
		inputfilebuf1=openchild(*inputchild1, &child1,
			mimeencoding ? 0:1);
	else
		inputfilebuf1= mimeencoding
			? openfile_or_pipe(inputfile1, "r")
			: openfile(inputfile1);
	openoutput();
}

void mimestruct::opencreatemultipartmime()
{
	if (inputchild1)
		inputfilebuf1=openchild(*inputchild1, &child1, 1);
	else
		inputfilebuf1=openfile_or_pipe(inputfile1, "r");
	openoutput();
}
