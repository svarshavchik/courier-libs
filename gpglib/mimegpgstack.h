#ifndef mimegpgstack_h
#define mimegpgstack_h

/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct mimestack {
	struct mimestack *next;
	char *boundary;
} ;

int libmail_mimestack_push(struct mimestack **, const char *);
void libmail_mimestack_pop(struct mimestack **);

struct mimestack *libmail_mimestack_search(struct mimestack *, const char *);

void libmail_mimestack_pop_to(struct mimestack **, struct mimestack *);

#ifdef  __cplusplus
} ;
#endif

#endif
