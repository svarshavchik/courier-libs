/*
** Copyright 1998 - 2023 S. Varshavchik.
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
std::string	b;
static std::string   errbuf;

	if (mio.Open(templock, O_CREAT | O_WRONLY, 0644) < 0)
	{
                errbuf="Unable to create a dot-lock at ";
                errbuf += templock;
                errbuf += ".\n";
                throw errbuf.c_str();
	}


	add_integer(b, getpid() );
	b += "\n";
	if (mio.write(b.c_str(), b.size()) < 0 || mio.flush() < 0)
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

		AlarmSleep sleep{nseconds};
	}
	if (refresh_interval >= 10)
		refresh.Set(refresh_interval);
}

void	DotLock::LockMailbox(const char *mailbox)
{
struct stat stat_buf;
std::string	dotlock_name;

	if (stat(mailbox, &stat_buf) < 0 ||
		( !S_ISCHR(stat_buf.st_mode) && !S_ISBLK(stat_buf.st_mode)))
	{
		dotlock_name=mailbox;

		std::string p=GetLockExt();

		if (p.empty())	dotlock_name += LOCKEXT_DEF;
		else	dotlock_name += p;

		Lock( dotlock_name.c_str() );
	}
}

void	DotLock::dorefresh()
{
	chmod(filename, 0644);
	refresh.Set(refresh_interval);
}
