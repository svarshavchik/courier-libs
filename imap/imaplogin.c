/*
** Copyright 1998 - 2016 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<netdb.h>
#if HAVE_SYS_SELECT_H
#include	<sys/select.h>
#endif
#include	<sys/time.h>

#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"proxy.h"

#include	<courierauth.h>
#include	<courierauthdebug.h>
#include	"tcpd/spipe.h"
#include	"numlib/numlib.h"
#include	"tcpd/tlsclient.h"


FILE *debugfile=0;
extern void initcapability();
extern void mainloop();
extern void imapcapability();
extern int have_starttls();
extern int tlsrequired();
extern int authenticate(const char *, char *, int);
unsigned long header_count=0, body_count=0;	/* Dummies */

extern unsigned long bytes_received_count; /* counter for received bytes (imaptoken.c) */
extern unsigned long bytes_sent_count; /* counter for sent bytes (imapwrite.c) */

int main_argc;
char **main_argv;
extern time_t start_time;

static const char *imapd;
static const char *defaultmaildir;

void rfc2045_error(const char *p)
{
	if (write(2, p, strlen(p)) < 0)
		_exit(1);
	_exit(0);
}

extern void cmdfail(const char *, const char *);
extern void cmdsuccess(const char *, const char *);

static int	starttls(const char *tag)
{
	char *argvec[4];

	char localfd_buf[NUMBUFSIZE+40];
	char buf2[NUMBUFSIZE];
	struct couriertls_info cinfo;
	int pipefd[2];

	if (libmail_streampipe(pipefd))
	{
		cmdfail(tag, "libmail_streampipe() failed.\r\n");
		return (-1);
	}

	couriertls_init(&cinfo);
	fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);

	strcat(strcpy(localfd_buf, "-localfd="),
	       libmail_str_size_t(pipefd[1], buf2));

	argvec[0]=localfd_buf;
	argvec[1]="-tcpd";
	argvec[2]="-server";
	argvec[3]=NULL;

	cmdsuccess(tag, "Begin SSL/TLS negotiation now.\r\n");
	writeflush();

	if (couriertls_start(argvec, &cinfo))
	{
		close(pipefd[0]);
		close(pipefd[1]);
		cmdfail(tag, "STARTTLS failed: ");
		writes(cinfo.errmsg);
		writes("\r\n");
		couriertls_destroy(&cinfo);
		return (-1);
	}

	couriertls_export_subject_environment(&cinfo);
	couriertls_destroy(&cinfo);

	close(pipefd[1]);
	close(0);
	close(1);
	if (dup(pipefd[0]) != 0 || dup(pipefd[0]) != 1)
	{
		perror("dup");
		exit(1);
	}
	close(pipefd[0]);

	/* We use select() with a timeout, so use non-blocking filedescs */

	if (fcntl(0, F_SETFL, O_NONBLOCK) ||
	    fcntl(1, F_SETFL, O_NONBLOCK))
	{
		perror("fcntl");
		exit(1);
	}
	fflush(stdin);
	return (0);
}

struct imapproxyinfo {
	const char *tag;
	const char *uid;
	const char *pwd;
};

static int login_imap(int, const char *, void *);

static const char *safe_getenv(const char *p)
{
	p=getenv(p);

	if (!p) p="";
	return p;
}

int login_callback(struct authinfo *ainfo, void *dummy)
{
	int rc;
	const char *tag=(const char *)dummy;
	char *p;

	p=getenv("IMAP_PROXY");

	if (p && atoi(p))
	{
		if (ainfo->options == NULL ||
		    (p=auth_getoption(ainfo->options,
				      "mailhost")) == NULL)
		{
			fprintf(stderr, "WARN: proxy enabled, but no proxy"
				 " host defined for %s\n",
				 ainfo->address);

			/* Fallthru to account login */

		}
		else if (ainfo->clearpasswd == NULL)
		{
			free(p);
			fprintf(stderr, "WARN: proxy enabled, but no password"
				 " for %s\n", ainfo->address);
			return -1;
		}
		else
		{
			struct proxyinfo pi;
			struct imapproxyinfo ipi;
			struct servent *se;
			int fd;

			se=getservbyname("imap", NULL);

			pi.host=p;
			pi.port=se ? ntohs(se->s_port):143;

			ipi.uid=ainfo->address;
			ipi.pwd=ainfo->clearpasswd;
			ipi.tag=tag;

			pi.connected_func=login_imap;
			pi.void_arg=&ipi;

			if ((fd=connect_proxy(&pi)) < 0)
			{
				free(p);
				return -1;
			}
			free(p);
			if (fd > 0)
			{
				alarm(0);

				fprintf(stderr, "INFO: LOGIN, user=%s, ip=[%s], port=[%s], protocol=%s\n",
					ainfo->address, safe_getenv("TCPREMOTEIP"),
					safe_getenv("TCPREMOTEPORT"),
					safe_getenv("PROTOCOL"));

				proxyloop(fd);
				exit(0);
			}

			/* FALLTHRU */
		}
	}

	rc=auth_callback_default_autocreate(ainfo);

	if (rc == 0)
	{
		p=malloc(sizeof("OPTIONS=") + strlen(ainfo->options ?
						     ainfo->options:""));

		if (p)
		{
			strcat(strcpy(p, "OPTIONS="),
			       ainfo->options ? ainfo->options:"");
			putenv(p);

			p=malloc(sizeof("IMAPLOGINTAG=")+strlen(tag));
			if (p)
			{
				strcat(strcpy(p, "IMAPLOGINTAG="), tag);
				putenv(p);

				p=malloc(sizeof("AUTHENTICATED=")+
					 strlen(ainfo->address));
				if (p)
				{
					strcat(strcpy(p, "AUTHENTICATED="),
					       ainfo->address);
					putenv(p);
					alarm(0);
					execl(imapd, imapd,
					      ainfo->maildir ?
					      ainfo->maildir:defaultmaildir,
					      NULL);
					fprintf(stderr, "ERR: exec(%s) failed!!\n",
								 imapd);
				}
			}
		}
	}

	return(rc);
}

int do_imap_command(const char *tag, int *flushflag)
{
	struct	imaptoken *curtoken=nexttoken();
	char authservice[40];

#if SMAP
	if (strcmp(tag, "\\SMAP1") == 0)
	{
		const char *p=getenv("SMAP_CAPABILITY");

		if (p && *p)
			putenv("PROTOCOL=SMAP1");
		else
			return -1;
	}
#endif

	courier_authdebug_login( 1, "command=%s", curtoken->tokenbuf );

	if (strcmp(curtoken->tokenbuf, "LOGOUT") == 0)
	{
		if (nexttoken()->tokentype != IT_EOL)   return (-1);
		writes("* BYE Courier-IMAP server shutting down\r\n");
		cmdsuccess(tag, "LOGOUT completed\r\n");
		writeflush();
		fprintf(stderr, "INFO: LOGOUT, ip=[%s], rcvd=%lu, sent=%lu\n",
			getenv("TCPREMOTEIP"), bytes_received_count, bytes_sent_count);
		exit(0);
	}
	if (strcmp(curtoken->tokenbuf, "NOOP") == 0)
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);
		cmdsuccess(tag, "NOOP completed\r\n");
		return (0);
	}
	if (strcmp(curtoken->tokenbuf, "CAPABILITY") == 0)
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);

		writes("* CAPABILITY ");
		imapcapability();
		writes("\r\n");
		cmdsuccess(tag, "CAPABILITY completed\r\n");
		return (0);
	}

	if (strcmp(curtoken->tokenbuf, "STARTTLS") == 0)
	{
		if (!have_starttls())	return (-1);
		if (starttls(tag))		return (-2);
		putenv("IMAP_STARTTLS=NO");
		putenv("IMAP_TLS_REQUIRED=0");
		putenv("IMAP_TLS=1");
		*flushflag=1;
		return (0);
	}

	if (strcmp(curtoken->tokenbuf, "LOGIN") == 0)
	{
	struct imaptoken *tok=nexttoken_nouc();
	char	*userid;
	char	*passwd;
	const char *p;
	int	rc;

		if (have_starttls() && tlsrequired())	/* Not yet */
		{
			cmdfail(tag, "STARTTLS required\r\n");
			return (0);
		}

		switch (tok->tokentype)	{
		case IT_ATOM:
		case IT_NUMBER:
		case IT_QUOTED_STRING:
			break;
		default:
			return (-1);
		}

		userid=strdup(tok->tokenbuf);
		if (!userid)
			write_error_exit(0);
		tok=nexttoken_nouc_okbracket();
		switch (tok->tokentype)	{
		case IT_ATOM:
		case IT_NUMBER:
		case IT_QUOTED_STRING:
			break;
		default:
			free(userid);
			return (-1);
		}

		passwd=my_strdup(tok->tokenbuf);

		if (nexttoken()->tokentype != IT_EOL)
		{
			free(userid);
			free(passwd);
			return (-1);
		}

		strcat(strcpy(authservice, "AUTHSERVICE"),
		       getenv("TCPLOCALPORT"));

		p=getenv(authservice);

		if (!p || !*p)
			p="imap";

		rc=auth_login(p, userid, passwd, login_callback, (void *)tag);
		courier_safe_printf("INFO: LOGIN FAILED, user=%s, ip=[%s]",
				  userid, getenv("TCPREMOTEIP"));
		free(userid);
		free(passwd);
		if (rc > 0)
		{
			perror("ERR: authentication error");
			writes("* BYE Temporary problem, please try again later\r\n");
			writeflush();
			exit(1);
		}
		sleep(5);
		cmdfail(tag, "Login failed.\r\n");
		return (0);
	}

	if (strcmp(curtoken->tokenbuf, "AUTHENTICATE") == 0)
	{
	char	method[32];
	int	rc;

		if (have_starttls() && tlsrequired())	/* Not yet */
		{
			cmdfail(tag, "STARTTLS required\r\n");
			return (0);
		}
		rc=authenticate(tag, method, sizeof(method));
		courier_safe_printf("INFO: LOGIN FAILED, method=%s, ip=[%s]",
				  method, getenv("TCPREMOTEIP"));
		if (rc > 0)
		{
			perror("ERR: authentication error");
			writes("* BYE Temporary problem, please try again later\r\n");
			writeflush();
			exit(1);
		}
		sleep(5);
		cmdfail(tag, "Login failed.\r\n");
		writeflush();
		return (-2);
	}

	return (-1);
}

extern void ignorepunct();

int main(int argc, char **argv)
{
	const char	*ip;

#ifdef HAVE_SETVBUF_IOLBF
	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
#endif

	if (argc != 3)
	{
		printf("* BYE imaplogin expected exactly two arguments.\r\n");
		fflush(stdout);
		exit(1);
	}

	alarm(60);
	imapd=argv[1];
	defaultmaildir=argv[2];
	initcapability();

	ip=getenv("TCPREMOTEIP");
	if (!ip)
		putenv("TCPREMOTEIP=127.0.0.1");
	ip=getenv("TCPREMOTEIP");

	if (!getenv("TCPLOCALPORT"))
		putenv("TCPLOCALPORT=143");

	time(&start_time);

#if	IMAP_CLIENT_BUGS

	ignorepunct();

#endif

	courier_authdebug_login_init();

	/* We use select() with a timeout, so use non-blocking filedescs */

	if (fcntl(0, F_SETFL, O_NONBLOCK) ||
	    fcntl(1, F_SETFL, O_NONBLOCK))
	{
		perror("fcntl");
		exit(1);
	}

	writes("* OK [CAPABILITY ");
	imapcapability();
	writes("] Courier-IMAP ready. "
	       "Copyright 1998-2017 Double Precision, Inc.  "
	       "See COPYING for distribution information.\r\n");
	fprintf(stderr, "DEBUG: Connection, ip=[%s]\n", ip);
	writeflush();
	main_argc=argc;
	main_argv=argv;

	putenv("PROTOCOL=IMAP");

	mainloop();
	return (0);
}

void bye()
{
	exit(0);
}

static void imap_write_str(const char *c,
			   void (*cb_func)(const char *,
					   size_t,
					   void *),
			   void *cb_arg)
{
	if (c == NULL)
	{
		(*cb_func)("NIL", 3, cb_arg);
	}

	(*cb_func)("\"", 1, cb_arg);

	while (*c)
	{
		size_t n;

		for (n=0; c[n]; n++)
			if (c[n] == '"' || c[n] == '\\')
				break;

		if (n)
		{
			(*cb_func)(c, n, cb_arg);

			c += n;
		}

		if (*c)
		{
			(*cb_func)("\\", 1, cb_arg);
			(*cb_func)(c, 1, cb_arg);
			++c;
		}
	}
	(*cb_func)("\"", 1, cb_arg);
}

static void imap_login_cmd(struct imapproxyinfo *ipi,
			   void (*cb_func)(const char *,
					   size_t,
					   void *),
			   void *cb_arg)
{
	(*cb_func)(ipi->tag, strlen(ipi->tag), cb_arg);
	(*cb_func)(" LOGIN ", 7, cb_arg);
	imap_write_str(ipi->uid, cb_func, cb_arg);
	(*cb_func)(" ", 1, cb_arg);
	imap_write_str(ipi->pwd, cb_func, cb_arg);
	(*cb_func)("\r\n", 2, cb_arg);
}

static void imap_capability_cmd(struct imapproxyinfo *ipi,
			       void (*cb_func)(const char *,
					       size_t,
					       void *),
			       void *cb_arg)
{
	(*cb_func)(ipi->tag, strlen(ipi->tag), cb_arg);
	(*cb_func)(" CAPABILITY\r\n", 13, cb_arg);
}

static void cb_cnt(const char *c, size_t l,
		   void *arg)
{
	*(size_t *)arg += l;
}

static void cb_cpy(const char *c, size_t l,
		   void *arg)
{
	char **p=(char **)arg;

	memcpy(*p, c, l);
	*p += l;
}

static char *get_imap_cmd(struct imapproxyinfo *ipi,
			  void (*cmd)(struct imapproxyinfo *ipi,
				      void (*cb_func)(const char *,
						      size_t,
						      void *),
				      void *cb_arg))

{
	size_t cnt=1;
	char *buf;
	char *p;

	(*cmd)(ipi, &cb_cnt, &cnt);

	buf=malloc(cnt);

	if (!buf)
	{
		fprintf(stderr, "CRIT: Out of memory!\n");
		return NULL;
	}

	p=buf;
	(*cmd)(ipi, &cb_cpy, &p);
	*p=0;
	return buf;
}

static int login_imap(int fd, const char *hostname, void *void_arg)
{
	struct imapproxyinfo *ipi=(struct imapproxyinfo *)void_arg;
	struct proxybuf pb;
	char linebuf[256];
	const char *p;
	char *cmd;

	DPRINTF("Proxy connected to %s", hostname);

	memset(&pb, 0, sizeof(pb));

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
		return -1;

	DPRINTF("%s: %s", hostname, linebuf);

	if ((p=strtok(linebuf, " \t")) == NULL ||
	    strcmp(p, "*") ||
	    (p=strtok(NULL, " \t")) == NULL ||
	    strcasecmp(p, "OK"))
	{
		fprintf(stderr, "WARN: Did not receive greeting from %s\n",
			hostname);
		return -1;
	}

	cmd=get_imap_cmd(ipi, imap_login_cmd);

	if (!cmd)
		return -1;

	if (proxy_write(fd, hostname, cmd, strlen(cmd)))
	{
		free(cmd);
		return -1;
	}
	free(cmd);

#if SMAP
	if (strcmp(ipi->tag, "\\SMAP1") == 0)
	{
		do
		{
			if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf),
					   0) < 0)
				return -1;

			DPRINTF("%s: %s", hostname, linebuf);

		} while (linebuf[0] != '+' && linebuf[0] != '-');


		if (linebuf[0] != '+')
		{
			fprintf(stderr, "WARN: Login to %s failed\n", hostname);
			return -1;
		}

		if (fcntl(1, F_SETFL, 0) < 0 ||
		    (printf("+OK connected to proxy server.\r\n"),
		     fflush(stdout)) < 0)
			return -1;
		return (0);
	}
#endif


	for (;;)
	{
		if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
			return -1;

		DPRINTF("%s: %s", hostname, linebuf);

		if ((p=strtok(linebuf, " \t")) == NULL ||
		    strcmp(p, ipi->tag) ||
		    (p=strtok(NULL, " \t")) == NULL)
			continue;

		if (strcasecmp(p, "OK"))
		{
			fprintf(stderr, "WARN: Login to %s failed\n", hostname);
			return -1;
		}
		break;
	}

	p=getenv("IMAP_PROXY_FOREIGN");

	if (p && atoi(p))
	{
		cmd=get_imap_cmd(ipi, imap_capability_cmd);

		if (!cmd)
			return -1;

		if (proxy_write(fd, hostname, cmd, strlen(cmd)))
		{
			free(cmd);
			return -1;
		}
		free(cmd);
	}
	else
	{
		if (fcntl(1, F_SETFL, 0) < 0 ||
		    (printf("%s OK connected to proxy server.\r\n",
			    ipi->tag), fflush(stdout)) < 0)
			return -1;
	}

	return 0;
}
