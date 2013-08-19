/*
** Copyright 1998 - 2004 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#ifndef	auth_h
#define	auth_h

extern int prelogin(const char *);
extern const char *do_login(const char *, const char *, const char *);

extern const char *login_returnaddr();
extern const char *login_fromhdr();

extern int login_changepwd(const char *, const char *, const char *, int *);
extern int changepw(const char *service,
		    const char *uid,
		    const char *opwd,
		    const char *npwd);

#endif
