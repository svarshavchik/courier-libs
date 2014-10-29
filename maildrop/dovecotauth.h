#ifndef	dovecotauth_h
#define	dovecotauth_h

/*
** Copyright 2009 Marko Njezic
** Licensed under the same terms as Courier Authlib AND/OR Courier Maildrop.
**
** Partially based on courierauth.h from Courier Authlib, which had the following statement:
**
** Copyright 2004 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#include	<sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

static const char dovecotauth_h_rcsid[]="$Id$";

struct dovecotauthinfo {
	const char *address;
	const char *sysusername;
	const uid_t *sysuserid;
	gid_t sysgroupid;
	const char *homedir;
	const char *maildir;
	} ;

/*
	This structure is modeled after authinfo structure from Courier Authlib.

	Either sysusername or sysuserid may be NULL, but not both of them.
	They, and sysgroupid, specify the authenticated user's system
	userid and groupid.  homedir points to the authenticated user's
	home directory.  address and maildir, are obvious.

	After populating this tructure, the lookup function calls the
	callback function that's specified in its second argument.  The
	callback function receives a pointer to the authinfo structure.

	The callback function also receives a context pointer, which is
	the third argument to the lookup function.

	The lookup function should return a negative value if the userid
	does not exist, a positive value if there was a temporary error
	looking up the userid, or whatever is the return code from the
	callback function, if the user exists.
*/

int dovecotauth_getuserinfo(const char *addr, const char *user,
	int (*func)(struct dovecotauthinfo *, void *), void *arg);

#ifdef	__cplusplus
}
#endif

#endif
