/*
** Copyright 1998 - 2024 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<ctype.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<netinet/in.h>
#include	<netdb.h>
#include	"waitlib/waitlib.h"
#include	"proxy.h"

#include	<courierauth.h>
#include	<courierauthdebug.h>
#include	<courierauthsasl.h>
#include	"tcpd/spipe.h"
#include	"numlib/numlib.h"
#include	"tcpd/tlsclient.h"
#include	<vector>
#include	<string>

extern "C" void pop3dcapa();
extern "C" void pop3dlang(const char *);
extern "C" int have_starttls();
extern "C" int tls_required();
extern "C" const char *pop3_externalauth();

static const char *pop3d;
static const char *defaultmaildir;
int utf8_enabled=0;

static const char *safe_getenv(const char *p)
{
	p=getenv(p);

	if (!p) p="";
	return p;
}

static char getline_buf[1024];
static size_t getline_buf_size=0;
static size_t getline_i=0;

static int safe_getc()
{
	if (getline_i >= getline_buf_size)
	{
		int n=read(0, getline_buf, sizeof(getline_buf));

		if (n < 0)
			n=0;

		if (n == 0)
			return -1;

		getline_buf_size=n;
		getline_i=0;
	}

	return (int)(unsigned char)getline_buf[getline_i++];
}

static void safe_fflush()
{
	getline_i=getline_buf_size=0;
}

static char *safe_fgets(char *buf, size_t buf_size)
{
	size_t i;

	for (i=0; i+1 < buf_size; ++i)
	{
		int ch=safe_getc();

		if (ch < 0)
		{
			if (i == 0)
				return NULL;
			break;
		}

		buf[i]=ch;
		if (ch == '\n')
		{
			++i;
			break;
		}
	}

	if (i < buf_size)
		buf[i]=0;

	return buf;
}

static int	starttls()
{
	char *argvec[4];

	char localfd_buf[NUMBUFSIZE+40];
	char buf2[NUMBUFSIZE];
	struct couriertls_info cinfo;
	int pipefd[2];

	if (libmail_streampipe(pipefd))
	{
		printf("-ERR libmail_streampipe() failed.\r\n");
		return (-1);
	}

	couriertls_init(&cinfo);
	fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);

	strcat(strcpy(localfd_buf, "-localfd="),
	       libmail_str_size_t(pipefd[1], buf2));

	argvec[0]=localfd_buf;

	char arg1[]="-tcpd";

	argvec[1]=arg1;

	char arg2[]="-server";

	argvec[2]=arg2;
	argvec[3]=NULL;

	printf("+OK Begin SSL/TLS negotiation now.\r\n");
	fflush(stdout);
	safe_fflush();
	cinfo.username=MAILUSER;

	if (couriertls_start(argvec, &cinfo))
	{
		close(pipefd[0]);
		close(pipefd[1]);
		printf("-ERR STARTTLS failed: %s\r\n",
		       cinfo.errmsg);
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
	setenv("POP3_STARTTLS", "NO", 1);
	setenv("POP3_TLS_REQUIRED", "0", 1);
	setenv("POP3_TLS", "1", 1);
	return (0);
}

static char *authresp(const char *s, void *dummy)
{
	static char buffer[BUFSIZ];
	char *p;
	char *buf;

	printf("+ %s\r\n", s);
	fflush(stdout);

	if ((buf=safe_fgets(buffer, sizeof(buffer))) == nullptr)
		return (buf);

	if ((p=strchr(buf, '\n')) == 0)	return (0);
	if (p > buf && p[-1] == '\r')	--p;
	*p=0;

	return (buf);
}

struct pop3proxyinfo {
	const char *uid;
	const char *pwd;
};

static int login_pop3(int, const std::string &, pop3proxyinfo *);

static int login_callback(struct authinfo *ainfo, void *dummy)
{
	int rc;
	char *p;

	p=getenv("POP3_PROXY");

	if (p && atoi(p))
	{
		if (ainfo->options == NULL ||
		    (p=auth_getoption(ainfo->options,
				      "mailhost")) == NULL)
		{
			fprintf(stderr,"WARN: proxy enabled, but no proxy"
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
			struct pop3proxyinfo ppi;
			struct servent *se;
			int fd;

			se=getservbyname("pop3", NULL);

			pi.host=p;
			pi.port=se ? ntohs(se->s_port):110;

			ppi.uid=ainfo->address;
			ppi.pwd=ainfo->clearpasswd;

			pi.connected_func=
				[&]
				(int fd, const std::string &hostname)
				{
					return login_pop3(fd, hostname, &ppi);
				};

			if ((fd=connect_proxy(&pi)) < 0)
			{
				free(p);
				return -1;
			}
			free(p);
			if (fd > 0)
			{
				alarm(0);
				fprintf(stderr, "INFO: LOGIN, user=%s, ip=[%s], port=[%s], protocol=%s%s\n",
					ainfo->address, safe_getenv("TCPREMOTEIP"),
					safe_getenv("TCPREMOTEPORT"),
					safe_getenv("PROTOCOL"),
					(p=getenv("POP3_TLS")) != 0 && atoi(p) ? ", stls=1" : "");

				proxyloop(fd);
				exit(0);
			}

			/* FALLTHRU */
		}
	}

	rc=auth_callback_default_autocreate(ainfo);

	if (rc == 0)
	{
		setenv("OPTIONS", ainfo->options ? ainfo->options:"", 1);
		setenv("AUTHENTICATED", ainfo->address, 1);

		setenv("UTF8", utf8_enabled ? "1":"0", 1);
		execl(pop3d, pop3d,
		      ainfo->maildir ?
		      ainfo->maildir:defaultmaildir,
		      NULL);
		fprintf(stderr, "ERR: exec(%s) failed!!\n",
			pop3d);
	}

	return (rc);
}

int main(int argc, char **argv)
{
	std::string user;
	char	*p;
	char	buf[BUFSIZ];
	int	c;
	const	char *ip=getenv("TCPREMOTEIP");
	const	char *port=getenv("TCPREMOTEPORT");

	char authservice[40];
	char *q ;

#ifdef HAVE_SETVBUF_IOLBF
	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
#endif

	if (!ip || !*ip)
	{
		ip="127.0.0.1";
	}

	if (!port || !*port)
		port="N/A";

	if (argc != 3)
	{
		printf("-ERR pop3login requires exactly two arguments.\r\n");
		fflush(stdout);
		exit(1);
	}

	pop3d=argv[1];
	defaultmaildir=argv[2];

	courier_authdebug_login_init();

	fprintf(stderr, "DEBUG: Connection, ip=[%s], port=[%s]\n", ip, port);
	printf("+OK Hello there.\r\n");

	fflush(stdout);
	fflush(stderr);
	alarm(60);
	while (safe_fgets(buf, sizeof(buf)))
	{
		c=1;
		for (p=buf; *p; p++)
		{
			if (*p == '\n')
				break;

			if (*p == ' ' || *p == '\t')	c=0;
			if (c)
				*p=toupper((int)(unsigned char)*p);
		}

		if (*p)
			*p=0;
		else while ((c=getchar()) != EOF && c != '\n')
			;
		p=strtok(buf, " \t\r");
		if (p)
		{
			courier_authdebug_login( 1, "command=%s", p );

			if ( strcmp(p, "QUIT") == 0)
			{
				fprintf(stderr, "INFO: LOGOUT, ip=[%s], port=[%s]\n",
					ip, port);
				fflush(stderr);
				printf("+OK Better luck next time.\r\n");
				fflush(stdout);
				break;
			}

			if ( strcmp(p, "UTF8") == 0)
			{
				printf("+OK UTF8 enabled\r\n");
				fflush(stdout);
				utf8_enabled=1;
				continue;
			}

			if ( strcmp(p, "USER") == 0)
			{
				if (tls_required())
				{
					printf("-ERR TLS required to log in.\r\n");
					fflush(stdout);
					continue;
				}

				p=strtok(0, "\r\n");
				if (p)
				{
					user=p;
					printf("+OK Password required.\r\n");
					fflush(stdout);
					continue;
				}
			} else if (strcmp(p, "CAPA") == 0)
			{
				pop3dcapa();
				continue;
			} else if (strcmp(p, "LANG") == 0)
			{
				pop3dlang(strtok(0, "\r\n"));
				continue;
			} else if (strcmp(p, "STLS") == 0)
			{
				if (!have_starttls())
				{
					printf("-ERR TLS support not available.\r\n");
					fflush(stdout);
					continue;
				}
				starttls();
				fflush(stdout);
				continue;
			} else if (strcmp(p, "AUTH") == 0)
			{
				char *authtype, *authdata;
				char	*method=strtok(0, " \t\r");

				if (tls_required())
				{
					printf("-ERR TLS required to log in.\r\n");
					fflush(stdout);
					continue;
				}

				if (method)
				{
					char *initreply=strtok(0, " \t\r");
					int	rc;
					char *p;

					for (p=method; *p; p++)
						*p=toupper(*p);

					if (initreply &&
					    strcmp(initreply, "=") == 0)
						*initreply=0;

					rc=auth_sasl_ex(method, initreply,
							pop3_externalauth(),
							authresp,
							NULL,
							&authtype,
							&authdata);

					if (rc == 0)
					{
						strcat(strcpy(authservice, "AUTHSERVICE"),getenv("TCPLOCALPORT"));
						q=getenv(authservice);

						char defaultpop3[]="pop3";

						if (!q || !*q)
							q=defaultpop3;

						rc=auth_generic_meta
							(NULL, q,
							 authtype,
							 authdata,
							 login_callback,
							 NULL);
						free(authtype);
						free(authdata);
					}

					courier_safe_printf
						("INFO: LOGIN "
						 "FAILED, method=%s, ip=[%s], port=[%s]",
						 method, ip, port);
					if (rc == AUTHSASL_ABORTED)
					    printf("-ERR Authentication aborted.\r\n");
					else if (rc > 0)
					{
					    perror("ERR: authentication error");
					    printf("-ERR Temporary problem, please try again later\r\n");
					    fflush(stdout);
					    exit(1);
					}
					else
					{
					    sleep(5);
					    printf("-ERR Authentication failed.\r\n");
					}

					fflush(stdout);
					continue;
				}
			} else if (strcmp(p, "PASS") == 0)
			{
				int rc;

				p=strtok(0, "\r\n");

				if (user.empty() || p == 0)
				{
					printf("-ERR USER/PASS required.\r\n");
					fflush(stdout);
					continue;
				}

				strcat(strcpy(authservice, "AUTHSERVICE"),getenv("TCPLOCALPORT"));
				q=getenv(authservice);

				char defaultpop3[]="pop3";

				if (!q || !*q)
					q=defaultpop3;

				rc=auth_login_meta(NULL, q, user.c_str(), p,
						   login_callback, NULL);
				courier_safe_printf
					("INFO: LOGIN "
					 "FAILED, user=%s, ip=[%s], port=[%s]",
					 user, ip, port);
				if (rc > 0)
				{
					perror("ERR: authentication error");
					printf("-ERR Temporary problem, please try again later\r\n");
					fflush(stdout);
					exit(1);
				}
				sleep(5);
				printf("-ERR Login failed.\r\n");
				fflush(stdout);
				continue;
			}
		}
		printf("-ERR Invalid command.\r\n");
		fflush(stdout);
	}
	fprintf(stderr, "DEBUG: Disconnected, ip=[%s], port=[%s]\n", ip, port);
	exit(0);
	return (0);
}

static int login_pop3(int fd, const std::string &hostname, pop3proxyinfo *ppi)
{
	struct proxybuf pb;
	char linebuf[256];
	std::string cmd;

	DPRINTF("Proxy connected to %s", hostname.c_str());

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
		return -1;

	DPRINTF("%s: %s", hostname.c_str(), linebuf);

	if (linebuf[0] != '+')
	{
		fprintf(stderr, "WARN: Did not receive greeting from %s\n",
			hostname.c_str());
		return -1;
	}

	if (utf8_enabled)
	{
		if (proxy_write(fd, hostname, "UTF8\r\n", 6))
		{
			return -1;
		}

		if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
		{
			return -1;
		}

		DPRINTF("%s: %s", hostname.c_str(), linebuf);

		if (linebuf[0] != '+')
		{
			fprintf(stderr, "WARN: UTF8 rejected by %s\n",
				hostname.c_str());
			return -1;
		}
	}

	cmd="USER ";
	cmd += ppi->uid;
	cmd += "\r\n";

	if (proxy_write(fd, hostname, cmd.c_str(), cmd.size()))
	{
		return -1;
	}

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
	{
		return -1;
	}

	DPRINTF("%s: %s", hostname.c_str(), linebuf);

	if (linebuf[0] != '+')
	{
		fprintf(stderr, "WARN: Login userid rejected by %s\n",
			hostname.c_str());
		return -1;
	}

	cmd="PASS ";
	cmd += ppi->pwd;
	cmd += "\r\n";

	if (proxy_write(fd, hostname, cmd.c_str(), cmd.size()))
	{
		return -1;
	}

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
	{
		return -1;
	}

	DPRINTF("%s: %s", hostname.c_str(), linebuf);

	if (linebuf[0] != '+')
	{
		fprintf(stderr, "WARN: Login password rejected by %s\n",
			hostname.c_str());
		return -1;
	}

	if (fcntl(1, F_SETFL, 0) < 0 ||
	    (printf("+OK Connected to proxy server.\r\n"), fflush(stdout)) < 0)
		return -1;

	return 0;
}
