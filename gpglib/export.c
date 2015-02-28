/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
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
#include	<courier-unicode.h>

#include	"gpg.h"
#include	"gpglib.h"

#include	"numlib/numlib.h"

extern int libmail_gpg_stdin, libmail_gpg_stdout, libmail_gpg_stderr;
extern pid_t libmail_gpg_pid;

/*
** List keys
*/

int libmail_gpg_exportkey(const char *gpgdir,
		  int secret,
		  const char *fingerprint,
		  int (*out_func)(const char *, size_t, void *),
		  int (*err_func)(const char *, size_t, void *),
		  void *voidarg)
{
	char *argvec[6];
	int rc;

	argvec[0]="gpg";
	argvec[1]="--armor";
	argvec[2]="--no-tty";
	argvec[3]= secret ? "--export-secret-keys":"--export";
	argvec[4]=(char *)fingerprint;
	argvec[5]=0;

	if (libmail_gpg_fork(&libmail_gpg_stdin, &libmail_gpg_stdout,
			     &libmail_gpg_stderr, gpgdir, argvec) < 0)
		rc= -1;
	else
	{
		int rc2;

		close(libmail_gpg_stdin);
		libmail_gpg_stdin=-1;

		rc=libmail_gpg_read(out_func, err_func, NULL, 0, voidarg);
		rc2=libmail_gpg_cleanup();
		if (rc2)
			rc=rc2;
	}
	return (rc);
}
