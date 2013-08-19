/*
 */
#ifndef	strftime_h
#define	strftime_h	1

#include	<time.h>

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
#if	HAVE_SQWEBMAIL_UNICODE
#if	USE_LIBCHARSET || HAVE_LANGINFO_CODESET

extern size_t strftime_unicode(char *, size_t, const char *, const struct tm *);

#define	strftime(s,max,fmt,tm) strftime_unicode(s,max,fmt,tm)

#endif
#endif
#endif
#endif

#endif

