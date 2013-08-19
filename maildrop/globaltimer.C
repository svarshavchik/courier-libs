#include "config.h"
/*
** Copyright 1998 - 2006 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"globaltimer.h"
#include	"exittrap.h"
#include	<sysexits.h>
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif


GlobalTimer::GlobalTimer()
{
}

GlobalTimer::~GlobalTimer()
{
}

static const char msg[]="maildrop: Timeout quota exceeded.\n";

void GlobalTimer::handler()
{
	ExitTrap::onexit();
	if (write(2, msg, sizeof(msg)-1) < 0)
		; /* ignored */

	_exit(EX_TEMPFAIL);
}
