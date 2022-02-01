/*
** Copyright 2000-2022 S. Varshavchik.
** See COPYING for distribution information.
*/


#include	"config.h"
#include	"imapd.h"
#include	"thread.h"
#include	"searchinfo.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	"imapwrite.h"
#include	"imaptoken.h"
#include	"imapscanclient.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc822/imaprefs.h"
#include	<courier-unicode.h>
#include	<list>
#include	<algorithm>

static void thread_os_callback(struct searchinfo *, struct searchinfo *, int,
	unsigned long, void *);
static void thread_ref_callback(struct searchinfo *, struct searchinfo *, int,
	unsigned long, void *);

extern struct imapscaninfo current_maildir_info;


struct os_threadinfo {
	std::string subj;
	time_t sent_date;
	unsigned long n;
	} ;

static void os_add(std::vector<os_threadinfo> &v,
		   unsigned long n, const char *s,
		   time_t sent_date)
{
	v.push_back({s, sent_date, n});
}

/* Print the meat of the THREAD ORDEREDSUBJECT response */

static void printos(const std::vector<os_threadinfo> &array)
{
	size_t	i;

	std::list<size_t> thread_start;

	/*
	** thread_start - indexes to start of each thread, sort indexes by
	** sent_date
	*/

	for (i=0; i<array.size(); i++)
	{
		/* Find start of next thread */

		if (i > 0 && array[i-1].subj == array[i].subj)
			continue;

		/* Insert into the list, sorted by sent date */

		auto tptr=thread_start.begin();
		for (auto e=thread_start.end(); tptr != e; ++tptr)
			if ( array[*tptr].sent_date > array[i].sent_date)
				break;

		thread_start.insert(tptr, i);
	}

	for (size_t i : thread_start)
	{
		size_t	j;
		const char *p;

		for (j=i+1; j<array.size(); j++)
		{
			if (array[i].subj != array[j].subj)
				break;
		}

		p="(";
		while (i < j)
		{
			writes(p);
			p=" ";
			writen(array[i].n);
			++i;
		}
		writes(")");
	}
}

void dothreadorderedsubj(struct searchinfo *si, struct searchinfo *sihead,
			 const char *charset, int isuid)
{
	std::vector<os_threadinfo> os;

	search_internal(si, sihead, charset, isuid, thread_os_callback, &os);

	std::sort(os.begin(), os.end(),
		  [&]
		  (const os_threadinfo &a, const os_threadinfo &b)
		  {
			  auto rc=a.subj.compare(b.subj);

			  if (rc < 0)
				  return true;

			  if (rc > 0)
				  return false;

			  return (a.sent_date < b.sent_date ? true:
				  a.sent_date > b.sent_date ? false:
				  a.n < b.n);
		  });

	/* Print the array */

	printos(os);
}

/*
This callback function is called once search finds a qualifying message.
We save its message number and subject in a link list.
*/

static void thread_os_callback(struct searchinfo *si,
			       struct searchinfo *sihead,
			       int isuid, unsigned long i,
			       void *voidarg)
{
	if (sihead->type == search_orderedsubj)
	{
		/* SHOULD BE ALWAYS TRUE */
		time_t t=0;

		if (sihead->bs)
			rfc822_parsedate_chk(sihead->bs, &t);

		os_add(
			*reinterpret_cast<std::vector<os_threadinfo> *>(
				voidarg
			),
			isuid ? current_maildir_info.msgs[i].uid:i+1,
			sihead->as ? sihead->as:"",
			t);
	}
}

static void printthread(struct imap_refmsg *, int);

void dothreadreferences(struct searchinfo *si, struct searchinfo *sihead,
			const char *charset,
			int isuid)
{
	struct imap_refmsgtable *reftable;
	struct imap_refmsg *root;

	if (!(reftable=rfc822_threadalloc()))
	{
		write_error_exit(0);
		return;
	}

	search_internal(si, sihead, charset, 0,
			thread_ref_callback, reftable);

	root=rfc822_thread(reftable);
	printthread(root, isuid);
	rfc822_threadfree(reftable);
}

static void thread_ref_callback(struct searchinfo *si,
			       struct searchinfo *sihead,
			       int isuid, unsigned long i,
			       void *voidarg)
{
	if (sihead->type == search_references1 && sihead->a &&
	    sihead->a->type == search_references2 && sihead->a->a &&
	    sihead->a->a->type == search_references3 && sihead->a->a->a &&
	    sihead->a->a->a->type == search_references4)
	{
		const char *ref, *inreplyto, *subject, *date, *msgid;

		ref=sihead->as;
		inreplyto=sihead->bs;
		date=sihead->a->as;
		subject=sihead->a->a->as;
		msgid=sihead->a->a->a->as;

#if 0
		fprintf(stderr, "REFERENCES: ref=%s, inreplyto=%s, subject=%s, date=%s, msgid=%s\n",
			ref ? ref:"",
			inreplyto ? inreplyto:"",
			subject ? subject:"",
			date ? date:"",
			msgid ? msgid:"");
#endif

		if (!rfc822_threadmsg( (struct imap_refmsgtable *)voidarg,
				       msgid, ref && *ref ? ref:inreplyto,
				       subject, date, 0, i))
			write_error_exit(0);
	}
}

static void printthread(struct imap_refmsg *msg, int isuid)
{
	const char *pfix="";

	while (msg)
	{
		if (!msg->isdummy)
		{
			writes(pfix);
			writen(isuid ?
			       current_maildir_info.msgs[msg->seqnum].uid:
			       msg->seqnum+1);
			pfix=" ";
		}

		if (msg->firstchild && (msg->firstchild->nextsib
					|| msg->firstchild->isdummy
					|| msg->parent == NULL))
		{
			writes(pfix);
			for (msg=msg->firstchild; msg; msg=msg->nextsib)
			{
				struct imap_refmsg *msg2;

				msg2=msg;

				if (msg2->isdummy)
					msg2=msg2->firstchild;

				for (; msg2; msg2=msg2->firstchild)
				{
					if (!msg2->isdummy ||
					    msg2->nextsib)
						break;
				}

				if (msg2)
				{
					writes("(");
					printthread(msg, isuid);
					writes(")");
				}
			}
			break;
		}
		msg=msg->firstchild;
	}
}

/* ---------------------------------- SORT ---------------------------- */

/* sortmsginfo holds the sorting information for a message. */

struct sortmsginfo {
	unsigned long n;		/* msg number/uid */
	std::vector<std::string> sortfields; /* array of sorting fields */
	};

struct sortmsgs {
	std::vector<sortmsginfo> array;
	std::vector<char> reverse_sort_order;
	} ;

static void sort_callback(struct searchinfo *, struct searchinfo *, int,
	unsigned long, void *);

void dosortmsgs(struct searchinfo *si, struct searchinfo *sihead,
		const char *charset, int isuid)
{
	sortmsgs sm;

	char rev=0;

	for (auto p=sihead; p; p=p->a)
		switch (p->type)	{
		case search_reverse:
			rev=1-rev;
			break;
		case search_orderedsubj:
		case search_arrival:
		case search_cc:
		case search_date:
		case search_from:
		case search_size:
		case search_to:
			sm.reverse_sort_order.push_back(rev);
			rev=0;
			break;
		default:
			break;
		}

	search_internal(si, sihead, charset, isuid, sort_callback, &sm);

	std::sort(sm.array.begin(), sm.array.end(),
		  [&]
		  (const sortmsginfo &a, const sortmsginfo &b)
		  {
			  auto ap=a.sortfields.begin(),
				  bp=b.sortfields.begin();

			  for (auto reverse:sm.reverse_sort_order)
			  {
				  int	n=ap->compare(*bp);
				  ++ap;
				  ++bp;

				  if (n < 0)
					  return reverse == 0;

				  if (n > 0)
					  return reverse != 0;
			  }

			  return a.n < b.n;
		  });

	for (const auto &msg:sm.array)
	{
		writes(" ");
		writen(msg.n);
	}
}

static void sort_callback(struct searchinfo *si, struct searchinfo *sihead,
	int isuid, unsigned long n, void *voidarg)
{
	struct sortmsgs *sm=(struct sortmsgs *)voidarg;
	struct searchinfo *ss;

	sm->array.push_back({});

	sortmsginfo &msg=sm->array.back();

	auto s=sm->reverse_sort_order.size();
	msg.sortfields.resize(s);

	size_t i=0;

/* fprintf(stderr, "--\n"); */

	for (ss=sihead; ss; ss=ss->a)
	{
		const char *p;

		if (i >= s)
			break;	/* Something's fucked up, better handle it
				** gracefully, instead of dumping core.
				*/
		switch (ss->type)	{
		case search_reverse:
			continue;
		case search_orderedsubj:
		case search_arrival:
		case search_cc:
		case search_date:
		case search_from:
		case search_size:
		case search_to:
			p=ss->as;
			if (!p)	p="";
			msg.sortfields[i]=p;
			/* fprintf(stderr, "%d %s\n", msg->sortorder[i], msg->sortfields[i]); */
			++i;
			continue;
		default:
			break;
		}
		break;
	}

	msg.n=isuid ? current_maildir_info.msgs[n].uid:n+1;
}
