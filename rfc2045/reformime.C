/*
** Copyright 1998 - 2018 Double Precision, Inc.  See COPYING for
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
#include	<langinfo.h>
#include	<sstream>

#if	HAVE_STRINGS_H
#include	<strings.h>
#endif

#if	HAVE_LOCALE_H
#include	<locale.h>
#endif

#include	<stdlib.h>
#include	<ctype.h>
#include	<pwd.h>
#include	<fcntl.h>
#include	<signal.h>
#include	"rfc2045.h"
#include	"encode.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045charset.h"
#include	<courier-unicode.h>

#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	"numlib/numlib.h"
#include	<iostream>
#include	<algorithm>
#include	<charconv>
#include	<fstream>

#if     HAS_GETHOSTNAME
#else
int gethostname(const char *, size_t);
#endif

extern int rfc2045_in_reformime;

static const char *defchset;


void rfc2045_error(const char *errmsg)
{
	fprintf(stderr, "reformime: %s\n", errmsg);
	exit(1);
}

void usage()
{
	std::cerr <<
		"Usage: reformime [options]\n"
		"    -d - parse a delivery status notification.\n"
		"    -e - extract contents of MIME section.\n"
		"    -x - extract MIME section to a file.\n"
		"    -X - pipe MIME section to a program.\n"
		"    -i - show MIME info.\n"
		"    -V - Validate MIME correctness.\n"
		"    -s n.n.n.n[,n.n.n.n]* - specify MIME section(s).\n"
		"    -r - rewrite message, filling in missing MIME headers.\n"
		"    -r7 - also convert 8bit/raw encoding to quoted-printable, if possible.\n"
		"    -r8 - also convert quoted-printable encoding to 8bit, if possible.\n"
		"    -rU - convert quoted-printable encoding to 8bit, unconditionally.\n"
		"    -c charset - default charset for rewriting, -o, and -O.\n"
		"    -m [file] [file]... - create a MIME message digest.\n"
		"    -h \"header\" - decode RFC 2047-encoded header.\n"
		"    -o \"header\" - encode unstructured header using RFC 2047.\n"
		"    -O \"header\" - encode address list header using RFC 2047.\n\n";

	exit(1);
}

static char *tempname(const char *tempdir)
{
char	pidbuf[NUMBUFSIZE], timebuf[NUMBUFSIZE], hostnamebuf[256];
static unsigned counter=0;
time_t	t;
char	*p;

	libmail_str_pid_t(getpid(), pidbuf);
	time(&t);
	libmail_str_time_t(t, timebuf);
	hostnamebuf[sizeof(hostnamebuf)-1]=0;
	if (gethostname(hostnamebuf, sizeof(hostnamebuf)))
		hostnamebuf[0]=0;
	p=(char *)malloc(strlen(tempdir)+strlen(pidbuf)+strlen(timebuf)+
			 strlen(hostnamebuf)+100);
	if (!p)	return (0);
	sprintf(p, "%s/%s.%s-%u.%s", tempdir, timebuf, pidbuf, counter++,
		hostnamebuf);
	return (p);
}

void read_message()
{
	char	buf[BUFSIZ];
	FILE	*tempfp=0;
	ssize_t l;

	if (fseek(stdin, 0L, SEEK_END) < 0 ||
		fseek(stdin, 0L, SEEK_SET) < 0)	/* Pipe, save to temp file */
	{
		tempfp=tmpfile();

		while ((l=fread(buf, 1, sizeof(buf), stdin)) > 0)
		{
			if (fwrite(buf, l, 1, tempfp) != 1)
			{
				perror("fwrite");
				exit(1);
			}
		}

		dup2(fileno(tempfp), 0);
		fclose(tempfp);
	}
}

static void notfound(const char *p)
{
	fprintf(stderr, "reformime: MIME section %s not found.\n", p);
	exit(1);
}

std::string idstr(const std::vector<int> &id)
{
	std::ostringstream o;

	const char *idsep="";
	for (int s:id)
	{
		o << idsep << s;
		idsep=".";
	}
	return o.str();
}

void print_info(const rfc2045::entity &entity,
		const std::vector<int> &id)
{
	std::cout << "section: " << idstr(id)
		  << "\ncontent-type: " << entity.content_type.value
		  << "\n";

	auto value=entity.content_type.parameters.find("name");
	if (value != entity.content_type.parameters.end())
		std::cout << "content-name: "
			  << value->second.value_in_charset()
			  << "\n";

	std::cout << "content-transfer-encoding: "
		  << rfc2045::to_cte(entity.content_transfer_encoding)
		  << "\n";
	std::cout << "charset: " << entity.content_type_charset()
		  << "\n";

	rfc2045::entity::rfc2231_header content_disposition{
		entity.content_disposition
	};

	if (content_disposition.value.size())
		std::cout << "content-disposition: "
			  << content_disposition.value << "\n";

	auto cd_value=content_disposition.parameters.find("name");
	if (cd_value != content_disposition.parameters.end())
		std::cout << "content-disposition-name: "
			  << cd_value->second.value_in_charset()
			  << "\n";

	cd_value=content_disposition.parameters.find("filename");
	if (cd_value != content_disposition.parameters.end())
		std::cout << "content-disposition-filename: "
			  << cd_value->second.value_in_charset()
			  << "\n";

	if (!entity.content_id.empty())
		std::cout << "content-id: <" << entity.content_id << ">\n";
	if (!entity.content_description.empty())
	{
		std::cout << "content-description: ";
		rfc822::display_header(
			"content-description",
			entity.content_description,
			rfc2045_getdefaultcharset(),
			std::ostreambuf_iterator<char>{std::cout});
		std::cout << "\n";
	}

	std::cout << "starting-pos: " << entity.startpos << "\n";
	std::cout << "starting-pos-body: " << entity.startbody << "\n";
	std::cout << "ending-pos: " << entity.endbody << "\n";
	std::cout << "line-count: " << entity.nlines << "\n";
	std::cout << "body-line-count: " << entity.nbodylines << "\n\n";
}

void do_print_section(const rfc2045::entity &e,
		      std::streambuf &src,
		      std::streambuf &out)
{
	rfc2045::entity::line_iter<false>::decoder decoder{
		[&]
		(const char *p, size_t n)
		{
			out.sputn(p, n);
		},
		src
	};

	decoder.decode_header=false;
	decoder.decode_body=true;
	decoder.decode_subentities=false;

	decoder.decode(e);
}

void rewrite(rfc2045::entity &message, rfc822::fdstreambuf &src,
	     rfc2045::convert rwmode)
{
	auto out_p=std::cout.rdbuf();

	if (!message.autoconvert_check(rwmode))
	{
		std::copy(std::istreambuf_iterator<char>{&src},
			  std::istreambuf_iterator<char>{},
			  std::ostreambuf_iterator<char>{out_p});
		return;
	}

	rfc2045::entity::line_iter<false>::autoconvert(
		message,
		[&]
		(const char *p, size_t n)
		{
			if (static_cast<size_t>(out_p->sputn(p, n)) != n)
			{
				perror("write");
				exit(1);
			}
		},
		src,
		"reformime (" RFC2045PKG " " RFC2045VER ")"
	);
}

std::string get_suitable_filename(const rfc2045::entity &message,
				  std::string_view pfix,
				  bool ignore_filename)
{
	std::string filename;

	rfc2045::entity::rfc2231_header content_disposition{
		message.content_disposition
	};

	auto cd_value=content_disposition.parameters.find("filename");
	if (cd_value != content_disposition.parameters.end())
		filename=cd_value->second.value_in_charset();

	if (filename.empty())
	{
		cd_value=content_disposition.parameters.find("name");
		if (cd_value != content_disposition.parameters.end())
			filename=cd_value->second.value_in_charset();
	}

	if (filename.empty())
	{
		auto cd_value=message.content_type.parameters.find("name");
		if (cd_value != content_disposition.parameters.end())
			filename=cd_value->second.value_in_charset();
	}

	// Strip leading/trailing spaces

	filename.erase(
		filename.begin(),
		std::find_if(filename.begin(), filename.end(),
			     [](unsigned c){ return !isspace(c);})
	);

	for (auto b=filename.begin(), e=filename.end(); b != e; --e)
	{
		unsigned char c=e[-1];

		if (!isspace(c))
		{
			filename.erase(e, filename.end());
			break;
		}
	}

	for (auto b=filename.begin(), e=filename.end(); b != e; --e)
	{
		unsigned char c=e[-1];

		if (c == '/' || c == '\\')
		{
			filename.erase(filename.begin(), e);
			break;
		}
	}

	if (filename.size()>32)
	{
		filename.erase(filename.begin(), filename.end()-32);
	}

	if (ignore_filename)
	{
		char	numbuf[sizeof(size_t)*2+1];
		static size_t counter=0;

		auto p=std::to_chars(numbuf, numbuf+sizeof(numbuf)-1,
				     ++counter, 16).ptr;

		*p++='-';

		filename.insert(filename.begin(), numbuf, p);
	}
	else if (filename.empty())
	{
		filename=tempname(".");
		filename.erase(filename.begin(),
			       filename.begin()+2);	/* Skip over ./ */
	}

	filename.insert(filename.begin(), pfix.begin(), pfix.end());

	for (char &c:filename)
	{
		unsigned char d=c;
		if (!isalnum(d) && d != '.' && d != '-' && d != '=')
			c='_';
	}
	if (pfix.size() == 0)
	{
		std::fstream tty{"/dev/tty"};

		if (!tty)
		{
			perror("/dev/tty");
			exit(1);
		}

		tty << "Extract " << message.content_type.value
		    << "? " << std::flush;

		std::string resp;

		std::getline(tty, resp);

		switch (*resp.c_str()) {
		case 'y':
		case 'Y':
			break;
		default:
			filename.clear();
			return filename;
		}

		tty.close();
		tty.open("/dev/tty");
		if (!tty)
		{
			perror("/dev/tty");
			exit(1);
		}
		tty << "Filename [" << filename << "]: " << std::flush;
		std::getline(tty, resp);

		if (!resp.empty())
			filename=resp;
	}

	return filename;
}

void extract_file(const rfc2045::entity &message,
		  std::streambuf &source,
		  std::string_view filename,
		  int argc, char **argv)
{
	bool ignore=false;
	int fd;

	for (;;)
	{
		auto f=get_suitable_filename(message, filename, ignore);

		if (f.empty())
			return;

		fd=open(f.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0666);
		if (fd < 0)
		{
			if (errno == EEXIST)
			{
				std:: cout << f << " exists.\n";
				ignore=true;
				continue;
			}

			perror(f.c_str());
			exit(1);
		}
		break;
	}

	rfc822::fdstreambuf out{fd};

	do_print_section(message, source, out);

	if (out.pubsync() < 0)
	{
		perror("write");
		exit(1);
	}
}

void extract_pipe(const rfc2045::entity &message,
		  std::streambuf &source,
		  std::string_view filename,
		  int argc, char **argv)
{
	pid_t	pid, p2;
	int	waitstat;

	auto f=get_suitable_filename(message, "FILENAME=", false);

	int	pipefd[2];

	if (argc == 0)
	{
		std::cerr << "reformime: Invalid -X option.\n";
		exit(1);
	}

	if (pipe(pipefd))
	{
		perror("pipe");
		exit(1);
	}

	rfc822::fdstreambuf out{pipefd[1]};

	while ((pid=fork()) == -1)
	{
		perror("fork");
		sleep(2);
	}

	if (pid == 0)
	{
		out=rfc822::fdstreambuf{};

		putenv(const_cast<char *>(f.c_str()));

		std::string content_type_env;

		content_type_env.reserve(message.content_type.value.size()+
					 sizeof("CONTENT_TYPE=")-1);

		content_type_env="CONTENT_TYPE=";
		content_type_env += message.content_type.value;
		putenv(const_cast<char *>(content_type_env.c_str()));

		dup2(pipefd[0], 0);
		close(pipefd[0]);

		execv(argv[0], argv);
		perror("exec");
		_exit(1);
	}
	close(pipefd[0]);
	signal(SIGPIPE, SIG_IGN);
	do_print_section(message, source, out);
	signal(SIGPIPE, SIG_DFL);
	if (out.pubsync() < 0)
	{
		perror("write");
		exit(1);
	}
	out=rfc822::fdstreambuf{};

	while ((p2=wait(&waitstat)) != pid && p2 != -1)
		;

	if ((p2 == pid) && WIFEXITED(waitstat))
	{
		if (WEXITSTATUS(waitstat) != 0)
		{
			fprintf(stderr, "reformime: %s exited with status %d.\n",
				argv[0], WEXITSTATUS(waitstat));
			exit(WEXITSTATUS(waitstat) + 20);
		}
	}
}

static void for_mime_section(
	const rfc2045::entity &message,
	std::string_view mimesection,
	std::function<void (const rfc2045::entity &,
			    const std::vector<int> &)> cb)
{
	if (!mimesection.size())
	{
		message.enumerate(
			[&]
			(auto &id, auto &e)
			{
				cb(e, id);
			}
		);
	}
	else
	{
		while (mimesection.size())
		{
			auto s=mimesection.data(),p=s;
			size_t n=mimesection.size();

			for (; n; ++p, --n)
			{
				if (*p == ',')
					break;
			}

			std::string_view this_id{
				s, static_cast<size_t>(p-s)
			};

			message.enumerate(
				[&]
				(auto &id, auto &e)
				{
					if (idstr(id) != this_id)
						return;

					cb(e, id);
				}
			);

			if (n)
			{
				++p;
				--n;
			}
			mimesection=std::string_view{p, n};
		}
	}
}

static void extract_section(struct rfc2045 *top_rfcp, const char *mimesection,
	const char *extract_filename, int argc, char **argv,
	void	(*extract_func)(struct rfc2045 *, const char *,
		int, char **))
{
	if (mimesection)
	{
		top_rfcp=rfc2045_find(top_rfcp, mimesection);
		if (!top_rfcp)
			notfound(mimesection);
		if (top_rfcp->firstpart)
		{
			fprintf(stderr, "reformime: MIME section %s is a compound section.\n", mimesection);
			exit(1);
		}
		(*extract_func)(top_rfcp, extract_filename, argc, argv);
		return;
	}

	/* Recursive */

	if (top_rfcp->firstpart)
	{
		for (top_rfcp=top_rfcp->firstpart; top_rfcp;
			top_rfcp=top_rfcp->next)
			extract_section(top_rfcp, mimesection,
				extract_filename, argc, argv, extract_func);
		return;
	}

	if (!top_rfcp->isdummy)
		(*extract_func)(top_rfcp, extract_filename, argc, argv);
}

static void mimedigest1(int, char **);
static char mimebuf[BUFSIZ];

static void mimedigest(int argc, char **argv)
{
char	*p;
struct filelist { struct filelist *next; char *fn; } *first=0, *last=0;
unsigned pcnt=0;
char	**l;

	if (argc > 0)
	{
		mimedigest1(argc, argv);
		return;
	}

	while (fgets(mimebuf, sizeof(mimebuf), stdin))
	{
	struct	filelist *q;

		if ((p=strchr(mimebuf, '\n')) != 0)	*p=0;
		q=(filelist *)malloc(sizeof(struct filelist));
		if (!q || !(q->fn=strdup(mimebuf)))
		{
			perror("malloc");
			exit(1);
		}

		if (last)	last->next=q;
		else	first=q;
		last=q;
		q->next=0;
		++pcnt;
	}
	if (pcnt == 0)	return;

	if ( (l=(char **)malloc(sizeof (char *) * pcnt)) == 0)
	{
		perror("malloc");
	}
	pcnt=0;

	for (last=first; last; last=last->next)
		l[pcnt++]=last->fn;

	mimedigest1(pcnt, l);
	free(l);
	while(first)
	{
		last=first->next;
		free(first->fn);
		free(first);
		first=last;
	}
}

static void mimedigest1(int argc, char **argv)
{
	time_t	t;
	char	boundarybuf[200];
	unsigned boundarycnt=0;
	int	i;
	FILE	*fp;
	int	*utf8;
	if (argc == 0)
		return;

	time (&t);

	utf8=(int *)malloc(sizeof(int)*argc);

	/* Search for a suitable boundary */

	do
	{
	int	l;

		sprintf(boundarybuf, "reformime_%lu_%lu_%u",
			(unsigned long)t,
			(unsigned long)getpid(),
			++boundarycnt);

		l=strlen(boundarybuf);

		for (i=0; i<argc; i++)
		{
			int	err=0;
			struct rfc2045 *parser=rfc2045_alloc();

			if (!parser)
			{
				perror(argv[i]);
				exit(1);
			}
			if ((fp=fopen(argv[i], "r")) == 0)
			{
				perror(argv[i]);
				exit(1);
			}

			while (fgets(mimebuf, sizeof(mimebuf), fp))
			{
				rfc2045_parse(parser, mimebuf, strlen(mimebuf));

				if (mimebuf[0] != '-' || mimebuf[1] != '-')
					continue;

				if (strncasecmp(mimebuf+2, boundarybuf, l) == 0)
				{
					err=1;
					break;
				}
			}
			fclose(fp);
			utf8[i]=parser->rfcviolation & RFC2045_ERR8BITHEADER
				? 1:0;
			rfc2045_free(parser);
			if (err)	break;
		}
	} while (i < argc);

	printf("Mime-Version:1.0\n"
		"Content-Type: multipart/digest; boundary=\"%s\"\n\n%s",
			boundarybuf, RFC2045MIMEMSG);

	for (i=0; i<argc; i++)
	{
		if ((fp=fopen(argv[i], "r")) == 0)
		{
			perror(argv[i]);
			exit(1);
		}

		printf("\n--%s\nContent-Type: %s\n\n",
		       boundarybuf,
		       utf8[i] ? RFC2045_MIME_MESSAGE_GLOBAL:
		       RFC2045_MIME_MESSAGE_RFC822);

		while (fgets(mimebuf, sizeof(mimebuf), fp))
			printf("%s", mimebuf);
		fclose(fp);
	}
	free(utf8);
	printf("\n--%s--\n", boundarybuf);
}

static void display_decoded_header(const char *ptr, size_t cnt, void *dummy)
{
	if (cnt == 0)
		putchar('\n');
	else
		fwrite(ptr, cnt, 1, stdout);
}

static int main2(const char *mimecharset, int argc, char **argv)
{
	int	argn;
	char	optc;
	char	*optarg;
	std::string_view mimesection;
	bool dorewrite{false}, doinfo{false}, dodecode{false};
	int	dodsn=0, domimedigest=0;
	int	dodecodehdr=0, dodecodeaddrhdr=0, doencodemime=0,
		doencodemimehdr=0;

	const char	*decode_header="";
	rfc2045::convert rwmode{rfc2045::convert::standardize};
	int     convtoutf8=0;
	bool	dovalidate{false};
	void	(*do_extract)(const rfc2045::entity &message,
			      std::streambuf &source,
			      std::string_view filename,
			      int argc, char **argv)=nullptr;

	std::string_view extract_filename;
	int rc=0;


	rfc2045_in_reformime=1;

	for (argn=1; argn<argc; )
	{
		if (argv[argn][0] != '-')	break;
		optarg=0;
		optc=argv[argn][1];
		if (optc && argv[argn][2])	optarg=argv[argn]+2;
		++argn;
		switch	(optc)	{
		case 'c':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg && *optarg)
			{
				char *p=unicode_convert_tobuf("",
								optarg,
								unicode_u_ucs4_native,
								NULL);

				if (!p)
				{
					fprintf(stderr, "Unknown charset: %s\n",
						optarg);
					exit(1);
				}
				free(p);
				mimecharset=optarg;
			}
			break;

		case 's':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg && *optarg)	mimesection=optarg;
			break;
		case 'i':
			doinfo=true;
			break;
		case 'e':
			dodecode=true;
			break;
		case 'r':
			dorewrite=true;
			if (optarg && *optarg == '7')
				rwmode=rfc2045::convert::sevenbit;
			if (optarg && *optarg == '8')
				rwmode=rfc2045::convert::eightbit;;
			if (optarg && *optarg == 'U')
				rwmode=rfc2045::convert::eightbit_always;
			break;
		case 'm':
			domimedigest=1;
			break;
		case 'd':
			dodsn=1;
			break;
		case 'D':
			dodsn=2;
			break;
		case 'x':
			do_extract=extract_file;
			if (optarg)
				extract_filename=optarg;
			break;
		case 'X':
			do_extract=extract_pipe;
			break;
		case 'V':
			dovalidate=true;
			break;
		case 'h':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg)
			{
				decode_header=optarg;
			}
			dodecodehdr=1;
			break;
		case 'H':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg)
			{
				decode_header=optarg;
			}
			dodecodeaddrhdr=1;
			break;
		case 'o':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg)
			{
				decode_header=optarg;
			}
			doencodemime=1;
			break;
		case 'O':
			if (!optarg && argn < argc)
				optarg=argv[argn++];
			if (optarg)
			{
				decode_header=optarg;
			}
			doencodemimehdr=1;
			break;
		case 'u':
			convtoutf8=1;
			break;
		default:
			usage();
		}
	}

	defchset=mimecharset;

	rfc2045_setdefaultcharset(defchset);

	if (domimedigest)
	{
		mimedigest(argc-argn, argv+argn);
		return (0);
	}
	else if (dodecodehdr)
	{
		if (rfc822_display_hdrvalue("Subject",
					    decode_header,
					    mimecharset,
					    display_decoded_header,
					    NULL,
					    NULL) < 0)
		{
			perror("rfc822_display_hdrvalue");
			return (1);
		}

		printf("\n");
		return (0);
	}
	else if (dodecodeaddrhdr)
	{
		if (rfc822_display_hdrvalue("To",
					    decode_header,
					    mimecharset,
					    display_decoded_header,
					    NULL,
					    NULL) < 0)
		{
			perror("rfc822_display_hdrvalue");
			return (1);
		}

		printf("\n");
		return (0);
	}

	if (doencodemime)
	{
		char *s=rfc2047_encode_str(decode_header, mimecharset,
					   rfc2047_qp_allow_any);

		if (s)
		{
			printf("%s\n", s);
			free(s);
		}
		return (0);
	}
	if (doencodemimehdr)
	{
		struct rfc822t *t=rfc822t_alloc_new(decode_header, NULL, NULL);
		struct rfc822a *a=t ? rfc822a_alloc(t):NULL;
		char *s;

		if (a && (s=rfc2047_encode_header_addr(a, mimecharset)) != NULL)
		{
			printf("%s\n", s);
			free(s);
		}

		if (a) rfc822a_free(a);
		if (t) rfc822t_free(t);
		return (0);
	}

	read_message();

	rfc2045::entity message;

	int fd=dup(0);

	if (fd < 0)
	{
		perror("dup");
		exit(1);
	}

	rfc822::fdstreambuf src{fd};

	if (src.pubseekpos(0) < 0)
	{
		perror("seek");
		exit(1);
	}

	{
		std::istreambuf_iterator<char> b{&src}, e;
		rfc2045::entity::line_iter<false>::iter parser{b, e};
		message.parse(parser);
		if (src.pubseekpos(0) < 0)
		{
			perror("seek");
			exit(1);
		}
	}

	if (doinfo || do_extract || dodecode)
	{
		for_mime_section(
			message, mimesection,
			[&]
			(const rfc2045::entity &e,
			 const std::vector<int> &id)
			{
				if (doinfo)
				{
					print_info(e, id);
				}

				if (dodecode)
				{
					if (!e.subentities.size())
					{
						do_print_section(
							e, src,
							*std::cout.rdbuf());
						return;
					}
				}
				else
				{
					if (!do_extract)
						return;

					if (!e.subentities.size())
					{
						do_extract(
							e,
							src,
							extract_filename,
							argc-argn,
							argv+argn
						);
						return;
					}
				}

				if (mimesection.size())
				{
					std::cerr
						<< e.content_type.value
						<< " ("
						<< idstr(id)
						<< ") cannot be extracted.\n";
					rc=1;
				}
			}
		);
	}
	else if (dorewrite)
		rewrite(message, src, rwmode);
	else if (dodsn)
		message.getdsn(
			src,
			[&]
			(const rfc2045::entity::dsn &dsn)
			{
				auto &addr= dodsn == 2 &&
					!dsn.original_recipient.empty() ?
					dsn.original_recipient:
					dsn.final_recipient;

				if (addr.empty())
					return;

				std::cout << dsn.action << " " << addr << "\n";
			});
	else if (dovalidate)
	{
		for_mime_section(
			message,
			"",
			[&]
			(const rfc2045::entity &e,
			 const std::vector<int> &id)
			{
				auto errors=e.errors.describe();

				if (e.errors.fatal())
				{
					std::cout << "Fatal error ("
						  << idstr(id) <<"):\n";
					rc=1;
				}
				else if (!errors.empty())
				{
					std::cout << "Warnings ("
						  << idstr(id) << "):\n";
				}

				for (auto &msg:errors)
					std::cout << "    " << msg
						  << "\n";
			});
	}
	else if (convtoutf8)
	{
		rfc2045::entity::line_iter<false>::decoder decoder{
			[]
			(const char *p, size_t n)
			{
				std::cout.write(p, n);
			},
			src,
			unicode::utf_8
		};

		decoder.add_eol=true;
		decoder.header_name_lc=false;

		decoder.decode(message);
	}
	else
	{
		message.enumerate(
			[]
			(const auto &id, const auto &)
			{
				const char *sep="";

				for (auto v:id)
				{
					std::cout << sep << v;
					sep=".";
				}
				std::cout << "\n";
			}
		);
	}

	if (std::cout.rdbuf()->pubsync() < 0)
	{
		perror("write");
		rc=1;
	}
	exit(rc);
	return (rc);
}

int main(int argc, char **argv)
{
	int	rc;

	rc=main2(unicode_default_chset(), argc, argv);
	return rc;
}
