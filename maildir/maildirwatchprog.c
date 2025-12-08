/*
** Copyright 2021 S. Varshavchik.
** See COPYING for distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<sys/types.h>
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <poll.h>
#include	"maildirwatch.h"

static void usage()
{
	printf("Usage: maildirwatch maildir program arguments...\n");
	exit(1);
}

static int forkexec(int argc, char **argv)
{
	pid_t p=fork();
	int s;

	if (p < 0)
	{
		perror("fork");
		return  0;
	}

	if (p == 0)
	{
		char **argvptr=malloc(sizeof(char *)*(argc+1));
		int n;

		if (!argvptr)
		{
			perror("malloc");
			exit(1);
		}

		for (n=0; n<argc; ++n)
			argvptr[n]=argv[n];
		argvptr[n]=0;
		execvp(argvptr[0], argvptr);
		perror(argv[0]);
		exit(1);
	}

	if (waitpid(p, &s, 0) != p)
		return 0;

	if (!WIFEXITED(s))
	{
		if (WIFSIGNALED(s))
			printf("-%d\n", (int)WTERMSIG(s));
		else
			printf("-0\n");
		return 0;
	}

	if (WEXITSTATUS(s))
	{
		printf("%d\n", (int)WEXITSTATUS(s));
		return 0;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	struct maildirwatch *watch;
	struct maildirwatch_contents mc;
	int fdret;
	int rc;

	if (argc < 3)
		usage();

	/*
	** Call maildirwatch_alloc, then maildirwatch_start.
	*/

	watch=maildirwatch_alloc(argv[1]);

	if (maildirwatch_start(watch, &mc) < 0)
	{
		perror(argv[1]);
		exit(1);
	}

	/*
	** poll() the returned file descriptor as long as
	** maildirwatch_started returns 0.
	*/

	while ((rc=maildirwatch_started(&mc, &fdret)) == 0)
	{
		struct pollfd pfd;

		pfd.fd=fdret;
		pfd.events=POLLIN;

		if (poll(&pfd, 1, -1) < 0)
		{
			perror(argv[1]);
			exit(1);
		}
	}

	if (rc < 0)
	{
		perror(argv[1]);
		exit(1);
	}

	/*
	** The maildir is now being monitored.
	*/

	while (forkexec(argc-2, argv+2))
	{
		int was_changed=0;

		while (1)
		{
			int changed;
			int timeout;

			/*
			** maildirwatch_check() checks if the maildir has
			** been changed.
			*/
			int rc=maildirwatch_check(&mc, &changed, &fdret,
						  &timeout);


			if (rc < 0)
			{
				perror(argv[1]);
				exit(1);
			}

			/*
			** If it's changed, you can call it again.
			** maildirwatch_check() does not block.
			*/
			if (changed)
			{
				was_changed=1;
				continue;
			}

			if (was_changed)
				break;

			/*
			** If it's not changed, poll() for the number of
			** seconds specified by the timeout.
			**
			** In polling mode fdret is 0, so sleep, then
			** check the maildir manually.
			*/

			if (fdret < 0)
			{
				sleep(timeout);
				continue;
			}

			struct pollfd pfd;

			pfd.fd=fdret;
			pfd.events=POLLIN;

			if (poll(&pfd, 1, timeout * 1000) < 0)
			{
				perror("poll");
				exit(1);
			}
		}
	}

	/*
	** Cleanup.
	*/

	maildirwatch_end(&mc);
	maildirwatch_free(watch);
	return (0);
}
