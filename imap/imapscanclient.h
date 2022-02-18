#ifndef	imapscanclient_h
#define	imapscanclient_h

#include "config.h"
#include "maildir/maildirkeywords.h"

#include <vector>
#include <string>

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
** Stuff we want to know about an individual message in the maildir.
*/

struct imapscanmessageinfo {
	unsigned long uid=0;	/* See RFC 2060 */
	std::string filename;
	struct libmail_kwMessage *keywordMsg=nullptr; /* If not NULL - keywords */
	char recentflag=0;
	char changedflags=0;	/* Set by imapscan_open */
	char copiedflag=0;	/* This message was copied to another folder */

	char storeflag=0;  /* Used by imap_addRemoveKeywords() */

	/* When reading keywords, hash messages by filename */

	struct imapscanmessageinfo *firstBucket=0, *nextBucket=0;

	} ;

/*
** Stuff we want to know about the maildir.
*/

struct imapscaninfo_base {
	std::vector<imapscanmessageinfo> msgs;
	unsigned long uidv=0;		/* See RFC 2060 */
	unsigned long left_unseen=0;
	unsigned long nextuid=0;

	struct libmail_kwHashtable *keywordList; /* All defined keywords */

	struct maildirwatch *watcher=nullptr;

	imapscaninfo_base();
	~imapscaninfo_base();
};

struct imapscaninfo : imapscaninfo_base {

	imapscaninfo()=default;

	imapscaninfo &operator=(imapscaninfo &&) noexcept;

	imapscaninfo(const imapscaninfo &)=delete;
	imapscaninfo &operator=(const imapscaninfo &)=delete;

	imapscaninfo(imapscaninfo &&) noexcept;

	unsigned long unseen() const;
} ;

/*
** In imapscan_maildir, move the following msgs to cur.
*/

struct uidplus_info {
	struct uidplus_info *next;
	char *tmpfilename;
	char *curfilename;

	char *tmpkeywords;
	char *newkeywords;

	unsigned long uid; /* Initialized by imapscan_maildir2 */
	unsigned long old_uid; /* Initialized by do_copy() */

	time_t mtime;
} ;


int imapscan_maildir(imapscaninfo *, const std::string &, int, int,
		     struct uidplus_info *);

int imapscan_openfile(const std::string &, imapscaninfo *, unsigned);


struct libmail_kwMessage *imapscan_createKeyword(imapscaninfo *,
					      unsigned long n);

int imapscan_updateKeywords(const std::string &filename,
			    struct libmail_kwMessage *newKeywords);

int imapscan_restoreKeywordSnapshot(FILE *, imapscaninfo *);
int imapscan_saveKeywordSnapshot(FILE *fp, imapscaninfo *);

int imapmaildirlock(imapscaninfo *scaninfo,
		    const char *maildir,
		    int (*func)(void *),
		    void *void_arg);

char *readline(unsigned, FILE *);

#endif
