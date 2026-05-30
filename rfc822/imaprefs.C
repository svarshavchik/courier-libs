/*
** Copyright 2000-2003 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/

#include	"config.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>

#include	"rfc822.h"
#include	"imaprefs.h"

static void swapmsgdata(imap_refmsg *a, imap_refmsg *b)
{
	char *cp;
	char c;
	time_t t;
	unsigned long ul;

#define swap(a,b,tmp) (tmp)=(a); (a)=(b); (b)=(tmp);

	swap(a->msgid, b->msgid, cp);
	swap(a->isdummy, b->isdummy, c);
	swap(a->flag2, b->flag2, c);

	swap(a->timestamp, b->timestamp, t);
	swap(a->seqnum, b->seqnum, ul);

#undef	swap
}

imap_refmsgtable::imap_refmsgtable()=default;

imap_refmsgtable::~imap_refmsgtable()
{
	imap_refmsghash *h;
	imap_subjlookup *s;
	imap_refmsg *m;

	for (size_t i=0; i<sizeof(hashtable)/sizeof(hashtable[0]); i++)
		while ((h=hashtable[i]) != 0)
		{
			hashtable[i]=h->nexthash;
			free(h);
		}

	for (size_t i=0; i<sizeof(subjtable)/sizeof(subjtable[0]); i++)
		while ((s=subjtable[i]) != 0)
		{
			subjtable[i]=s->nextsubj;
			free(s->subj);
			free(s);
		}

	while ((m=firstmsg) != 0)
	{
		firstmsg=m->next;
		if (m->subj)
			free(m->subj);
		free(m);
	}
}

static int hashmsgid(const char *msgid)
{
unsigned long hashno=0;

	while (*msgid)
	{
	unsigned long n= (hashno << 1);

#define	HMIDS	(((imap_refmsgtable *)0)->hashtable)
#define	HHMIDSS	( sizeof(HMIDS) / sizeof( HMIDS[0] ))

		if (hashno & HHMIDSS )
			n ^= 1;

		hashno= n ^ (unsigned char)*msgid++;
	}

	return (hashno % HHMIDSS);
}

imap_refmsg *imap_refmsgtable::threadallocmsg(const char *msgid)
{
int n=hashmsgid(msgid);
auto msgp{reinterpret_cast<imap_refmsg *>(
	malloc(sizeof(imap_refmsg)+1+strlen(msgid)))};
imap_refmsghash *h, **hp;

	if (!msgp)	return (0);
	memset(msgp, 0, sizeof(*msgp));
	strcpy ((msgp->msgid=(char *)(msgp+1)), msgid);

	h=reinterpret_cast<imap_refmsghash *>(
		malloc(sizeof(imap_refmsghash)));
	if (!h)
	{
		free(msgp);
		return (0);
	}

	for (hp= &hashtable[n]; *hp; hp= & (*hp)->nexthash)
	{
		if (strcmp( (*hp)->msg->msgid, msgp->msgid) > 0)
			break;
	}

	h->nexthash= *hp;
	*hp=h;
	h->msg=msgp;

	msgp->last=lastmsg;

	if (lastmsg)
		lastmsg->next=msgp;
	else
		firstmsg=msgp;

	lastmsg=msgp;
	return (msgp);
}

imap_refmsg *imap_refmsgtable::threadsearchmsg(const char *msgid)
{
int n=hashmsgid(msgid);
imap_refmsghash *h;

	for (h= hashtable[n]; h; h= h->nexthash)
	{
	int	rc=strcmp(h->msg->msgid, msgid);

		if (rc == 0)	return (h->msg);
		if (rc > 0)
			break;
	}
	return (0);
}

int imap_refmsgtable::findsubj(
	const char *s,
	int *isrefwd,
	int create,
	imap_subjlookup **ptr
)
{
	char *ss=rfc822_coresubj(s, isrefwd);
	int n;
	imap_subjlookup **h;
	imap_subjlookup *newsubj;

	if (!ss)	return (-1);
	n=hashmsgid(ss);

	for (h= &subjtable[n]; *h; h= &(*h)->nextsubj)
	{
	int	rc=strcmp((*h)->subj, ss);

		if (rc == 0)
		{
			free(ss);
			*ptr= *h;
			return (0);
		}
		if (rc > 0)
			break;
	}
	if (!create)
	{
		free(ss);
		*ptr=0;
		return (0);
	}

	newsubj=reinterpret_cast<imap_subjlookup *>(
		malloc(sizeof(imap_subjlookup)));
	if (!newsubj)
	{
		free(ss);
		return (-1);
	}
	memset(newsubj, 0, sizeof(*newsubj));
	newsubj->subj=ss;
	newsubj->nextsubj= *h;
	newsubj->msgisrefwd= *isrefwd;
	*h=newsubj;
	*ptr=newsubj;
	return (0);
}

static void linkparent(imap_refmsg *msg, imap_refmsg *lastmsg)
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


static void breakparent(imap_refmsg *m)
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
imap_refmsg *imap_refmsgtable::dorefcreate(const char *newmsgid, rfc822a *a)
{
	imap_refmsg *lastmsg=0, *m;
	imap_refmsg *msg;
	int n;

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

	for (n=0; n<a->naddrs; n++)
	{
		char *msgid=a->addrs[n].tokens ? rfc822_getaddr(a, n):NULL;

		msg=msgid ? threadsearchmsg(msgid):0;
		if (!msg)
		{
			msg=threadallocmsg(msgid ? msgid:"");
			if (!msg)
			{
				if (msgid)
					free(msgid);

				return (0);
			}
			msg->isdummy=1;
		}

		if (msgid)
			free(msgid);

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

	msg=*newmsgid ? threadsearchmsg(newmsgid):0;

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

static imap_refmsg *threadmsg_common(imap_refmsg *m,
					    const char *subjheader,
					    const char *dateheader,
					    time_t dateheader_tm,
					    unsigned long seqnum);

imap_refmsg *imap_refmsgtable::threadmsg(const char *msgidhdr,
				     const char *refhdr,
				     const char *subjheader,
				     const char *dateheader,
				     time_t dateheader_tm,
				     unsigned long seqnum)
{
	struct rfc822t *t;
	struct rfc822a *a;
	imap_refmsg *m;

	t=rfc822t_alloc_new(refhdr ? refhdr:"", NULL, NULL);
	if (!t)
	{
		return (0);
	}

	a=rfc822a_alloc(t);
	if (!a)
	{
		rfc822t_free(t);
		return (0);
	}

	m=threadmsgaref(msgidhdr, a, subjheader, dateheader,
			       dateheader_tm, seqnum);

	rfc822a_free(a);
	rfc822t_free(t);
	return m;
}


imap_refmsg *imap_refmsgtable::threadmsgrefs(const char *msgid_s,
					 const char * const * msgidList,
					 const char *subjheader,
					 const char *dateheader,
					 time_t dateheader_tm,
					 unsigned long seqnum)
{
	imap_refmsg *m;
	struct rfc822token *tArray;
	struct rfc822addr *aArray;

	struct rfc822a a;
	size_t n, i;

	for (n=0; msgidList[n]; n++)
		;

	if ((tArray=reinterpret_cast<struct rfc822token *>(
		malloc((n+1) * sizeof(*tArray)))) == NULL)
		return NULL;

	if ((aArray=reinterpret_cast<struct rfc822addr *>(
		malloc((n+1) * sizeof(*aArray)))) == NULL)
	{
		free(tArray);
		return NULL;
	}

	for (i=0; i<n; i++)
	{
		tArray[i].next=NULL;
		tArray[i].token=0;
		tArray[i].ptr=msgidList[i];
		tArray[i].len=strlen(msgidList[i]);

		aArray[i].name=NULL;
		aArray[i].tokens=&tArray[i];
	}

	a.naddrs=n;
	a.addrs=aArray;

	m=threadmsgaref(msgid_s, &a, subjheader, dateheader,
			       dateheader_tm, seqnum);

	free(tArray);
	free(aArray);
	return m;
}


imap_refmsg *imap_refmsgtable::threadmsgaref(
	const char *msgidhdr,
	rfc822a *refhdr,
	const char *subjheader,
	const char *dateheader,
	time_t dateheader_tm,
	unsigned long seqnum
)
{
	struct rfc822t *t;
	struct rfc822a *a;
	imap_refmsg *m;

	char *msgid_s;

	t=rfc822t_alloc_new(msgidhdr ? msgidhdr:"", NULL, NULL);
	if (!t)
		return (0);
	a=rfc822a_alloc(t);
	if (!a)
	{
		rfc822t_free(t);
		return (0);
	}

	msgid_s=a->naddrs > 0 ? rfc822_getaddr(a, 0):strdup("");

	rfc822a_free(a);
	rfc822t_free(t);

	if (!msgid_s)
		return (0);

	m=dorefcreate(msgid_s, refhdr);

	free(msgid_s);

	if (!m)
		return (0);


	return threadmsg_common(m, subjheader, dateheader,
				dateheader_tm, seqnum);
}

static imap_refmsg *threadmsg_common(imap_refmsg *m,
					    const char *subjheader,
					    const char *dateheader,
					    time_t dateheader_tm,
					    unsigned long seqnum)
{
	if (subjheader && (m->subj=strdup(subjheader)) == 0)
		return (0);	/* Cleanup in rfc822_threadfree() */

	if (dateheader)
	{
		rfc822_parsedate_chk(dateheader, &dateheader_tm);
	}

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

imap_refmsg *imap_refmsgtable::threadgetroot()
{
	imap_refmsg *root, *m;

	if (rootptr)
		return (rootptr);

	root=threadallocmsg("(root)");

	if (!root)	return (0);

	root->parent=root;	/* Temporary */
	root->isdummy=1;

	for (m=firstmsg; m; m=m->next)
		if (!m->parent)
		{
			if (m->isdummy && m->firstchild == 0)
				continue; /* Can happen in reference creation */

			linkparent(m, root);
		}
	root->parent=NULL;
	return (rootptr=root);
}

/*
**
**       (3) Prune dummy messages from the thread tree.  Traverse each
**        thread under the root, and for each message:
*/

void imap_refmsgtable::threadprune()
{
	imap_refmsg *msg;

	for (msg=firstmsg; msg; msg=msg->next)
	{
		imap_refmsg *saveparent, *m;

		if (!msg->parent)
			continue;	/* The root, need it later. */

		if (!msg->isdummy)
			continue;

		/*
		**
		** If it is a dummy message with NO children, delete it.
		*/

		if (msg->firstchild == 0)
		{
			breakparent(msg);
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

		if (msg->firstchild->nextsib &&
		    msg->parent->parent)
			continue;

		saveparent=msg->parent;
		breakparent(msg);

		while ((m=msg->firstchild) != 0)
		{
			breakparent(m);
			linkparent(m, saveparent);
		}
	}
}

static int cmp_msgs(const void *, const void *);

int imap_refmsgtable::threadsortsubj(imap_refmsg *root)
{
	imap_refmsg *toproot;

/*
** (4) Sort the messages under the root (top-level siblings only)
** by sent date.  In the case of an exact match on sent date or if
** either of the Date: headers used in a comparison can not be
** parsed, use the order in which the messages appear in the
** mailbox (that is, by sequence number) to determine the order.
** In the case of a dummy message, sort its children by sent date
** and then use the first child for the top-level sort.
*/
	size_t cnt, i;
	imap_refmsg **sortarray;

	for (cnt=0, toproot=root->firstchild; toproot;
	     toproot=toproot->nextsib)
	{
		if (toproot->isdummy)
			threadsortsubj(toproot);
		++cnt;
	}

	if ((sortarray=reinterpret_cast<imap_refmsg **>(
		malloc(sizeof(imap_refmsg *)*(cnt+1)))) == 0)
		return (-1);

	for (cnt=0; (toproot=root->firstchild) != NULL; ++cnt)
	{
		sortarray[cnt]=toproot;
		breakparent(toproot);
	}

	qsort(sortarray, cnt, sizeof(*sortarray), cmp_msgs);

	for (i=0; i<cnt; i++)
		linkparent(sortarray[i], root);
	free(sortarray);
	return (0);
}

int imap_refmsgtable::threadgathersubj(imap_refmsg *root)
{
	imap_refmsg *toproot, *p;

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
		const char *subj;
		imap_subjlookup *subjtop;
		int isrefwd;

		/*
		** (i) Find the subject of this thread by extracting the
		** base subject from the current message, or its first child
		** if the current message is a dummy.
		*/

		p=toproot;
		if (p->isdummy)
			p=p->firstchild;

		subj=p->subj ? p->subj:"";


		/*
		** (ii) If the extracted subject is empty, skip this
		** message.
		*/

		if (*subj == 0)
			continue;

		/*
		** (iii) Lookup the message associated with this extracted
		** subject in the table.
		*/

		if (findsubj(subj, &isrefwd, 1, &subjtop))
			return (-1);

		/*
		**
		** (iv) If there is no message in the table with this
		** subject, add the current message and the extracted
		** subject to the subject table.
		*/

		if (subjtop->msg == 0)
		{
			subjtop->msg=toproot;
			subjtop->msgisrefwd=isrefwd;
			continue;
		}

		/*
		** Otherwise, replace the message in the table with the
		** current message if the message in the table is not a
		** dummy AND either of the following criteria are true:
		*/

		if (!subjtop->msg->isdummy)
		{
			/*
			** The current message is a dummy
			**
			*/

			if (toproot->isdummy)
			{
				subjtop->msg=toproot;
				subjtop->msgisrefwd=isrefwd;
				continue;
			}

			/*
			** The message in the table is a reply or forward (its
			** original subject contains a subj-refwd part and/or a
			** "(fwd)" subj-trailer) and the current message is
			not.
			*/

			if (subjtop->msgisrefwd && !isrefwd)
			{
				subjtop->msg=toproot;
				subjtop->msgisrefwd=isrefwd;
			}
		}
	}
	return (0);
}

/*
** (C) Merge threads with the same subject.  For each message
** under the root:
*/

int imap_refmsgtable::threadmergesubj(imap_refmsg *root)
{
	imap_refmsg *toproot, *p, *q, *nextroot;
	char *str;

	for (toproot=root->firstchild; toproot; toproot=nextroot)
	{
		const char *subj;
		imap_subjlookup *subjtop;
		int isrefwd;

		nextroot=toproot->nextsib;

		/*
		** (i) Find the subject of this thread as in step 4.B.i
		** above.
		*/

		p=toproot;
		if (p->isdummy)
			p=p->firstchild;

		subj=p->subj ? p->subj:"";

		/*
		** (ii) If the extracted subject is empty, skip this
		** message.
		*/

		if (*subj == 0)
			continue;

		/*
		** (iii) Lookup the message associated with this extracted
		** subject in the table.
		*/

		if (findsubj(subj, &isrefwd, 0, &subjtop) || subjtop == 0)
			return (-1);

		/*
		** (iv) If the message in the table is the current message,
		** skip it.
		*/

		/* NOTE - ptr comparison IS NOT LEGAL */

		subjtop->msg->flag2=1;
		if (toproot->flag2)
		{
			toproot->flag2=0;
			continue;
		}
		subjtop->msg->flag2=0;

		/*
		** Otherwise, merge the current message with the one in the
		** table using the following rules:
		**
		** If both messages are dummies, append the current
		** message's children to the children of the message in
		** the table (the children of both messages become
		** siblings), and then delete the current message.
		*/

		if (subjtop->msg->isdummy && toproot->isdummy)
		{
			while ((p=toproot->firstchild) != 0)
			{
				breakparent(p);
				linkparent(p, subjtop->msg);
			}
			breakparent(toproot);
			continue;
		}

		/*
		** If the message in the table is a dummy and the current
		** message is not, make the current message a child of
		** the message in the table (a sibling of it's children).
		*/

		if (subjtop->msg->isdummy)
		{
			breakparent(toproot);
			linkparent(toproot, subjtop->msg);
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
			p=subjtop->msg;
			if (p->isdummy)
				p=p->firstchild;

			subj=p->subj ? p->subj:"";

			str=rfc822_coresubj(subj, &isrefwd);

			if (!str)
				return (-1);
			free(str);	/* Don't really care */

			if (!isrefwd)
			{
				breakparent(toproot);
				linkparent(toproot, subjtop->msg);
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

		swapmsgdata(q, subjtop->msg);

		while ((p=subjtop->msg->firstchild) != 0)
		{
			breakparent(p);
			linkparent(p, q);
		}
		linkparent(q, subjtop->msg);

		breakparent(toproot);
		linkparent(toproot, subjtop->msg);
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

static int cmp_msgs(const void *a, const void *b)
{
	imap_refmsg *ma=*(imap_refmsg **)a;
	imap_refmsg *mb=*(imap_refmsg **)b;
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

struct imap_threadsortinfo {
	imap_refmsgtable *mt;
	imap_refmsg **sort_table;
	size_t sort_table_cnt;
} ;

static int dothreadsort(struct imap_threadsortinfo *,
			imap_refmsg *);

int imap_refmsgtable::threadsortbydate()
{
	struct imap_threadsortinfo itsi;
	int rc;

	itsi.mt=this;
	itsi.sort_table=0;
	itsi.sort_table_cnt=0;

	rc=dothreadsort(&itsi, rootptr);

	if (itsi.sort_table)
		free(itsi.sort_table);
	return (rc);
}

static int dothreadsort(struct imap_threadsortinfo *itsi,
			imap_refmsg *p)
{
	imap_refmsg *q;
	size_t i, n;

	for (q=p->firstchild; q; q=q->nextsib)
		dothreadsort(itsi, q);

	n=0;
	for (q=p->firstchild; q; q=q->nextsib)
		++n;

	if (n > itsi->sort_table_cnt)
	{
		auto new_array{reinterpret_cast<imap_refmsg **>(
			itsi->sort_table ?
			 realloc(itsi->sort_table,
				 sizeof(imap_refmsg *)*n)
			 :malloc(sizeof(imap_refmsg *)*n))};

		if (!new_array)
			return (-1);

		itsi->sort_table=new_array;
		itsi->sort_table_cnt=n;
	}

	n=0;
	while ((q=p->firstchild) != 0)
	{
		breakparent(q);
		itsi->sort_table[n++]=q;
	}

	qsort(itsi->sort_table, n, sizeof(imap_refmsg *), cmp_msgs);

	for (i=0; i<n; i++)
		linkparent(itsi->sort_table[i], p);
	return (0);
}

imap_refmsg *imap_refmsgtable::thread()
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
