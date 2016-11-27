/*
** Copyright 2001-2010 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include "gpg.h"

#include <sys/types.h>
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include	"mimegpgfork.h"

static int libmail_gpgmime_fork(const char *gpgdir,
			const char *passphrase_fd,
			int xargc, char **xargv, int argc, char **argv,
			struct gpgmime_forkinfo *fi)
{
	int pipes[3][2];
	int n;
	pid_t p;
	struct sigaction sa;

	for (n=0; n<3; n++)
	{
		if (pipe(pipes[n]) < 0)
		{
			while (n)
			{
				--n;
				close(pipes[n][0]);
				close(pipes[n][1]);
				return (-1);
			}
		}
	}

	if (fcntl(pipes[0][1], F_SETFL, O_NONBLOCK) ||
	    fcntl(pipes[1][0], F_SETFL, O_NONBLOCK) ||
	    fcntl(pipes[2][0], F_SETFL, O_NONBLOCK))
	{
		for (n=0; n<3; n++)
		{
			close(pipes[n][0]);
			close(pipes[n][1]);
		}
		return (-1);
	}

	signal(SIGCHLD, SIG_DFL);

	p=fork();
	if (p < 0)
	{
		for (n=0; n<3; n++)
		{
			close(pipes[n][0]);
			close(pipes[n][1]);
		}
		return (-1);
	}

	if (p == 0)
	{
		char **newargv;
		int i;
		const char *gpg;

		dup2(pipes[0][0], 0);
		dup2(pipes[1][1], 1);
		dup2(pipes[2][1], 2);

		for (n=0; n<3; n++)
		{
			close(pipes[n][0]);
			close(pipes[n][1]);
		}

		newargv=malloc( (xargc + argc + 7) * sizeof(char *));
		if (!newargv)
		{
			perror("malloc");
			_exit(1);
		}

		i=0;
		newargv[i++]="gpg";
		if (passphrase_fd)
		{
			newargv[i++]="--batch";
			newargv[i++]="--passphrase-fd";
			newargv[i++]=(char *)passphrase_fd;
#if GPG_REQUIRES_PINENTRY_MODE_OPTION
			newargv[i++]="--pinentry-mode";
			newargv[i++]="loopback";
#endif
		}

		for (n=0; n<xargc; n++)
			newargv[i++]=xargv[n];
		for (n=0; n<argc; n++)
			newargv[i++]=argv[n];

		newargv[i]=0;

		if (gpgdir)
		{
			char *s;

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

		gpg=getenv("GPG");
		if (!gpg || !*gpg)
			gpg=GPG;

		execvp(gpg, newargv);
		perror(gpg);
		_exit(1);
	}

	fi->gpg_errflag=0;
	fi->togpg_fd=pipes[0][1];
	close(pipes[0][0]);
	fi->fromgpg_fd=pipes[1][0];
	close(pipes[1][1]);
	fi->fromgpg_errfd=pipes[2][0];
	close(pipes[2][1]);

	fi->gpg_writecnt=0;
	fi->gpg_errcnt=0;
	fi->gpg_errbuf[0]=0;
	fi->gpg_pid=p;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler=SIG_IGN;
	sigaction(SIGPIPE, &sa, &fi->old_pipe_sig);

	fi->gpg_readhandler=0;
	fi->gpg_voidarg=0;
	return (0);
}

static void gpgmime_writeflush(struct gpgmime_forkinfo *);

void libmail_gpgmime_write(struct gpgmime_forkinfo *fi, const char *p, size_t n)
{
	while (n)
	{
		size_t i;

		if (fi->gpg_writecnt == sizeof(fi->gpg_writebuf))
			gpgmime_writeflush(fi);

		i=sizeof(fi->gpg_writebuf) - fi->gpg_writecnt;

		if ((size_t)i > n)
			i=n;

		memcpy(fi->gpg_writebuf + fi->gpg_writecnt, p, i);

		fi->gpg_writecnt += i;

		p += i;
		n -= i;
	}
}

static void libmail_gpgmime_read(struct gpgmime_forkinfo *fi, fd_set *fdr)
{
	char readbuf[BUFSIZ];
	int i;

	if (fi->fromgpg_fd >= 0 && FD_ISSET(fi->fromgpg_fd, fdr))
	{
		i=read(fi->fromgpg_fd, readbuf, sizeof(readbuf));

		if (i <= 0)
		{
			close(fi->fromgpg_fd);
			fi->fromgpg_fd= -1;
		}
		else
		{
			if (fi->gpg_readhandler)
				fi->gpg_errflag=
					(*fi->gpg_readhandler)(readbuf, i,
							       fi->gpg_voidarg
							       );
		}
	}

	if (fi->fromgpg_errfd >= 0 && FD_ISSET(fi->fromgpg_errfd, fdr))
	{
		i=read(fi->fromgpg_errfd, readbuf, sizeof(readbuf));

		if (i <= 0)
		{
			close(fi->fromgpg_errfd);
			fi->fromgpg_errfd= -1;
		}
		else
		{
			if (i >= sizeof(fi->gpg_errbuf)-1-fi->gpg_errcnt)
				i=sizeof(fi->gpg_errbuf)-1-fi->gpg_errcnt;
			if (i)
			{
				memcpy(fi->gpg_errbuf+fi->gpg_errcnt,
				       readbuf, i);
				fi->gpg_errbuf[fi->gpg_errcnt += i]=0;
			}
		}
	}
}

static void gpgmime_writeflush(struct gpgmime_forkinfo *fi)
{
	const char *p=fi->gpg_writebuf;
	unsigned n=fi->gpg_writecnt;

	while (n && !fi->gpg_errflag)
	{
		fd_set fdr, fdw;
		int maxfd=fi->togpg_fd, i;

		FD_ZERO(&fdr);
		FD_ZERO(&fdw);

		FD_SET(fi->togpg_fd, &fdw);

		if (fi->fromgpg_fd >= 0)
		{
			FD_SET(fi->fromgpg_fd, &fdr);
			if (fi->fromgpg_fd > maxfd)
				maxfd=fi->fromgpg_fd;
		}

		if (fi->fromgpg_errfd >= 0)
		{
			FD_SET(fi->fromgpg_errfd, &fdr);
			if (fi->fromgpg_errfd > maxfd)
				maxfd=fi->fromgpg_errfd;
		}

		if (select(maxfd+1, &fdr, &fdw, NULL, NULL) <= 0)
			continue;

		libmail_gpgmime_read(fi, &fdr);

		if (!FD_ISSET(fi->togpg_fd, &fdw))
			continue;

		i=write(fi->togpg_fd, p, n);

		if (i <= 0)
			fi->gpg_errflag=1;
		else
		{
			p += i;
			n -= i;
		}
	}
	fi->gpg_writecnt=0;
}

int libmail_gpgmime_finish(struct gpgmime_forkinfo *fi)
{
	pid_t p2;
	int waitstat;

	gpgmime_writeflush(fi);
	close(fi->togpg_fd);
	sigaction(SIGPIPE, &fi->old_pipe_sig, NULL);

	while (!fi->gpg_errflag &&
	       (fi->fromgpg_fd >= 0 || fi->fromgpg_errfd >= 0))
	{
		fd_set fdr;
		int maxfd=0;

		FD_ZERO(&fdr);

		if (fi->fromgpg_fd >= 0)
		{
			FD_SET(fi->fromgpg_fd, &fdr);
			if (fi->fromgpg_fd > maxfd)
				maxfd=fi->fromgpg_fd;
		}

		if (fi->fromgpg_errfd >= 0)
		{
			FD_SET(fi->fromgpg_errfd, &fdr);
			if (fi->fromgpg_errfd > maxfd)
				maxfd=fi->fromgpg_errfd;
		}

		if (select(maxfd+1, &fdr, NULL, NULL, NULL) <= 0)
			continue;

		libmail_gpgmime_read(fi, &fdr);
	}

	while ((p2=wait(&waitstat)) != fi->gpg_pid)
	{
		if (p2 < 0 && errno == ECHILD)
			break;
	}

	if (fi->gpg_errflag == 0)
	{
		if (!WIFEXITED(waitstat))
			fi->gpg_errflag=1;
		else
			fi->gpg_errflag=WEXITSTATUS(waitstat);
	}
	return (fi->gpg_errflag);
}

int libmail_gpgmime_forksignencrypt(const char *gpgdir,
				    const char *passphrase_fd,
				    int flags,
				    int argc, char **argv,
				    int (*output_func)(const char *,
						       size_t, void *),
				    void *output_voidarg,
				    struct gpgmime_forkinfo *gpgfi)
{
	char *xargvec[5];
	int xargc=0;
	int rc;

	if (flags & GPG_SE_SIGN)
	{
		xargvec[xargc++]="-s";
		if (! (flags & GPG_SE_ENCRYPT))
			xargvec[xargc++]="-b";
	}

	if (flags & GPG_SE_ENCRYPT)
		xargvec[xargc++]="-e";

	xargvec[xargc++]="-a";

	rc=libmail_gpgmime_fork(gpgdir, passphrase_fd,
			xargc, xargvec, argc, argv, gpgfi);

	gpgfi->gpg_readhandler= output_func;
	gpgfi->gpg_voidarg=output_voidarg;
	return (rc);
}

int libmail_gpgmime_forkchecksign(const char *gpgdir,
			  const char *passphrase_fd,
			  const char *content_filename,
			  const char *signature_filename,
			  int argc, char **argv,
			  struct gpgmime_forkinfo *gpgfi)
{
	char *xargvec[3];
	char **newargv;
	int i;
	int rc;

	xargvec[0]="--verify";
	xargvec[1]="--charset";
	xargvec[2]=GPG_CHARSET;

	newargv=(char **)malloc((argc+2)*sizeof(char *));
	if (!newargv)
	{
		perror("malloc");
		exit(1);
	}

	for (i=0; i<argc; i++)
		newargv[i]=argv[i];
	newargv[i++]=(char *)signature_filename;
	newargv[i++]=(char *)content_filename;

	rc=libmail_gpgmime_fork(gpgdir, passphrase_fd, 3, xargvec, i, newargv, gpgfi);
	free(newargv);
	return (rc);
}

int libmail_gpgmime_forkdecrypt(const char *gpgdir,
			const char *passphrase_fd,
			int argc, char **argv,
			int (*output_func)(const char *, size_t, void *),
			void *output_voidarg,
			struct gpgmime_forkinfo *gpgfi)
{
	char *xargv[3];
	int rc;

	xargv[0]="--decrypt";
	xargv[1]="--charset";
	xargv[2]=GPG_CHARSET;

	rc=libmail_gpgmime_fork(gpgdir, passphrase_fd, 3, xargv, argc, argv, gpgfi);

	gpgfi->gpg_readhandler= output_func;
	gpgfi->gpg_voidarg= output_voidarg;
	return (rc);
}

const char *libmail_gpgmime_getoutput(struct gpgmime_forkinfo *gpgfi)
{
	return (gpgfi->gpg_errbuf);
}

const char *libmail_gpgmime_getcharset(struct gpgmime_forkinfo *gpgfi)
{
	return (GPG_CHARSET);
}
