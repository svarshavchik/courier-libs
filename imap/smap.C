/*
** Copyright 2003-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<ctype.h>
#include	<signal.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if	HAVE_UTIME_H
#include	<utime.h>
#endif
#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#if HAVE_LOCALE_H
#include	<locale.h>
#endif

#include	<sys/types.h>
#include	<sys/stat.h>

#include	"mysignal.h"
#include	"imapd.h"
#include	"fetchinfo.h"
#include	"searchinfo.h"
#include	"storeinfo.h"
#include	"mailboxlist.h"
#include	"thread.h"
#include	"outbox.h"

#include	"imapwrite.h"
#include	"imaptoken.h"
#include	"imapscanclient.h"
#include	"searchinfo.h"
#include	"maildir/config.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirrequota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirwatch.h"
#include	"maildir/maildiraclt.h"
#include	"maildir/maildirnewshared.h"
#include	"maildir/maildirinfo.h"
#include	<courier-unicode.h>

#include	"rfc2045/rfc2045.h"
#include	"rfc822/rfc822.h"

#include	<string>
#include	<vector>
#include	<list>
#include	<algorithm>
#include	<optional>

#define SMAP_BUFSIZ 8192

#define SHARED "shared"

#define LIST_FOLDER 1
#define LIST_DIRECTORY 2

#define FETCH_UID 1
#define FETCH_SIZE 2
#define FETCH_FLAGS 4
#define FETCH_KEYWORDS 8
#define FETCH_INTERNALDATE 16

extern dev_t homedir_dev;
extern ino_t homedir_ino;

int mdcreate(const char *mailbox);
bool mddelete(const std::string &s);
void dirsync(std::string folder);

extern const char *folder_rename(maildir::info &mi1,
				 maildir::info &mi2);

extern int snapshot_init(const char *, const char *);
extern int keywords();

extern unsigned long header_count, body_count;

extern std::string compute_myrights(maildir::aclt_list &l,
				    const std::string &l_owner);

struct addRemoveKeywordInfo;

void snapshot_select(int);
extern void doflags(rfc822::fdstreambuf &fp, const fetchinfo *fi,
		    imapscaninfo *i, unsigned long msgnum,
		    rfc2045::entity &mimep);
extern void set_time(const std::string &tmpname, time_t timestamp);
extern void imapenhancedidle(void);

extern void expunge();
extern void doNoop(int);
extern int do_store(unsigned long, int, void *);
extern int reflag_filename(struct imapscanmessageinfo *mi,
			       struct imapflags *flags, int fd);

extern void do_expunge(unsigned long expunge_start,
		       unsigned long expunge_end,
		       int force);

static int current_mailbox_shared;

extern imapscaninfo current_maildir_info;
void get_message_flags(struct imapscanmessageinfo *,
		       char *, struct imapflags *);
void fetchflags(unsigned long);

extern bool acl_lock(const std::string &maildir,
		     const std::function< bool() >&callback);

extern void aclminimum(const std::string &);

const rfc2045::entity &fetch_alloc_rfc2045(
	unsigned long,
	rfc822::fdstreambuf &
);
rfc822::fdstreambuf &open_cached_fp(unsigned long);
void fetch_free_cache();

FILE *maildir_mkfilename(const char *mailbox, struct imapflags *flags,
			 unsigned long s,
			 std::string &tmpname,
			 std::string &newname);

/*
** Parse a word from the current SMAP command.
*/

typedef std::vector<const char *> smap_words_t;

static char *getword(char **ptr)
{
	char *p= *ptr, *q, *r;

	while (*p && isspace((int)(unsigned char)*p))
		p++;

	if (*p != '"')
	{
		for (q=p; *q; q++)
		{
			if (isspace((int)(unsigned char)*q))
			{
				*q++=0;
				break;
			}
		}

		*ptr=q;
		return p;
	}

	++p;
	r=q=p;

	while (*r)
	{
		if (*r == '"')
		{
			if (r[1] == '"')
			{
				r += 2;
				*q++='"';
				continue;
			}
			++r;
			break;
		}

		*q++ = *r++;
	}

	*q=0;
	*ptr=r;
	return p;
}

#define UC(c) if ( (c) >= 'a' && (c) <= 'z') (c) += 'A' - 'a'

static void up(char *p)
{
	while (*p)
	{
		UC(*p);
		p++;
	}
}

/*
** Write a WORD reply.
*/
static void smapword_s(const char *w);

void smapword(const char *w)
{
	writes("\"");
	smapword_s(w);
	writes("\"");
}

static void smapword_s(const char *w)
{
	while (w && *w)
	{
		size_t i;

		for (i=0; w[i]; i++)
			if (w[i] == '"')
				break;
		if (i)
			writemem(w, i);

		w += i;

		if (*w)
		{
			writes("\"\"");
			++w;
		}
	}
}

// Collect parsed words, until an empty word.

static smap_words_t fn_fromwords(char **ptr)
{
	smap_words_t words;

	char *p;

	while (*(p=getword(ptr)))
	{
		words.push_back(p);
	}

	return words;
}

/*
** LIST-related functions.
*/

struct list_hier {
	std::string hier;
	int flags;

	list_hier(const std::string &hier) : hier{hier}, flags{0} {}
};

struct list_callback_info {

	std::vector<list_hier> hier; /* Hierarchy being listed */

	std::vector<list_hier> found;
};

static void list(const char *folder, const char *descr, int type)
{
	writes("* LIST ");

	smapword(folder);

	writes(" ");

	smapword(descr);

	if (type & LIST_FOLDER)
		writes(" FOLDER");
	if (type & LIST_DIRECTORY)
		writes(" DIRECTORY");
	writes("\n");
}

/*
** Callback from maildir_list.  f="INBOX.folder.name"
**
*/

struct list_callback_utf8 {

	std::function<void (const char *, const std::vector<std::string> &)
		      > callback_func;
	std::string homedir;
	std::string owner;
};

static void list_callback(const char *f,
			  list_callback_utf8 *utf8)
{
	maildir::aclt_list l;

	auto fn=maildir::smapfn_fromutf8(f);

	f=strchr(f, '.');
	if (!f)
		f="";

	if (l.read(utf8->homedir, f) == 0)
	{
		std::string owner;

		owner.reserve(utf8->owner.size()+5);

		owner="user=";
		owner += utf8->owner;

		auto myrights=compute_myrights(l, owner);

		if (myrights.find(ACL_LOOKUP[0]) != myrights.npos)
			utf8->callback_func(f, fn);
	}
}

/*
** list_callback callback that accumulates existing folders beneath a
** certain hierarchy.
*/

static void list_utf8_callback(const char *n,
			       const std::vector<std::string> &f,
			       struct list_callback_info *lci)
{
	auto hb=lci->hier.begin(), he=lci->hier.end();

	auto fb=f.begin(), fe=f.end();

	for (;;)
	{
		if (fb == fe)
			return;

		if (hb != he)
		{
			if (*fb != hb->hier)
				break;

			++hb;
			++fb;
			continue;
		}

		auto h=std::find_if(lci->found.begin(),
				    lci->found.end(),
				    [&]
				    (list_hier &h)
				    {
					    return *fb == h.hier;
				    });

		if (h == lci->found.end())
		{
			lci->found.emplace_back(*fb);
			h=--lci->found.end();
		}

		if (fb + 1 != fe)
			h->flags |= LIST_DIRECTORY;
		else
			h->flags |= LIST_FOLDER;
		break;
	}
}

/*
** SMAP1 list command goes here.  Dirty hack: build the hierarchy list on
** the stack.
*/

struct smap_find_info {
	std::string homedir;
	std::string maildir;
};

static void smap_find_cb(struct maildir_newshared_enum_cb *cb,
			 smap_find_info *ifs);
static int smap_list_cb(struct maildir_newshared_enum_cb *cb,
			struct list_callback_utf8 *list_utf8_info,
			struct list_callback_info *lci);
static bool read_acls(maildir::aclt_list &aclt_list,
		      maildir::info &minfo);

static void listcmd(char **ptr)
{
	struct list_callback_info lci;
	char *p;

	while (*(p=getword(ptr)))
	{
		lci.hier.emplace_back(p);
	}

	int hierlist=0;

	if (lci.hier.empty()) /* No arguments to LIST */
	{
		list(INBOX, "New Mail", LIST_FOLDER);
		list(INBOX, "Folders", LIST_DIRECTORY);
		list(PUBLIC, "Public Folders", LIST_DIRECTORY);
	}
	else
	{
		list_callback_utf8 list_utf8_info;

		list_utf8_info.callback_func=
			[&]
			(const char *f, const std::vector<std::string> &fn)
			{
				list_utf8_callback(f, fn, &lci);
			};

		if (lci.hier.front().hier == PUBLIC)
		{
			struct maildir_shindex_cache *curcache;
			auto p=++lci.hier.begin();
			bool eof;

			curcache=maildir_shared_cache_read(NULL, NULL, NULL);

			while (curcache && p != lci.hier.end())
			{
				struct smap_find_info sfi;

				size_t i;

				for (i=0; i<curcache->nrecords; i++)
					if (p->hier ==
					    curcache->records[i].name)
						break;

				if (i >= curcache->nrecords)
				{
					curcache=NULL;
					break;
				}

				curcache->indexfile.startingpos=
					curcache->records[i].offset;

				eof=maildir::newshared_nextAt(
					&curcache->indexfile,
					[&]
					{
						smap_find_cb(
							&curcache->indexfile,
							&sfi
						);
					});

				if (eof)
				{
					fprintf(stderr, "ERR: Internal error -"
						" maildir_newshared_nextAt: %s\n",
						strerror(errno));
					curcache=NULL;
					break;
				}

				if (sfi.homedir.empty())
				{
					curcache=
						maildir_shared_cache_read(
							curcache,
							sfi.maildir.c_str(),
							p->hier.c_str());
					++p;
					continue;
				}

				list_callback_info nested_lci;

				nested_lci.hier.reserve(
					lci.hier.end() - p);

				nested_lci.hier.push_back(std::string{INBOX});
				nested_lci.hier.insert(
					nested_lci.hier.end(),
					p+1, lci.hier.end());

				list_utf8_info.callback_func=
					[&]
					(const char *f,
					 const std::vector<std::string> &fn)
					{
						list_utf8_callback(
							f, fn,
							&nested_lci);
					};

				auto d=maildir::location(
					sfi.homedir,
					sfi.maildir);

				list_utf8_info.homedir=d;
				list_utf8_info.owner=p->hier;

				maildir::list(d,
					      [&]
					      (const std::string &name)
					      {
						      list_callback(
							      name.c_str(),
							      &list_utf8_info
						      );
					      });
				curcache=NULL;

				lci.found.insert(lci.found.end(),
						 nested_lci.found.begin(),
						 nested_lci.found.end());
				break;
			}

			if (curcache) /* List a shared hierarchy */
			{
				curcache->indexfile.startingpos=0;
				eof=false;

				do
				{
					eof=(curcache->indexfile.startingpos
					     ? maildir::newshared_next:
					     maildir::newshared_nextAt)(
						     &curcache->indexfile,
						     [&]
						     {
							     smap_list_cb(
								     &curcache->
								     indexfile,
								     &list_utf8_info,
								     &lci
							     );
						     });
				} while (!eof);

				hierlist=1;
			}
		}
		else
		{
			list_utf8_info.homedir=".";
			list_utf8_info.owner=getenv("AUTHENTICATED");
			maildir::list(".",
				      [&]
				      (const std::string &name)
				      {
					      list_callback(name.c_str(),
							    &list_utf8_info);
				      });
		}

		smap_words_t vecs;

		vecs.reserve(lci.hier.size()+1);

		for (const auto &h:lci.hier)
			vecs.push_back(h.hier.c_str());

		for (auto &h:lci.found)
		{
			maildir::aclt_list aclt_list;

			vecs.push_back(h.hier.c_str());

			auto minfo=maildir::info_smap_find(
				vecs,
				getenv("AUTHENTICATED")
			);
			vecs.pop_back();

			if (minfo)
			{
				if (read_acls(aclt_list, minfo))
				{
					auto acl=compute_myrights(aclt_list,
								  minfo.owner);

					if (acl.find(ACL_LOOKUP[0])
					    == acl.npos)
					{
						h.flags=LIST_DIRECTORY;

						if (hierlist)
							list(h.hier.c_str(),
							     h.hier.c_str(),
							     h.flags);

					}
					else
					{
						list(h.hier.c_str(),
						     h.hier.c_str(),
						     h.flags);
					}
				}
				else
				{
					fprintf(stderr,
						"ERR: Cannot read ACLs"
						" for %s(%s): %s\n",
						(!minfo.homedir.empty()
						 ? minfo.homedir
						 : std::string{"."}).c_str(),
						(!minfo.maildir.empty()
						 ? minfo.maildir
						 : std::string{"unknown"}
						).c_str(),
						strerror(errno));
				}
			}
			else
			{
				fprintf(stderr,
					"ERR: Internal error in list():"
					" cannot find folder %s: %s\n",
					h.hier.c_str(),
					strerror(errno));
			}
		}
	}
	writes("+OK LIST completed\n");
}

static void smap_find_cb(struct maildir_newshared_enum_cb *cb,
			 smap_find_info *ifs)
{
	if (cb->homedir)
		ifs->homedir=cb->homedir;
	if (cb->maildir)
		ifs->maildir=cb->maildir;
}

static int smap_list_cb(struct maildir_newshared_enum_cb *cb,
			struct list_callback_utf8 *list_utf8_info,
			struct list_callback_info *lci)
{
	struct stat stat_buf;

	if (cb->homedir == NULL)
	{
		lci->found.emplace_back(cb->name);
		auto h=--lci->found.end();
		h->flags = LIST_DIRECTORY;
		return 0;
	}

	auto d=maildir::location(cb->homedir, cb->maildir);

	if (d.empty())
	{
		perror("ERR: get_topmaildir");
		return 0;
	}

	if (stat(d.c_str(), &stat_buf) < 0 ||
	    (stat_buf.st_dev == homedir_dev &&
	     stat_buf.st_ino == homedir_ino))
	{
		return 0;
	}

	list_utf8_info->homedir=d;
	list_utf8_info->owner=cb->name;
	auto h=std::move(lci->hier);
	lci->hier.clear();
	auto f=std::move(lci->found);
	lci->found.clear();
	maildir::list(d,
		      [&]
		      (const std::string &name)
		      {
			      list_callback(name.c_str(),
					    list_utf8_info);
		      });

	lci->hier=std::move(h);
	if (lci->found.empty())
	{
		lci->found=std::move(f);
	}
	else
	{
		auto p=++lci->found.begin();

		if (p != lci->found.end())
		{
			fprintf(stderr, "ERR: Unexpected folder list"
				" in smap_list_cb()\n");
			lci->found.erase(p, lci->found.end());
		}

		--p;
		lci->found.insert(lci->found.end(),
				  h.begin(), h.end());

		p->hier=cb->name;
	}

	return (0);
}

/*
** Read the name of a new folder.  Returns the pathname to the folder, suitable
** for immediate creation.
*/

static std::string getCreateFolder_int(char **ptr, char *need_perms)
{
	maildir::aclt_list aclt_list;

	auto fn=fn_fromwords(ptr);

	if (need_perms)
	{
		if (fn.size() == 0)
		{
			*need_perms=0;
			errno=EINVAL;
			return "";
		}

		auto saved=fn.back();

		fn.pop_back();

		auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));

		if (!minfo)
		{
			return "";
		}

		fn.push_back(saved);

		if (!read_acls(aclt_list, minfo))
		{
			return "";
		}

		auto save=compute_myrights(aclt_list, minfo.owner);

		for (size_t i=0; need_perms[i]; i++)
			if (save.find(need_perms[i]) == save.npos)
			{
				*need_perms=0;
				errno=EPERM;
				return "";
			}
	}

	auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));

	if (!minfo)
		return "";

	if (minfo.homedir.empty() || minfo.maildir.empty())
	{
		errno=ENOENT;
		return "";
	}

	auto n=maildir::name2dir(minfo.homedir, minfo.maildir);

	if (need_perms && strchr(need_perms, ACL_CREATE[0]))
	{
		/* Initialize the ACL structures */

		(void)read_acls( aclt_list, minfo);
	}

	return n;
}

static std::string getCreateFolder(char **ptr, char *perms)
{
	auto p=getCreateFolder_int(ptr, perms);

	if (p.substr(0, 2) == "./")
		p=p.substr(2);

	return p;
}

static bool read_acls(maildir::aclt_list &aclt_list,
		      maildir::info &minfo)
{
	if (minfo.homedir.empty() || minfo.maildir.empty())
	{
		if (minfo.mailbox_type == MAILBOXTYPE_NEWSHARED)
		{
			/* Intermediate node in public hier */

			aclt_list.add("anyone", ACL_LOOKUP);
			return true;
		}

		return false;
	}

	auto q=maildir::name2dir(".", minfo.maildir);
	if (q.empty())
	{
		fprintf(stderr, "ERR: Internal error"
			" in read_acls(%s)\n", minfo.maildir.c_str());
		return false;
	}

	auto rc=aclt_list.read(minfo.homedir,
			       q.substr(0, 2) == "./"
			       ? q.substr(2):q);

	if (!current_maildir_info.current_mailbox.empty())
	{
		q=maildir::name2dir(minfo.homedir, minfo.maildir);

		if (!q.empty())
		{
			if (q == current_maildir_info.current_mailbox)
			{
				auto r=compute_myrights(aclt_list,
							minfo.owner);

				current_maildir_info.current_mailbox_acl=r;
			}
		}
	}
	return rc == 0;
}

static std::string getExistingFolder_int(char **ptr, char *rightsWanted)
{
	auto fn=fn_fromwords(ptr);

	auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));

	if (!minfo)
	{
		return "";
	}

	if (minfo.homedir.empty() || minfo.maildir.empty())
	{
		errno=ENOENT;
		return "";
	}

	auto n=maildir::name2dir(minfo.homedir, minfo.maildir);

	if (!n.empty() && rightsWanted)
	{
		maildir::aclt_list aclt_list;

		if (!read_acls(aclt_list, minfo))
		{
			return "";
		}

		auto q=compute_myrights(aclt_list, minfo.owner);

		char *r, *s;

		for (r=s=rightsWanted; *r; r++)
			if (q.find(*r) != q.npos)
				*s++ = *r;
		*s=0;
	}

	return n;
}

static std::string getAccessToFolder(char **ptr, char *rightsWanted)
{
	auto p=getExistingFolder_int(ptr, rightsWanted);

	if (p.substr(0, 2) == "./")
	{
		p.erase(p.begin(), p.begin()+2);
	}

	return p;
}

static void smap1_noop(int real_noop)
{
	if (!current_maildir_info.current_mailbox.empty())
		doNoop(real_noop);
	writes("+OK Folder updated\n");
}

/* Parse a message set.  Return the next word following the message set */

typedef std::vector<std::tuple<unsigned long, unsigned long>> smapmsgset_t;

static char *markmsgset(char **ptr, smapmsgset_t &msgset)
{
	unsigned long n;
	char *w;

	msgset.clear();
	msgset.reserve(10);

	n=0;

	while (*(w=getword(ptr)))
	{
		unsigned long a=0, b=0;

		if (*w < '0' || *w > '9')
			break;

		while (*w >= '0' && *w <= '9')
		{
			a=a * 10 + *w-'0';
			w++;
		}

		b=a;

		if (*w == '-')
		{
			++w;
			b=0;

			while (*w >= '0' && *w <= '9')
			{
				b=b * 10 + *w-'0';
				w++;
			}
		}

		if (a <= n || b < a)
		{
			errno=EINVAL;
			return NULL;
		}

		n=b;

		msgset.emplace_back(a, b);
	}

	return w;
}

static void parseflags(char *q, struct imapflags *flags)
{
	char *p;

	if ((q=strchr(q, '=')) == NULL)
		return;
	++q;

	while (*q)
	{
		p=q;

		while (*q)
		{
			if (*q == ',')
			{
				*q++=0;
				break;
			}
			q++;
		}

		if (strcmp(p, "SEEN") == 0)
			flags->seen=true;
		else if (strcmp(p, "REPLIED") == 0)
			flags->answered=true;
		else if (strcmp(p, "DRAFT") == 0)
			flags->drafts=true;
		else if (strcmp(p, "DELETED") == 0)
			flags->deleted=true;
		else if (strcmp(p, "MARKED") == 0)
			flags->flagged=true;

	}
}

static void parsekeywords(char *q, mail::keywords::list &list)
{
	char *p;

	list.clear();

	if ((q=strchr(q, '=')) == NULL)
		return;
	++q;

	while (*q)
	{
		p=q;

		while (*q)
		{
			if (*q == ',')
			{
				*q++=0;
				break;
			}
			q++;
		}

		if (*p)
			list.insert(p);
	}
}

static int applymsgset(const smapmsgset_t &msgset,
		       const std::function<int (unsigned long)> &callback)
{
	for (const auto &range:msgset)
	{
		auto &start=std::get<0>(range);
		auto &end=std::get<1>(range);


		for (auto n=start; n <= end; n++)
		{
			if (current_maildir_info.current_mailbox.empty()
			    ||
			    n > current_maildir_info.msgs.size())
				break;

			auto rc=callback(n-1);

			if (rc)
				return rc;
		}
	}
	return 0;
}

static void do_attrfetch(unsigned long n, int);

static int applyflags(unsigned long n, struct storeinfo *si,
		      bool is_keywords)
{
	int attrs;

	if (n >= current_maildir_info.msgs.size())
		return 0;

	attrs= is_keywords ? FETCH_KEYWORDS:FETCH_FLAGS;

	if (!si->plusminus)
	{
		if (!is_keywords) /* STORE FLAGS= */
			si->keywords=
				current_maildir_info.msgs[n].keywords
				.keywords();
		else /* STORE KEYWORDS= */
			get_message_flags(&current_maildir_info.msgs.at(n), 0,
					  &si->flags);
	}

	/* do_store may clobber si->keywords.  Punt */
	// TODO

	auto newKw=si->keywords;
	if (do_store(n+1, 0, si))
	{
		si->keywords=newKw;
		return -1;
	}
	si->keywords=newKw;

	do_attrfetch(n, attrs);
	return 0;
}

static void setdate(unsigned long n, time_t datestamp)
{
	auto filename=maildir::filename(
		current_maildir_info.current_mailbox,
		"",
		current_maildir_info.msgs[n].filename);

	if (!filename.empty())
		set_time(filename, datestamp);
}

static void msg_expunge(unsigned long n)
{
	do_expunge(n, n+1, 1);
}

struct smapfetchinfo {
	int peek;
	char *entity;
	char *hdrs;
	char *mimeid;
};

static int hashdr(const char *hdrList, std::string_view hdr)
{
	if (!hdrList || !*hdrList)
		return 1;

	while (*hdrList)
	{
		size_t n;
		int is_envelope=0;
		int is_mime=0;

		if (*hdrList == ',')
		{
			++hdrList;
			continue;
		}

		if (strncmp(hdrList, ":ENVELOPE", 9) == 0)
		{
			switch (hdrList[9]) {
			case 0:
			case ',':
				is_envelope=1;
				break;
			}
		}

		if (strncmp(hdrList, ":MIME", 5) == 0)
		{
			switch (hdrList[5]) {
			case 0:
			case ',':
				is_mime=1;
				break;
			}
		}


		if (is_envelope || is_mime)
		{
			char hbuf[30];

			auto htrunc=hdr.substr(0, 29);
			memcpy(hbuf, htrunc.data(), htrunc.size());
			hbuf[htrunc.size()]=0;
			up(hbuf);

			if (strcmp(hbuf, "DATE") == 0)
				return 1;
			if (strcmp(hbuf, "SUBJECT") == 0)
				return 1;
			if (strcmp(hbuf, "FROM") == 0)
				return 1;
			if (strcmp(hbuf, "SENDER") == 0)
				return 1;
			if (strcmp(hbuf, "REPLY-TO") == 0)
				return 1;
			if (strcmp(hbuf, "TO") == 0)
				return 1;
			if (strcmp(hbuf, "CC") == 0)
				return 1;
			if (strcmp(hbuf, "BCC") == 0)
				return 1;
			if (strcmp(hbuf, "IN-REPLY-TO") == 0)
				return 1;
			if (strcmp(hbuf, "MESSAGE-ID") == 0)
				return 1;
			if (strcmp(hbuf, "REFERENCES") == 0)
				return 1;

			if (is_mime)
			{
				if (strcmp(hbuf, "MIME-VERSION") == 0)
					return 1;

				if (strncmp(hbuf, "CONTENT-", 8) == 0)
					return 1;
			}
		}

		for (n=0; hdrList[n] && hdrList[n] != ',' && n < hdr.size(); n++)
		{
			char a=hdrList[n];
			char b=hdr[n];

			UC(b);
			if (a != b)
				break;
		}

		if ((hdrList[n] == 0 || hdrList[n] == ',') && n == hdr.size())
			return 1;

		hdrList += n;
		while (*hdrList && *hdrList != ',')
			++hdrList;
	}
	return 0;
}

static void writemimeid(const rfc2045::entity &rfcp)
{
	size_t n=1;
	auto p=rfcp.get_parent_entity();
	if (p)
	{
		writemimeid(*p);
		writes(".");

		n=&rfcp - p->subentities.data();
		if (!rfc2045_message_content_type(
			p->content_type.value.c_str())
			)
			n++;
	}
	writen(n);
}

static int dump_hdrs(rfc822::fdstreambuf &fp, unsigned long n,
		     const rfc2045::entity &rfcp, const char *hdrs,
		     const char *type)
{
	rfc2045::entity::line_iter<false>::headers headers{rfcp, fp};

	headers.name_lc=false;

	int rc=0;

	if (type && strcmp(type, "RAWHEADERS") == 0)
		headers.keep_eol=true;

	writes("{.");
	writen(rfcp.startbody - rfcp.startpos);
	writes("} FETCH ");
	writen(n+1);
	if (type)
	{
		writes(" ");
		writes(type);
		writes("\n");
	}
	else	/* MIME */
	{
		writes(" LINES=");
		writen(rfcp.nbodylines);
		writes(" SIZE=");
		writen(rfcp.endbody - rfcp.startbody);
		writes(" \"MIME.ID=");

		if (rfcp.get_parent_entity())
		{
			writemimeid(rfcp);
			writes("\" \"MIME.PARENT=");
			if (rfcp.get_parent_entity()->get_parent_entity())
				writemimeid(*rfcp.get_parent_entity());
		}
		writes("\"\n");
	}

	do
	{
		const auto &[header, value]=headers.name_content();

		if (!header.empty() && hashdr(hdrs, header))
		{
			if (header.front() == '.')
				writes(".");
			writemem(header.data(), header.size());
			writes(": ");
			writemem(value.data(), value.size());

			if (value.empty() || value.back() != '\n')
				writes("\n");

			header_count += header.size()+value.size()+3;
		}
	} while (headers.next());
	writes(".\n");

	return rc;
}

static int dump_body(rfc822::fdstreambuf &fp, unsigned long msgNum,
		     const rfc2045::entity &rfcp, bool dump_all)
{
	char buffer[SMAP_BUFSIZ];
	int first;

	auto start_body=rfcp.startbody;
	auto end_body=rfcp.endbody;

	if (dump_all)
		start_body=rfcp.startpos;

	if (fp.pubseekpos(start_body) !=
		static_cast<std::streamoff>(start_body))
		return -1;

	first=1;
	do
	{
		size_t n=sizeof(buffer);

		if (n > end_body - start_body)
			n=end_body - start_body;

		if (n > 0)
		{
			size_t n2=fp.sgetn(buffer, n);

			if (n2 != n)
			{
				errno=EIO;
				return -1;
			}
		}

		if (first)
		{
			if (start_body == end_body)
			{
				writes("{.0} FETCH ");
				writen(msgNum+1);
				writes(" CONTENTS\n.");
			}
			else
			{
				writes("{");
				writen(n);
				writes("/");
				writen(end_body - start_body);
				writes("} FETCH ");
				writen(msgNum+1);
				writes(" CONTENTS\n");
			}
		}
		else
		{
			writen(n);
			writes("\n");
		}

		first=0;
		writemem(buffer, n);

		start_body += n;
		body_count += n;
	} while (start_body < end_body);
	writes("\n");
	return 0;
}
static int mime(rfc822::fdstreambuf &fp, unsigned long n,
		const rfc2045::entity &rfcp, const char *hdrs)
{
	int rc=dump_hdrs(fp, n, rfcp, hdrs, NULL);

	if (rc)
		return rc;

	for (auto &part: rfcp.subentities)
	{
		rc=mime(fp, n, part, hdrs);
		if (rc)
			return rc;
	}

	return 0;
}

/*
** Find the specified MIME id.
*/

static const rfc2045::entity *findmimeid(
	const rfc2045::entity *rfcp,
	const char *mimeid
)
{
	std::optional<size_t> message_rfc822_mime_id{1};

	const rfc2045::entity *returnrfcp=nullptr;

	while (mimeid && *mimeid)
	{
		size_t n=0;

		if (*mimeid < '0' || *mimeid > '9')
			return nullptr;

		while (*mimeid >= '0' && *mimeid <= '9')
		{
			n=n * 10 + *mimeid-'0';
			mimeid++;
		}

		if (message_rfc822_mime_id)
		{
			if (n != *message_rfc822_mime_id)
				return nullptr;

			message_rfc822_mime_id.reset();
			returnrfcp=rfcp;
		}
		else
		{
			if (n < 1 || n > rfcp->subentities.size())
				return nullptr;
			rfcp=rfcp->subentities.data() + n-1;
			returnrfcp=rfcp;
			if (rfc2045_message_content_type(
				rfcp->content_type.value.c_str()
			) && !rfcp->subentities.empty())
			{
				message_rfc822_mime_id=0;
				rfcp=&rfcp->subentities[0];
			}
		}
		if (*mimeid == '.')
			++mimeid;
	}
	return returnrfcp;
}

namespace {
	struct decodebuf {
		const unsigned long msgNum;
		const size_t estimated_size;
		char buf[BUFSIZ];
		size_t i=0;

		bool decoded_chunk=false;

		decodebuf(unsigned long msgNum, size_t estimated_size)
			: msgNum{msgNum}, estimated_size{estimated_size}
		{
		}

		void operator()(const char *p, size_t n)
		{
			while (n > 0)
			{
				if (i == sizeof(buf))
					flush();

				size_t chunk=sizeof(buf)-i;

				if (chunk > n)
					chunk=n;
				memcpy(buf+i, p, chunk);
				i += chunk;
				n -= chunk;
			}
		}

		void flush()
		{
			if (i == 0)
				return;

			if (!decoded_chunk)
			{
				writes("{");
				writen(i);
				writes("/" );
				writen(estimated_size);
				writes("} FETCH ");
				writen(msgNum+1);
				writes(" CONTENTS\n");
				decoded_chunk=true;
			}
			else
			{
				writen(i);
				writes("\n");
			}
			writemem(buf, i);
			body_count += i;
			i=0;
		}
	};
}
static int dump_decoded(rfc822::fdstreambuf &fp, unsigned long msgNum,
			const rfc2045::entity &rfcp)
{
	auto size=rfcp.endbody-rfcp.startbody;

	if (rfcp.content_transfer_encoding == rfc2045::cte::base64)
		size=size/4*3;

	decodebuf buf(msgNum, size);
	rfc822::mime_decoder decoder(buf, fp);

	decoder.decode_header=false;
	decoder.decode_subentities=false;
	decoder.decode(rfcp);

	buf.flush();
	if (!buf.decoded_chunk)
	{
		writes("{.0} FETCH ");
		writen(msgNum+1);
		writes(" CONTENTS\n.");
	}
	writes("\n");
	return 0;
}

static int do_fetch(unsigned long n, smapfetchinfo &fi)
{
	rfc822::fdstreambuf &fp=open_cached_fp(n);
	int rc=0;

	if (fp.error())
		return -1;

	if (strcmp(fi.entity, "MIME") == 0)
	{
		const rfc2045::entity &rfcp=fetch_alloc_rfc2045(n, fp);

		if (fp.error())
			return -1;

		rc=mime(fp, n, rfcp, fi.hdrs);
	}
	else if (strcmp(fi.entity, "HEADERS") == 0 ||
		 strcmp(fi.entity, "RAWHEADERS") == 0)
	{
		const rfc2045::entity *rfcp=&fetch_alloc_rfc2045(n, fp);

		if (fi.mimeid && *fi.mimeid)
		{
			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_hdrs(fp, n, *rfcp, fi.hdrs, fi.entity);
	}
	else if (strcmp(fi.entity, "BODY") == 0
		 || strcmp(fi.entity, "ALL") == 0)
	{
		const rfc2045::entity *rfcp=&fetch_alloc_rfc2045(n, fp);

		if (fi.mimeid && *fi.mimeid)
		{
			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_body(fp, n, *rfcp, fi.entity[0] == 'A');
	}
	else if (strcmp(fi.entity, "BODY.DECODED") == 0)
	{
		const rfc2045::entity *rfcp=&fetch_alloc_rfc2045(n, fp);

		if (fi.mimeid && *fi.mimeid)
		{
			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_decoded(fp, n, *rfcp);
	}
	else
	{
		rc=0;
	}

	if (rc == 0 && !fi.peek)
	{
		struct	imapflags	flags;

		get_message_flags(&current_maildir_info.msgs.at(n),
				  0, &flags);
		if (!flags.seen)
		{
			flags.seen=1;
			reflag_filename(&current_maildir_info.msgs[n],
					&flags, fp.fileno());
			current_maildir_info.msgs[n].changedflags=1;
		}
	}

	if (current_maildir_info.msgs[n].changedflags)
		fetchflags(n);

	return rc;
}

void smap_fetchflags(unsigned long n)
{
	int items=FETCH_FLAGS | FETCH_KEYWORDS;

	do_attrfetch(n, items);
}

static void do_attrfetch(unsigned long n, int items)
{
	if (n >= current_maildir_info.msgs.size())
		return;

	writes("* FETCH ");
	writen(n+1);

	if (items & FETCH_FLAGS)
	{
		char	buf[256];

		get_message_flags(&current_maildir_info.msgs.at(n), buf, 0);

		writes(" FLAGS=");
		writes(buf);

		current_maildir_info.msgs[n].changedflags=0;
	}

	if ((items & FETCH_KEYWORDS) && keywords())
	{
		writes(" \"KEYWORDS=");

		const char *p="";

		current_maildir_info.msgs[n].keywords.enumerate(
			[&]
			(const std::string &kw)
			{
				writes(p);
				p=",";
				writes(kw.c_str());
			}
		);
		writes("\"");
	}

	if (items & FETCH_UID)
	{
		writes(" \"UID=");

		auto &filename=current_maildir_info.msgs.at(n).filename;

		auto p=filename.rfind(MDIRSEP[0]);

		if (p != filename.npos)
			filename[p]=0;
		smapword_s(filename.c_str());
		if (p != filename.npos)
			filename[p]=MDIRSEP[0];
		writes("\"");
	}

	if (items & FETCH_SIZE)
	{
		auto &p=current_maildir_info.msgs.at(n).filename;
		unsigned long cnt;

		if (!maildir::parsequota(p, cnt))
		{
			rfc822::fdstreambuf &fp=open_cached_fp(n);
			struct stat stat_buf;

			if (fp.fileno() >= 0 && fstat(fp.fileno(), &stat_buf) == 0)
				cnt=stat_buf.st_size;
			else
				cnt=0;
		}

		writes(" SIZE=");
		writen(cnt);
	}

	if (items & FETCH_INTERNALDATE)
	{
		struct stat stat_buf;
		rfc822::fdstreambuf &fp=open_cached_fp(n);

		if (fp.fileno() >= 0 && fstat(fp.fileno(), &stat_buf) == 0)
		{
			char buf[256];

			rfc822_mkdate_buf(stat_buf.st_mtime, buf);
			writes(" \"INTERNALDATE=");
			smapword_s(buf);
			writes("\"");
		}
	}
	writes("\n");
}

static unsigned long add_msg(FILE *fp, const char *format,
			     char *buffer,
			     size_t bufsize)
{
	unsigned long n=0;

	writes("> Go ahead\n");
	writeflush();

	if (*format == '.')
	{
		int last_eol=1;
		int dot_stuffed=0;
		int counter=-1;

		for (;;)
		{
			char c;

			if ( ((counter=counter + 1 ) % 8192) == 0)
				read_timeout(60);

			c=READ();

			if (c == '\r')
				continue;

			if (dot_stuffed && c == '\n')
				break;
			dot_stuffed=0;

			if (c == '.')
			{
				if (last_eol)
				{
					dot_stuffed=1;
					continue;
				}
			}
			last_eol= c == '\n';
			putc( (int)(unsigned char)c, fp);
			n++;
		}

		if (!last_eol)
		{
			putc('\n', fp);
			n++;
		}
	}
	else
	{
		unsigned long chunkSize;
		char last_char='\n';

		while (sscanf(format, "%lu", &chunkSize) == 1)
		{
			while (chunkSize)
			{
				size_t nn=bufsize;
				size_t i;

				if (nn > chunkSize)
					nn=(size_t)chunkSize;

				read_timeout(60);
				nn=doread(buffer, nn);

				chunkSize -= nn;
				n += nn;

				for (i=0; i<nn; i++)
				{
					last_char=buffer[i];

					if (last_char == '\r')
						continue;
					putc((int)(unsigned char)last_char,
					     fp);
				}
			}

			read_timeout(60);
			smap_readline(buffer, bufsize);
			format=buffer;
		}

		if (last_char != '\n')
		{
			putc('\n', fp);
			n++;
		}
	}

	if (n == 0)
	{
		++n;
		putc('\n', fp);
	}

	if (fflush(fp) < 0 || ferror(fp))
		return 0;
	return n;
}

static void adduid(const std::string &newname)
{
	size_t b=newname.rfind('/');

	if (b == newname.npos)
		b=0;
	else ++b;

	size_t e=newname.find(MDIRSEP[0], b);

	if (e == newname.npos)
		e=newname.size();

	writes("* ADD \"UID=");
	smapword_s(std::string{newname.begin()+b, newname.begin()+e}.c_str());
	writes("\"\n");
}

/* Copy msg to another folder */

static void copieduid(unsigned long n, const std::string &newname)
{
	writes("* COPY ");
	writen(n);
	writes(" \"NEWUID=");

	size_t b=newname.rfind('/')+1;

	size_t e=newname.find(MDIRSEP[0], b);

	if (e == newname.npos)
		e=newname.size();

	smapword_s(std::string{newname.begin()+b, newname.begin()+e}.c_str());
	writes("\"\n");
}

static void do_copyKeywords(const mail::keywords::list &keywords,
			    const std::string &destmailbox,
			    const std::string &newname)
{
	if (keywords.empty())
		return;

	imapscan_updateKeywords(destmailbox, newname, keywords);
	return;
}

static void fixnewfilename(std::string &filename)
{
	/* Nice hack: */

	auto n=filename.rfind('/');

	n=filename.find(MDIRSEP[0], n);

	if (n < filename.size() &&
	    strcmp(filename.c_str() + n, MDIRSEP "2,") == 0)
	{
		filename=filename.substr(0, n);

		n=filename.rfind('/');

		static const char new_str[]="new";

		std::copy(new_str, new_str+3, filename.begin()+(n-3));
	}
}

static int do_copymsg(unsigned long n, void *voidptr)
{
	char buf[SMAP_BUFSIZ];
	struct copyquotainfo *cqinfo=(struct copyquotainfo *)voidptr;
	struct imapflags new_flags;
	int fd;
	struct stat stat_buf;
	FILE *fp;
	std::string tmpname, newname;

	fd=imapscan_openfile(&current_maildir_info, n);
	if (fd < 0)	return (0);

	if (fstat(fd, &stat_buf) < 0)
	{
		close(fd);
		return (0);
	}

	get_message_flags(&current_maildir_info.msgs.at(n), 0, &new_flags);

	fp=maildir_mkfilename(cqinfo->destmailbox.c_str(),
			      &new_flags, stat_buf.st_size,
			      tmpname, newname);

	fixnewfilename(newname);

	if (!fp)
	{
		close(fd);
		return (-1);
	}

	while (stat_buf.st_size)
	{
	int	n=sizeof(buf);

		if (n > stat_buf.st_size)
			n=stat_buf.st_size;

		n=read(fd, buf, n);

		if (n <= 0 || (int)fwrite(buf, 1, n, fp) != n)
		{
			close(fd);
			fclose(fp);
			unlink(tmpname.c_str());
			fprintf(stderr,
			"ERR: error copying a message, user=%s, errno=%d\n",
				getenv("AUTHENTICATED"), errno);

			return (-1);
		}
		stat_buf.st_size -= n;
	}
	close(fd);

	if (fflush(fp) || ferror(fp))
	{
		fclose(fp);
		unlink(tmpname.c_str());
		fprintf(stderr,
			"ERR: error copying a message, user=%s, errno=%d\n",
			getenv("AUTHENTICATED"), errno);
		return (-1);
	}
	fclose(fp);

	do_copyKeywords(current_maildir_info.msgs.at(n).keywords.keywords(),
			cqinfo->destmailbox,
			strrchr(newname.c_str(), '/')+1);

	current_maildir_info.msgs.at(n).copiedflag=1;

	maildir::movetmpnew(tmpname, newname);
	set_time(newname, stat_buf.st_mtime);

	copieduid(n+1, newname);
	return 0;
}

static int do_movemsg(unsigned long n, void *voidptr)
{
	struct copyquotainfo *cqinfo=(struct copyquotainfo *)voidptr;

	if (n >= current_maildir_info.msgs.size())
		return 0;

	auto filename=maildir::filename(
		current_maildir_info.current_mailbox,
		"",
		current_maildir_info.msgs.at(n).filename);

	if (filename.empty())
		return 0;

	std::string newfilename;

	newfilename.reserve(cqinfo->destmailbox.size() + sizeof("/cur")-1
			    + (filename.size()-filename.rfind('/')));

	newfilename=cqinfo->destmailbox;

	newfilename += "/cur";
	newfilename.insert(newfilename.end(),
			   filename.begin() + filename.rfind('/'),
			   filename.end());

	do_copyKeywords(current_maildir_info.msgs.at(n).keywords.keywords(),
			cqinfo->destmailbox,
			strrchr(newfilename.c_str(), '/')+1);

	if (maildir::movetmpnew(filename, newfilename) == 0)
	{
		copieduid(n+1, newfilename);
		return 0;
	}

	if (do_copymsg(n, voidptr))
		return -1;

	unlink(filename.c_str());
	return 0;
}

static searchiter createSearch2(char *w, contentsearch &cs, char **ptr);

static searchiter createSearch(contentsearch &cs, char **ptr)
{
	char *w=getword(ptr);
	searchiter siAnd, n;

	up(w);

	if (strcmp(w, "MARKED") == 0)
	{
		w=getword(ptr);
		up(w);

		n=createSearch2(w, cs, ptr);

		if (n == cs.searchlist.end())
			return n;

		siAnd=cs.alloc_search();
		siAnd->type=search_and;

		siAnd->b=n;

		siAnd->a=n=cs.alloc_search();

		n->type=search_msgflag;
		n->as="\\FLAGGED";

		return siAnd;
	}

	if (strcmp(w, "UNMARKED") == 0)
	{
		w=getword(ptr);
		up(w);

		n=createSearch2(w, cs, ptr);

		if (n == cs.searchlist.end())
			return n;

		siAnd=cs.alloc_search();
		siAnd->type=search_and;

		siAnd->b=n;

		siAnd->a=n=cs.alloc_search();

		n->type=search_not;

		n=n->a=cs.alloc_search();

		n->type=search_msgflag;
		n->as="\\FLAGGED";

		return siAnd;
	}

	if (strcmp(w, "ALL") == 0)
	{
		w=getword(ptr);
		up(w);
		return createSearch2(w, cs, ptr);
	}

	{
		char *ww=getword(ptr);
		up(ww);
		n=createSearch2(ww, cs, ptr);

		if (n == cs.searchlist.end())
			return n;

		siAnd=cs.alloc_search();
		siAnd->type=search_and;

		siAnd->b=n;

		siAnd->a=n=cs.alloc_search();

		n->type=search_messageset;
		n->as=w;

		for (auto &c:n->as)
			if (c == '-')
				c=':';

		if (!ismsgset_str(n->as))
		{
			errno=EINVAL;
			return cs.searchlist.end();
		}
	}
	return siAnd;
}

static searchiter createSearch2(char *w, contentsearch &cs,
				char **ptr)
{
	int notflag=0;
	searchiter n;

	if (strcmp(w, "NOT") == 0)
	{
		notflag=1;
		w=getword(ptr);
		up(w);
	}

	if (strcmp(w, "REPLIED") == 0)
	{
		n=cs.alloc_search();
		n->type=search_msgflag;
		n->as="\\ANSWERED";
	}
	else if (strcmp(w, "DELETED") == 0)
	{
		n=cs.alloc_search();
		n->type=search_msgflag;
		n->as="\\DELETED";
	}
	else if (strcmp(w, "DRAFT") == 0)
	{
		n=cs.alloc_search();
		n->type=search_msgflag;
		n->as="\\DRAFT";
	}
	else if (strcmp(w, "SEEN") == 0)
	{
		n=cs.alloc_search();
		n->type=search_msgflag;
		n->as="\\SEEN";
	}
	else if (strcmp(w, "KEYWORD") == 0)
	{
		n=cs.alloc_search();
		n->type=search_msgkeyword;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "FROM") == 0 ||
		 strcmp(w, "TO") == 0 ||
		 strcmp(w, "CC") == 0 ||
		 strcmp(w, "BCC") == 0 ||
		 strcmp(w, "SUBJECT") == 0)
	{
		n=cs.alloc_search();
		n->type=search_header;
		n->cs=w;
		for (auto &c:n->cs)
		{
			c += 'a'-'A';
		}
		n->as=getword(ptr);
	}
	else if (strcmp(w, "HEADER") == 0)
	{
		n=cs.alloc_search();
		n->type=search_header;
		n->cs=getword(ptr);
		for (auto &c:n->cs)
		{
			c += 'a'-'A';
		}
		n->as=getword(ptr);
	}
	else if (strcmp(w, "BODY") == 0)
	{
		n=cs.alloc_search();
		n->type=search_body;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "TEXT") == 0)
	{
		n=cs.alloc_search();
		n->type=search_text;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "BEFORE") == 0)
	{
		n=cs.alloc_search();
		n->type=search_before;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "ON") == 0)
	{
		n=cs.alloc_search();
		n->type=search_on;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "SINCE") == 0)
	{
		n=cs.alloc_search();
		n->type=search_since;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "SENTBEFORE") == 0)
	{
		n=cs.alloc_search();
		n->type=search_sentbefore;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "SENTON") == 0)
	{
		n=cs.alloc_search();
		n->type=search_senton;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "SINCE") == 0)
	{
		n=cs.alloc_search();
		n->type=search_sentsince;
		n->as=getword(ptr);
	}
	else if (strcmp(w, "SMALLER") == 0)
	{
		n=cs.alloc_search();
		n->type=search_smaller;
		n->as=getword(ptr);

	}
	else if (strcmp(w, "LARGER") == 0)
	{
		n=cs.alloc_search();
		n->type=search_larger;
		n->as=getword(ptr);
	}
	else
	{
		errno=EINVAL;
		return cs.searchlist.end();
	}

	if (notflag)
	{
		searchiter p=cs.alloc_search();

		p->type=search_not;
		p->a=n;
		n=p;
	}
	return n;
}

static int do_copyto(const smapmsgset_t &msgset,
		     const char *toFolder,
		     int (*do_func)(unsigned long, void *),
		     const char *acls)
{
	int has_quota=0;
	acl_check_rights rights{acls};
	copyquotainfo cqinfo{toFolder, rights};
	struct maildirsize quotainfo;

	if (maildirquota_countfolder(toFolder))
	{
		if (maildir_openquotafile(&quotainfo, ".") == 0)
		{
			if (quotainfo.fd >= 0)
				has_quota=1;
			maildir_closequotafile(&quotainfo);
		}

		if (has_quota > 0 && applymsgset(
			    msgset,
			    [&]
			    (unsigned long n)
			    {
				    return do_copy_quota_calc(n+1, 0, &cqinfo);
			    }))
			has_quota= -1;
	}

	if (has_quota > 0 && cqinfo.nfiles > 0)
	{
		if (maildir_quota_add_start(".", &quotainfo,
					    cqinfo.nbytes,
					    cqinfo.nfiles,
					    getenv("MAILDIRQUOTA")))
		{
			errno=ENOSPC;
			return (-1);
		}

		maildir_quota_add_end(&quotainfo,
				      cqinfo.nbytes,
				      cqinfo.nfiles);
	}

	return applymsgset(
		msgset,
		[&]
		(unsigned long n)
		{
			return do_func(n, &cqinfo);
		});
}

static int copyto(const smapmsgset_t &msgset,
		  const char *toFolder, int do_move, const char *acls)
{
	if (!do_move)
		return do_copyto(msgset, toFolder, &do_copymsg, acls);

	if (!current_mailbox_shared &&
	    maildirquota_countfolder(current_maildir_info.current_mailbox.c_str()) ==
	    maildirquota_countfolder(toFolder))
	{
		if (do_copyto(msgset, toFolder, do_movemsg, acls))
			return -1;

		doNoop(0);
		return(0);
	}

	if (do_copyto(msgset, toFolder, &do_copymsg, acls))
		return -1;

	applymsgset(
		msgset,
		[]
		(unsigned long n)
		{
			msg_expunge(n);
			return 0;
		});
	doNoop(0);
	return 0;
}

struct smap1_search_results {

	unsigned prev_runs;

	unsigned long prev_search_hit;
	unsigned long prev_search_hit_start;
};

static void smap1_search_cb_range(smap1_search_results &searchResults)
{
	if (searchResults.prev_runs > 100)
	{
		writes("\n");
		searchResults.prev_runs=0;
	}

	if (searchResults.prev_runs == 0)
		writes("* SEARCH");

	writes(" ");
	writen(searchResults.prev_search_hit_start);
	if (searchResults.prev_search_hit_start !=
	    searchResults.prev_search_hit)
	{
		writes("-");
		writen(searchResults.prev_search_hit);
	}
	++searchResults.prev_runs;
}

static void smap1_search_cb(unsigned long i,
			    smap1_search_results &searchResults)
{
	++i;

	if (searchResults.prev_search_hit == 0)
	{
		searchResults.prev_search_hit=
			searchResults.prev_search_hit_start=i;
		return;
	}

	if (i != searchResults.prev_search_hit+1)
	{
		smap1_search_cb_range(searchResults);
		searchResults.prev_search_hit_start=i;
	}

	searchResults.prev_search_hit=i;
}

static void accessdenied(const char *acl_required)
{
	writes("-ERR Access denied: ACL \"");
	writes(acl_required);
	writes("\" is required\n");
}

static int getacl(const char *ident,
		  const char *acl,
		  void *cb_arg)
{
	int *n=(int *)cb_arg;

	if (*n > 5)
	{
		writes("\n");
		*n=0;
	}

	if (*n == 0)
		writes("* GETACL");

	writes(" ");
	smapword(ident);
	writes(" ");
	smapword(acl);
	++*n;
	return 0;
}

struct setacl_info {
	maildir::info minfo;
	char **ptr;

	setacl_info(const smap_words_t &fn, char **ptr)
		: minfo{
				maildir::info_smap_find(
					fn, getenv("AUTHENTICATED")
				)
			}, ptr{ptr}
	{
	}
};

static void dosetdeleteacl(setacl_info &sainfo, bool dodelete)
{
	int cnt;
	const char *identifier;
	const char *action;

	maildir::aclt_list aclt_list;

	if (!read_acls(aclt_list, sainfo.minfo))
	{
		writes("-ERR Unable to read existing ACLS: ");
		writes(strerror(errno));
		writes("\n");
		return;
	}

	auto q=compute_myrights(aclt_list,
				sainfo.minfo.owner);

	if (q.find(ACL_ADMINISTER[0]) == q.npos)
	{
		accessdenied(ACL_ADMINISTER);
		return;
	}

	while (*(identifier=getword(sainfo.ptr)))
	{
		if (dodelete)
		{
			aclt_list.del(identifier);
			continue;
		}

		action=getword(sainfo.ptr);

		if (*action == '+')
		{
			maildir::aclt newacl{action+1};

			auto iter=aclt_list.lookup(identifier);

			if (iter != aclt_list.end())
			{
				newacl += iter->acl;
			}

			aclt_list.add(identifier, newacl);
			continue;
		}

		if (*action == '-')
		{
			auto iter=aclt_list.lookup(identifier);

			if (iter == aclt_list.end())
				continue;

			maildir::aclt newacl{iter->acl};

			newacl -= action+1;

			if (newacl.empty())
				aclt_list.del(identifier);
			else
				aclt_list.add(identifier, newacl);

			continue;
		}

		if (strlen(action) == 0)
		{
			aclt_list.del(identifier);
		}
		else
		{
			aclt_list.add(identifier, action);
		}
	}

	auto path=maildir::name2dir(".", sainfo.minfo.maildir);

	std::string err_failedrights;

	if (!path.empty() &&
	    aclt_list.write(sainfo.minfo.homedir,
			    path.substr(0, 2) == "./"
			    ? path.substr(2):path,
			    sainfo.minfo.owner,
			    err_failedrights))
	{
		if (!err_failedrights.empty())
		{
			writes("* ACLMINIMUM ");
			writes(err_failedrights.c_str());
			writes(" ");
			aclminimum(err_failedrights);
			writes("\n");
		}
		writes("-ERR ACL update failed\n");
		return;
	}

	cnt=0;
	for (const auto &acl:aclt_list)
	{
		getacl(acl.identifier.c_str(),
		       acl.acl.c_str(), &cnt);
	}
	if (cnt)
		writes("\n");

	/* Reread ACLs if the current mailbox's ACLs have changed */

	if (!read_acls(aclt_list, sainfo.minfo))
	{
		writes("-ERR Unable to re-read ACLS: ");
		writes(strerror(errno));
		writes("\n");
		return;
	}

	writes("+OK Updated ACLs\n");
	return;
}

static maildir::info checkacl(const smap_words_t &folder,
			      const char *acls)
{
	auto minfo=maildir::info_smap_find(folder, getenv("AUTHENTICATED"));

	if (!minfo)
		return minfo;

	maildir::aclt_list aclt_list;

	if (!read_acls(aclt_list, minfo))
	{
		return {};
	}

	auto q=compute_myrights(aclt_list, minfo.owner);

	while (*acls)
	{
		if (q.find(*acls) == q.npos)
		{
			return {};
		}
		++acls;
	}
	return minfo;
}

void smap()
{
	char buffer[8192];
	char *ptr;
	imapflags add_flags;
	int in_add=0;
	std::string add_from;
	std::string add_notify;
	std::string add_folder;
	time_t add_internaldate=0;
	mail::keywords::list addKeywords;

	std::vector<std::string> rcptlist;

	char rights_buf[40];

	enabled_utf8=1;

#define GETFOLDER(acl) ( strcpy(rights_buf, (acl)), \
			getAccessToFolder(&ptr, rights_buf))

	for (;;)
	{
		char *p;

		writeflush();
		read_timeout(30 * 60);
		smap_readline(buffer, sizeof(buffer));

		ptr=buffer;

		p=getword(&ptr);
		up(p);

		if (strcmp(p, "ADD") == 0)
		{
			const char *okmsg="So far, so good...";
			int err_sent=0;

			in_add=1;
			while (*(p=getword(&ptr)))
			{
				char *q=strchr(p, '=');
				char *comma=q;

				if (q)
					*q++=0;
				up(p);

				if (strcmp(p, "FOLDER") == 0)
				{
					add_folder=
						GETFOLDER(ACL_INSERT
							  ACL_DELETEMSGS
							  ACL_SEEN
							  ACL_WRITE);
					if (add_folder.empty())
					{
						writes("-ERR Invalid folder: ");
						writes(strerror(errno));
						writes("\n");
						break;
					}

					if (strchr(rights_buf,
						   ACL_INSERT[0])
					    == NULL)
					{
						accessdenied(ACL_INSERT);
						add_folder.clear();
						break;
					}

					okmsg="Will add to this folder";
				}

				if (strcmp(p, "MAILFROM") == 0 && q)
				{
					add_from=q;

				}

				if (strcmp(p, "NOTIFY") == 0 && q)
				{
					add_notify=q;
					okmsg="NOTIFY set";
				}

				if (strcmp(p, "RCPTTO") == 0 && q)
				{
					rcptlist.emplace_back(q);
					okmsg="RCPT TO set";
				}

				if (strcmp(p, "FLAGS") == 0 && q)
				{
					add_flags={};

					*(q=comma)='=';
					parseflags(q, &add_flags);

					if (strchr(rights_buf,
						   ACL_SEEN[0])
					    == NULL)
						add_flags.seen=false;
					if (strchr(rights_buf,
						   ACL_DELETEMSGS[0])
					    == NULL)
						add_flags.deleted=false;
					if (strchr(rights_buf,
						   ACL_WRITE[0])
					    == NULL)
						add_flags.answered=
							add_flags.flagged=
							add_flags.drafts=false;

					okmsg="FLAGS set";
				}

				if (strcmp(p, "KEYWORDS") == 0 && q &&
				    keywords() && strchr(rights_buf,
							 ACL_WRITE[0]))
				{
					addKeywords.clear();

					*(q=comma)='=';
					parsekeywords(q, addKeywords);
					okmsg="KEYWORDS set";
				}

				if (strcmp(p, "INTERNALDATE") == 0 && q)
				{
					if (rfc822_parsedate_chk(q,
								 &add_internaldate)
					    == 0)
						okmsg="INTERNALDATE set";
				}

				if (p[0] == '{')
				{
					std::string tmpname, newname;
					char *s;
					char *tmpKeywords=NULL;
					char *newKeywords=NULL;
					FILE *fp;
					unsigned long n;

					std::string dest_folder=
						add_folder.empty()
						? "." : add_folder.c_str();

					fp=maildir_mkfilename(
						dest_folder.c_str(),
						&add_flags,
						0,
						tmpname,
						newname);

					if (!fp)
					{
						writes("-ERR ");
						writes(strerror(errno));
						writes("\n");
						break;
					}

					fixnewfilename(newname);

					n=add_msg(fp, p+1, buffer,
						  sizeof(buffer));

					if (n)
					{
						s=maildir_requota(
							newname.c_str(),
							n);

						if (!s)
							n=0;
						else
						{
							newname=s;
							free(s);
						}
					}

					if (n > 0 && !add_folder.empty() &&
					    maildirquota_countfolder(
						    add_folder.c_str()
					    )
					    && maildirquota_countfile(
						    newname.c_str()))
					{
						struct maildirsize quotainfo;

						if (maildir_quota_add_start(
							    add_folder.c_str(),
							    &quotainfo, n, 1,
							    getenv("MAILDIRQUOTA")))
						{
							errno=ENOSPC;
							n=0;
						}
						else
							maildir_quota_add_end(&quotainfo, n, 1);
					}

					fclose(fp);

					chmod(tmpname.c_str(), 0600);

					if (!add_folder.empty() && n
					    && !addKeywords.empty())
					{
						imapscan_updateKeywords(
							add_folder,
							strrchr(newname.c_str(), '/')+1,
							addKeywords);
					}

					std::list<std::string> argvec;

					const char arg1[]="-oi";
					argvec.emplace_back(std::begin(arg1),
							    std::end(arg1));

					const char arg2[]="-f";

					argvec.emplace_back(std::begin(arg2),
							    std::end(arg2));

					std::string defaulted_add_from=
						add_from;

					if (add_from.empty())
						defaulted_add_from=
							defaultSendFrom();

					argvec.emplace_back(
						defaulted_add_from.c_str(),
						defaulted_add_from.c_str()+
						defaulted_add_from.size()+1);

					if (!add_notify.empty())
					{
						const char arg3[]="-N";

						argvec.emplace_back(
							std::begin(arg3),
							std::end(arg3));

						argvec.emplace_back(
							add_notify.c_str(),
							add_notify.c_str()+
							add_notify.size()+1);
					}

					for (const auto &addr:rcptlist)
					{
						argvec.emplace_back(
							addr.c_str(),
							addr.c_str()+
							addr.size()+1);
					}

					if (!rcptlist.empty())
					{
						std::vector<char *> ptrs;

						ptrs.reserve(argvec.size()+2);
						ptrs.push_back(0);

						for (auto &arg:argvec)
						{
							ptrs.push_back(
								arg.data()
							);
						}

						ptrs.push_back(0);

						auto i=imapd_sendmsg(
							tmpname.c_str(),
							&ptrs[0],
							[]
							(const std::string &m)
							{
								writes("-ERR ");
								writes(m.c_str()
								);
								writes("\n");
							});

						if (i)
						{
							n=0;
							err_sent=1;
						}
					}

					if (tmpKeywords)
					{
						rename(tmpKeywords,
						       newKeywords);
						free(tmpKeywords);
						free(newKeywords);
					}

					if (!add_folder.empty() && n)
					{
						if (maildir::movetmpnew(
							    tmpname,
							    newname)
						    )
							n=0;
						else
						{
							if (add_internaldate)
								set_time(newname,
									 add_internaldate);
							adduid(newname);
						}
					}

					if (n == 0)
					{
						unlink(tmpname.c_str());
						if (!err_sent)
						{
							writes("-ERR ");
							writes(strerror(errno));
							writes("\n");
						}
						break;
					}

					unlink(tmpname.c_str());
					okmsg="Message saved";
					p=NULL;
					dirsync(dest_folder);
					break;
				}
			}

			if (p && *p)
				continue; /* Error inside the loop */

			writes("+OK ");
			writes(okmsg);
			writes("\n");

			if (p)
				continue;
		}

		if (in_add)
		{
			add_flags={};
			add_folder.clear();

			addKeywords.clear();

			in_add=0;
			add_from.clear();
			add_internaldate=0;
			add_notify.clear();
			rcptlist.clear();
			if (!p)
				continue; /* Just added a message */
		}

		if (strcmp(p, "LOGOUT") == 0)
			break;

		if (strcmp(p, "RSET") == 0)
		{
			writes("+OK Reset\n");
			continue;
		}

		if (strcmp(p, "GETACL") == 0 ||
		    strcmp(p, "ACL") == 0)
		{
			auto fn=fn_fromwords(&ptr);
			int cnt;

			auto minfo=maildir::info_smap_find(
				fn,
				getenv("AUTHENTICATED"));

			if (!minfo)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			maildir::aclt_list aclt_list;

			if (!read_acls(aclt_list, minfo))
			{
				writes("-ERR Unable to read"
				       " existing ACLS: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			auto q=compute_myrights(aclt_list,
						minfo.owner);

			if (strcmp(p, "ACL") ?
			    q.find(ACL_ADMINISTER[0]) == q.npos
			    :
			    !maildir_acl_canlistrights(q.c_str()))
			{
				accessdenied(ACL_ADMINISTER);
				continue;
			}
			if (strcmp(p, "ACL") == 0)
			{
				writes("* ACL ");
				smapword(q.c_str());
				writes("\n");
			}
			else
			{
				cnt=0;

				for (const auto &acl:aclt_list)
				{
					getacl(acl.identifier.c_str(),
					       acl.acl.c_str(), &cnt);
				}
				if (cnt)
					writes("\n");
			}
			writes("+OK ACLs retrieved\n");
			continue;
		}

		if (strcmp(p, "SETACL") == 0 ||
		    strcmp(p, "DELETEACL") == 0)
		{
			auto fn=fn_fromwords(&ptr);

			setacl_info sainfo{fn, &ptr};

			if (!sainfo.minfo)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			acl_lock(sainfo.minfo.homedir,
				 [&]
				 {
					 dosetdeleteacl(
						 sainfo,
						 *p == 'S' ? false:true
					 );
					 return true;
				 });
			continue;
		}

		if (strcmp(p, "LIST") == 0)
		{
			listcmd(&ptr);
			continue;
		}

		if (strcmp(p, "STATUS") == 0)
		{
			getword(&ptr);

			auto t=GETFOLDER(ACL_LOOKUP);

			if (t.empty())
			{
				writes("-ERR Cannot read folder status: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			if (strchr(rights_buf, ACL_LOOKUP[0]) == NULL)
			{
				accessdenied(ACL_LOOKUP);
				continue;
			}

			imapscaninfo loaded_info{t},
				*infoptr;

			if (t == current_maildir_info.current_mailbox)
			{
				infoptr= &current_maildir_info;
			}
			else
			{
				infoptr=&loaded_info;

				if (imapscan_maildir(infoptr, 1, 1))
				{
					writes("-ERR Cannot read"
					       " folder status: ");
					writes(strerror(errno));
					writes("\n");
					continue;
				}
			}

			writes("* STATUS EXISTS=");
			writen(infoptr->msgs.size()+infoptr->left_unseen);

			writes(" UNSEEN=");
			writen(infoptr->unseen());
			writes("\n+OK Folder status retrieved\n");
			continue;
		}

		if (strcmp(p, "CREATE") == 0)
		{
			strcpy(rights_buf, ACL_CREATE);

			auto t=getCreateFolder(&ptr, rights_buf);

			if (!t.empty())
			{
				if (mdcreate(t.c_str()))
				{
					writes("-ERR Cannot create folder: ");
					writes(strerror(errno));
					writes("\n");
				}
				else
				{
					writes("+OK Folder created\n");
				}
			}
			else
			{
				if (rights_buf[0] == 0)
					accessdenied(ACL_CREATE);
				else
				{
					writes("-ERR Cannot create folder: ");
					writes(strerror(errno));
					writes("\n");
				}
			}
			continue;
		}

		if (strcmp(p, "MKDIR") == 0)
		{
			strcpy(rights_buf, ACL_CREATE);
			auto t=getCreateFolder(&ptr, rights_buf);

			if (!t.empty())
			{
				writes("+OK Folder created\n");
			}
			else if (rights_buf[0] == 0)
				accessdenied(ACL_CREATE);
			else
			{
				writes("-ERR Cannot create folder: ");
				writes(strerror(errno));
				writes("\n");
			}
			continue;
		}

		if (strcmp(p, "RMDIR") == 0)
		{
			strcpy(rights_buf, ACL_DELETEFOLDER);
			auto t=getCreateFolder(&ptr, rights_buf);

			if (!t.empty())
			{
				writes("+OK Folder deleted\n");
			}
			else if (rights_buf[0] == 0)
				accessdenied(ACL_DELETEFOLDER);
			else
			{
				writes("-ERR Cannot create folder: ");
				writes(strerror(errno));
				writes("\n");
			}
			continue;
		}

		if (strcmp(p, "DELETE") == 0)
		{
			std::string t;

			auto fn=fn_fromwords(&ptr);

			auto minfo=maildir::info_smap_find(
				fn, getenv("AUTHENTICATED"));

			if (minfo)
			{
				if (!minfo.homedir.empty() &&
				    !minfo.maildir.empty())
				{
					maildir::aclt_list list;

					if (minfo.maildir == INBOX)
					{
						writes("-ERR INBOX may"
						       " not be"
						       " deleted\n");
						continue;
					}

					if (!read_acls(list, minfo))
					{
						accessdenied(ACL_DELETEFOLDER);
						continue;
					}

					auto q=compute_myrights(
						list,
						minfo.owner);

					if (q.find(ACL_DELETEFOLDER[0])
					    == q.npos)
					{
						accessdenied(
							ACL_DELETEFOLDER
						);
						continue;
					}
					t=maildir::name2dir(
						minfo.homedir,
						minfo.maildir
					);
				}
			}

			if (!t.empty() &&
			    t == current_maildir_info.current_mailbox)
			{
				writes("-ERR Cannot DELETE currently open"
				       " folder.\n");
				continue;
			}


			if (!t.empty())
			{
				if (mddelete(t.c_str()))
				{
					maildir_quota_recalculate(".");
					writes("+OK Folder deleted");
				}
				else
				{
					writes("-ERR Cannot delete folder: ");
					writes(strerror(errno));
				}
				writes("\n");
			}
			else
			{
				writes("-ERR Unable to delete folder: ");
				writes(strerror(errno));
				writes("\n");
			}
			continue;
		}

		if (strcmp(p, "RENAME") == 0)
		{
			const char *errmsg;

			auto fnsrc=fn_fromwords(&ptr);
			auto fndst=fn_fromwords(&ptr);

			if (fndst.size() == 0)
			{
				writes("-ERR Invalid destination folder name\n");
				continue;
			}

			auto msrc=checkacl(fnsrc, ACL_DELETEFOLDER);

			if (!msrc)
			{
				accessdenied(ACL_DELETEFOLDER);
				continue;
			}
			auto save=fndst.back();
			fndst.pop_back();

			auto mdst=checkacl(fndst, ACL_CREATE);

			fndst.push_back(save);

			if (!mdst)
			{
				accessdenied(ACL_CREATE);
				continue;
			}

			mdst=maildir::info_smap_find(fndst,
						     getenv("AUTHENTICATED"));

			if (!mdst)
			{
				writes("-ERR Internal error in RENAME: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			errmsg=folder_rename(msrc, mdst);

			if (errmsg)
			{
				writes("-ERR ");
				writes(*errmsg == '@' ? errmsg+1:errmsg);
				if (*errmsg == '@')
					writes(strerror(errno));
				writes("\n");
			}
			else
			{
				writes("+OK Folder renamed.\n");
			}
			continue;
		}

		if (strcmp(p, "OPEN") == 0 ||
		    strcmp(p, "SOPEN") == 0)
		{
			const char *snapshot=0;

			current_maildir_info=imapscaninfo{""};
			current_mailbox_shared=0;

			fetch_free_cache();

			if (p[0] == 'S')
				snapshot=getword(&ptr);

			auto fn=fn_fromwords(&ptr);

			auto minfo=maildir::info_smap_find(
				fn,
				getenv("AUTHENTICATED"));

			if (!minfo)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			maildir::aclt_list aclt_list;

			if (!read_acls(aclt_list, minfo))
			{
				writes("-ERR Unable to read"
				       " existing ACLS: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			auto q=compute_myrights(aclt_list, minfo.owner);

			if (q.find(ACL_READ[0]) == q.npos)
			{
				accessdenied(ACL_READ);
				continue;
			}
			current_maildir_info=imapscaninfo{
				maildir::name2dir(minfo.homedir,
						  minfo.maildir)
			};
			current_maildir_info.current_mailbox_acl=q;

			snapshot_select(snapshot != NULL);

			if (snapshot_init(
				    current_maildir_info.current_mailbox.c_str()
				    , snapshot))
			{
				writes("* SNAPSHOTEXISTS ");
				smapword(snapshot);
				writes("\n");
				smap1_noop(0);
				continue;
			}

			if (imapscan_maildir(&current_maildir_info, 0, 0)
			    == 0)
			{
				snapshot_init(
					current_maildir_info.current_mailbox
					.c_str(), NULL);

				writes("* EXISTS ");
				writen(current_maildir_info.msgs.size());
				writes("\n+OK Folder opened\n");
				continue;
			}

			writes("-ERR Cannot open the folder: ");
			writes(strerror(errno));
			writes("\n");

			current_maildir_info=imapscaninfo{""};
			continue;
		}

		if (strcmp(p, "CLOSE") == 0)
		{
			current_maildir_info=imapscaninfo{""};
			writes("+OK Folder closed\n");
			continue;
		}

		if (strcmp(p, "NOOP") == 0)
		{
			smap1_noop(1);
			continue;
		}

		if (strcmp(p, "IDLE") == 0)
		{
			imapenhancedidle();

			read_timeout(60);
			smap_readline(buffer, sizeof(buffer));
			ptr=buffer;
			p=getword(&ptr);
			up(p);
			if (strcmp(p, "RESUME"))
			{
				writes("-ERR RESUME is required to follow IDLE\n");
			}
			else
				writes("+OK Resumed...\n");
			continue;
		}

		if (current_maildir_info.current_mailbox.empty())
		{
			static char null[]="";
			// TODO
			p=null; /* FALLTHROUGH */
		}

		if (strcmp(p, "EXPUNGE") == 0)
		{
			smapmsgset_t msgset;

			p=markmsgset(&ptr, msgset);

			if (p)
			{
				if (!current_maildir_info.has_acl(
					    ACL_EXPUNGE[0]
				    ))
				{
					accessdenied(ACL_EXPUNGE);
					continue;
				}

				if (!msgset.empty())
				{
					if (!current_maildir_info.has_acl(
						    ACL_DELETEMSGS[0]
					    ))
					{
						accessdenied(ACL_DELETEMSGS);
						continue;
					}
					applymsgset(
						msgset,
						[]
						(unsigned long n)
						{
							msg_expunge(n);
							return 0;
						});
				}
				else
					expunge();
				smap1_noop(0);
				continue;
			}
		}

		if (strcmp(p, "STORE") == 0)
		{
			struct storeinfo si;
			int dummy;
			smapmsgset_t msgset;

			p=markmsgset(&ptr, msgset);

			dummy=0;

			if (!p)
				dummy=1;

			while (p && *p)
			{
				char *q=strchr(p, '=');

				if (q)
					*q=0;
				up(p);
				/* Uppercase only the keyword, for now */
				if (q)
					*q='=';

				if (strncmp(p, "FLAGS=", 6) == 0)
				{
					up(p);
					parseflags(p, &si.flags);
					if ((dummy=applymsgset(
						     msgset,
						     [&]
						     (unsigned long n)
						     {
							     return applyflags(
								     n, &si,
								     false
							     );
						     })) != 0)
						break;
				}
				else if ((*p ==
					  static_cast<char>(plusminus_t::plus)
					  || *p ==
					  static_cast<char>(plusminus_t::minus)
					 ) && strncmp(p+1, "FLAGS=", 6) == 0)
				{
					up(p);
					si.plusminus=static_cast<plusminus_t>(
						p[0]
					);
					parseflags(p, &si.flags);
					if ((dummy=applymsgset(
						     msgset,
						     [&]
						     (unsigned long n)
						     {
							     return applyflags(
								     n, &si,
								     false
							     );
						     })) != 0)
						break;
				}
				else if (strncmp(p, "KEYWORDS=", 9) == 0 &&
					 keywords())
				{
					parsekeywords(p, si.keywords);
					dummy=applymsgset(
						msgset,
						[&]
						(unsigned long n)
						{
							return applyflags(
								n, &si, true
							);
						});

					if (dummy != 0)
						break;
				}
				else if ((*p ==
					  static_cast<char>(plusminus_t::plus)
					  || *p ==
					  static_cast<char>(plusminus_t::minus)
					 ) && strncmp(p+1, "KEYWORDS=", 9) == 0)
				{
					si.plusminus=static_cast<plusminus_t>(
						p[0]
					);
					parsekeywords(p, si.keywords);
					dummy=applymsgset(
						msgset,
						[&]
						(unsigned long n)
						{
							return applyflags(
								n, &si, true
							);
						});
				}
				else if (strncmp(p, "INTERNALDATE=", 13) == 0)
				{
					time_t t;

					up(p);

					if (rfc822_parsedate_chk(p+13, &t)
					    == 0 &&
					    (dummy=applymsgset(
						    msgset,
						    [&]
						    (unsigned long n)
						    {
							    setdate(n, t);
							    return 0;
						    }))
					    != 0)
						break;
				}

				p=getword(&ptr);
			}
			if (dummy)
			{
				writes("-ERR Cannot update folder status: ");
				writes(strerror(errno));
				writes("\n");
			}
			else
				writes("+OK Folder status updated\n");
			continue;
		}

		if (strcmp(p, "FETCH") == 0)
		{
			smapmsgset_t msgset;
			struct smapfetchinfo fi;
			int fetch_items=0;

			for (p=markmsgset(&ptr, msgset);
			     p && *p; p=getword(&ptr))
			{
				if ((fi.entity=strchr(p, '=')) == NULL)
				{
					up(p);

					if (strcmp(p, "UID") == 0)
						fetch_items |= FETCH_UID;
					if (strcmp(p, "SIZE") == 0)
						fetch_items |= FETCH_SIZE;
					if (strcmp(p, "FLAGS") == 0)
						fetch_items |= FETCH_FLAGS;
					if (strcmp(p, "KEYWORDS") == 0)
						fetch_items |= FETCH_KEYWORDS;
					if (strcmp(p, "INTERNALDATE") == 0)
						fetch_items
							|= FETCH_INTERNALDATE;
					continue;
				}

				*fi.entity++=0;

				fi.hdrs=strrchr(fi.entity, '(');
				if (fi.hdrs)
				{
					char *q;

					*fi.hdrs++=0;

					q=strrchr(fi.hdrs, ')');
					if (q)
						*q=0;
					up(fi.hdrs);
				}

				fi.mimeid=strrchr(fi.entity, '[');
				if (fi.mimeid)
				{
					char *q;

					*fi.mimeid++=0;
					q=strrchr(fi.mimeid, ']');
					if (q)
						*q=0;
				}

				up(p);

				if (strcmp(p, "CONTENTS") == 0 ||
				    strcmp(p, "CONTENTS.PEEK") == 0)
				{
					fi.peek=strchr(p, '.') != NULL;
					if (applymsgset(
						    msgset,
						    [&]
						    (unsigned long n)
						    {
							    return do_fetch(
								    n, fi
							    );
						    }) == 0)
					{
						continue;
					}
				}
				else
				{
					continue;
				}

				writes("-ERR Cannot retrieve message: ");
				writes(strerror(errno));
				writes("\n");
				break;
			}

			if (!p || !*p)
			{
				if (fetch_items && applymsgset(
					    msgset,
					    [&]
					    (unsigned long n)
					    {
						    do_attrfetch(
							    n, fetch_items);
						    return 0;
					    }))
				{
					writes("-ERR Cannot retrieve message: ");
					writes(strerror(errno));
					writes("\n");
				}
				else
					writes("+OK Message retrieved.\n");
			}
			continue;
		}

		if (strcmp(p, "COPY") == 0
		    || strcmp(p, "MOVE") == 0)
		{
			smapmsgset_t msgset;
			int domove= *p == 'M';

			p=markmsgset(&ptr, msgset);

			if (!msgset.empty() && *p == 0)
			{
				auto p=GETFOLDER(ACL_INSERT
						 ACL_DELETEMSGS
						 ACL_SEEN
						 ACL_WRITE);

				if (!p.empty())
				{
					if (strchr(rights_buf, ACL_INSERT[0])
					    == NULL)
					{
						accessdenied(ACL_INSERT);
						continue;
					}

					if (copyto(msgset,
						   p.c_str(), domove,
						   rights_buf) == 0)
					{
						dirsync(p);
						writes("+OK Messages copied.\n"
						       );
						continue;
					}
				}

				writes("-ERR Cannot copy messages: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}
			writes("-ERR Syntax error.\n");
			continue;
		}

		if (strcmp(p, "SEARCH") == 0)
		{
			contentsearch cs;
			struct smap1_search_results searchResults;

			auto si=createSearch(cs, &ptr);

			if (si == cs.searchlist.end())
			{
				writes("-ERR SEARCH failed: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			searchResults.prev_runs=0;
			searchResults.prev_search_hit=0;
			searchResults.prev_search_hit_start=0;

			cs.search_internal(
				si, "utf-8",
				[&]
				(unsigned long i)
				{
					smap1_search_cb(i, searchResults);
				});

			if (searchResults.prev_search_hit)
				smap1_search_cb_range(searchResults);

			if (searchResults.prev_runs)
				writes("\n");

			writes("+OK Search completed.\n");
			continue;
		}


		writes("-ERR Syntax error.\n");
	}

	writes("* BYE Courier-SMAP server shutting down\n"
	       "+OK LOGOUT completed\n");
	writeflush();
}
