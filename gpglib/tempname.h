#ifndef tempname_h
#define tempname_h
/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define TEMPNAMEBUFSIZE	64

int libmail_tempfile(char *);

#ifdef  __cplusplus
} ;
#endif

#endif
