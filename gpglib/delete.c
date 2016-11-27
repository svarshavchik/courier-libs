/*
** Copyright 2001-2016 Double Precision, Inc.  See COPYING for
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
** Delete a key.
*/

static int dodeletekey(int (*)(const char *, size_t, void *),
		       void *);

int libmail_gpg_deletekey(const char *gpgdir, int secret,
			  const char *fingerprint,
			  int (*dump_func)(const char *, size_t, void *),
			  void *voidarg)
{
	char *argvec[9];
	int rc;

	argvec[0]="gpg";
	argvec[1]="--command-fd";
	argvec[2]="0";
	argvec[3]= secret ? "--delete-secret-key":"--delete-key";
	argvec[4]="-q";
	argvec[5]="--no-tty";
	argvec[6]="--yes";
	argvec[7]=(char *)fingerprint;
	argvec[8]=0;

	if (libmail_gpg_fork(&libmail_gpg_stdin, &libmail_gpg_stdout, NULL,
			     gpgdir, argvec) < 0)
		rc= -1;
	else
	{
		int rc2;

		rc=dodeletekey(dump_func, voidarg);
		rc2=libmail_gpg_cleanup();
		if (rc2)
			rc=rc2;
	}
	return (rc);
}

static int dodeletekey(int (*dump_func)(const char *, size_t, void *),
		    void *voidarg)
{
	int rc=libmail_gpg_write("Y\n", 2, dump_func, NULL, NULL, 0, voidarg);
	int rc2;

	if (rc == 0)
		rc=libmail_gpg_read(dump_func, NULL, NULL, 0, voidarg);
	rc2=libmail_gpg_cleanup();
	if (rc == 0)
		rc=rc2;
	return (rc);
}
