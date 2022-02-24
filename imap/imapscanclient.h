#ifndef	imapscanclient_h
#define	imapscanclient_h

#include "config.h"
#include "maildir/maildirkeywords.h"
#include "maildir/maildirwatch.h"

#include <vector>
#include <string>

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
** Metadata associated with the message's keywords
*/

struct keyword_meta {

	// Index of the keyword's message.

	size_t index;

	keyword_meta(size_t index) : index{index} {}
};

typedef mail::keywords::message<keyword_meta> keywords_t;

/*
** Stuff we want to know about an individual message in the maildir.
*/

struct imapscanmessageinfo {
	unsigned long uid=0;	/* See RFC 2060 */
	std::string filename;

	keywords_t keywords;

	char recentflag=0;
	char changedflags=0;	/* Set by imapscan_open */
	char copiedflag=0;	/* This message was copied to another folder */

	char storeflag=0;  /* Used by imap_addRemoveKeywords() */

	/* All messages found by the search. */
	bool found_in_search=false;

	// After a resync, this is a new record for an existing message. Copy
	// over relevant details from the previous record
	//
	// Returns true if the message's flags or keywords were changed.

	bool update_from(const imapscanmessageinfo &previous);
} ;

/*
** Stuff we want to know about the maildir.
*/

struct imapscaninfo_base {
	imapscaninfo_base(const std::string &current_mailbox);

	std::string current_mailbox;
	std::string current_mailbox_acl;

	bool has_acl(char c)
	{
		return current_mailbox_acl.find(c) != current_mailbox.npos;
	}

	std::vector<imapscanmessageinfo> msgs;
	unsigned long uidv=0;		/* See RFC 2060 */
	unsigned long left_unseen=0;
	unsigned long nextuid=0;

	mail::keywords::hashtable<keyword_meta> keywords;

	~imapscaninfo_base();
};

struct imapscaninfo : imapscaninfo_base {

	imapscaninfo(const std::string &current_mailbox);
	imapscaninfo(imapscaninfo *);

	imapscaninfo &operator=(imapscaninfo &&) noexcept;

	imapscaninfo(const imapscaninfo &)=delete;
	imapscaninfo &operator=(const imapscaninfo &)=delete;

	imapscaninfo(imapscaninfo &&) noexcept;

	unsigned long unseen() const;

	maildir::watch watcher;
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


int imapscan_maildir(imapscaninfo *, int, int, struct uidplus_info *);

int imapscan_openfile(imapscaninfo *, unsigned);


struct libmail_kwMessage *imapscan_createKeyword(imapscaninfo *,
					      unsigned long n);

void imapscan_updateKeywords(const std::string &filename,
			     const mail::keywords::list &keywords);
void imapscan_updateKeywords(const std::string &maildir,
			     const std::string &filename,
			     const mail::keywords::list &keywords);

void imapscan_restoreKeywordSnapshot(std::istream &, imapscaninfo *);
void imapscan_saveKeywordSnapshot(FILE *, imapscaninfo *);

int imapmaildirlock(imapscaninfo *scaninfo,
		    const char *maildir,
		    int (*func)(void *),
		    void *void_arg);

char *readline(unsigned, FILE *);

#endif
