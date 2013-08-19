/*
** Copyright 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#if	TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	<errno.h>
#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include        <sys/socket.h>
#include	<sys/uio.h>
#include        <sys/un.h>

static int read_environ(int);

static int start_daemon(const char *lockfilename);

static void run_daemon(int fd, int termfd, int connfd, void (*handler)(void *),
		       void *dummy);

static void run_prefork(int fd, size_t ndaemons, void (*handler)(void *),
			void *dummy);

void cgi_daemon(int nprocs, const char *lockfile,
		void (*postinit)(void *), void (*handler)(void *),
		void *dummy)
{
	int fd=start_daemon(lockfile);

	if (postinit)
		(*postinit)(dummy);

	if (nprocs > 0)
		run_prefork(fd, nprocs, handler, dummy);
	else
		run_daemon(fd, -1, -1, handler, dummy);
}

/* Start in daemon mode.  Return listening socket file descriptor */

static int start_daemon(const char *lockfile)
{
	int     fd;
	struct  sockaddr_un skun;

	unlink(lockfile);

	fd=socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
	{
		perror("socket");
		return (-1);
	}

        skun.sun_family=AF_UNIX;
        strcpy(skun.sun_path, lockfile);
        if (bind(fd, (const struct sockaddr *)&skun, sizeof(skun)) ||
                listen(fd, SOMAXCONN) ||
                chmod(skun.sun_path, 0777) ||
                fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        {
                perror(lockfile);
                close(fd);
                return (-1);
        }
	return fd;
}

static int prefork(int listenfd, int *allpipes, size_t ndaemos,
		   int *termpipe, void (*handler)(void *), void *dummy);

static void run_prefork(int fd, size_t ndaemons, void (*handler)(void *),
			void *dummy)
{
	int *cpipes; /* Completion pipes from preforked processes */
	int termpipe[2]; /* Termination pipe to preforked processes */
	size_t i;

	if ((cpipes=malloc(sizeof(int)*ndaemons)) == NULL)
	{
		fprintf(stderr,
		       "CRIT: malloc failed: %s\n",
		       strerror(errno));
		exit(1);
	}

	if (pipe(termpipe) < 0)
	{
		fprintf(stderr,
			"CRIT: pipe failed: %s\n",
			strerror(errno));
		exit(1);
	}


	/* Start the initial set of preforked daemons */

	for (i=0; i<ndaemons; i++)
		cpipes[i]= -1;

	for (i=0; i<ndaemons; i++)
		cpipes[i]=prefork(fd, cpipes, ndaemons, termpipe, handler,
				  dummy);


	for (;;)
	{
		fd_set	fdr;
		int	maxfd=0;

		FD_ZERO(&fdr);

		for (i=0; i<ndaemons; i++)
		{
			if (cpipes[i] >= maxfd)
				maxfd=cpipes[i]+1;

			FD_SET(cpipes[i], &fdr);
		}

		if (select(maxfd, &fdr, NULL, NULL, NULL) <= 0)
			continue;

		/*
		** When child process gets a connection, it closes its
		** completion pipe, which makes the pipe selectable for
		** read.
		*/

		for (i=0; i<ndaemons; i++)
		{
			if (FD_ISSET(cpipes[i], &fdr))
			{
				close(cpipes[i]);
				cpipes[i]= -1;
				cpipes[i]=prefork(fd, cpipes,
						  ndaemons, termpipe, handler,
						  dummy);
			}
		}
	}
}

/* Start a preforked process */

static int prefork(int listenfd, int *allpipes, size_t ndaemons,
		   int *termpipe, void (*handler)(void *), void *dummy)
{
	int newpipe[2];
	pid_t p;
	int waitstat;
	size_t i;

	if (pipe(newpipe) < 0)
	{
		fprintf(stderr,
			"CRIT: pipe failed: %s\n", strerror(errno));
		exit(1);
	}

	while ((p=fork()) < 0)
	{
		fprintf(stderr,
			"CRIT: fork failed: %s\n", strerror(errno));
		sleep(5);
	}

	if (p) /* parent */
	{
		close(newpipe[1]);

		/* Wait for first child process to go away */

		while (wait(&waitstat) != p)
			;

		return (newpipe[0]);
	}

	close(newpipe[0]);
	close(termpipe[1]);

	/* New child doesn't need pipes from other children */

	for (i=0; i<ndaemons; i++)
		if (allpipes[i] >= 0)
			close(allpipes[i]);

	/* Fork once more, so that the parent process can continue */

	if (fork())
		exit(0);

	run_daemon(listenfd, termpipe[0], newpipe[1], handler, dummy);
	return (-1);
}

static void run_daemon(int fd, int termfd, int acceptedfd,
		       void (*handler)(void *), void *dummy)
{
	int cfd;

	for (;;)
	{
		fd_set	fdr;
		pid_t p;
		int maxfd;

		FD_ZERO(&fdr);

		FD_SET(fd, &fdr);

		maxfd=fd;

		if (termfd >= 0)
		{
			if (termfd > maxfd)
				maxfd=termfd;

			FD_SET(termfd, &fdr);
		}

		if (select(maxfd+1, &fdr, NULL, NULL, NULL) <= 0)
			continue;
		if (termfd >= 0 &&
		    FD_ISSET(termfd, &fdr)) /* Terminate all child procs */
			exit(0);

		if (!FD_ISSET(fd, &fdr))
			continue;

		cfd=accept(fd, NULL, 0);

		if (cfd < 0)
			continue;

		if (acceptedfd >= 0) /* preforked daemon */
		{
			if (termfd >= 0)
				close(termfd);
			close(acceptedfd);
			break;
		}

		p=fork();

		if (p < 0)
		{
			fprintf(stderr,
			       "CRIT: fork() failed: %s\n", strerror(errno));
			continue;
		}

		if (p)
		{
			int dummy;

			close(cfd);
			while (wait(&dummy) != p)
				continue;
			continue;
		}

		/* Child forks once more, parent exits */

		if (fork())
			exit(0);

		break;

	}

	/* child */

	if (fcntl(cfd, F_SETFL, 0) < 0)
	{
		fprintf(stderr,
		       "CRIT: fcntl(): %s\n", strerror(errno));
		exit(0);
	}

	close(fd);
	if (read_environ(cfd))
	{
		close(0);
		close(1);
		if (dup(cfd) != 0 || dup(cfd) != 1)
		{
			fprintf(stderr,
			       "CRIT: dup() did not work as expected\n");
			exit(0);
		}
		close(cfd);
	}

	if (fcntl(0, F_SETFL, 0) < 0 ||
	    fcntl(1, F_SETFL, 0) < 0)
	{
		fprintf(stderr,
		       "CRIT: fcntl() failed: %s\n", strerror(errno));
		exit(0);
	}

	(*handler)(dummy);
	exit(0);
}


/* Read environment from the sqwebmail stub */

static void force_read(int cfd, char *p, size_t l)
{
	int m;

	while (l)
	{
		m=read(cfd, p, l);
		if (m <= 0)
		{
			fprintf(stderr,
			       "WARN: socket closed while reading"
			       " environment.\n");
			exit(0);
		}

		p += m;
		l -= m;
	}
}

/* Receive CGI environment */

static int read_environ(int cfd)
{
	static char buf[SOCKENVIRONLEN];
	size_t l;
	char *p;
	int passfd;

	force_read(cfd, buf, 1+sizeof(l));

	memcpy(&l, buf, sizeof(l));
	passfd=buf[sizeof(l)];

	if (l >= sizeof(buf))
	{
		fprintf(stderr,
		       "WARN: invalid environment received via socket.\n");
		exit(0);
	}

	alarm(10); /* Just in case - punt */
	force_read(cfd, buf, l);
	buf[l]=0;
	alarm(0);

	/* Vet environment strings for only known good strings */

	p=buf;
	while (p < buf+l)
	{
		if (strchr(p, '=') == NULL ||
		    !VALIDCGIVAR(p))
		{
			fprintf(stderr,
			       "WARN: invalid environment received"
			       " via socket: %s\n" , p);
			exit(0);
		}

		putenv(p);

		while (*p++)
			;
	}

	/* Receive file descriptors, if supported by the platform */

#if	CGI_PASSFD

	if (passfd)
	{
		struct iovec iov;
		char dummy;

#if CGI_PASSFD_MSGACCRIGHTS

		int fdbuf[2];
		struct msghdr msg;
#endif

#if CGI_PASSFD_MSGCONTROL

		int fdbuf[2];
		struct msghdr msg;
		struct cmsghdr *cmsg;
		char buf[CMSG_SPACE(sizeof(fdbuf))];
#endif

#if CGI_PASSFD_MSGACCRIGHTS
		memset(&iov, 0, sizeof(iov));
		msg.msg_accrights=(caddr_t)fdbuf;
		msg.msg_accrightslen=sizeof(fdbuf);
#endif


#if CGI_PASSFD_MSGCONTROL
		memset(&msg, 0, sizeof(msg));
		msg.msg_control=buf;
		msg.msg_controllen=sizeof(buf);
#endif

		msg.msg_iov=&iov;
		msg.msg_iovlen=1;

		iov.iov_base=&dummy;
		iov.iov_len=1;

		if (recvmsg(cfd, &msg, 0) <= 0)
		{
			perror("Internal error - recvmsg() failed");
			exit(0);
		}

#if CGI_PASSFD_MSGACCRIGHTS

		if (msg.msg_accrightslen < sizeof(fdbuf))
		{
			perror("Internal error - malformed recvmsg()");
			exit(0);
		}

#endif

#if CGI_PASSFD_MSGCONTROL
		if (msg.msg_controllen < sizeof(buf) ||
		    (cmsg = CMSG_FIRSTHDR(&msg))->cmsg_level != SOL_SOCKET ||
		    cmsg->cmsg_type != SCM_RIGHTS ||
		    cmsg->cmsg_len != CMSG_LEN(sizeof(fdbuf)))
		{
			perror("Internal error - malformed recvmsg()");
			exit(0);
		}

		memcpy(fdbuf, CMSG_DATA(cmsg), sizeof(fdbuf));
#endif
		close(0);
		close(1);
		if (dup(fdbuf[0]) != 0 || dup(fdbuf[1]) != 1)
			fprintf(stderr,
			       "CRIT: dup() did not work as expected in"
			       " read_environ()\n");
		close(fdbuf[0]);
		close(fdbuf[1]);
		return 0;
	}
#endif

	return 1;
}
