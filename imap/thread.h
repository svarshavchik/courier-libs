#ifndef	thread_h
#define	thread_h

#include	"searchinfo.h"
/*
** Copyright 2000-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

struct threadinfo;

extern int thread_orderedsubj(struct threadinfo *, struct threadinfo *);

struct unicode_info;

void dothreadorderedsubj(struct searchinfo *, struct searchinfo *,
			 const char *, int);
void dothreadreferences(struct searchinfo *, struct searchinfo *,
			const char *, int);

void dosortmsgs(struct searchinfo *, struct searchinfo *,
		const char *, int);

#endif
