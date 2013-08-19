/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "pcp.h"

static int compar_times(const void *a, const void *b)
{
	const struct PCP_event_time *aa=*(const struct PCP_event_time **)a;
	const struct PCP_event_time *bb=*(const struct PCP_event_time **)b;

	return (aa->start < bb->start ? -1:
		aa->start > bb->start ? 1:
		aa->end < bb->end ? -1:
		aa->end > bb->end ? 1:0);
}

const struct PCP_event_time **
pcp_add_sort_times(const struct PCP_event_time *t,
		   unsigned n)
{
	const struct PCP_event_time **ptr;
	unsigned i;

	if (n == 0)
	{
		errno=EINVAL;
		return (NULL);
	}

	ptr=malloc(n*sizeof(const struct PCP_event_time *));
	if (!ptr)
		return (NULL);

	for (i=0; i<n; i++)
		ptr[i]=t+i;

	if (n)
		qsort(ptr, n, sizeof(*ptr),
		      compar_times);

	for (i=0; i<n; i++)
	{
		if (ptr[i]->start > ptr[i]->end)
		{
			free(ptr);
			errno=EINVAL;
			return (NULL);
		}

		if (i > 0 && ptr[i-1]->end > ptr[i]->start)
		{
			free(ptr);
			errno=EINVAL;
			return (NULL);
		}
	}

	return (ptr);
}

int pcp_read_saveevent(struct PCP_save_event *ae,
		       char *buf, int bufsize)
{
	int n;

	if (ae->write_event_func)
		return ( ((*ae->write_event_func)
			  (buf, bufsize, ae->write_event_func_misc_ptr)));

	if (!ae->write_event_buf && ae->write_event_fd >= 0)
		return ( read(ae->write_event_fd, buf, bufsize));

	if (ae->write_event_buf == 0)
	{
		errno=EIO;
		return (-1);
	}

	for (n=0; n<bufsize && *ae->write_event_buf; n++)
		buf[n]= *ae->write_event_buf++;

	return (n);
}
