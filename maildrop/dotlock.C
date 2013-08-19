/*
** Copyright 1998 - 2006 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"dotlock.h"
#include	"funcs.h"
#include	"xconfig.h"
#include	"mio.h"
#include	"buffer.h"
#include	"alarmsleep.h"
#include	"alarmtimer.h"
#include	"varlist.h"
#include	"config.h"
#include	<sys/types.h>
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<stdlib.h>


DotLock::DotLock() : refresh(this)
{
}

DotLock::~DotLock()
{
	Unlock();
}

void	DotLock::Unlock()
{
	refresh.Cancel();
	Close();
}

int DotLock::attemptlock(const char *templock, const char *finallock)
{
Mio	mio;
Buffer	b;
static Buffer   errbuf;

	if (mio.Open(templock, O_CREAT | O_WRONLY, 0644) < 0)
	{
                errbuf="Unable to create a dot-lock at ";
                errbuf += (const char *)templock;
                errbuf += ".\n";
                errbuf += '\0';
                throw (const char *)errbuf;
	}


	b.append( (unsigned long)getpid() );
	b += '\n';
	if (mio.write((const char *)b, b.Length()) < 0 || mio.flush() < 0)
	{
		mio.Close();
		unlink(templock);
		throw "Unable to write to a dot-lock.";
	}
	mio.Close();
	if (mio.errflag())
	{
		unlink(templock);
		throw "Unable to close a dot-lock.";
	}
	try
	{
	struct	stat	buf;

		if (link(templock, finallock) < 0)
			; /* ignored */

	int	rc=stat(templock, &buf);
		if (rc == 0 && buf.st_nlink == 2)
		{
			name(finallock);
			unlink(templock);
			return (0);
		}
		unlink(templock);
	}
	catch (...)
	{
		unlink(templock);
		throw;
	}
	return (-1);
}

void	DotLock::Lock(const char *lockfile)
{
	Unlock();

unsigned nseconds=GetLockSleep();
unsigned timeout=GetLockTimeout();
const char *q, *r;

	refresh_interval=GetLockRefresh();

	for (q=r=lockfile; *q; q++)
		if (*q == '/')	r=q+1;

	q= TempName( r-lockfile ? lockfile:"", r-lockfile);

struct	stat	last_stat;
int	has_last_stat=0;
AlarmTimer	stat_timer;

	memset(&last_stat, 0, sizeof(last_stat));
	while (attemptlock(q, lockfile))
	{
	struct	stat	current_stat;

		if (stat(lockfile, &current_stat) == 0)
		{
			if (!has_last_stat ||
				current_stat.st_ino != last_stat.st_ino ||
				current_stat.st_ctime != last_stat.st_ctime ||
				current_stat.st_atime != last_stat.st_atime)
			{
				has_last_stat=1;
				last_stat=current_stat;
				stat_timer.Set(timeout);
			}
			else if (stat_timer.Expired())
			{
				unlink(lockfile);
			}
		}

	AlarmSleep sleep(nseconds);
	}
	if (refresh_interval >= 10)
		refresh.Set(refresh_interval);
}

void	DotLock::LockMailbox(const char *mailbox)
{
struct stat stat_buf;
Buffer	dotlock_name;

	if (stat(mailbox, &stat_buf) < 0 ||
		( !S_ISCHR(stat_buf.st_mode) && !S_ISBLK(stat_buf.st_mode)))
	{
		dotlock_name=mailbox;

	const	char *p=GetLockExt();

		if (!p || !*p)	dotlock_name += LOCKEXT_DEF;
		else	dotlock_name += p;

		dotlock_name += '\0';
		Lock( dotlock_name );
	}
}

void	DotLock::dorefresh()
{
	chmod(filename, 0644);
	refresh.Set(refresh_interval);
}
