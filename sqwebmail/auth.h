/*
** Copyright 1998 - 2004 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#ifndef	auth_h
#define	auth_h

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

extern const char *do_login(const char *, const char *, const char *);

extern const char *login_returnaddr();
#ifdef __cplusplus
extern std::string login_fromhdr();
#endif
extern const char *myhostname();
extern int nochangepass();

extern int login_changepwd(const char *, const char *, const char *, int *);
extern int changepw(const char *service,
		    const char *uid,
		    const char *opwd,
		    const char *npwd);
#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif
