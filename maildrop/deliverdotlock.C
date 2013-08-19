#include "config.h"
#include	"deliverdotlock.h"
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif


void DeliverDotLock::cleanup()
{
	truncate();
	DotLock::cleanup();
}

DeliverDotLock::DeliverDotLock() : truncate_fd(-1), truncate_size(0)
{
	constructed();
}

DeliverDotLock::~DeliverDotLock()
{
	destroying();
}

void DeliverDotLock::truncate()
{
	if (truncate_fd >= 0 &&
		ftruncate(truncate_fd, truncate_size) < 0)
	{
		static const char msg[]="Unable to truncate mailbox.\n";

		if (write(2, msg, sizeof(msg)-1) < 0)
			; /* Ignore */
	}
	truncate_fd= -1;
}
