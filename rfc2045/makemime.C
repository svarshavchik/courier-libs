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

#include	<vector>
#include	<string>
#include	<algorithm>

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

static int fd_wanted(const char *filename, const char *mode)
{
	if (strcmp(filename, "-") == 0)		/* stdin or stdout */
		return strcmp(mode, "r") ? 1:0;
	if (*filename == '&')
		return atoi(filename+1);	/* file descriptor */
	return -1;				/* or a file */
}

/******************************************************************************

Open some file or a pipe for reading and writing.

******************************************************************************/

static FILE *openfile_or_pipe(const char *filename, const char *mode)
{
int	fd, fd_to_dup = fd_wanted(filename, mode);
FILE	*fp;

	if (fd_to_dup == 0)
		return stdin;

	if (fd_to_dup >= 0)
		fd = dup(fd_to_dup);
	else
		fd=open(filename, (strcmp(mode, "r") ?
			O_WRONLY|O_CREAT|O_TRUNC:O_RDONLY), 0666);

	if (fd < 0)
	{
		perror(filename);
		exit(1);
	}
	fp=fdopen(fd, mode);
	if (!fp)
	{
		perror("fdopen");
		exit(1);
	}
	return (fp);
}

/******************************************************************************

Open some file.  If we get a pipe, open a temporary file, and drain pipe's
contents into it.

******************************************************************************/

static FILE *openfile(const char *filename)
{
FILE	*fp=openfile_or_pipe(filename, "r");
int	fd=fileno(fp);

	if (!isreg(fd))	/* Must be a pipe */
	{
	FILE *t=tmpfile();
	int	c;

		if (!t)
		{
			perror("tmpfile");
			exit(1);
		}

		while ((c=getc(fp)) != EOF)
			putc(c, t);
		if (ferror(fp) || fflush(t)
			|| ferror(t) || fseek(t, 0L, SEEK_SET) == -1)
		{
			perror("write");
			exit(1);
		}
		fclose(fp);
		fp=t;
	}
	else
	{
	off_t	orig_pos = lseek(fd, 0L, SEEK_CUR);

		if (orig_pos == -1 ||
			fseek(fp, orig_pos, SEEK_SET) == -1)
		{
			perror("fseek");
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
	FILE	*fp=openfile_or_pipe(file.c_str(), "r");
	char	buffer[BUFSIZ];
	char	*p;
	int	c;

	args.clear();

	while (fgets(buffer, sizeof(buffer), fp) != 0)
	{
	const	char *q;

		if ((p=strchr(buffer, '\n')) != 0)
			*p=0;
		else while ((c=getc(fp)) != '\n' && c != EOF)
			;	/* Just dump the excess */

		/* Skip the filler. */

		q=buffer;
		while (*q && isspace((int)(unsigned char)*q))
			++q;
		if (!*q)	continue;
		if (*q == '#')	continue;
		if (strcmp(buffer, "-") == 0)
		{
			if (isreg(fileno(fp)))
			{
				long orig_pos = ftell(fp);
				if (orig_pos == -1 ||
					lseek(fileno(fp), orig_pos, SEEK_SET) == -1)
				{
					perror("seek");
					exit(1);
				}
			}
			break;
		}

		args.push_back(q);
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

	const char *inputfile1=nullptr, *inputfile2=nullptr;

	// Child mimestructs, each vector will have at most 1 child struct
	std::vector<mimestruct> inputchild1, inputchild2;
	FILE *inputfp1=nullptr, *inputfp2=nullptr;
	pid_t	child1=0, child2=0;

	/* Output file.  Defaults to "-", stdout */

	const char *outputfile=nullptr;
	FILE	*outputfp=nullptr;

		/* The handler and open functions */

	void (mimestruct::*handler_func)()=nullptr;
	void (mimestruct::*open_func)()=nullptr;

	void createsimplemime();
	void createmultipartmime();
	void joinmultipart();

	void opencreatesimplemime();
	void opencreatemultipartmime();
	void openjoinmultipart();

	FILE *openchild(mimestruct *child,
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
} ;

/******************************************************************************

Recursively build the mimestruct tree.

******************************************************************************/

mimestruct parseargs(std::vector<std::string>::const_iterator &argsb,
		     std::vector<std::string>::const_iterator &argse)
{
	mimestruct m;

	if (argsb == argse)	usage();

	if (argsb->compare(0, 2, "-c") == 0)
	{
		m.handler_func= &mimestruct::createsimplemime;
		m.open_func= &mimestruct::opencreatesimplemime;
		if (argsb->size() > 2)
		{
			m.mimetype=argsb->c_str()+2;
			++argsb;
		}
		else
		{
			++argsb;
			if (argsb != argse &&
			    *argsb->c_str() != '-' && *argsb->c_str() != ')')
			{
				m.mimetype=argsb->c_str();
				++argsb;
			}
			else
				m.mimetype="application/octet-stream";
		}

		while (isspace((int)(unsigned char)*m.mimetype))
			++m.mimetype;
	}
	else if (argsb->compare(0, 2, "-m") == 0)
	{
		m.handler_func= &mimestruct::createmultipartmime;
		m.open_func= &mimestruct::opencreatemultipartmime;
		if (argsb->size() > 2)
		{
			m.mimetype=argsb->c_str()+2;
			++argsb;
		}
		else
		{
			++argsb;
			if (argsb != argse && *argsb->c_str() != '-' &&
			    *argsb->c_str() != ')')
			{
				m.mimetype=argsb->c_str();
				++argsb;
			}
			else
				m.mimetype="multipart/mixed";
		}
		while (isspace((int)(unsigned char)*m.mimetype))
			++m.mimetype;
	}
	else if (argsb->compare(0, 2, "-j") == 0)
	{
		const char *filename;

		m.handler_func= &mimestruct::joinmultipart;
		m.open_func= &mimestruct::openjoinmultipart;
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
			m.inputchild2.push_back(parseargs(argsb, argse));
			if (argsb == argse || *argsb != ")")
				usage();
			++argsb;
		}
		else
			m.inputfile2=filename;
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
			m.outputfile=f;
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
			m.textplaincharset=f;
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
			m.contentname=f;
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

			m.mimeencoding=f;
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

			m.aheaders.push_back(f);
			continue;
		}
		break;
	}

	/* We must now have the input file argument */

	if (argsb == argse)	usage();

	if (*argsb == "(")
	{
		++argsb;
		m.inputchild1.push_back(parseargs(argsb, argse));
		if (argsb == argse || *argsb != ")")
			usage();
		++argsb;
	}
	else
	{
		m.inputfile1=argsb->c_str();
		++argsb;
	}

	return (m);
}

/******************************************************************************

After we're done, terminate with a zero exit code if all child processes also
terminated with a zero exit code.  Otherwise, terminate with a non-zero exit
code thus propagating any child's non-zero exit code to parent.

******************************************************************************/

void mimestruct::goodexit(int exitcode)
{
	if (outputfp && (fflush(outputfp) || ferror(outputfp)))
	{
		perror("makemime");
		exit(1);
	}

	/*
	** Drain any leftover input, so that the child doesn't get
	** a SIGPIPE.
	*/

	while (inputfp1 && !feof(inputfp1) && !ferror(inputfp1))
		getc(inputfp1);

	while (inputfp2 && !feof(inputfp2) && !ferror(inputfp2))
		getc(inputfp2);

	if (inputfp1)
	{
		if (ferror(inputfp1))
		{
			perror("makemime");
			exitcode=1;
		}

		fclose(inputfp1);
	}
	if (inputfp2)
	{
		if (ferror(inputfp2))
		{
			perror("makemime");
			exitcode=1;
		}

		fclose(inputfp2);
	}

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
	mimestruct m{parseargs(b, e)};
	if (b != e)	usage();	/* Some arguments left */

	(m.*m.open_func)();
	(m.*m.handler_func)();
	m.goodexit(0);
	return (0);
}

static int encode_outfp(const char *p, size_t n, void *vp)
{
	if (fwrite(p, n, 1, *(FILE **)vp) != 1)
		return -1;
	return 0;
}

static int do_printRfc2231Attr(const char *param,
			       const char *value,
			       void *voidArg)
{
	fprintf( ((mimestruct *)voidArg)->outputfp,
		 ";\n  %s=%s", param, value);
	return 0;
}

void mimestruct::createsimplemime()
{
	struct libmail_encode_info encode_info;
	const char *orig_charset=textplaincharset;

	/* Determine encoding by reading the file, as follows:
	**
	** Default to 7bit.  Use 8bit if high-ascii bytes found.  Use
	** quoted printable if lines more than 200 characters found.
	** Use base64 if a null byte is found.
	*/

	if (mimeencoding == 0)
	{
		long	orig_pos=ftell(inputfp1);
		int	binaryflag;

		if (orig_pos == -1)
		{
			perror("ftell");
			goodexit(1);
		}

		mimeencoding=libmail_encode_autodetect_fpoff(inputfp1,
								0,
								0, -1,
								&binaryflag);

		if (ferror(inputfp1)
			|| fseek(inputfp1, orig_pos, SEEK_SET)<0)
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
		fprintf(outputfp, "%s\n", a.c_str());

	fprintf(outputfp, "Content-Type: %s", mimetype);
	if (orig_charset && *orig_charset)
	{
		const char *c;

		fprintf(outputfp, "; charset=\"");
		for (c=orig_charset; *c; c++)
		{
			if (*c != '"' && *c != '\\')
				putc(*c, outputfp);
		}
		fprintf(outputfp, "\"");
	}

	if (contentname && *contentname)
	{
		const char *chset=textplaincharset ? textplaincharset
			: "utf-8";

		rfc2231_attrCreate("name", contentname, chset, NULL,
				   do_printRfc2231Attr, this);
	}

	fprintf(outputfp, "\nContent-Transfer-Encoding: %s\n\n",
		mimeencoding);

	libmail_encode_start(&encode_info, mimeencoding,
			     &encode_outfp,
			     &outputfp);
	{
		char input_buf[BUFSIZ];
		int n;

		while ((n=fread(input_buf, 1, sizeof(input_buf),
				inputfp1)) > 0)
		{
			if ( libmail_encode(&encode_info, input_buf, n))
				break;
		}

		libmail_encode_end(&encode_info);
	}
}

/******************************************************************************

Satisfy paranoia by making sure that the MIME boundary we picked does not
appear in the contents of the bounded section.

******************************************************************************/

static int tryboundary(mimestruct *m, FILE *f, const char *bbuf)
{
char	buf[BUFSIZ];
char	*p;
int	l=strlen(bbuf);
int	c;
long	orig_pos=ftell(f);

	if (orig_pos == -1)
	{
		perror("ftell");
		m->goodexit(1);
	}

	while ((p=fgets(buf, sizeof(buf), f)) != 0)
	{
		if (p[0] == '-' && p[1] == '-' &&
			strncmp(p+2, bbuf, l) == 0)
			break;

		if ((p=strchr(buf, '\n')) != 0)
			*p=0;
		else while ((c=getc(f)) != EOF && c != '\n')
			;
	}

	if (ferror(f) || fseek(f, orig_pos, SEEK_SET)<0)
	{
		perror("fseek");
		m->goodexit(1);
	}

	return (p ? 1:0);
}

/******************************************************************************

Create a MIME boundary for some content.

******************************************************************************/

static const char *mkboundary(mimestruct *m, FILE *f)
{
pid_t	pid=getpid();
time_t	t;
static unsigned n=0;
static char bbuf[NUMBUFSIZE*4];
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
	} while (tryboundary(m, f, bbuf));
	return (bbuf);
}

void mimestruct::createmultipartmime()
{
	const char *b=mkboundary(this, inputfp1);
	int	c;

	if (mimeencoding == 0)
		mimeencoding="8bit";

	for (auto &a:aheaders)
		fprintf(outputfp, "%s\n", a.c_str());
	fprintf(outputfp, "Content-Type: %s; boundary=\"%s\"\n"
			"Content-Transfer-Encoding: %s\n\n"
			RFC2045MIMEMSG
			"\n--%s\n",
		mimetype, b,
		mimeencoding,
		b);
	while ((c=getc(inputfp1)) != EOF)
		putc(c, outputfp);
	fprintf(outputfp, "\n--%s--\n", b);
}

void mimestruct::joinmultipart()
{
const char *new_boundary;
char	*old_boundary=0;
int	old_boundary_len=0;
char	buffer[BUFSIZ];
char	*p;
int	c;

	do
	{
		new_boundary=mkboundary(this, inputfp1);
	} while (tryboundary(this, inputfp2, new_boundary));

	/* Copy the header */

	for (;;)
	{
		if (fgets(buffer, sizeof(buffer), inputfp2) == 0)
		{
			buffer[0]=0;
			break;
		}

		if (strcmp(buffer, "\r\n") == 0 ||
		    buffer[0] == '\n' || strncmp(buffer, "--", 2) == 0)
			break;

		if (strncasecmp(buffer, "content-type:", 13))
		{
			fprintf(outputfp, "%s", buffer);
			if ((p=strchr(buffer, '\n')) != 0)	continue;
			while ((c=getc(inputfp2)) != EOF && c != '\n')
				putc(c, outputfp);
			continue;
		}

		if ((p=strchr(buffer, '\n')) == 0)
			while ((c=getc(inputfp2)) != EOF && c != '\n')
				;

		p=strchr(buffer+13, ';');
		if (p)	*p=0;
		fprintf(outputfp, "Content-Type:%s; boundary=\"%s\"\n",
			buffer+13, new_boundary);

		for (;;)
		{
			c=getc(inputfp2);
			if (c != EOF)	ungetc(c, inputfp2);
			if (c == '\n' || !isspace((int)(unsigned char)c))
				break;
			while ((c=getc(inputfp2)) != EOF && c != '\n')
				;
		}
	}

	do
	{
		if (strncmp(buffer, "--", 2) == 0)
		{
			if (old_boundary == 0)
			{
				old_boundary=static_cast<char *>(malloc(strlen(buffer)+1));
				if (!old_boundary)
				{
					perror("malloc");
					exit(1);
				}
				strcpy(old_boundary, buffer);
				if ((p=strchr(old_boundary, '\n')) != 0)
				{
					if (p > old_boundary && p[-1] == '\r')
						--p;
					*p=0;
				}
				p=old_boundary+strlen(old_boundary);
				if (p >= old_boundary+4 &&
					strcmp(p-2, "--") == 0)
					p[-2]=0;
				old_boundary_len=strlen(old_boundary);
			}


			if (strncasecmp(buffer, old_boundary,
				old_boundary_len) == 0)
			{
				if ((p=strchr(buffer, '\n')) != 0)
					*p=0;
				else while ((c=getc(inputfp2)) != '\n'
					&& c != EOF)
					;

				c=strlen(buffer);
				if (c > 0 && buffer[c-1] == '\r')
					buffer[--c]=0;

				if (c >= 4 && strcmp(buffer+(c-2), "--") == 0)
					break;
				fprintf(outputfp, "--%s\n",
					new_boundary);
				continue;
			}
		}
		fprintf(outputfp, "%s", buffer);
		if ((p=strchr(buffer, '\n')) == 0)
			while ((c=getc(inputfp2)) != '\n' && c != EOF)
				;
	} while (fgets(buffer, sizeof(buffer), inputfp2) != 0);

	fprintf(outputfp, "--%s\n", new_boundary);

	while ((c=getc(inputfp1)) != EOF)
		putc(c, outputfp);

	fprintf(outputfp, "\n--%s--\n", new_boundary);
	goodexit(0);
}

/******************************************************************************

Open input from a child process

******************************************************************************/

FILE *mimestruct::openchild(mimestruct *child,
			    pid_t	*pidptr,
			    int usescratch)
{
int	pipefd[2];
char	buf[NUMBUFSIZE];
char	buf2[NUMBUFSIZE+1];
FILE	*fp;

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

		if (inputfp1)	fclose(inputfp1);
		if (inputfp2)	fclose(inputfp2);

		/* Open, then execute the child process */

		(child->*child->open_func)();
		(child->*child->handler_func)();
		child->goodexit(0);
	}
	close(pipefd[1]);

	/*
	** Open the pipe by calling openfile(), automatically creating
	** the scratch file, if necessary.
	*/

	buf[0]='&';
	strcpy(buf+1, libmail_str_size_t(pipefd[0], buf2));

	fp= usescratch ? openfile(buf):openfile_or_pipe(buf, "r");
	close(pipefd[0]);	/* fd was duped by openfile */
	return (fp);
}

void mimestruct::openoutput()
{
	if (!outputfile)
		outputfile="-";

	outputfp= openfile_or_pipe(outputfile, "w");
}

void mimestruct::openjoinmultipart()
{
	/* number two is the multipart section */
	if (inputchild2.size())
		inputfp2=openchild(&inputchild2[0], &child2, 1);
	else
		inputfp2=openfile(inputfile2);


	if (inputchild1.size())
		inputfp1=openchild(&inputchild1[0], &child1, 1);
	else
		inputfp1=openfile(inputfile1);
	openoutput();
}

void mimestruct::opencreatesimplemime()
{
	if (inputchild1.size())
		inputfp1=openchild(&inputchild1[0], &child1,
			mimeencoding ? 0:1);
	else
		inputfp1= mimeencoding
			? openfile_or_pipe(inputfile1, "r")
			: openfile(inputfile1);
	openoutput();
}

void mimestruct::opencreatemultipartmime()
{
	if (inputchild1.size())
		inputfp1=openchild(&inputchild1[0], &child1, 1);
	else
		inputfp1=openfile_or_pipe(inputfile1, "r");
	openoutput();
}
