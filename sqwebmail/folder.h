/*
*/
#ifndef	folder_h
#define	folder_h

#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif

/*
** Copyright 1998 - 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#if	TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif

typedef struct {
	size_t  msgnum; /* Filled in by maildir_read() and maildir_search() */
	char	*filename;
	char	*date_s;
	char	*from_s;
	char	*subject_s;
	char	*size_s;
	time_t	date_n;
	unsigned long size_n;
	time_t	mi_mtime;
	ino_t	mi_ino;
	} MSGINFO;

#define	MSGINFO_FILENAME(n)	((const char *)(n)->filename)
#define	MSGINFO_DATE(n)	((const char *)(n)->date_s)
#define	MSGINFO_FROM(n)	((const char *)(n)->from_s)
#define	MSGINFO_SUBJECT(n)	((const char *)(n)->subject_s)
#define	MSGINFO_SIZE(n)	((const char *)(n)->size_s)

extern void folder_search(const char *, size_t);
extern void folder_contents_title();
extern void folder_contents(const char *, size_t);
extern void folder_navigate(const char *, size_t, long, int, int,
			    unsigned long *);
extern void folder_delmsgs(const char *, size_t);
extern void folder_showmsg(const char *, size_t);
extern void folder_keyimport(const char *, size_t);
extern void folder_initnextprev(const char *, size_t);
extern void folder_nextprev(), folder_msgmove();
extern void folder_delmsg(size_t);
extern void folder_list(), folder_list2(), folder_rename_list();
extern void folder_showtransfer();
extern void folder_download(const char *, size_t, const char *);
extern void folder_showquota();

#define	MSGTYPE_NEW	'N'
#define	MSGTYPE_DELETED	'D'
#define	MSGTYPE_REPLIED	'R'

typedef struct {
	char *prefix;
	char *match;
	char *suffix;
} MATCHEDSTR;


#endif
