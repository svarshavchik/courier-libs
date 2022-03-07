/*
** Copyright 2002-2009 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<signal.h>
#include	<ctype.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	"outbox.h"
#include	"imapwrite.h"

#include	<vector>
#include	<sstream>
#include	<sys/types.h>
#include	<sys/stat.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

/*
** Is this the magic outbox?
*/

int is_outbox(const char *mailbox)
{
	const char *p=getenv("OUTBOX");

	if (strncmp(mailbox, "./", 2) == 0)
		mailbox += 2;

	if (p == NULL || *p == 0 || strcmp(p, mailbox))
		return (0);

	return (1);
}

std::string defaultSendFrom()
{
	const char *from;

	from=getenv("AUTHADDR");
	if (!from || !*from)
		from=getenv("AUTHENTICATED");
	if (from && !*from)
		from=NULL;

	return from ? from:"";
}

/*
** After a message is copied to a folder, mail it, if it's an OUTBOX.
*/

int check_outbox(const char *message, const char *mailbox)
{
	char *argv[10];

	if (!is_outbox(mailbox))
		return (0);

	auto from=defaultSendFrom();

	char opt1[]="-oi";
	argv[1]=opt1;

	char opt2[]="-t";
	argv[2]=opt2;
	argv[3]=NULL;

	char opt3[]="-f";

	const char *p=from.c_str();
	std::vector<char> frombuf{p, p+from.size()+1};

	if (!from.empty())
	{
		argv[3]=opt3;
		argv[4]=frombuf.data();
		argv[5]=NULL;
	}

	return imapd_sendmsg(
		message, argv,
		[]
		(const std::string &errmsg)
		{
			fprintf(stderr,
				"ERR: error sending a message, user=%s:"
				" %s\n",
				getenv("AUTHENTICATED"), errmsg.c_str()
			);
		}
	);
}

int imapd_sendmsg(const char *message, char **argv,
		  const std::function<void (const std::string &)> &err_func)
{
	std::string tempbuf;
	int ch;
	auto from=defaultSendFrom();
	const char *hdrfrom;

	const char *prog;

	int pipefd[2];
	FILE *pipefp, *pipesendmail;
	pid_t pid, pid2;
	int waitstat;

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	if (pipe(pipefd) < 0)
		write_error_exit("pipe");

	prog=getenv("SENDMAIL");
	if (!prog  || !*prog)
		prog="sendmail";


	pid=fork();

	if (pid < 0)
		write_error_exit("fork");

	if (pid > 0)	/* Parent reads err message, checks exit status */
	{
		close(pipefd[1]);
		pipefp=fdopen(pipefd[0], "r");
		if (pipefp == NULL)
			write_error_exit("fdopen");
		while ((ch=getc(pipefp)) != EOF)
		{
			if ((unsigned char)ch < ' ')
				ch='/';

			if (tempbuf.size() < 512)
				tempbuf.push_back(ch);
		}
		fclose(pipefp);
		close(pipefd[0]);

		while ((pid2=wait(&waitstat)) != pid)
			if (pid2 < 0 && errno == ECHILD)
				break;

		if (pid2 < 0 || !WIFEXITED(waitstat) || WEXITSTATUS(waitstat))
		{
			if (tempbuf.empty())
			{
				std::ostringstream o;

				o << prog;

#ifdef WIFSIGNALED
#ifdef WTERMSIG
				if (WIFSIGNALED(waitstat))
					o << " terminated with signal "
					  << WTERMSIG(waitstat);
				else
#endif
#endif
					o << " failed without logging an error."
						" This shouldn't happen.";

				tempbuf=o.str();
			}

			err_func(tempbuf);
			return (-1);
		}
		return (0);
	}

	close(pipefd[0]);
	close(1);
	close(2);
	if (dup(pipefd[1]) != 1 || dup(pipefd[1]) != 2)
	{
		perror("dup(pipefd) failed");
		exit(1);
	}
	close(pipefd[1]);

	pipefp=fopen(message, "r");
	if (pipefp == NULL)
	{
		perror(message);
		exit(1);
	}

	/* Child forks again, second child process runs sendmail */

	if (pipe(pipefd) < 0)
	{
		perror("pipe");
		exit(1);
	}

	pid=fork();

	if (pid < 0)
	{
		perror("fork");
		exit(1);
	}

	if (pid == 0)
	{
		fclose(pipefp);
		close(pipefd[1]);
		close(0);
		errno=EINVAL;
		if (dup(pipefd[0]) != 0)
		{
			perror("dup");
			exit(1);
		}

		argv[0]=(char *)prog;

		execvp(prog, argv);
		perror(prog);
		exit(1);
	}

	close(pipefd[0]);

	pipesendmail=fdopen(pipefd[1], "w");
	if (!pipesendmail)
	{
		perror("fdopen");
		kill(pid, SIGTERM);
		close(pipefd[1]);
		exit(1);
	}

	if ((hdrfrom=getenv("HEADERFROM")) != NULL && *hdrfrom && !from.empty())
		fprintf(pipesendmail, "%s: %s\n", hdrfrom, from.c_str());
	while ((ch=getc(pipefp)) != EOF)
		putc(ch, pipesendmail);
	fclose(pipefp);
	fclose(pipesendmail);

	while ((pid2=wait(&waitstat)) != pid)
		if (pid2 < 0 && errno == ECHILD)
			break;

	if (pid2 < 0 || !WIFEXITED(waitstat) || WEXITSTATUS(waitstat))
	{
		fprintf(stderr, "Message send FAILED.\n");
		exit(1);
	}
	exit(0);
	return (0);
}
