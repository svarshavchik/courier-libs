/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/

#include	"config.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>

#include	"rfc822/rfc822.h"
#include	"imaprefs.h"

static void swapmsgdata(
	rfc822::refmsgtable::refmsg *a,
	rfc822::refmsgtable::refmsg *b
)
{
	std::swap(a->msgid, b->msgid);
	std::swap(a->isdummy, b->isdummy);
	std::swap(a->flag2, b->flag2);

	std::swap(a->timestamp, b->timestamp);
	std::swap(a->seqnum, b->seqnum);
}

rfc822::refmsgtable::refmsgtable()=default;

rfc822::refmsgtable::~refmsgtable()=default;

rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadallocmsg(const char *msgid)
{
	auto n=msgids.emplace(msgid).first;

	msglist.emplace_back();
	auto &msgp=msglist.back();

	msgp.msgid=n->c_str();

	hashtable.emplace(*n, &msgp);

	return (&msgp);
}

rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadsearchmsg(
	std::string_view msgid
)
{
	auto n=hashtable.find(msgid);
	if (n != hashtable.end())
		return (n->second);
	return nullptr;
}

int rfc822::refmsgtable::findsubj(
	std::string_view s,
	int *isrefwd,
	int create,
	subjtableval_t *&val
)
{
	const auto &[coresubj, hasrefwd] = rfc822::coresubj(s);
	*isrefwd=hasrefwd;

	if (!create)
	{
		auto n=subjects.find(coresubj);

		if (n != subjects.end())
		{
			auto iter=subjtable.find(*n);
			if (iter != subjtable.end())
			{
				val = &iter->second;
				return (0);
			}
		}
		val=nullptr;
		return (0);
	}

	auto n=subjects.insert(coresubj).first;

	auto iter=subjtable.emplace(*n, subjtableval_t{!!hasrefwd, nullptr});

	val = &iter.first->second;
	return (0);
}

static void linkparent(
	rfc822::refmsgtable::refmsg *msg,
	rfc822::refmsgtable::refmsg *lastmsg
)
{
	msg->parent=lastmsg;
	msg->prevsib=lastmsg->lastchild;
	if (msg->prevsib)
		msg->prevsib->nextsib=msg;
	else
		lastmsg->firstchild=msg;

	lastmsg->lastchild=msg;
	msg->nextsib=0;
}


static void breakparent(rfc822::refmsgtable::refmsg *m)
{
	if (!m->parent)	return;

	if (m->prevsib)	m->prevsib->nextsib=m->nextsib;
	else		m->parent->firstchild=m->nextsib;

	if (m->nextsib)	m->nextsib->prevsib=m->prevsib;
	else		m->parent->lastchild=m->prevsib;
	m->parent=0;
}

/* Create a new message node and thread it into the tree.
   a - references header */
rfc822::refmsgtable::refmsg *rfc822::refmsgtable::dorefcreate(
	const char *newmsgid,
	const rfc822::addresses &a
)
{
	refmsg *lastmsg=0, *m;
	refmsg *msg;

/*
            (A) Using the Message-IDs in the message's references, link
            the corresponding messages together as parent/child.  Make
            the first reference the parent of the second (and the second
            a child of the first), the second the parent of the third
            (and the third a child of the second), etc.  The following
            rules govern the creation of these links:

               If no reference message can be found with a given
               Message-ID, create a dummy message with this ID.  Use
               this dummy message for all subsequent references to this
               ID.
*/

	for (auto &addr : a)
	{
		std::string msgid;

		msgid.reserve(addr.address.print(
			rfc822::length_counter{}
		));

		addr.address.print(
			std::back_inserter(msgid)
		);

		if (msgid.empty())
			continue;

		msg=threadsearchmsg(msgid);
		if (!msg)
		{
			msg=threadallocmsg(msgid.c_str());
			if (!msg)
				return (0);
			msg->isdummy=1;
		}

/*
               If a reference message already has a parent, don't change
               the existing link.
*/

		if (lastmsg == 0 || msg->parent)
		{
			lastmsg=msg;
			continue;
		}

/*
               Do not create a parent/child link if creating that link
               would introduce a loop.  For example, before making
               message A the parent of B, make sure that A is not a
               descendent of B.

*/

		for (m=lastmsg; m; m=m->parent)
			if (strcmp(m->msgid, msg->msgid) == 0)
				break;
		if (m)
		{
			lastmsg=msg;
			continue;
		}

		linkparent(msg, lastmsg);

		lastmsg=msg;
	}

/*
            (B) Create a parent/child link between the last reference
            (or NIL if there are no references) and the current message.
            If the current message has a parent already, break the
            current parent/child link before creating the new one.  Note
            that if this message has no references, that it will now
            have no parent.

               NOTE: The parent/child links MUST be kept consistent with
               one another at ALL times.

*/

	msg=newmsgid && *newmsgid ? threadsearchmsg(newmsgid):nullptr;

	/*
	       If a message does not contain a Message-ID header line,
	       or the Message-ID header line does not contain a valid
	       Message ID, then assign a unique Message ID to this
	       message.

	       Implementation note: empty msgid, plus dupe check below,
	       implements that.
	*/

	if (msg && msg->isdummy)
	{
		msg->isdummy=0;
		if (msg->parent)
			breakparent(msg);
	}
	else
	{
#if 1
		/*
		** If two or more messages have the same Message ID, assign
		** a unique Message ID to each of the duplicates.
		**
		** Implementation note: just unlink the existing message from
		** it's parents/children.
		*/
		if (msg)
		{
			while (msg->firstchild)
				breakparent(msg->firstchild);
			breakparent(msg);
			newmsgid="";

			/* Create new entry with an empty msgid, if any more
			** msgids come, they'll hit the dupe check again.
			*/

		}
#endif
		msg=threadallocmsg(newmsgid);
		if (!msg)	return (0);
	}

	if (lastmsg)
	{
		for (m=lastmsg; m; m=m->parent)
			if (strcmp(m->msgid, msg->msgid) == 0)
				break;
		if (!m)
			linkparent(msg, lastmsg);
	}
	return (msg);
}

static rfc822::refmsgtable::refmsg *threadmsg_common(
	rfc822::refmsgtable::refmsg *m,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
);

rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadmsg(
	const char *msgidhdr,
	const char *refhdr,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
)
{
	rfc822::tokens t{refhdr ? refhdr:""};
	rfc822::addresses a{t};

	return threadmsgaref(msgidhdr, a, subjheader, dateheader,
			       dateheader_tm, seqnum);
}


rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadmsgrefs(
	const char *msgid_s,
	const std::vector<std::string_view> &msgidList,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
)
{
	size_t n=0;

	for (auto &msgid : msgidList)
		n += msgid.size() + 3;

	std::string msg;
	msg.reserve(n);

	for (auto &msgid : msgidList)
	{
		msg += "<";
		msg += msgid;
		msg += "> ";
	}

	rfc822::tokens t{msg};
	rfc822::addresses a{t};

	return threadmsgaref(msgid_s, a, subjheader, dateheader,
			       dateheader_tm, seqnum);
}


rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadmsgaref(
	const char *msgidhdr,
	const rfc822::addresses &refhdr,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
)
{
	std::string parsed_msgid;
	{
		rfc822::tokens t{msgidhdr};
		rfc822::addresses a{t};

		if (a.size() == 1)
		{
			parsed_msgid.reserve(a.front().address.print(
				rfc822::length_counter{}
			));

			a.front().address.print(
				std::back_inserter(parsed_msgid)
			);
		}
	}

	auto m=dorefcreate(parsed_msgid.c_str(), refhdr);

	return threadmsg_common(m, subjheader, dateheader,
				dateheader_tm, seqnum);
}

static rfc822::refmsgtable::refmsg *threadmsg_common(
	rfc822::refmsgtable::refmsg *m,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
)
{
	if (subjheader)
		m->subj=subjheader;

	if (dateheader)
		dateheader_tm=rfc822::parse_date(dateheader).value_or(0);

	m->timestamp=dateheader_tm;

	m->seqnum=seqnum;

	return (m);
}

/*
         (2) Gather together all of the messages that have no parents
         and make them all children (siblings of one another) of a dummy
         parent (the "root").  These messages constitute first messages
         of the threads created thus far.

*/

rfc822::refmsgtable::refmsg *rfc822::refmsgtable::threadgetroot()
{
	refmsg *root;

	if (rootptr)
		return (rootptr);

	root=threadallocmsg("(root)");

	if (!root)	return (nullptr);

	root->parent=root;	/* Temporary */
	root->isdummy=1;

	for (auto &msg : msglist)
		if (!msg.parent)
		{
			if (msg.isdummy && msg.firstchild == nullptr)
				continue; /* Can happen in reference creation */

			linkparent(&msg, root);
		}
	root->parent=nullptr;
	return (rootptr=root);
}

/*
**
**       (3) Prune dummy messages from the thread tree.  Traverse each
**        thread under the root, and for each message:
*/

void rfc822::refmsgtable::threadprune()
{
	for (auto &msg : msglist)
	{
		if (!msg.parent)
			continue;	/* The root, need it later. */

		if (!msg.isdummy)
			continue;

		/*
		**
		** If it is a dummy message with NO children, delete it.
		*/

		if (!msg.firstchild)
		{
			breakparent(&msg);
			/*
			** Don't free the node, it'll be done on msgtable
			** purge.
			*/
			continue;
		}

		/*
		** If it is a dummy message with children, delete it, but
		** promote its children to the current level.  In other words,
		** splice them in with the dummy's siblings.
		**
		** Do not promote the children if doing so would make them
		** children of the root, unless there is only one child.
		*/

		if (msg.firstchild->nextsib &&
		    msg.parent->parent)
			continue;

		auto saveparent=msg.parent;
		breakparent(&msg);

		while (msg.firstchild)
		{
			auto m=msg.firstchild;

			breakparent(m);
			linkparent(m, saveparent);
		}
	}
}

static int cmp_msgs(rfc822::refmsgtable::refmsg *m1,
		    rfc822::refmsgtable::refmsg *m2);

int rfc822::refmsgtable::threadsortsubj(refmsg *root)
{
	refmsg *toproot;

/*
** (4) Sort the messages under the root (top-level siblings only)
** by sent date.  In the case of an exact match on sent date or if
** either of the Date: headers used in a comparison can not be
** parsed, use the order in which the messages appear in the
** mailbox (that is, by sequence number) to determine the order.
** In the case of a dummy message, sort its children by sent date
** and then use the first child for the top-level sort.
*/

	size_t cnt=0;

	for (toproot=root->firstchild; toproot;
	     toproot=toproot->nextsib)
	{
		if (toproot->isdummy)
			threadsortsubj(toproot);
		++cnt;
	}

	std::vector<rfc822::refmsgtable::refmsg *> sortarray;
	sortarray.reserve(cnt);

	for (cnt=0; (toproot=root->firstchild) != NULL; ++cnt)
	{
		sortarray.push_back(toproot);
		breakparent(toproot);
	}

	std::sort(sortarray.begin(), sortarray.end(),
			[](rfc822::refmsgtable::refmsg *m1,
			   rfc822::refmsgtable::refmsg *m2)
			{
				return (cmp_msgs(m1, m2) < 0);
			});

	for (auto &m : sortarray)
		linkparent(m, root);
	return (0);
}

int rfc822::refmsgtable::threadgathersubj(refmsg *root)
{
	refmsg *toproot, *p;

/*
** (5) Gather together messages under the root that have the same
** extracted subject text.
**
** (A) Create a table for associating extracted subjects with
** messages.
**
** (B) Populate the subject table with one message per
** extracted subject.  For each message under the root:
*/

	for (toproot=root->firstchild; toproot; toproot=toproot->nextsib)
	{
		int isrefwd;
		subjtableval_t *subjtop;

		/*
		** (i) Find the subject of this thread by extracting the
		** base subject from the current message, or its first child
		** if the current message is a dummy.
		*/

		p=toproot;
		if (p->isdummy)
			p=p->firstchild;

		/*
		** (ii) If the extracted subject is empty, skip this
		** message.
		*/

		if (p->subj.empty())
			continue;

		/*
		** (iii) Lookup the message associated with this extracted
		** subject in the table.
		*/

		if (findsubj(p->subj, &isrefwd, 1, subjtop))
			return (-1);

		/*
		**
		** (iv) If there is no message in the table with this
		** subject, add the current message and the extracted
		** subject to the subject table.
		*/
		auto &[refwd, msg]=*subjtop;
		if (msg == 0)
		{
			msg=toproot;
			refwd=!!isrefwd;
			continue;
		}

		/*
		** Otherwise, replace the message in the table with the
		** current message if the message in the table is not a
		** dummy AND either of the following criteria are true:
		*/

		if (!msg->isdummy)
		{
			/*
			** The current message is a dummy
			**
			*/

			if (toproot->isdummy)
			{
				msg=toproot;
				refwd=!!isrefwd;
				continue;
			}

			/*
			** The message in the table is a reply or forward (its
			** original subject contains a subj-refwd part and/or a
			** "(fwd)" subj-trailer) and the current message is
			not.
			*/

			if (refwd && !isrefwd)
			{
				msg=toproot;
				refwd=!!isrefwd;
			}
		}
	}
	return (0);
}

/*
** (C) Merge threads with the same subject.  For each message
** under the root:
*/

int rfc822::refmsgtable::threadmergesubj(refmsg *root)
{
	refmsg *toproot, *p, *q, *nextroot;

	for (toproot=root->firstchild; toproot; toproot=nextroot)
	{
		subjtableval_t *subjtop;
		int isrefwd;

		nextroot=toproot->nextsib;

		/*
		** (i) Find the subject of this thread as in step 4.B.i
		** above.
		*/

		p=toproot;
		if (p->isdummy)
			p=p->firstchild;

		/*
		** (ii) If the extracted subject is empty, skip this
		** message.
		*/

		if (p->subj.empty())
			continue;

		/*
		** (iii) Lookup the message associated with this extracted
		** subject in the table.
		*/

		if (findsubj(p->subj, &isrefwd, 0, subjtop) || subjtop == 0)
			return (-1);

		auto &[refwd, msg]=*subjtop;

		/*
		** (iv) If the message in the table is the current message,
		** skip it.
		*/

		/* NOTE - ptr comparison IS NOT LEGAL */

		msg->flag2=1;
		if (toproot->flag2)
		{
			toproot->flag2=0;
			continue;
		}
		msg->flag2=0;

		/*
		** Otherwise, merge the current message with the one in the
		** table using the following rules:
		**
		** If both messages are dummies, append the current
		** message's children to the children of the message in
		** the table (the children of both messages become
		** siblings), and then delete the current message.
		*/

		if (msg->isdummy && toproot->isdummy)
		{
			while ((p=toproot->firstchild) != 0)
			{
				breakparent(p);
				linkparent(p, msg);
			}
			breakparent(toproot);
			continue;
		}

		/*
		** If the message in the table is a dummy and the current
		** message is not, make the current message a child of
		** the message in the table (a sibling of it's children).
		*/

		if (msg->isdummy)
		{
			breakparent(toproot);
			linkparent(toproot, msg);
			continue;
		}

		/*
		** If the current message is a reply or forward and the
		** message in the table is not, make the current message
		** a child of the message in the table (a sibling of it's
		** children).
		*/

		if (isrefwd)
		{
			p=msg;
			if (p->isdummy)
				p=p->firstchild;

			const auto &[_, isrefwd2] = rfc822::coresubj(p->subj);

			if (!isrefwd2)
			{
				breakparent(toproot);
				linkparent(toproot, msg);
				continue;
			}
		}

		/*
		** Otherwise, create a new dummy container and make both
		** messages children of the dummy, and replace the
		** message in the table with the dummy message.
		*/

		/* What we do is create a new message, then move the
		** contents of subjtop->msg (including its children)
		** to the new message, then make the new message a child
		** of subjtop->msg, and mark subjtop->msg as a dummy msg.
		*/

		q=threadallocmsg("(dummy)");
		if (!q)
			return (-1);

		q->isdummy=1;

		swapmsgdata(q, msg);

		while ((p=msg->firstchild) != 0)
		{
			breakparent(p);
			linkparent(p, q);
		}
		linkparent(q, msg);

		breakparent(toproot);
		linkparent(toproot, msg);
	}
	return (0);
}

/*
** (6) Traverse the messages under the root and sort each set of
** siblings by sent date.  Traverse the messages in such a way
** that the "youngest" set of siblings are sorted first, and the
** "oldest" set of siblings are sorted last (grandchildren are
** sorted before children, etc).  In the case of an exact match on
** sent date or if either of the Date: headers used in a
** comparison can not be parsed, use the order in which the
** messages appear in the mailbox (that is, by sequence number) to
** determine the order.  In the case of a dummy message (which can
** only occur with top-level siblings), use its first child for
** sorting.
*/

static int cmp_msgs(
	rfc822::refmsgtable::refmsg *ma,
	rfc822::refmsgtable::refmsg *mb
)
{
	time_t ta, tb;
	unsigned long na, nb;

	while (ma && ma->isdummy)
		ma=ma->firstchild;

	while (mb && mb->isdummy)
		mb=mb->firstchild;

	ta=tb=0;
	na=nb=0;
	if (ma)
	{
		ta=ma->timestamp;
		na=ma->seqnum;
	}
	if (mb)
	{
		tb=mb->timestamp;
		nb=mb->seqnum;
	}

	return (ta && tb && ta != tb ? ta < tb ? -1: 1:
		na < nb ? -1: na > nb ? 1:0);
}

namespace {
	struct imap_threadsortinfo {
		rfc822::refmsgtable *mt;
		std::vector<rfc822::refmsgtable::refmsg *> sort_table;
	};
}

static int dothreadsort(
	struct imap_threadsortinfo *itsi,
	rfc822::refmsgtable::refmsg *p
);

int rfc822::refmsgtable::threadsortbydate()
{
	struct imap_threadsortinfo itsi;
	int rc;

	itsi.mt=this;

	rc=dothreadsort(&itsi, rootptr);

	return (rc);
}

static int dothreadsort(
	struct imap_threadsortinfo *itsi,
	rfc822::refmsgtable::refmsg *p
)
{
	rfc822::refmsgtable::refmsg *q;
	for (q=p->firstchild; q; q=q->nextsib)
		dothreadsort(itsi, q);

	itsi->sort_table.clear();
	while ((q=p->firstchild) != 0)
	{
		breakparent(q);
		itsi->sort_table.push_back(q);
	}

	std::sort(itsi->sort_table.begin(),
			  itsi->sort_table.end(),
			  [](rfc822::refmsgtable::refmsg *a, rfc822::refmsgtable::refmsg *b)
			  {
				  return (cmp_msgs(a, b) < 0);
			  });

	for (rfc822::refmsgtable::refmsg *q: itsi->sort_table)
		linkparent(q, p);
	return (0);
}

rfc822::refmsgtable::refmsg *rfc822::refmsgtable::thread()
{
	if (!rootptr)
	{
		threadprune();
		if ((rootptr=threadgetroot()) == 0)
			return (0);
		if (threadsortsubj(rootptr) ||
		    threadgathersubj(rootptr) ||
		    threadmergesubj(rootptr) ||
		    threadsortbydate())
		{
			rootptr=0;
			return (0);
		}
	}

	return (rootptr);
}
