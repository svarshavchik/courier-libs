#ifndef	sconnect_h
#define	sconnect_h

#if	HAVE_CONFIG_H
#include	"soxwrap/soxwrap_config.h"
#endif

#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#include	<sys/types.h>
#include	<sys/socket.h>

/*
** Copyright 2001 Double Precision, Inc.
** See COPYING for distribution information.
*/


#ifdef  __cplusplus
extern "C"
#endif
int s_connect(int, const struct sockaddr *, size_t, time_t);

#endif
