/*
*/
#ifndef	gpg_h
#define	gpg_h

/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif
#include	<stdlib.h>
#include	<stdio.h>

extern void gpglistpub();
extern void gpglistsec();

extern void gpgcreate();
extern void gpgdo();

extern void gpgselectkey();
extern void gpgencryptkeys(const char *);
extern int gpgbadarg(const char *);

extern void gpgselectpubkey();
extern void gpgselectprivkey();
extern int gpgexportkey(const char *, int,
			int (*)(const char *, size_t, void *),
			void *);

int gpgdomsg(int, int, const char *, const char *);

extern int gpgdecode(int, int);

#endif
