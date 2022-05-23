/*
** Copyright 1998 - 1999 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<signal.h>


static int n;

static void trap(int signum)
{
	n=signum;
}

void trap_signals()
{
	n=0;
	signal(SIGTERM, trap);
	signal(SIGINT, trap);
	signal(SIGHUP, trap);
}

int release_signals()
{
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	return (n);
}
