#ifndef	imaprefs_h
#define	imaprefs_h

/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif
#include	<string>
#include	<string_view>
#include	<set>
#include	<unordered_map>
#include	<tuple>
#include	<vector>
#include	"rfc822/rfc822.h"

/*
** Implement REFERENCES threading.
*/

/* The data structures */

struct imap_refmsg {
	imap_refmsg *next, *last;	/* Link list of all msgs */
	imap_refmsg *parent;		/* my parent */
	imap_refmsg *firstchild, *lastchild; /* Children link list */
	imap_refmsg *prevsib, *nextsib;	/* Link list of siblings */

	char isdummy;			/* this is a dummy node (for now) */
	char flag2;			/* Additional flag */

	const char *msgid;		/* msgid of this message */

	char *subj;			/* dynalloced subject of this msg */
	time_t timestamp;		/* Timestamp */
	unsigned long seqnum;		/* Sequence number */
};

class imap_refmsgtable {
public:
	imap_refmsgtable();
	~imap_refmsgtable();

	imap_refmsg *threadmsg(
		const char *msgidhdr,
		const char *refhdr,
		const char *subjheader,

		const char *dateheader,
		time_t dateheader_tm,

		unsigned long seqnum
	);

	imap_refmsg *threadmsgrefs(
		const char *msgid_s,
		const std::vector<std::string_view> &msgidList,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	imap_refmsg *thread();

        imap_refmsg *firstmsg{nullptr};
        imap_refmsg *lastmsg{nullptr};

	// All message IDs are stored here, and referenced by string_views.
	std::set<std::string> msgids;

	// All subjects are stored here, and referenced by string_views.
	std::set<std::string> subjects;

	// Look up message by message ID. We should not see messages with
	// duplicate messageids, but if we are fed them we'll know about the
	// first one.

	std::unordered_map<std::string_view, imap_refmsg *> hashtable;

	// Maps all core subjects to all messages that have that core subject,
	// and a boolean if the subject has "re" or "fwd" prefixes.
	typedef std::tuple<bool, imap_refmsg *> subjtableval_t;

	std::unordered_map<std::string_view, subjtableval_t> subjtable;

        imap_refmsg *rootptr{nullptr};            /* The root */

private:
        imap_refmsg *dorefcreate(
		const char *newmsgid,
		const rfc822::addresses &a
	);

	imap_refmsg *threadmsgaref(
		const char *msgidhdr,
		const rfc822::addresses &a,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	int findsubj(
		const char *s,
		int *isrefwd,
		int create,
		subjtableval_t *&val
	);

	imap_refmsg *threadallocmsg(const char *msgid);
	void threadprune();
	imap_refmsg *threadgetroot();
	imap_refmsg *threadsearchmsg(std::string_view msgid);
	int threadsortsubj(imap_refmsg *root);
	int threadgathersubj(imap_refmsg *root);
	int threadmergesubj(imap_refmsg *root);
	int threadsortbydate();
};

#endif
