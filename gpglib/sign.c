/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<courier-unicode.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/time.h>
#include	<sys/wait.h>
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#include	"gpg.h"
#include	"gpglib.h"

#include	"numlib/numlib.h"

extern int libmail_gpg_stdin, libmail_gpg_stdout, libmail_gpg_stderr;
extern pid_t libmail_gpg_pid;


/*
** Sign a key.
*/

static int dosignkey(int (*)(const char *, size_t, void *),
		     const char *cmdstr,
		     void *);

int libmail_gpg_signkey(const char *gpgdir, const char *signthis, const char *signwith,
		int passphrase_fd,
		int (*dump_func)(const char *, size_t, void *),
		void *voidarg)
{
	char *argvec[14];
	int rc;
	char passphrase_fd_buf[NUMBUFSIZE];
	int i;

	argvec[0]="gpg";
	argvec[1]="--command-fd";
	argvec[2]="0";
	argvec[3]="--default-key";
	argvec[4]=(char *)signwith;
	argvec[5]="-q";
	argvec[6]="--no-tty";

	i=7;
	if (passphrase_fd >= 0 && fcntl(passphrase_fd, F_SETFD, 0) >= 0)
	{
		GPGARGV_PASSPHRASE_FD(argvec, i, passphrase_fd,
				      passphrase_fd_buf);
#if GPG_REQUIRES_PINENTRY_MODE_OPTION
		argvec[i++]="--pinentry-mode";
		argvec[i++]="loopback";
#endif
	}

	argvec[i++]="--sign-key";
	argvec[i++]=(char *)signthis;
	argvec[i]=0;

	if (libmail_gpg_fork(&libmail_gpg_stdin, &libmail_gpg_stdout, NULL, gpgdir, argvec) < 0)
		rc= -1;
	else
	{
		int rc2;

		char cmdstr[10];

		strcpy(cmdstr, "Y\n");

		rc=dosignkey(dump_func, cmdstr, voidarg);
		rc2=libmail_gpg_cleanup();
		if (rc2)
			rc=rc2;
	}
	return (rc);
}

static int dosignkey(int (*dump_func)(const char *, size_t, void *),
		     const char *cmdstr,
		     void *voidarg)
{
	int rc=libmail_gpg_write( cmdstr, strlen(cmdstr),
			 dump_func, NULL, NULL, 0, voidarg);
	int rc2;

	if (rc == 0)
		rc=libmail_gpg_read(dump_func, NULL, NULL, 0, voidarg);
	rc2=libmail_gpg_cleanup();
	if (rc == 0)
		rc=rc2;
	return (rc);
}

int libmail_gpg_makepassphrasepipe(const char *passphrase,
				   size_t passphrase_size)
{
	int pfd[2];
	pid_t p;

	if (pipe(pfd) < 0)
		return -1;

	p=fork();

	if (p < 0)
	{
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	}

	if (p == 0)
	{
		p=fork();

		if (p)
			_exit(0);

		close(pfd[0]);

		while (passphrase_size)
		{
			ssize_t n=write(pfd[1], passphrase, passphrase_size);

			if (n < 0)
				break;
			passphrase += n;
			passphrase_size -= n;
		}
		_exit(0);
	}
	waitpid(p, NULL, 0);
	close(pfd[1]);
	return(pfd[0]);
}
