/*
*/
#ifndef	sqconfig_h
#define	sqconfig_h

/*
** Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif

#if	TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif

extern const char *read_sqconfig(const char *, const char *, time_t *);
extern void write_sqconfig(const char *, const char *, const char *);

#endif
