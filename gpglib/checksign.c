/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
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
** Check signature.
*/

static int dochecksign(int (*)(const char *, size_t, void *),
		    void *);

int libmail_gpg_checksign(const char *gpgdir, const char *content,
		  const char *signature,
		  int (*dump_func)(const char *, size_t, void *),
		  void *voidarg)
{
	char *argvec[10];
	int rc;

	argvec[0]="gpg";
	argvec[1]="--no-tty";
	argvec[2]="--verify";
	argvec[3]=(char *)signature;
	argvec[4]=(char *)content;
	argvec[5]=0;

	if (libmail_gpg_fork(&libmail_gpg_stdin, &libmail_gpg_stdout,
			     &libmail_gpg_stderr, gpgdir, argvec) < 0)
		rc= -1;
	else
	{
		int rc2;

		rc=dochecksign(dump_func, voidarg);
		rc2=libmail_gpg_cleanup();
		if (rc2)
			rc=rc2;
	}
	return (rc);
}

static int dochecksign(int (*dump_func)(const char *, size_t, void *),
		    void *voidarg)
{
	int rc=libmail_gpg_write("Y\n", 2, dump_func, NULL, NULL, 0, voidarg);
	int rc2;

	if (rc == 0)
		rc=libmail_gpg_read(dump_func, dump_func, NULL, 0, voidarg);
	rc2=libmail_gpg_cleanup();
	if (rc == 0)
		rc=rc2;
	return (rc);
}
