/*
** Copyright 1998 - 2014 Double Precision, Inc.
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


extern void pop3dcapa();
extern int have_starttls();
extern int tls_required();
extern const char *pop3_externalauth();

static const char *pop3d;
static const char *defaultmaildir;

static const char *safe_getenv(const char *p)
{
	p=getenv(p);

	if (!p) p="";
	return p;
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
	argvec[1]="-tcpd";
	argvec[2]="-server";
	argvec[3]=NULL;

	printf("+OK Begin SSL/TLS negotiation now.\r\n");
	fflush(stdout);
	fflush(stdin);

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

	putenv("POP3_STARTTLS=NO");
	putenv("POP3_TLS_REQUIRED=0");
	putenv("POP3_TLS=1");
	return (0);
}

static char *authresp(const char *s, void *dummy)
{
char	*p;
char	buf[BUFSIZ];

	printf("+ %s\r\n", s);
	fflush(stdout);

	if (fgets(buf, sizeof(buf), stdin) == 0)	return (0);
	if ((p=strchr(buf, '\n')) == 0)	return (0);
	if (p > buf && p[-1] == '\r')	--p;
	*p=0;

	p=strdup(buf);
	if (!p)
	{
		perror("malloc");
		return (0);
	}
	return (p);
}

struct pop3proxyinfo {
	const char *uid;
	const char *pwd;
};

static int login_pop3(int, const char *, void *);

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

			pi.connected_func=login_pop3;
			pi.void_arg=&ppi;

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
		char *p=malloc(sizeof("OPTIONS=") + strlen(ainfo->options ?
							   ainfo->options:""));

		if (p)
		{
			strcat(strcpy(p, "OPTIONS="),
			       ainfo->options ? ainfo->options:"");
			putenv(p);

			p=malloc(sizeof("AUTHENTICATED=")+
				 strlen(ainfo->address));
			if (p)
			{
				strcat(strcpy(p, "AUTHENTICATED="),
				       ainfo->address);
				putenv(p);

				alarm(0);
				execl(pop3d, pop3d,
				      ainfo->maildir ?
				      ainfo->maildir:defaultmaildir,
				      NULL);
				fprintf(stderr, "ERR: exec(%s) failed!!\n",
							 pop3d);
			}
		}
	}

	return (rc);
}

int main(int argc, char **argv)
{
char	*user=0;
char	*p;
char	buf[BUFSIZ];
int	c;
const	char *ip=getenv("TCPREMOTEIP");
char authservice[40];
char *q ;

#ifdef HAVE_SETVBUF_IOLBF
	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
#endif

	if (!ip || !*ip)
	{
		ip="127.0.0.1";
	}

	if (argc != 3)
	{
		printf("-ERR pop3login requires exactly two arguments.\r\n");
		fflush(stdout);
		exit(1);
	}

	pop3d=argv[1];
	defaultmaildir=argv[2];

	courier_authdebug_login_init();

	fprintf(stderr, "DEBUG: Connection, ip=[%s]\n", ip);
	printf("+OK Hello there.\r\n");

	fflush(stdout);
	fflush(stderr);
	alarm(60);
	while (fgets(buf, sizeof(buf), stdin))
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
				fprintf(stderr, "INFO: LOGOUT, ip=[%s]\n",
					ip);
				fflush(stderr);
				printf("+OK Better luck next time.\r\n");
				fflush(stdout);
				break;
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
					if (user)	free(user);
					if ((user=malloc(strlen(p)+1)) == 0)
					{
						printf("-ERR Server out of memory, aborting connection.\r\n");
						fflush(stdout);
						perror("malloc");
						exit(1);
					}
					strcpy(user, p);
					printf("+OK Password required.\r\n");
					fflush(stdout);
					continue;
				}
			} else if (strcmp(p, "CAPA") == 0)
			{
				pop3dcapa();
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
						initreply="";

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
						if (!q || !*q)
							q="pop3";

						rc=auth_generic(q,
							     authtype,
							     authdata,
							     login_callback,
							     NULL);
						free(authtype);
						free(authdata);
					}

					courier_safe_printf("INFO: LOGIN "
						"FAILED, method=%s, ip=[%s]",
						method, ip);
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

				if (!user || p == 0)
				{
					printf("-ERR USER/PASS required.\r\n");
					fflush(stdout);
					continue;
				}

				strcat(strcpy(authservice, "AUTHSERVICE"),getenv("TCPLOCALPORT"));
				q=getenv(authservice);
				if (!q || !*q)
					q="pop3";

				rc=auth_login(q, user, p, login_callback, NULL);
				courier_safe_printf("INFO: LOGIN "
					"FAILED, user=%s, ip=[%s]",
					user, ip);
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
	fprintf(stderr, "DEBUG: Disconnected, ip=[%s]\n", ip);
	exit(0);
	return (0);
}

static int login_pop3(int fd, const char *hostname, void *void_arg)
{
	struct pop3proxyinfo *ppi=(struct pop3proxyinfo *)void_arg;
	struct proxybuf pb;
	char linebuf[256];
	char *cmd;

	DPRINTF("Proxy connected to %s", hostname);

	memset(&pb, 0, sizeof(pb));

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
		return -1;

	DPRINTF("%s: %s", hostname, linebuf);

	if (linebuf[0] != '+')
	{
		fprintf(stderr, "WARN: Did not receive greeting from %s\n",
			hostname);
		return -1;
	}

	cmd=malloc(strlen(ppi->uid) + strlen(ppi->pwd)+100);
	/* Should be enough */

	if (!cmd)
		return -1;

	sprintf(cmd, "USER %s\r\n", ppi->uid);

	if (proxy_write(fd, hostname, cmd, strlen(cmd)))
	{
		free(cmd);
		return -1;
	}

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
	{
		free(cmd);
		return -1;
	}

	DPRINTF("%s: %s", hostname, linebuf);

	if (linebuf[0] != '+')
	{
		free(cmd);
		fprintf(stderr, "WARN: Login userid rejected by %s\n",
			hostname);
		return -1;
	}

	sprintf(cmd, "PASS %s\r\n", ppi->pwd);

	if (proxy_write(fd, hostname, cmd, strlen(cmd)))
	{
		free(cmd);
		return -1;
	}

	if (proxy_readline(fd, &pb, linebuf, sizeof(linebuf), 1) < 0)
	{
		free(cmd);
		return -1;
	}

	DPRINTF("%s: %s", hostname, linebuf);

	if (linebuf[0] != '+')
	{
		free(cmd);
		fprintf(stderr, "WARN: Login password rejected by %s\n",
			hostname);
		return -1;
	}

	free(cmd);
	if (fcntl(1, F_SETFL, 0) < 0 ||
	    (printf("+OK Connected to proxy server.\r\n"), fflush(stdout)) < 0)
		return -1;

	return 0;
}
