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
** Copyright 1998 - 2001 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif

#ifdef __cplusplus
#include <string>
#include <optional>

struct MSGINFO {
	size_t  msgnum{0}; /* Filled in by maildir_read() and maildir_search() */
	std::string filename;
	std::string date_s;
	std::string from_s;
	std::string subject_s;
	std::string size_s;
	time_t	date_n{0};
	unsigned long size_n{0};
	time_t	mi_mtime{0};
	ino_t	mi_ino{0};
};

#define	MSGINFO_FILENAME(n)	((n).filename.c_str())
#define	MSGINFO_DATE(n)	((n).date_s.c_str())
#define	MSGINFO_FROM(n)	((n).from_s.c_str())
#define	MSGINFO_SUBJECT(n)	((n).subject_s.c_str())
#define	MSGINFO_SIZE(n)	((n).size_s.c_str())

extern "C" {
#endif
#if 0
}
#endif

extern void folder_search(const char *, size_t);
extern void folder_contents_title();
extern void folder_contents(const char *, size_t);
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
extern void print_safe(const char *);
extern void call_print_safe_to_stdout(const char *p, size_t cnt);
extern void folder_cleanup();
extern void maildir_cleanup();
extern void folder_navigate(
	const char *dir,
	size_t pos,
	long highend,
	bool morebefore,
	bool moreafter,
	const std::optional<size_t> &last_message_searched_ptr
);
extern const char *redirect_hash(const char *);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#define	MSGTYPE_NEW	'N'
#define	MSGTYPE_DELETED	'D'
#define	MSGTYPE_REPLIED	'R'

struct MATCHEDSTR {
	std::string prefix;
	std::string match;
	std::string suffix;
};


#endif
