/*
** Copyright 2003-2007 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"cgi.h"
#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<signal.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include        <sys/socket.h>
#include        <sys/un.h>
#if	HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#include	<ctype.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif

#if	HAVE_SYS_SELECT_H
#include	<sys/select.h>
#endif

#if	HAVE_SYS_UIO_H
#include	<sys/uio.h>
#endif


extern char **environ;

static void err(const char *func, const char *msg)
{
	cginocache();

	printf("Content-Type: text/html; charset='utf-8'\n\n"
	       "<html><head><title>Internal error</title></head>\n"
	       "<body><h1>Internal Error</h1>\n"
	       "<p>The webmail system is temporarily unavailable.  An error"
	       " occured in function %s: %s</p></body></html>\n", func,
	       msg);
	fflush(stdout);
	exit(0);
}

static void connect_err(const char *func)
{
	cginocache();

	printf("Content-Type: text/html; charset='us-ascii'\n\n"
	       "<html><head><title>System unavailable</title></head>\n"
	       "<body><h1>System unavailable</h1>\n"
	       "<p>The web page you're trying to access is not available"
	       " at this time. Please try again later.\n"
	       "</p><p>"
	       "(%s: %s)</p></body></html>\n", func, strerror(errno));
	fflush(stdout);
	exit(0);
}

static void sys_err(const char *func)
{
	err(func, strerror(errno));
}

static const char *force_write(int s, const char *p, size_t l)
{
	while (l)
	{
		int n;

		n=write(s, p, l);
		if (n <= 0)
			return ("write");
		p += n;
		l -= n;
	}
	return NULL;
}

/* Pass along the CGI environment variables to sqwebmaild */

static void send_environ(int fd, int passfd)
{
	char buf[SOCKENVIRONLEN];

	char *p=buf+sizeof(size_t)+1;
	size_t l=sizeof(buf)-sizeof(size_t)-2;
	size_t i;
	const char *cp;

	buf[sizeof(l)]=passfd;

	for (i=0; environ[i]; i++)
	{
		size_t m;

		if (!VALIDCGIVAR(environ[i]))
			continue;

		m=strlen(environ[i])+1;

		if (m > l)
			err("CGI", "CGI environment exceeds allowed "
			    "maximum size.");

		memcpy(p, environ[i], m);
		p += m;
		l -= m;
	}

	l=p-(buf+sizeof(l)+1);
	memcpy(buf, &l, sizeof(l));

	cp=force_write(fd, buf, l + sizeof(l)+1);
	if (cp)
		sys_err(cp);

	/*
	** If the platform supports it, pass the file descriptors
	** to sqwebmaild.
	*/

#if	CGI_PASSFD

	if (passfd)
	{
		struct iovec iov;
		char dummy;

#if CGI_PASSFD_MSGACCRIGHTS

		int fdbuf[2];
		struct msghdr msg;

		fdbuf[0]=0;
		fdbuf[1]=1;
		memset(&iov, 0, sizeof(iov));
		msg.msg_accrights=(caddr_t)fdbuf;
		msg.msg_accrightslen=sizeof(fdbuf);
#endif

#if CGI_PASSFD_MSGCONTROL

		int fdbuf[2];
		struct msghdr msg;
		struct cmsghdr *cmsg;
		char buf[CMSG_SPACE(sizeof(fdbuf))];

		fdbuf[0]=0;
		fdbuf[1]=1;

		memset(&msg, 0, sizeof(msg));
		msg.msg_control=buf;
		msg.msg_controllen=sizeof(buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level=SOL_SOCKET;
		cmsg->cmsg_type=SCM_RIGHTS;
		cmsg->cmsg_len=CMSG_LEN(sizeof(fdbuf));
		memcpy(CMSG_DATA(cmsg), fdbuf, sizeof(fdbuf));
#endif
		msg.msg_iov=&iov;
		msg.msg_iovlen=1;
		iov.iov_base=&dummy;
		iov.iov_len=1;

		dummy=0;
		if (sendmsg(fd, &msg, 0) < 0)
			sys_err("sendmsg(filedescriptor)");
	}
#endif

}

static void passthrough(int s, int passed_fd)
{
	char toclientbuf[8192];
	char tosqbuf[8192];

	char *toclientptr, *tosqptr;
	size_t toclientlen, tosqlen;
	int stdin_closed=0;

	toclientptr=NULL;
	tosqptr=NULL;
	toclientlen=0;
	tosqlen=0;

	if (passed_fd)
	{
		stdin_closed=1;  /* sqwebmaild will read on the fd itself */
		if (fcntl(s, F_SETFL, O_NDELAY) < 0)
			sys_err("fcntl");
	}

	/* When the file descriptor is passed, we will not do any actual I/O,
	** so there's no need to set stdin/stdout to nonblock mode
	*/
	else if (fcntl(0, F_SETFL, O_NDELAY) < 0 ||
		 fcntl(1, F_SETFL, O_NDELAY) < 0 ||
		 fcntl(s, F_SETFL, O_NDELAY) < 0)
		sys_err("fcntl");

	for (;;)
	{
		fd_set fdr, fdw;

		FD_ZERO(&fdr);
		FD_ZERO(&fdw);

		if (tosqlen)
			FD_SET(s, &fdw);
		else if (!stdin_closed)
			FD_SET(0, &fdr);

		if (toclientlen)
			FD_SET(1, &fdw);
		else
			FD_SET(s, &fdr);

		if (select(s+1, &fdr, &fdw, 0, NULL) <= 0)
		{
			fcntl(1, F_SETFL, 0);
			sys_err("select");
		}

		if (tosqlen)
		{
			if (FD_ISSET(s, &fdw))
			{
				int m=write(s, tosqptr, tosqlen);

				if (m <= 0)
				{
					fcntl(1, F_SETFL, 0);
					sys_err("write");
				}

				tosqptr += m;
				tosqlen -= m;
			}
		}
		else
		{
			if (FD_ISSET(0, &fdr))
			{
				int m=read(0, tosqbuf, sizeof(tosqbuf));

				if (m < 0) /* network error */
					return;

				if (m == 0)
					stdin_closed=1;

				tosqptr=tosqbuf;
				tosqlen=m;
			}
		}

		if (toclientlen)
		{
			if (FD_ISSET(1, &fdw))
			{
				int m=write(1, toclientptr, toclientlen);

				if (m <= 0)
					return; /* Client aborted, nocare */

				toclientptr += m;
				toclientlen -= m;
			}
		}
		else
		{
			if (FD_ISSET(s, &fdr))
			{
				int m=read(s, toclientbuf,
					   sizeof(toclientbuf));

				if (m <= 0)
					return;

				toclientptr=toclientbuf;
				toclientlen=m;
			}
		}
	}
}

void cgi_connectdaemon(const char *sockfilename, int pass_fd)
{
	int	s;
	struct  sockaddr_un ssun;
	int	triedagain=0;
	int	rc;

	/* Connect to sqwebmaild via a socket */

	signal(SIGPIPE, SIG_IGN);
	if ((s=socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
		sys_err("socket");

	if (fcntl(s, F_SETFL, O_NDELAY) < 0)
		sys_err("fcntl");

	ssun.sun_family=AF_UNIX;
	strcpy(ssun.sun_path, sockfilename);

	while ((rc=connect(s, (struct sockaddr *)&ssun, sizeof(ssun))) < 0
	       && errno == EAGAIN)
	{
		if (++triedagain > 5)
			break;
		sleep(1);
		ssun.sun_family=AF_UNIX;
		strcpy(ssun.sun_path, sockfilename);
	}

	if (rc < 0)
	{
		struct	timeval	tv;
		fd_set	fds;

		int	errcode;
		socklen_t errcode_l;

		if (errno != EINPROGRESS)
			connect_err("connect");

		tv.tv_sec=30;
		tv.tv_usec=0;
		FD_ZERO(&fds);
		FD_SET(s, &fds);
		if (select(s+1, 0, &fds, 0, &tv) <= 0)
			connect_err("select");


		errcode_l=sizeof(errcode);

		if (getsockopt(s, SOL_SOCKET, SO_ERROR, &errcode, &errcode_l)
		    < 0)
			connect_err("setsockopt");


		if (errcode)
		{
			errno=errcode;
			connect_err("connect");
		}
	}

	if (triedagain)
	{
		fprintf(stderr,
		       "CRIT: Several attempts were necessary to connect to sqwebmaild\n");
		fprintf(stderr,
		       "CRIT: Consider increasing the number of pre-forked sqwebmaild processes\n");
	}

	if (fcntl(s, F_SETFL, 0) < 0)
		sys_err("fcntl");

	send_environ(s, pass_fd);
	passthrough(s, pass_fd);
	close(s);
	exit(0);
}
