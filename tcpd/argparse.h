#ifndef	argparse_h
#define	argparse_h

/*
** Copyright 2000 S. Varshavchik.
** See COPYING for distribution information.
*/


#include	"config.h"

struct args {
	const char *name;
	const char **valuep;
	void (*funcp)(const char *);
	} ;

int argparse(int argc, char **, struct args *);

#endif
