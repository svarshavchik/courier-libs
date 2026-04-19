/*
*/
#ifndef	maildir_h
#define	maildir_h

/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	"folder.h"

struct unicode_info;

#ifdef __cplusplus
#include <string>
#include <vector>
#include "rfc822/rfc822.h"

extern "C" {
#endif
#if 0
}
#endif

extern void maildir_remcache(const char *);
extern void maildir_reload(const char *);

extern void maildir_count(const char *, size_t &, size_t &);

extern int maildir_name2pos(const char *, const char *, size_t *);

extern char maildirfile_type(const char *);
extern void maildir_markread(const char *, size_t);
extern void maildir_markreplied(const char *, const char *);
extern void maildir_msgdeletefile(const char *, const char *, size_t);
extern void maildir_msgpurge(const char *, size_t);
extern void maildir_msgpurgefile(const char *, const char *);
extern void maildir_purgemimegpg();
extern void maildir_purgesearch();

extern int maildir_msgmove(const char *, size_t, const char *);
extern int maildir_msgmovefile(const char *, const char *, const char *, size_t);
extern void maildir_autopurge();
extern char *maildir_readheader(FILE *, char **, int);
extern char *maildir_readheader_mimepart(FILE *, char **, int,
					off_t *, const off_t *);
extern char *maildir_readline(FILE *);
extern char *maildir_readheader_nolc(FILE *, char **);

extern void maildir_readfolders(char ***);
extern int maildir_create(const char *);
extern int maildir_delete(const char *, int);
extern int maildir_rename_wrapper(const char *, const char *);

extern void maildir_writemsg(int, const char *, size_t);
extern void maildir_writemsgstr(int, const char *);
extern void listrights();
extern void getacl();

/*
** Hack: to correctly set Content-Transfer-Encoding: header on sent mail,
** the message is written out with the header set to 7bit, but the file
** position of "7bit" is saved. writemsg notes if there were any 8bit
** characters, and, if necessary, we reseek and change the header in place!
*/

extern off_t	writebufpos;	/* File position updated by writemsg */
extern int	writebuf8bit;	/* 8 bit character flag */

extern int maildir_writemsg_flush(int);
extern size_t maildir_countof(const char *);
extern void maildir_savefoldermsgs(const char *);

#if 0
{
#endif
#ifdef __cplusplus
}

extern std::string maildir_find(const char *, const char *);
extern std::string maildir_posfind(const char *, size_t *);
extern int maildir_recreatemsg(const char *, const char *, std::string &);
extern int maildir_createmsg(const char *, const char *, std::string &);
extern int maildir_closemsg(int, const char *, const std::string &, int,
	unsigned long);
extern int maildir_closemsg(rfc822::fdstreambuf &,
			    const char *, const std::string &, int,
			    unsigned long);
extern void maildir_deletenewmsg(int n, const char *, const std::string &);
extern void maildir_deletenewmsg(rfc822::fdstreambuf &,
			     const char *, const std::string &filename);
extern void maildir_deletenewmsg(rfc822::fdstreambuf &,
				 const char *, const std::string &);
/*
** Convert folder names to modified-UTF8.
*/

extern std::string folder_fromutf8(std::string_view);
extern std::string folder_toutf8(std::string_view);
extern std::string maildir_basename(std::string_view);

typedef std::vector<
	std::tuple<MSGINFO, std::vector<MATCHEDSTR>>
> maildir_contents_t;

extern maildir_contents_t maildir_read(const char *, size_t,
	size_t &, bool &, bool &);
extern void maildir_search(const char *dirname,
			   size_t pos,
			   const char *searchtxt,
			   const char *charset,
			   size_t nfiles);

extern maildir_contents_t maildir_loadsearch(
	size_t nfiles,
	size_t &last_message_searched
);

extern std::vector<std::string> maildir_listfolders(
	const char *inbox_name,
	const char *homedir
);

extern void maildir_remflagname(std::string &filename, char flag);

#endif

/*
** Cache kept in the Maildir directory of the sorted contents of the cur
** subdirectory.
*/

#define	MAILDIRCURCACHE	"sqwebmail-curcache"

/*
** Another cache file, but just of message counts in maildir/cur.
*/

#define	MAILDIRCOUNTCACHE "sqwebmail-curcnt"

#endif
