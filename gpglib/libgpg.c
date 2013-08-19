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

#include	"gpg.h"
#include	"gpglib.h"

int libmail_gpg_stdin= -1, libmail_gpg_stdout= -1,
	libmail_gpg_stderr= -1;
pid_t libmail_gpg_pid= -1;

int libmail_gpg_cleanup()
{
	int rc=0;

	if (libmail_gpg_stdin >= 0)
	{
		close(libmail_gpg_stdin);
		libmail_gpg_stdin= -1;
	}

	if (libmail_gpg_stdout >= 0)
	{
		close(libmail_gpg_stdout);
		libmail_gpg_stdout= -1;
	}

	if (libmail_gpg_stderr >= 0)
	{
		close(libmail_gpg_stderr);
		libmail_gpg_stderr= -1;
	}

	if (libmail_gpg_pid >= 0 && kill(libmail_gpg_pid, 0) >= 0)
	{
		int waitstat;
		pid_t p;

		while ((p=wait(&waitstat)) != libmail_gpg_pid)
		{
			if (p < 0 && errno == ECHILD)
				return (-1);
		}

		rc= WIFEXITED(waitstat) ? WEXITSTATUS(waitstat): -1;
		libmail_gpg_pid= -1;
	}
	return (rc);
}

static char *optionfile(const char *gpgdir)
{
	char *p=malloc(strlen(gpgdir)+sizeof("/options"));

	if (p)
		strcat(strcpy(p, gpgdir), "/options");
	return (p);
}

/*
** Determine of GPG is available by checking for the options file, and the
** gpg binary itself.
*/

int libmail_gpg_has_gpg(const char *gpgdir)
{
	char *p=optionfile(gpgdir);
	struct stat stat_buf;
	int rc;

	if (!p)
		return (-1);

	rc=stat(p, &stat_buf);
	free(p);
	if (rc == 0)
		rc=stat(GPG, &stat_buf);

	return (rc);
}
