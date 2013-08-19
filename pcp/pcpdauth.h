#ifndef pcpdauth_h
#define pcpdauth_h

/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <sys/types.h>
#include <pwd.h>

struct userid_callback {
	const char *userid;
	const char *homedir;
	const char *maildir;
	uid_t uid;

	int (*callback_func)(struct userid_callback *, void *);
	void *callback_arg;
} ;

int authpcp_userid(const char *, int (*)(struct userid_callback *, void *),
		void *);

int authpcp_login(const char *, const char *,
		  int (*)(struct userid_callback *, void *),
		  void *);


char *auth_myhostname();
char *auth_choplocalhost(const char *);

#endif
