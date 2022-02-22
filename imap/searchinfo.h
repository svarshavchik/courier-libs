#ifndef	searchinfo_h
#define	searchinfo_h

#include "maildir/maildirsearch.h"
#include	"imapscanclient.h"
#include	"rfc2045/rfc2045.h"
#include <stdio.h>
#include <string>
#include <list>
#include <functional>

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

	int	value=0;/* When evaluating: 0 - false, 1 - true, -1 - unknown */
			/* Not used in AND, OR, and NOT nodes */

	mail::Search sei;	/* Used when searching */

	bool search_keyword=false;
	} ;

class contentsearch {

public:

	std::list<searchinfo> searchlist;

	contentsearch()=default;

	contentsearch(const contentsearch &)=delete;

	contentsearch &operator=(const contentsearch &)=delete;

	searchiter alloc_search();
	searchiter alloc_parsesearch();
	searchiter alloc_searchextra(searchiter, search_type);


	static void debug_search(searchiter);

	typedef std::function<void (unsigned long)> search_callback_t;

	void search_internal(searchiter,
			     const std::string &,
			     search_callback_t);

	void search_set_charset_conv(const std::string &);

private:
	searchiter alloc_search_andlist();
	searchiter alloc_search_notkey();
	searchiter alloc_search_key();

	void search_oneatatime(searchiter si,
			       unsigned long i,
			       const std::string &charset,
			       search_callback_t callback_func);

	void search_byKeyword(searchiter tree,
			      searchiter keyword,
			      const std::string &charset,
			      search_callback_t callback_func);

	void fill_search_header(const std::string &,
				struct rfc2045 *, FILE *,
				struct imapscanmessageinfo *);

	void fill_search_body(struct rfc2045 *, FILE *,
			      struct imapscanmessageinfo *);

public:

	void dothreadorderedsubj(searchiter, const std::string &, bool);
	void dothreadreferences(searchiter, const std::string &, bool);
	void dosortmsgs(searchiter, const std::string &, bool);


};

#endif
