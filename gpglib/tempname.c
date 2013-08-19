/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif
#include "tempname.h"

static const char base64[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-,";

int libmail_tempfile(char *filename_buf)
{
	unsigned long seed;
	int i;
	int fd;

#if HAVE_GETTIMEOFDAY
	struct timeval tv;

	gettimeofday(&tv, NULL);

	seed=tv.tv_sec;
	seed ^= tv.tv_usec << 16;
#else
	time_t t;

	time(&t);
	seed=t;
#endif
	seed ^= getpid();

	for (i=0; i<1000; i++, seed += 5000)
	{
		char *p;
		unsigned long n;

		strcpy(filename_buf, "/tmp/mimegpg.");

		p=filename_buf + strlen(filename_buf);

		n=seed;
		*p++=base64[ n % 64 ]; n /= 64;
		*p++=base64[ n % 64 ]; n /= 64;
		*p++=base64[ n % 64 ]; n /= 64;
		*p++=base64[ n % 64 ]; n /= 64;
		*p++=base64[ n % 64 ]; n /= 64;
		*p++=base64[ n % 64 ];
		*p=0;

		if ((fd=open(filename_buf, O_RDWR | O_CREAT | O_EXCL, 0600))
		    >= 0)
			return (fd);


		if (errno != EEXIST)
			break;
	}
	return (-1);
}
