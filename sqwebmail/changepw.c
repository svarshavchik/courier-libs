#include "config.h"
/*
** Copyright 2000-2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#include	"auth.h"
#include	<courierauth.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<unistd.h>
#include	<errno.h>
#include	<sys/time.h>
#include	<sys/select.h>
#include	"htmllibdir.h"
#include	"numlib/numlib.h"
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif



static int badstr(const char *p)
{
	while (p && *p)
	{
		if ((int)(unsigned char)*p < ' ')
			return 1;
		++p;
	}
	return 0;
}

int changepw(const char *service,
	     const char *uid,
	     const char *opwd,
	     const char *npwd)
{
	pid_t p, p2;
	int waitstat;
	int pipefd[2];
	FILE *fp;

	if (badstr(uid) || badstr(opwd) || badstr(npwd))
	{
		errno=EINVAL;
		return -1;
	}

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);

        if (pipe(pipefd) < 0)
        {
                perror("CRIT: authdaemon: pipe() failed");
                errno=EINVAL;
                return (-1);
        }

        p=fork();

        if (p == 0)
        {
                char *argv[2];

		argv[0]=SQWEBPASSWD;
		argv[1]=0;
		dup2(pipefd[0], 0);
		close(pipefd[0]);
		close(pipefd[1]);
		execv(argv[0], argv);
                perror("CRIT: failed to execute " SQWEBPASSWD);
		exit(1);
	}

	close(pipefd[0]);

	if ((fp=fdopen(pipefd[1], "w")) != 0)
	{
		fprintf(fp, "%s\t%s\t%s\t%s\n",
			service, uid, opwd, npwd);
		fclose(fp);
	}
	close(pipefd[1]);

        while ((p2=wait(&waitstat)) != p)
        {
                if (p2 < 0 && errno == ECHILD)
                {
                        perror("CRIT: authdaemon: wait() failed");
                        errno=EPERM;
                        return (-1);
                }
        }

        if (WIFEXITED(waitstat) && WEXITSTATUS(waitstat) == 0)
                return (0);

	errno=EPERM;
	return (-1);
}
