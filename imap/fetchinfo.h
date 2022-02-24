#ifndef	fetchinfo_h
#define	fetchinfo_h

#include <string>
#include <list>

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

struct fetchinfo {
	std::string name;
	std::string bodysection;
	bool hasbodysection=false;
	int ispartial=0;
	unsigned long partialstart;
	unsigned long partialend;
	std::list<fetchinfo> bodysublist;
} ;

bool fetchinfo_alloc(bool oneonly, std::list<fetchinfo> &list);

void fetch_free_cache();

void save_cached_offsets(off_t, off_t, off_t);

int get_cached_offsets(off_t, off_t *, off_t *);

#endif
