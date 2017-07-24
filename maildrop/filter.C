/*
** Copyright 1998 - 2006 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"funcs.h"
#include	"message.h"
#include	"messageinfo.h"
#include	"mio.h"
#include	"pipefds.h"
#include	"formatmbox.h"
#include	"xconfig.h"
#include	"varlist.h"
#include	"maildrop.h"
#include	"config.h"
#include	<sys/types.h>
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	"mywait.h"
#include	"mytime.h"
#include	<signal.h>
#include	<errno.h>
#if	HAVE_STRINGS_H
#include	<strings.h>
#endif


///////////////////////////////////////////////////////////////////////////
//
//  Filter message through an external command.
//
///////////////////////////////////////////////////////////////////////////

int xfilter(const char *, int);

int filter(const char *filtercmd)
{
	try
	{
	int	rc=xfilter(filtercmd, 0);

		if (rc == 0)
		{
		Message	*ptr=maildrop.savemsgptr;

			maildrop.savemsgptr=maildrop.msgptr;
			maildrop.msgptr=ptr;
			maildrop.msgptr->setmsgsize();
			maildrop.msginfo.filtered();
		}
		maildrop.savemsgptr->Init();
		return(rc);
	}
	catch (...)
	{
		maildrop.savemsgptr->Init();
		throw;
	}
}

int xfilter(const char *filtercmd, int ignorewerr)
{
FormatMbox	format_mbox;
char	buffer[1024];

	maildrop.savemsgptr->Init();

//	if (format_mbox.HasMsg())	return (0);	// Empty
	(void)format_mbox.HasMsg();

Buffer	cmdbuf;

	cmdbuf= filtercmd;
	cmdbuf += '\0';

PipeFds	pipe0, pipe1;

	if (pipe0.Pipe() < 0 || pipe1.Pipe() < 0)
		throw "Cannot create pipe.";

pid_t	pid=fork();

	if (pid < 0)
		throw "Cannot fork.";

	if (pid == 0)
	{
		try
		{
			pipe0.close1();
			pipe1.close0();
			dup2(pipe0.fds[0], 0);
			pipe0.close0();
			dup2(pipe1.fds[1], 1);
			pipe1.close1();
			subshell(filtercmd);
		}
		catch (const char *p)
		{
			if (write(2, p, strlen(p)) < 0 ||
			    write(2, "\n", 1) < 0)
				; /* ignore */
			_exit(100);
		}
#if NEED_NONCONST_EXCEPTIONS
		catch (char *p)
		{
			if (write(2, p, strlen(p)) < 0 ||
			    write(2, "\n", 1) < 0)
				; /* ignore */
			_exit(100);
		}
#endif
		catch (...)
		{
			_exit(101);
		}
	}

	pipe0.close0();
	pipe1.close1();

	format_mbox.Init(0);

//////////////////////////////////////////////////////////////////////////
//
// Write message contents to the subprocess.  Simultaneously read process's
// output, and save it.  This is done simultaneously, via select() call.
//
//////////////////////////////////////////////////////////////////////////

const	char	*writebufptr=0;
int		writebuflen=0;

fd_set	readfd, writefd;

	FD_ZERO(&readfd);
	FD_ZERO(&writefd);

int	errflag=0;
int	maxfd=pipe1.fds[0];

	if (pipe0.fds[1] > maxfd)
		maxfd=pipe0.fds[1];
	++maxfd;

	fcntl(pipe1.fds[0], F_SETFL, O_NDELAY);
	fcntl(pipe0.fds[1], F_SETFL, O_NDELAY);

	for (;;)
	{
		FD_SET(pipe1.fds[0], &readfd);
		if (pipe0.fds[1] >= 0)
			FD_SET(pipe0.fds[1], &writefd);

		if (!writebuflen && pipe0.fds[1] >= 0)
		{		// Need more to write.
		Buffer	*p=format_mbox.NextLine();

			if (!p)
			{
				FD_CLR(pipe0.fds[1], &writefd);
				pipe0.close1();	// End of msg.
			}
			else
			{
				writebufptr= *p;
				writebuflen= p->Length();
			}
		}

	int	n=select(maxfd, &readfd, &writefd, NULL, NULL);

		if (n < 0)
		{
			if (errno != EINTR)
				throw "maildrop: select() error.";
			continue;
		}

		if (pipe0.fds[1] >= 0 && FD_ISSET(pipe0.fds[1], &writefd))
		{
		int	n= ::write(pipe0.fds[1], writebufptr, writebuflen);

			if (n < 0)
			{
				if (errno != EINTR
#ifdef EAGAIN
				    && errno != EAGAIN
#endif
#ifdef EWOULDBLOCK
				    && errno != EWOULDBLOCK
#endif

				    )	// Perfectly OK
				{
					FD_CLR(pipe0.fds[1], &writefd);
					pipe0.close1();
					writebuflen=0;
					if (!ignorewerr)
					{
						merr << "maildrop: error writing to filter.\n";
						errflag=1;
						break;
					}
				}
			}
			else
			{
				writebufptr += n;
				writebuflen -= n;
			}
		}

		if (FD_ISSET(pipe1.fds[0], &readfd))
		{
		int	readbuflen=::read(pipe1.fds[0], buffer, sizeof(buffer));

			if (readbuflen < 0)
			{
				if (errno != EINTR
#ifdef EAGAIN
				    && errno != EAGAIN
#endif
#ifdef EWOULDBLOCK
				    && errno != EWOULDBLOCK
#endif
				    )
				{
					merr << "maildrop: error reading from filter.\n";
					errflag=1;
					break;
				}
				continue;
			}
			if (readbuflen == 0)	// FILTERED
			{
				if ( (pipe0.fds[1] >= 0) && (!ignorewerr) )
				{
					merr << "maildrop: filter terminated prematurely.\n";
					errflag=1;	// Not everything
					break;		// was written, though.
				}
				break;
			}

			maildrop.savemsgptr->Init(buffer, readbuflen);
		}
	}

	pipe0.close1();
	pipe1.close0();

int	wait_stat;

	while (wait(&wait_stat) != pid)
		;
	wait_stat= WIFEXITED(wait_stat) ? WEXITSTATUS(wait_stat):-1;

	if (!wait_stat && errflag)
		wait_stat= -1;

	{
	Buffer	name, val;

		val.append( (unsigned long)wait_stat);
		name="RETURNCODE";
		SetVar(name, val);
	}

	if (wait_stat)
		return (-1);
	return (0);
}

void executesystem(const char *cmd)
{
	int devnull=open("/dev/null", O_RDONLY);
	pid_t	pid;

	if (devnull < 0)
		throw "Cannot open /dev/null";

	pid=fork();
	if (pid < 0)
	{
		close(devnull);
		throw "Cannot fork.";
	}

	if (pid == 0)
	{
		try
		{
			dup2(devnull, 0);
			close(devnull);
			subshell(cmd);
		}
		catch (const char *p)
		{
			if (write(2, p, strlen(p)) < 0 ||
			    write(2, "\n", 1) < 0)
				; /* ignore */
			_exit(100);
		}
#if NEED_NONCONST_EXCEPTIONS
		catch (char *p)
		{
			if (write(2, p, strlen(p)) < 0 ||
			    write(2, "\n", 1) < 0)
				; /* ignore */
			_exit(100);
		}
#endif
		catch (...)
		{
			_exit(101);
		}
	}
	close(devnull);

int	wait_stat;

	while (wait(&wait_stat) != pid)
		;
	wait_stat= WIFEXITED(wait_stat) ? WEXITSTATUS(wait_stat):-1;

	{
	Buffer	name, val;

		val.append( (unsigned long)wait_stat);
		name="RETURNCODE";
		SetVar(name, val);
	}
}
