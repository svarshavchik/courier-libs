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
#include	<list>
#include	"rfc822/rfc822.h"

/*
** Implement REFERENCES threading, as used in IMAP.
*/

namespace rfc822 {
	class refmsgtable;
}

class rfc822::refmsgtable {
public:
	refmsgtable();
	~refmsgtable();

	struct refmsg {
		// Tree parent/children
		refmsg *parent{nullptr};
		refmsg *firstchild{nullptr};
		refmsg *lastchild{nullptr};

		// Sibling list
		refmsg *prevsib{nullptr};
		refmsg *nextsib{nullptr};

		bool isdummy{false};		/* this is a dummy node (for now) */
		char flag2{0};			/* Additional flag */

		const char *msgid{nullptr};	/* msgid of this message */

		std::string subj;
		time_t timestamp{0};		/* Timestamp */
		unsigned long seqnum{0};	/* Sequence number */
	};

	refmsg *threadmsg(
		const char *msgidhdr,
		const char *refhdr,
		const char *subjheader,

		const char *dateheader,
		time_t dateheader_tm,

		unsigned long seqnum
	);

	refmsg *threadmsgrefs(
		const char *msgid_s,
		const std::vector<std::string_view> &msgidList,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	refmsg *thread();

private:
	// All messages are stored here.
	std::list<refmsg> msglist;

	// All message IDs are stored here, and referenced by string_views.
	std::set<std::string> msgids;

	// All subjects are stored here, and referenced by string_views.
	std::set<std::string> subjects;

	// Look up message by message ID. We should not see messages with
	// duplicate messageids, but if we are fed them we'll know about the
	// first one.

	std::unordered_map<std::string_view, refmsg *> hashtable;

	// Maps all core subjects to all messages that have that core subject,
	// and a boolean if the subject has "re" or "fwd" prefixes.
	typedef std::tuple<bool, refmsg *> subjtableval_t;

	std::unordered_map<std::string_view, subjtableval_t> subjtable;

        refmsg *rootptr{nullptr};            /* The root */

        refmsg *dorefcreate(
		const char *newmsgid,
		const rfc822::addresses &a
	);

	refmsg *threadmsgaref(
		const char *msgidhdr,
		const rfc822::addresses &a,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	int findsubj(
		std::string_view s,
		int *isrefwd,
		int create,
		subjtableval_t *&val
	);

	refmsg *threadallocmsg(const char *msgid);
	void threadprune();
	refmsg *threadgetroot();
	refmsg *threadsearchmsg(std::string_view msgid);
	int threadsortsubj(refmsg *root);
	int threadgathersubj(refmsg *root);
	int threadmergesubj(refmsg *root);
	int threadsortbydate();
};

#endif
