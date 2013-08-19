/*
**
** Copyright 2003-2006, Double Precision Inc.
**
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"ldapaddressbook.h"

#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<errno.h>
#include	<sys/types.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#define exit(_a_) _exit(_a_)

int ldapabook_search(const struct ldapabook *b,	/* Search this address book */
		     const char *script,
		     const char *search,
		     int (*callback_func)(const char *utf8_name,
					  const char *address,
					  void *callback_arg),
		     void (*callback_err)(const char *errmsg,
					  void *callback_arg),
		     void *callback_arg)
{
	int	pipefd[2];
	pid_t	p;
	const char *argv[40];
	char	buf1[BUFSIZ];
	char	buf2[BUFSIZ];
	FILE	*t, *fp;
	int	rc_code=0;
	pid_t p2;
	int waitstat;

	signal(SIGCHLD, SIG_DFL);

	if (pipe(pipefd) < 0)	return (-1);

	if ((t=tmpfile()) == NULL)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		return (-1);
	}

	if ((p=fork()) == -1)
	{
		fclose(t);
		close(pipefd[0]);
		close(pipefd[1]);
		return (-1);
	}

	if (p == 0)
	{
		dup2(pipefd[1], 1);
		close(pipefd[0]);
		close(pipefd[1]);

		dup2(fileno(t), 2);
		fclose(t);

		argv[0]=script;
		argv[1]=b->host;
		argv[2]=b->port;
		argv[3]=b->suffix;
		argv[4]=search;
		argv[5]=NULL;

		execvp(script, (char **)argv);
		perror(script);
		exit(1);
	}

	fp=fdopen(pipefd[0], "r");
	close(pipefd[1]);

	if (!fp)
	{
		sprintf(buf1, "%1.256s", strerror(errno));

		close(pipefd[0]);

		while ((p2=wait(NULL)) != p)
			;
		fclose(t);

		(*callback_err)(buf1, callback_arg);
		return -1;
	}

	while (fgets(buf1, sizeof(buf1), fp) != NULL &&
	       fgets(buf2, sizeof(buf2), fp) != NULL)
	{
		char *p=strchr(buf1, '\n');

		if (p) *p=0;

		p=strchr(buf2, '\n');
		if (p) *p=0;

		if (rc_code == 0)
			rc_code=(*callback_func)(buf1, buf2, callback_arg);
	}

	fclose(fp);
	close(pipefd[0]);

	while ((p2=wait(&waitstat)) != p)
		;

	if (waitstat && rc_code == 0)
	{
		rc_code= -1;
		fseek(t, 0L, SEEK_SET);

		if (fgets(buf1, sizeof(buf1), t))
		{
			char *p=strchr(buf1, '\n');
			if (p) *p=0;

			(*callback_err)(buf1, callback_arg);
		}
	}
	fclose(t);
	return rc_code;
}
