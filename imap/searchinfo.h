#ifndef	searchinfo_h
#define	searchinfo_h

#include "maildir/maildirsearch.h"

#include <string>
#include <list>

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/


	/* Search keys */

typedef enum {
	search_messageset,
	search_all,
	search_msgflag,	/* Includes ANSWERED DELETED DRAFT FLAGGED RECENT SEEN */
	search_msgkeyword, /* KEYWORD */

	search_not,	/* Logical NOT, used to implement UNANSWERED
			UNDELETED UNDRAFT UNFLAGGED UNKEYWORD UNSEEN, etc... */

	/* NOTE: NEW gets parsed as ( RECENT UNSEEN )  OLD gets parsed as
	NOT RECENT */

	search_and,
	search_or,


	search_header, /* Also used to implement BCC, CC, FROM, TO, SUBJECT */

	search_before,
	search_body,
	search_larger,
	search_on,
	search_sentbefore,
	search_senton,
	search_sentsince,
	search_since,
	search_smaller,
	search_text,
	search_uid,

	/*
	** search_orderedsubj is a dummy node that's allocated in order to
	** implement an ORDEREDSUBJ THREAD/SORT. as points to the stripped
	** subject from the message.
	*/

	search_orderedsubj,

	/*
	** search_references? are dummy nodes that are allocated in order to
	** implement a REFERENCES THREAD.
	*/

	search_references1,	/*  References: and In-Reply-To: header */
	search_references2,	/*  Date: header */
	search_references3,	/*  Subject: header */
	search_references4,	/*  Message-ID: header */

	/*
	** And the following dummies are used for similar purposes for the
	** SORT command.
	*/

	search_arrival,
	search_cc,
	search_date,
	search_from,
	search_reverse,
	search_size,
	search_to

	} search_type;

struct searchinfo;

/* This structure is used when doing content searching */

typedef std::list<searchinfo>::iterator searchiter;

/* A SEARCH request gets parsed into the following structure */

struct searchinfo {
	searchiter a, b;	/* Nested search requests */

	searchinfo(std::list<searchinfo> &searchlist);

	search_type	type;

	std::string as, bs, cs;		/* As needed */

	const struct unicode_info *bs_charset=nullptr;
	/* search_text: text string in orig charset is as, text string in
	   bs_charset charset is in bs */


	int	value=0;/* When evaluating: 0 - false, 1 - true, -1 - unknown */
			/* Not used in AND, OR, and NOT nodes */

	mail::Search sei;	/* Used when searching */

	struct libmail_keywordEntry *ke=nullptr;
	} ;

searchiter alloc_search(std::list<searchinfo> &);
searchiter alloc_parsesearch(std::list<searchinfo> &);
searchiter alloc_searchextra(searchiter,
	std::list<searchinfo> &, search_type);
void debug_search(searchiter );

struct unicode_info;

void dosearch(searchiter, std::list<searchinfo> &,
	      const std::string &, int);

void search_internal(searchiter, std::list<searchinfo> &,
		     const std::string &,
		     int, void (*)(searchiter,
				   std::list<searchinfo> &, int,
				   unsigned long, void *), void *);

void search_set_charset_conv(std::list<searchinfo> &,
			     const std::string &);

#endif
