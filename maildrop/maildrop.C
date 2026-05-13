#include "config.h"
/*
** Copyright 1998 - 2006 S. Varshavchik.
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
#include	<iostream>

extern void killprocgroup();

int Maildrop::sigchildfd[2];
int Maildrop::sigfpe;

static void sig_fpe(int)
{
	maildrop.sigfpe=1;
	signal (SIGFPE, sig_fpe);
}

static void sig_chld(int)
{
	if (write(maildrop.sigchildfd[1], "", 1) < 0)
		;
}

Maildrop::Maildrop() : m1{*this}, m2{*this}
{
	verbose_level=0;
	isdelivery=0;
	sigfpe=0;
	includelevel=0;
	embedded_mode=0;
	msgptr= &m1;
	savemsgptr= &m2;
#if AUTHLIB_TEMPREJECT
	authlib_essential=1;
#else
	authlib_essential=0;
#endif
}

void Maildrop::cleanup()
{
	ExitTrap::onexit();
	close(sigchildfd[0]);
	close(sigchildfd[1]);
	killprocgroup();
}

void Maildrop::bye(int n)
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
	signal(SIGCHLD, sig_chld);
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
		std::cerr << argv[0] << ": " << p << "\n"
			  << std::flush;
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "%s", p);
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
	catch (char *p)
	{
		std::cerr << argv[0] << ": " << p << "\n"
			  << std::flush;
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "%s", p);
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
	catch (int n)
	{
		cleanup();
		return (n);
	}
	catch (...)
	{
		std::cerr << argv[0] << ": Internal error.\n"
			  << std::flush;
#if SYSLOG_LOGGING
		syslog(LOG_INFO, "Internal error.");
#endif
		cleanup();
		return (EX_TEMPFAIL);
	}
}
