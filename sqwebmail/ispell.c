/*
** Copyright 1998 - 2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"ispell.h"
#include	<stdio.h>
#include	<stdlib.h>
#if	HAVE_CONFIG_H
#include	<config.h>
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<sys/types.h>
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif

#include	<signal.h>
#include	<string.h>

static pid_t ispell_pid;
static RETSIGTYPE (*prevsighandler)(int);

#ifndef	ISPELL
#define	ISPELL	"/usr/bin/ispell"
#endif

static void wait4pid()
{
int	waitstat;

	if (prevsighandler == SIG_DFL)
		while ( wait(&waitstat) != ispell_pid)
			;
}

static int fork_ispell(const char *ispellline, const char *dict,
				struct ispell *isp)
{
int	fdbuf[2], fdbuf2[2];
size_t	l;
int	waitstat;
int	c;
FILE	*ispell_resp;
size_t	ispell_resp_buf_size=0;
const char *args[6];
int	argn;

	isp->ispell_buf=0;

	prevsighandler=signal(SIGCHLD, SIG_DFL);
	signal(SIGCHLD, prevsighandler);

	if (pipe(fdbuf) < 0)
	{
		perror("pipe");
		return (-1);
	}

	switch (ispell_pid=fork())	{
	case -1:
		perror("fork");
		close(fdbuf[0]);
		close(fdbuf[1]);
		return (-1);
	case 0:
		if (pipe(fdbuf2) < 0) {
			perror("pipe");
			_exit(-1);
		}

		/*
		** First child will fork and run the real ispell, and write to
		** it on stdin.
		*/

		switch (ispell_pid=fork())	{
		case -1:
			perror("fork");
			_exit(-1);
		case 0:
			dup2(fdbuf2[0], 0);
			dup2(fdbuf[1], 1);
			close(fdbuf[0]);
			close(fdbuf[1]);
			close(fdbuf2[0]);
			close(fdbuf2[1]);
			argn=0;
			args[argn++]="ispell";
			args[argn++]="-a";
			if (dict)
			{
				args[argn++]="-d";
				args[argn++]=dict;
			}
			args[argn]=0;
			execv(ISPELL, (char **)args);
			perror("fork_ispell: execv " ISPELL);
			_exit(1);
		}
		close(fdbuf[0]);
		close(fdbuf[1]);
		close(fdbuf2[0]);
		signal(SIGCHLD, SIG_DFL);
		l=strlen(ispellline);
		while (l)
		{
		int	n=write(fdbuf2[1], ispellline, l);

			if (n <= 0)
			{
				perror("fork_ispell: write");
				_exit(1);
			}
			ispellline += n;
			l -= n;
		}
		if (write(fdbuf2[1], "\n", 1) != 1)
		{
			perror("fork_ispell: write");
			_exit(1);
		}
		close(fdbuf2[1]);
		while ( wait(&waitstat) != ispell_pid)
			;
		_exit(0);
	}

	close(fdbuf[1]);
	ispell_resp=fdopen(fdbuf[0], "r");
	if (!ispell_resp)
	{
		perror("fork_ispell: fdopen");
		close(fdbuf[0]);
		wait4pid();
		return (-1);
	}

	l=0;

	for (;;)
	{
		if (l >= ispell_resp_buf_size)
		{
		char	*newbuf;
		size_t	l=ispell_resp_buf_size + BUFSIZ;
			newbuf=isp->ispell_buf ? realloc(isp->ispell_buf, l)
					:malloc(l);
			if (!newbuf)
			{
				perror("fork_ispell: malloc/realloc");
				if (isp->ispell_buf)	free(isp->ispell_buf);
				isp->ispell_buf=0;
				wait4pid();
				fclose(ispell_resp);
				return (-1);
			}
			isp->ispell_buf=newbuf;
			ispell_resp_buf_size=l;
		}

		if ( (c=getc(ispell_resp)) == EOF )
			break;

		isp->ispell_buf[l++]=c;
	}

	isp->ispell_buf[l]=0;
	fclose(ispell_resp);
	wait4pid();
	return (0);
}

struct ispell *ispell_run(const char *dict, const char *line)
{
char	*p, *q;
struct	ispell *isp;
struct	ispell_misspelled **islistptr, *ip;
int	c;
int	nmisses;
struct	ispell_suggestion **sp;

	if ((isp=(struct ispell *)malloc(sizeof(struct ispell))) == 0)
	{
		perror("malloc");
		return (0);
	}
	isp->ispell_buf=0;
	isp->first_misspelled=0;
	islistptr=&isp->first_misspelled;

	if (fork_ispell(line, dict, isp))
	{
		ispell_free(isp);
		fprintf(stderr, "ERR: ispell_run: fork_ispell failed\n");
		return (0);
	}

	if ((p=strchr(isp->ispell_buf, '\n')) == 0)
	{
		fprintf(stderr, "ERR: ispell_run: incomplete result from ispell\n");
		ispell_free(isp);
		return (0);
	}
	++p;
	q=0;
	islistptr= &isp->first_misspelled;
	for ( ; p && *p != '\n'; p=q)
	{
		if ((q=strchr(p, '\n')) != 0)
			*q++=0;

		if ( *p != '&' && *p != '?' && *p != '#')	continue;

		if ( (ip=*islistptr=(struct ispell_misspelled *)
			malloc(sizeof(struct ispell_misspelled))) == 0)
		{
			perror("malloc");
			ispell_free(isp);
			return (0);
		}

		ip->next=0;
		islistptr= &ip->next;

		c=*p++;
		while (*p == ' ')	++p;
		ip->misspelled_word=p;
		while (*p && *p != ' ')	++p;
		if (*p)	*p++=0;

		nmisses=0;
		while (*p == ' ')	++p;
		if (c == '&' || c == '?')
		{
			while (*p >= '0' && *p <= '9')
				nmisses=nmisses * 10 + *p++ - '0';
			while (*p == ' ')	++p;
		}

		ip->word_pos=0;
		while (*p >= '0' && *p <= '9')
			ip->word_pos=ip->word_pos * 10 + *p++ - '0';
		ip->first_suggestion=0;
		if (nmisses == 0 || *p != ':')	continue;
		++p;
		sp= &ip->first_suggestion;
		while (nmisses)
		{
			if ( (*sp=(struct ispell_suggestion *)
				malloc(sizeof(struct ispell_suggestion)))
				== 0)
			{
				perror("malloc");
				ispell_free(isp);
				return (0);
			}
			(*sp)->suggested_word=strtok(p, ", ");
			p=0;
			(*sp)->next=0;
			sp= &(*sp)->next;
			--nmisses;
		}
	}
	return (isp);
}

void ispell_free(struct ispell *isp)
{
struct ispell_misspelled *msp;

	while ((msp=isp->first_misspelled) != 0)
	{
	struct ispell_suggestion *sgp;

		isp->first_misspelled=msp->next;

		while ((sgp=msp->first_suggestion) != 0)
		{
			msp->first_suggestion=sgp->next;
			free(sgp);
		}

		free(msp);
	}

	if (isp->ispell_buf)	free(isp->ispell_buf);
	free(isp);
}
