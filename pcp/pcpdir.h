#ifndef pcpdir_h
#define pcpdir_h

/*
** Copyright 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcp.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

/* PCP driver for filesystem-based calendar */

#define HOSTNAMELEN 256

struct PCPdir {
	struct PCP pcp;
	char *username;
	char *dirname;
	char *indexname;
	char *newindexname;
	char hostname_buf[HOSTNAMELEN];
	char unique_filename_buf[256];
	unsigned uniq_cnt;
} ;

extern int retrheaders(struct PCPdir *pd, struct PCP_retr *ri,
		       const char *filename);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
