#include	"funcs.h"
#include	"buffer.h"
#include	"xconfig.h"
#include	"varlist.h"
#include	"maildrop.h"
#include	"config.h"
#include	<string.h>
#include	<signal.h>
#include	<grp.h>
#if	HAVE_MEMORY_H
#include	<memory.h>
#endif
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#ifdef _AIX
#undef	GETPGRP_VOID
#define	GETPGRP_VOID
#endif

#if	HAS_GETHOSTNAME
#else
extern "C" int gethostname(const char *, size_t);
#endif


void	memorycopy(void *dst, void *src, int cnt)
{
	if (cnt <= 0)	return;
	memmove(dst, src, cnt);
}

void	outofmem()
{
	throw "Out of memory.";
}

void	seekerr()
{
	throw "Seek error.";
}

const char *TempName(const char *dir, unsigned l)
{
static Buffer buf;
static unsigned counter=0;
char	hostname[256];

	hostname[0]=0;
	gethostname(hostname, 256);
	hostname[sizeof(hostname)-1]=0;

	buf=dir;
	if (l > 0)	buf.Length(l);
	buf.append( (unsigned long)getpid() );
	buf += '.';
	buf.append( (unsigned long)counter++ );
	buf += '.';
	buf += hostname;
	buf += '\0';

	return (buf);
}

int backslash_char(int c)
{
	switch (c)	{
	case 'r':
		return '\r';
	case 'n':
		return '\n';
	case 't':
		return '\t';
	case 'f':
		return '\f';
	case 'v':
		return '\v';
	}
	return (c);
}

///////////////////////////////////////////////////////////////////////////
//
// V0.55a - implement process group kill.  Upon startup, set our process
// group.  Upon exit, kill all processes in our process group.
//
// Functions: setprocgroup() - sets our process group.
//            killprocgroup() - kills our process group.
//
///////////////////////////////////////////////////////////////////////////

static int procgroup_set=0;

void setprocgroup()
{
#if	HAS_SETPGRP

	(void)setpgrp();
	procgroup_set=1;
#else
#if	HAS_SETPGID
	(void)setpgid(0,0);
	procgroup_set=1;
#endif
#endif
}

static GETGROUPS_T getprocgroup()
{
#if	HAS_GETPGRP

#ifdef	GETPGRP_VOID

	return ( getpgrp() );
#else
	return ( getpgrp( getpid()) );
#endif
#else
#if	HAS_GETPGID
	return ( getpgid( 0 ) );
#else
	return ( getpid());		// Shouldn't happen
#endif
#endif
}

void killprocgroup()
{
	if (!procgroup_set)	return;

	signal(SIGHUP, SIG_IGN);	// Don't kill this process

#if	HAS_SETPGRP

	(void)kill( -getprocgroup(), SIGHUP );

#else
#if	HAS_SETPGID
	(void)kill( -getprocgroup(), SIGHUP );
#endif
#endif
}

