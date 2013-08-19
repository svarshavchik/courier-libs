#ifndef pcpdauthtoken_h
#define pcpdauthtoken_h

/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <time.h>
#include <sys/time.h>

/*
** Internally-generated random authentication token seeds.
**
** An authentication token consists of: user@domain.time.hash, where
** 'hash' is an HMAC-SHA1 hash of 'user@domain.time', with a randomly-generated
** 128-bit secret token.
*/

void authtoken_init();
time_t authtoken_check();
char *authtoken_create(const char *, time_t);
int authtoken_verify(const char *, const char *, time_t *);

#endif
