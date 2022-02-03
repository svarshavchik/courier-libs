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

void dothreadorderedsubj(searchiter, std::list<searchinfo> &,
			 const std::string &, int);
void dothreadreferences(searchiter, std::list<searchinfo> &,
			const std::string &, int);

void dosortmsgs(searchiter, std::list<searchinfo> &,
		const std::string &, int);

#endif
