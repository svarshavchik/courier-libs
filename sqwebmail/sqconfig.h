/*
*/
#ifndef	sqconfig_h
#define	sqconfig_h

/*
** Copyright 1998 - 1999 S. Varshavchik.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif

#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif

extern const char *read_sqconfig(const char *, const char *, time_t *);
extern void write_sqconfig(const char *, const char *, const char *);

#endif
