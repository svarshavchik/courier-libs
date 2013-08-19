/*
** Copyright 2001-2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<signal.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/time.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include	"gpg.h"
#include	"gpglib.h"

extern int libmail_gpg_stdin, libmail_gpg_stdout, libmail_gpg_stderr;
extern pid_t libmail_gpg_pid;


/*
** Helper function: for and run pgp, with the given file descriptors and
** options.
*/

pid_t libmail_gpg_fork(int *libmail_gpg_stdin, int *libmail_gpg_stdout,
		       int *libmail_gpg_stderr,
		       const char *gpgdir,
		       char **argvec)
{
	int pipein[2], pipeout[2], pipeerr[2];
	pid_t p;
	char *s;

	if (libmail_gpg_stdin && pipe(pipein) < 0)
		return (-1);

	if (libmail_gpg_stdout && pipe(pipeout) < 0)
	{
		if (libmail_gpg_stdin)
		{
			close(pipein[0]);
			close(pipein[1]);
		}
		return (-1);
	}

	if (libmail_gpg_stderr && pipe(pipeerr) < 0)
	{
		if (libmail_gpg_stdout)
		{
			close(pipeout[0]);
			close(pipeout[1]);
		}

		if (libmail_gpg_stdin)
		{
			close(pipein[0]);
			close(pipein[1]);
		}
		return (-1);
	}

	signal(SIGCHLD, SIG_DFL);
	p=libmail_gpg_pid=fork();
	if (p < 0)
	{
		if (libmail_gpg_stderr)
		{
			close(pipeerr[0]);
			close(pipeerr[1]);
		}

		if (libmail_gpg_stdout)
		{
			close(pipeout[0]);
			close(pipeout[1]);
		}
		if (libmail_gpg_stdin)
		{
			close(pipein[0]);
			close(pipein[1]);
		}

		return (-1);
	}

	if (p)
	{
		signal(SIGPIPE, SIG_IGN);

		if (libmail_gpg_stderr)
		{
			close(pipeerr[1]);
			*libmail_gpg_stderr=pipeerr[0];
		}

		if (libmail_gpg_stdout)
		{
			close(pipeout[1]);
			*libmail_gpg_stdout=pipeout[0];
		}

		if (libmail_gpg_stdin)
		{
			close(pipein[0]);
			*libmail_gpg_stdin=pipein[1];
		}
		return (0);
	}

	if (libmail_gpg_stderr)
	{
		dup2(pipeerr[1], 2);
		close(pipeerr[0]);
		close(pipeerr[1]);
	}
	else if (libmail_gpg_stdout)
	{
		dup2(pipeout[1], 2);
	}

	if (libmail_gpg_stdout)
	{
		dup2(pipeout[1], 1);
		close(pipeout[0]);
		close(pipeout[1]);
	}

	if (libmail_gpg_stdin)
	{
		dup2(pipein[0], 0);
		close(pipein[0]);
		close(pipein[1]);
	}

	if (gpgdir)
	{
		s=malloc(sizeof("GNUPGHOME=")+strlen(gpgdir));
		if (!s)
		{
			perror("malloc");
			exit(1);
		}
		strcat(strcpy(s, "GNUPGHOME="), gpgdir);
		if (putenv(s) < 0)
		{
			perror("putenv");
			exit(1);
		}
	}

	{
		const char *gpg=getenv("GPG");
		if (!gpg || !*gpg)
			gpg=GPG;

		execv(gpg, argvec);
		perror(gpg);
	}
	_exit(1);
	return (0);
}

int libmail_gpg_write(const char *p, size_t cnt,
		      int (*stdout_func)(const char *, size_t, void *),
		      int (*stderr_func)(const char *, size_t, void *),
		      int (*timeout_func)(void *),
		      unsigned timeout,
		      void *voidarg)
{
	char buf[BUFSIZ];

	fd_set fdr, fdw;
	struct timeval tv;

	if (!timeout_func)
		timeout=0;
	while (cnt)
	{
		int maxfd=0;
		int n;

		FD_ZERO(&fdr);
		FD_ZERO(&fdw);

		FD_SET(libmail_gpg_stdin, &fdw);

		if (libmail_gpg_stdout >= 0)
		{
			FD_SET(libmail_gpg_stdout, &fdr);
			if (libmail_gpg_stdout > maxfd)
				maxfd=libmail_gpg_stdout;
		}

		if (libmail_gpg_stderr >= 0)
		{
			FD_SET(libmail_gpg_stderr, &fdr);
			if (libmail_gpg_stderr > maxfd)
				maxfd=libmail_gpg_stderr;
		}

		tv.tv_usec=0;
		tv.tv_sec=timeout;
		n=select(maxfd+1, &fdr, &fdw, NULL, timeout ? &tv:NULL);
		if (n == 0)
		{
			n=(*timeout_func)(voidarg);
			if (n)
				return(n);
			continue;
		}
		if (n < 0)
			continue;

		if (FD_ISSET(libmail_gpg_stdin, &fdw))
		{
			int n=write(libmail_gpg_stdin, p, cnt);

			if (n <= 0)
				return (-1);

			p += n;
			cnt -= n;
		}

		if (libmail_gpg_stdout >= 0 &&
		    FD_ISSET(libmail_gpg_stdout, &fdr))
		{
			int n=read(libmail_gpg_stdout, buf, sizeof(buf));

			if (n <= 0)
			{
				close(libmail_gpg_stdout);
				libmail_gpg_stdout= -1;
			}
			else if (stdout_func &&
				 (n=(*stdout_func)(buf, n, voidarg)) != 0)
				return (n);
		}

		if (libmail_gpg_stderr >= 0 &&
		    FD_ISSET(libmail_gpg_stderr, &fdr))
		{
			int n=read(libmail_gpg_stderr, buf, sizeof(buf));

			if (n <= 0)
			{
				close(libmail_gpg_stderr);
				libmail_gpg_stderr= -1;
			}
			else if (stderr_func &&
				 (n=(*stderr_func)(buf, n, voidarg)) != 0)
				return (n);
		}
	}
	return (0);
}

int libmail_gpg_read(int (*stdout_func)(const char *, size_t, void *),
		     int (*stderr_func)(const char *, size_t, void *),
		     int (*timeout_func)(void *),
		     unsigned timeout,
		     void *voidarg)
{
	char buf[BUFSIZ];

	fd_set fdr;
	struct timeval tv;

	if (libmail_gpg_stdin >= 0)
	{
		close(libmail_gpg_stdin);
		libmail_gpg_stdin= -1;
	}

	if (!timeout_func)
		timeout=0;

	while ( libmail_gpg_stdout >= 0 || libmail_gpg_stderr >= 0)
	{
		int maxfd=0;
		int n;

		FD_ZERO(&fdr);

		if (libmail_gpg_stdout >= 0)
		{
			FD_SET(libmail_gpg_stdout, &fdr);
			if (libmail_gpg_stdout > maxfd)
				maxfd=libmail_gpg_stdout;
		}

		if (libmail_gpg_stderr >= 0)
		{
			FD_SET(libmail_gpg_stderr, &fdr);
			if (libmail_gpg_stderr > maxfd)
				maxfd=libmail_gpg_stderr;
		}

		tv.tv_usec=0;
		tv.tv_sec=timeout;
		n=select(maxfd+1, &fdr, NULL, NULL, timeout ? &tv:NULL);

		if (n == 0)
		{
			n=(*timeout_func)(voidarg);
			if (n)
				return(n);
			continue;
		}
		if (n < 0)
			continue;

		if (libmail_gpg_stdout >= 0 &&
		    FD_ISSET(libmail_gpg_stdout, &fdr))
		{
			int n=read(libmail_gpg_stdout, buf, sizeof(buf));

			if (n <= 0)
			{
				close(libmail_gpg_stdout);
				libmail_gpg_stdout= -1;
			}
			else if (stdout_func &&
				 (n=(*stdout_func)(buf, n, voidarg)) != 0)
				return (n);
		}

		if (libmail_gpg_stderr >= 0 &&
		    FD_ISSET(libmail_gpg_stderr, &fdr))
		{
			int n=read(libmail_gpg_stderr, buf, sizeof(buf));

			if (n <= 0)
			{
				close(libmail_gpg_stderr);
				libmail_gpg_stderr= -1;
			}
			else if (stderr_func &&
				 (n=(*stderr_func)(buf, n, voidarg)) != 0)
				return (n);
		}
	}
	return (0);
}
