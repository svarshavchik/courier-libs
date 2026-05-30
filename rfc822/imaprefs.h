/*
*/
#ifndef	imaprefs_h
#define	imaprefs_h

/*
** Copyright 2000-2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif

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

	char *msgid;			/* msgid of this message */

	char *subj;			/* dynalloced subject of this msg */
	time_t timestamp;		/* Timestamp */
	unsigned long seqnum;		/* Sequence number */
};

struct imap_refmsghash {
	imap_refmsghash *nexthash;
	imap_refmsg *msg;
} ;

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
		const char * const * msgidList,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	imap_refmsg *thread();

        imap_refmsg *firstmsg{nullptr};
        imap_refmsg *lastmsg{nullptr};
        /* hash table message id lookup */
        imap_refmsghash *hashtable[512]{};

        struct imap_subjlookup *subjtable[512]{};

        imap_refmsg *rootptr{nullptr};            /* The root */

private:
        imap_refmsg *dorefcreate(const char *newmsgid, rfc822a *a);

	imap_refmsg *threadmsgaref(
		const char *msgidhdr,
		rfc822a *refhdr,
		const char *subjheader,
		const char *dateheader,
		time_t dateheader_tm,
		unsigned long seqnum
	);

	int findsubj(
		const char *s,
		int *isrefwd,
		int create,
		struct imap_subjlookup **ptr
	);

	imap_refmsg *threadallocmsg(const char *msgid);
	void threadprune();
	imap_refmsg *threadgetroot();
	imap_refmsg *threadsearchmsg(const char *msgid);
	int threadsortsubj(imap_refmsg *root);
	int threadgathersubj(imap_refmsg *root);
	int threadmergesubj(imap_refmsg *root);
	int threadsortbydate();
};

	/* INTERNAL FUNCTIONS FOLLOW */


struct imap_subjlookup {
	struct imap_subjlookup *nextsubj;
	char *subj;
	imap_refmsg *msg;
	int msgisrefwd;
} ;

#endif
