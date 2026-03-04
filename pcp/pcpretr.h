#ifndef pcpretr_h
#define pcpretr_h

/*
** Copyright 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include "config.h"

#include <stdio.h>
#include <time.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif
struct xretrinfo {
	FILE *tmpfile;
	int status;
	struct xretr_participant_list *participant_list;
	struct xretr_time_list *time_list;

} ;

struct xretr_participant_list {
	struct xretr_participant_list *next;
	char *participant;
} ;

struct xretr_time_list {
	struct xretr_time_list *next;
	time_t from;
	time_t to;
} ;

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
