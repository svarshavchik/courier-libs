/*
*/
#ifndef	maildir_h
#define	maildir_h

/*
** Copyright 1998 - 2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	"folder.h"

struct unicode_info;

MSGINFO *maildir_ngetinfo(const char *);
void maildir_nfreeinfo(MSGINFO *);

extern void maildir_free(MSGINFO **, unsigned);
extern void matches_free(MATCHEDSTR **, unsigned);


extern void maildir_remcache(const char *);
extern void maildir_reload(const char *);

extern MSGINFO **maildir_read(const char *, unsigned,
	size_t *, int *, int *);
extern void maildir_search(const char *dirname,
			   size_t pos,
			   const char *searchtxt,
			   const char *charset,
			   unsigned nfiles);

extern void maildir_loadsearch(unsigned nfiles,
			       MSGINFO ***retmsginfo,
			       MATCHEDSTR ***retmatches,
			       unsigned long *last_message_searched);

extern void maildir_count(const char *, unsigned *, unsigned *);

extern char *maildir_basename(const char *);
extern char *maildir_find(const char *, const char *);
extern char *maildir_posfind(const char *, size_t *);
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

extern void maildir_listfolders(const char *inbox_name,
				const char *homedir, char ***);
extern void maildir_readfolders(char ***);
extern void maildir_freefolders(char ***);
extern int maildir_create(const char *);
extern int maildir_delete(const char *, int);
extern int maildir_rename_wrapper(const char *, const char *);

extern int maildir_createmsg(const char *, const char *, char **);
extern int maildir_recreatemsg(const char *, const char *, char **);
extern void maildir_writemsg(int, const char *, size_t);
extern void maildir_writemsgstr(int, const char *);
extern int maildir_closemsg(int, const char *, const char *, int,
	unsigned long);
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
extern void maildir_deletenewmsg(int n, const char *, const char *);
extern unsigned maildir_countof(const char *);
extern void maildir_savefoldermsgs(const char *);

/*
** Convert folder names to modified-UTF7.
*/

extern char *folder_toutf7(const char *);
extern char *folder_fromutf7(const char *);

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
