/*
*/
#ifndef	buf_h
#define	buf_h
/*
** Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
** distribution information.
*/



 /* Oh, to hell with it, I have to write this... */

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<stdlib.h>
#include	<stdio.h>

struct buf {
	char *ptr;
	size_t size, cnt;
	};

#define	buf_init(p)	( (p)->ptr=0, (p)->size=0, (p)->cnt=0)
#define	buf_free(p)	do { if ( (p)->ptr) free ((p)->ptr); buf_init(p);} while (0)

void	buf_cpy(struct buf *, const char *);
void	buf_cpyn(struct buf *, const char *, size_t);
void	buf_cat(struct buf *, const char *);
void	buf_catn(struct buf *, const char *, size_t);
void	buf_memcpy(struct buf *, const char *, size_t);
void	buf_memcat(struct buf *, const char *, size_t);
void	buf_trimleft(struct buf *, size_t);
void	buf_append(struct buf *, char c);

#endif
