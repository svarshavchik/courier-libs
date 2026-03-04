/*
** Copyright 2000-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/
/*
*/
#ifndef	addressbook_h
#define	addressbook_h

#include	<stdio.h>
#include	<string>
#include	<string_view>

extern void addressbook();
extern void ab_listselect();
extern void ab_listselect_fp(FILE *);
extern std::string ab_find(std::string_view nick);
extern void ab_add(const char *name, const char *address, const char *nick);
extern void ab_addrselect();
extern int ab_get_nameaddr( int (*)(const char *, const char *, void *),
			    void *);
extern void ab_nameaddr_show(const std::string &, const std::string &);

#endif
