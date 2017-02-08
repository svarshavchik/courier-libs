#include "config.h"
/*
** Copyright 1998 - 2006 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"maildrop.h"
#include	"exittrap.h"
#include	<signal.h>
#include	<sysexits.h>
#include	<errno.h>
#if SYSLOG_LOGGING
#include	<syslog.h>
#endif

extern void killprocgroup();


int Maildrop::sigfpe;

static RETSIGTYPE sig_fpe(int)
{
	maildrop.sigfpe=1;
	signal (SIGFPE, sig_fpe);
#if RETSIGTYPE != void
	return (0);
#endif
}

void Maildrop::cleanup()
{
	ExitTrap::onexit();
	killprocgroup();
}

RETSIGTYPE Maildrop::bye(int n)
{
static const char msg[]="maildrop: signal 0x";
static const char hex[]="0123456789ABCDEF";

	cleanup();
	if (write(2, msg, sizeof(msg)-1) < 0 ||
	    write(2, hex+ ((n / 16) & 0x0F), 1) < 0 ||
	    write(2, hex+ (n & 0x0F), 1) < 0 ||
	    write(2, "\n", 1) < 0)
		; /* gcc shut up */

	_exit(EX_TEMPFAIL);

#if RETSIGTYPE != void
	return (0);
#endif
}

int Maildrop::trap(int (*func)(int, char *[]), int argc, char *argv[])
{
int	n;

	for (n=0; n<NSIG; n++)
		signal(n, bye);
	signal(SIGPIPE, SIG_IGN);
#ifdef SIGWINCH
	signal(SIGWINCH, SIG_IGN);
#endif
	signal(SIGCHLD, SIG_DFL);
	signal(SIGFPE,  sig_fpe);

#if SYSLOG_LOGGING
	openlog("maildrop", LOG_PID, LOG_MAIL);
#endif

	try
	{
	int	r=(*func)(argc, argv);

		cleanup();
		return (r);
	}
	catch (const char *p)
	{
		merr << argv[0] << ": " << p << "\n";
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "%s", p);
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
#if NEED_NONCONST_EXCEPTIONS
	catch (char *p)
	{
		merr << argv[0] << ": " << p << "\n";
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "%s", p);
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
#endif
	catch (int n)
	{
		cleanup();
		return (n);
	}
	catch (...)
	{
		merr << argv[0] << ": Internal error.\n";
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "Internal error.");
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
}
