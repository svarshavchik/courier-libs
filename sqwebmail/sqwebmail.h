/*
*/
#ifndef	sqwebmail_h
#define	sqwebmail_h

/*
** Copyright 1998 - 2006 S. Varshavchik.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif

#ifdef __cplusplus
extern "C" {
#if 0
}
#endif
#endif

extern void error(const char *), error2(const char *, int);
extern void error3(const char *, int, const char *, const char *, int);

#define	enomem()	error2(__FILE__,__LINE__)
#define eio(x,y)	error3(__FILE__,__LINE__,x,y,-1)
#define emsg(x,y)	error3(__FILE__,__LINE__,x,y,0)

/* Location of the user's Maildir */

#define USER_DIR	"Maildir"

/* For PAM-based authentication */

#define	SQWEBMAIL_PAM	"webmail"

/* Socket filename */

#define SOCKFILENAME SOCKDIR "/sqwebmail.sock"

/* Where we keep the IP address we authenticated from */

#define	IPFILE		"sqwebmail-ip"

/* Last time the sent folder was renamed */

#define SENTSTAMP	"sqwebmail-sentstamp"

/* File that keeps the time of last access */

#define	TIMESTAMP	"sqwebmail-timestamp"

/* Various configuration stuff */

#define	CONFIGFILE	"sqwebmail-config"

/* More configuration stuff */

#define GPGCONFIGFILE	"sqwebmail-gpgconfig"

/* Eliminate duplicate messages being sent based on form reloads by using
** unique message tokens.
*/

#define	TOKENFILE	"sqwebmail-token"

/* Sig file */

#define	SIGNATURE	"sqwebmail-sig"

#define	CHECKFILENAME(p) { if (!*p || strchr((p), '/') || *p == '.') enomem(); }

/* Cached shared paths */

#define SHAREDPATHCACHE	"sqwebmail-sharedpath"

/* Wrap lines for new messages */
#define	MYLINESIZE	76

/* Automake dribble */

extern void cleanup();

extern void http_redirect_argu(const char *, unsigned long);
extern void http_redirect_argss(const char *, const char *, const char *);
extern void http_redirect_argsss(const char *, const char *, const char *,
				const char *);

#define	ISCTRL(c)	((unsigned char)(c) < (unsigned char)' ')

extern void fake_exit(int);

extern void addarg(const char *);
extern void freeargs();
extern void insert_include(const char *);
extern const char *getarg(const char *);
extern char *get_templatedir();
extern char *get_imageurl();

#define	GPGDIR "gpg"

#define	MIMEGPGFILENAME "mimegpgfilename"
#define SEARCHRESFILENAME "searchres"

#if 0
{
#endif
#ifdef __cplusplus
}
#include <string>

std::string trim_spaces(const char *s);

extern const char *sqwebmail_sessiontoken;
extern const char *sqwebmail_content_language;
extern const char *sqwebmail_content_locale;
extern const char *sqwebmail_content_ispelldict;
extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_system_charset;

#endif
#endif
