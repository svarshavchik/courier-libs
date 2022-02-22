/*
** Copyright 2003-2011 S. Varshavchik.
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
#include	<algorithm>

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
extern "C" int keywords();

extern unsigned long header_count, body_count;

extern std::string compute_myrights(maildir::aclt_list &l,
				    const std::string &l_owner);

struct addRemoveKeywordInfo;

void snapshot_select(int);
extern void doflags(FILE *fp, struct fetchinfo *fi,
		    imapscaninfo *i, unsigned long msgnum,
		    struct rfc2045 *mimep);
extern void set_time(const std::string &tmpname, time_t timestamp);
extern int imapenhancedidle(void);
extern void imapidle(void);

extern void expunge();
extern void doNoop(int);
extern int do_store(unsigned long, int, void *);
extern int reflag_filename(struct imapscanmessageinfo *mi,
			       struct imapflags *flags, int fd);

extern void do_expunge(unsigned long expunge_start,
		       unsigned long expunge_end,
		       int force);

extern char *current_mailbox, *current_mailbox_acl;
static int current_mailbox_shared;

extern imapscaninfo current_maildir_info;
void get_message_flags(struct imapscanmessageinfo *,
		       char *, struct imapflags *);
void fetchflags(unsigned long);

extern bool acl_lock(const std::string &maildir,
		     const std::function< bool() >&callback);

extern void aclminimum(const std::string &);

struct rfc2045 *fetch_alloc_rfc2045(unsigned long, FILE *);
FILE *open_cached_fp(unsigned long);
void fetch_free_cache();

FILE *maildir_mkfilename(const char *mailbox, struct imapflags *flags,
			 unsigned long s,
			 std::string &tmpname,
			 std::string &newname);

/*
** Parse a word from the current SMAP command.
*/

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

static void up(std::string &p)
{
	for (auto &c:p)
	{
		UC(c);
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

struct fn_word_list {
	struct fn_word_list *next;
	char *w;
};

/*
** Create a folder word array by reading words from the SMAP command.
*/

static char **fn_fromwords(char **ptr)
{
	struct fn_word_list *h=NULL, *n, **t=&h;
	size_t cnt=0;
	char *p;
	char **fn;

	while (*(p=getword(ptr)))
	{
		n=(fn_word_list *)malloc(sizeof(struct fn_word_list));

		if (!n || !(n->w=strdup(p)))
		{
			if (n)
				free(n);

			while ((n=h) != NULL)
			{
				h=n->next;
				free(n->w);
				free(n);
			}
			return NULL;
		}

		n->next=NULL;
		*t=n;
		t= &n->next;
		cnt++;
	}

	if (!h)
	{
		errno=EINVAL;
		return NULL;
	}

	fn=(char **)malloc((cnt+1)*sizeof(char *));
	cnt=0;

	while ((n=h) != NULL)
	{
		h=n->next;

		if (fn)
			fn[cnt]=n->w;
		else
			free(n->w);
		free(n);
		cnt++;
	}
	if (fn)
		fn[cnt]=0;
	return fn;
}

/*
** LIST-related functions.
*/

struct list_hier {
	struct list_hier *next;
	char *hier;
	int flags;
};

struct list_callback_info {

	struct list_hier *hier; /* Hierarchy being listed */

	struct list_hier *found;
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

	void (*callback_func)(const char *, char **, void *);
	void *callback_arg;
	const char *homedir;
	const char *owner;
};

static void list_callback(const char *f,
			  list_callback_utf8 *utf8)
{
	maildir::aclt_list l;

	char **fn=maildir_smapfn_fromutf8(f);

	if (!fn)
	{
		perror(f);
		return;
	}

	f=strchr(f, '.');
	if (!f)
		f="";

	if (l.read(utf8->homedir, f) == 0)
	{
		std::string owner;

		owner.reserve(strlen(utf8->owner)+5);

		owner="user=";
		owner += utf8->owner;

		auto myrights=compute_myrights(l, owner);

		if (myrights.find(ACL_LOOKUP[0]) != myrights.npos)
			(*utf8->callback_func)(f, fn, utf8->callback_arg);
	}
	maildir_smapfn_free(fn);
}

/*
** list_callback callback that accumulates existing folders beneath a
** certain hierarchy.
*/

static void list_utf8_callback(const char *n, char **f, void *vp)
{
	struct list_callback_info *lci=(struct list_callback_info *)vp;
	struct list_hier *h=lci->hier;

	for (;;)
	{
		if (!*f)
			return;

		if (h)
		{
			if (strcmp(h->hier, *f))
				break;

			h=h->next;
			f++;
			continue;
		}

		for (h=lci->found; h; h=h->next)
		{
			if (strcmp(h->hier, *f) == 0)
				break;
		}

		if (!h)
		{
			if ((h=(list_hier *)malloc(sizeof(struct list_hier))) == NULL ||
			    (h->hier=strdup(*f)) == NULL)
			{
				if (h)
					free(h);
				perror("malloc");
				break;
			}

			h->next=lci->found;
			lci->found=h;
			h->flags=0;
		}

		if (f[1])
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

static void do_listcmd(struct list_hier **head,
		       struct list_hier **tail,
		       char **ptr);

static void listcmd(struct list_hier **head,
		    struct list_hier **tail,
		    char **ptr)
{
	char *p;

	if (*(p=getword(ptr)))
	{
		struct list_hier node;
		node.next=NULL;
		node.hier=p;

		*tail= &node;
		listcmd(head, &node.next, ptr);
		return;
	}
	do_listcmd(head, tail, ptr);
}

struct smap_find_info {
	char *homedir;
	char *maildir;
};

static int smap_find_cb(struct maildir_newshared_enum_cb *cb);
static int smap_list_cb(struct maildir_newshared_enum_cb *cb);
static bool read_acls(maildir::aclt_list &aclt_list,
		      maildir::info &minfo);

static void do_listcmd(struct list_hier **head,
		       struct list_hier **tail,
		       char **ptr)
{
	struct list_hier *p;
	size_t cnt;
	char **vecs;
	int hierlist=0;

	if (!*head) /* No arguments to LIST */
	{
		list(INBOX, "New Mail", LIST_FOLDER);
		list(INBOX, "Folders", LIST_DIRECTORY);
		list(PUBLIC, "Public Folders", LIST_DIRECTORY);
	}
	else
	{
		struct list_callback_info lci;
		struct list_callback_utf8 list_utf8_info;

		list_utf8_info.callback_func= &list_utf8_callback;
		list_utf8_info.callback_arg= &lci;

		lci.hier= *head;
		lci.found=NULL;

		if (strcmp(lci.hier->hier, PUBLIC) == 0)
		{
			struct maildir_shindex_cache *curcache;
			struct list_hier *p=lci.hier->next;
			struct smap_find_info sfi;
			int eof;
			char *d;

			curcache=maildir_shared_cache_read(NULL, NULL, NULL);

			while (curcache && p)
			{
				size_t i;
				int rc;
				struct list_hier inbox;

				for (i=0; i<curcache->nrecords; i++)
					if (strcmp(curcache->records[i].name,
						   p->hier) == 0)
						break;
				if (i >= curcache->nrecords)
				{
					curcache=NULL;
					break;
				}

				sfi.homedir=NULL;
				sfi.maildir=NULL;
				curcache->indexfile.startingpos=
					curcache->records[i].offset;
				rc=maildir_newshared_nextAt(&curcache
							    ->indexfile,
							    &eof,
							    smap_find_cb,
							    &sfi);

				if (rc || eof)
				{
					fprintf(stderr, "ERR: Internal error -"
						" maildir_newshared_nextAt: %s\n",
						strerror(errno));
					curcache=NULL;
					break;
				}

				if (!sfi.homedir)
				{
					curcache=
						maildir_shared_cache_read(curcache,
									  sfi.maildir,
									  p->hier);
					p=p->next;
					free(sfi.maildir);
					continue;
				}

				inbox.next=p->next;

				static char inbox_str[]=INBOX; //TODO
				inbox.hier=inbox_str;

				d=maildir_location(sfi.homedir, sfi.maildir);
				free(sfi.homedir);
				free(sfi.maildir);

				lci.hier= &inbox;
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
				free(d);
				curcache=NULL;
				break;
			}

			if (curcache) /* List a shared hierarchy */
			{
				int rc;

				curcache->indexfile.startingpos=0;
				eof=0;

				do
				{
					rc=(curcache->indexfile.startingpos
						? maildir_newshared_next:
						maildir_newshared_nextAt)
						(&curcache->indexfile, &eof,
						 &smap_list_cb,
						 &list_utf8_info);

					if (rc)
						fprintf(stderr,
							"ERR: Internal error -"
							" maildir_newshared_next: %s\n",
							strerror(errno));
				} while (rc == 0 && !eof);

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

		for (cnt=0, p= *head; p; p=p->next)
			++cnt;

		vecs=(char **)malloc(sizeof(char *)*(cnt+2));

		if (!vecs)
		{
			while (lci.found)
			{
				struct list_hier *h=lci.found;

				lci.found=h->next;

				free(h->hier);
				free(h);
			}
			write_error_exit(0);
		}


		for (cnt=0, p= *head; p; p=p->next)
		{
			vecs[cnt]=p->hier;
			++cnt;
		}

		while (lci.found)
		{
			struct list_hier *h=lci.found;
			maildir::aclt_list aclt_list;

			lci.found=h->next;

			vecs[cnt]=h->hier;
			vecs[cnt+1]=0;

			auto minfo=maildir::info_smap_find(
				vecs,
				getenv("AUTHENTICATED")
			);

			if (minfo)
			{
				if (read_acls(aclt_list, minfo))
				{
					auto acl=compute_myrights(aclt_list,
								  minfo.owner);

					if (acl.find(ACL_LOOKUP[0])
					    == acl.npos)
					{
						h->flags=LIST_DIRECTORY;

						if (hierlist)
							list(h->hier,
							     h->hier,
							     h->flags);

					}
					else
					{
						list(h->hier, h->hier,
						     h->flags);
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
					h->hier,
					strerror(errno));
			}

			free(h->hier);
			free(h);
		}
		free(vecs);
	}
	writes("+OK LIST completed\n");
}

static int smap_find_cb(struct maildir_newshared_enum_cb *cb)
{
	struct smap_find_info *ifs=(struct smap_find_info *)cb->cb_arg;

	if (cb->homedir)
		ifs->homedir=my_strdup(cb->homedir);
	if (cb->maildir)
		ifs->maildir=my_strdup(cb->maildir);
	return 0;
}

static int smap_list_cb(struct maildir_newshared_enum_cb *cb)
{
	struct list_callback_utf8 *list_utf8_info=
		(struct list_callback_utf8 *)cb->cb_arg;
	struct list_callback_info *lci=
		(struct list_callback_info *)list_utf8_info->callback_arg;
	char *d;

	struct list_hier *h;
	struct stat stat_buf;

	if (cb->homedir == NULL)
	{
		if ((h=(list_hier *)malloc(sizeof(struct list_hier))) == NULL ||
		    (h->hier
		     =strdup(cb->name)) == NULL)
		{
			if (h)
				free(h);
			perror("ERR: malloc");
			return 0;
		}

		h->next= lci->found;
		lci->found=h;
		h->flags = LIST_DIRECTORY;
		return 0;
	}

	d=maildir_location(cb->homedir, cb->maildir);

	if (!d)
	{
		perror("ERR: get_topmaildir");
		return 0;
	}

	if (stat(d, &stat_buf) < 0 ||
	    (stat_buf.st_dev == homedir_dev &&
	     stat_buf.st_ino == homedir_ino))
	{
		free(d);
		return 0;
	}

	list_utf8_info->homedir=d;
	list_utf8_info->owner=cb->name;
	lci->hier=NULL;
	h=lci->found;
	lci->found=NULL;
	maildir::list(d,
		      [&]
		      (const std::string &name)
		      {
			      list_callback(name.c_str(),
					    list_utf8_info);
		      });
	free(d);

	if (!lci->found)
		lci->found=h;
	else
	{
		char *p;

		while (lci->found->next) /* SHOULDN'T HAPPEN!!! */
		{
			struct list_hier *p=lci->found->next;

			lci->found->next=p->next;
			free(p->hier);
			free(p);
			fprintf(stderr, "ERR: Unexpected folder list"
				" in smap_list_cb()\n");
		}
		lci->found->next=h;

		p=my_strdup(cb->name);
		free(lci->found->hier);
		lci->found->hier=p;
	}

	return (0);
}

/*
** Read the name of a new folder.  Returns the pathname to the folder, suitable
** for immediate creation.
*/

static std::string getCreateFolder_int(char **ptr, char *need_perms)
{
	char **fn;
	size_t i;
	char *save;
	maildir::aclt_list aclt_list;

	fn=fn_fromwords(ptr);
	if (!fn)
		return "";

	if (need_perms)
	{
		for (i=0; fn[i]; i++)
			;

		if (i == 0)
		{
			*need_perms=0;
			maildir_smapfn_free(fn);
			errno=EINVAL;
			return "";
		}

		save=fn[--i];
		fn[i]=NULL;

		auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));

		if (!minfo)
		{
			fn[i]=save;
			maildir_smapfn_free(fn);
			return "";
		}

		fn[i]=save;

		if (!read_acls(aclt_list, minfo))
		{
			maildir_smapfn_free(fn);
			return "";
		}

		auto save=compute_myrights(aclt_list, minfo.owner);

		for (i=0; need_perms[i]; i++)
			if (save.find(need_perms[i]) == save.npos)
			{
				maildir_smapfn_free(fn);
				*need_perms=0;
				errno=EPERM;
				return "";
			}
	}

	auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));
	maildir_smapfn_free(fn);

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

	if (current_mailbox)
	{
		q=maildir::name2dir(minfo.homedir, minfo.maildir);

		if (!q.empty())
		{
			if (q == current_mailbox)
			{
				auto r=compute_myrights(aclt_list,
							minfo.owner);

				if (r != current_mailbox_acl)
				{
					free(current_mailbox_acl);
					current_mailbox_acl=
						my_strdup(r.c_str());
				}
			}
		}
	}
	return rc == 0;
}

static std::string getExistingFolder_int(char **ptr, char *rightsWanted)
{
	char **fn;

	fn=fn_fromwords(ptr);
	if (!fn)
		return "";

	auto minfo=maildir::info_smap_find(fn, getenv("AUTHENTICATED"));

	if (!minfo)
	{
		maildir_smapfn_free(fn);
		return "";
	}
	maildir_smapfn_free(fn);

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
	if (current_mailbox)
		doNoop(real_noop);
	writes("+OK Folder updated\n");
}

/* Parse a message set.  Return the next word following the message set */

struct smapmsgset {
	struct smapmsgset *next;
	unsigned nranges;
	unsigned long range[2][2];
};

static struct smapmsgset msgset;
static const char digit[]="0123456789";

static char *markmsgset(char **ptr, int *hasmsgset)
{
	unsigned long n;
	char *w;

	struct smapmsgset *msgsetp;

	while ((msgsetp=msgset.next) != NULL)
	{
		msgset.next=msgsetp->next;
		free(msgsetp);
	}

	msgsetp= &msgset;

	msgsetp->nranges=0;

	*hasmsgset=0;

	n=0;

	while (*(w=getword(ptr)))
	{
		unsigned long a=0, b=0;
		const char *d;

		if (!*w || (d=strchr(digit, *w)) == NULL)
			break;

		*hasmsgset=1;

		while ( *w && (d=strchr(digit, *w)) != NULL)
		{
			a=a * 10 + d-digit;
			w++;
		}

		b=a;

		if (*w == '-')
		{
			++w;
			b=0;
			while ( *w && (d=strchr(digit, *w)) != NULL)
			{
				b=b * 10 + d-digit;
				w++;
			}
		}

		if (a <= n || b < a)
		{
			errno=EINVAL;
			return NULL;
		}

		n=b;

		if (msgsetp->nranges >=
		    sizeof(msgsetp->range)/sizeof(msgsetp->range[0]))
		{
			if ((msgsetp->next=(smapmsgset *)malloc(sizeof(struct smapmsgset)))
			    == NULL)
			{
				write_error_exit(0);
			}

			msgsetp=msgsetp->next;
			msgsetp->next=NULL;
			msgsetp->nranges=0;
		}

		msgsetp->range[msgsetp->nranges][0]=a;
		msgsetp->range[msgsetp->nranges][1]=b;
		++msgsetp->nranges;
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
			flags->seen=1;
		else if (strcmp(p, "REPLIED") == 0)
			flags->answered=1;
		else if (strcmp(p, "DRAFT") == 0)
			flags->drafts=1;
		else if (strcmp(p, "DELETED") == 0)
			flags->deleted=1;
		else if (strcmp(p, "MARKED") == 0)
			flags->flagged=1;

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

static int applymsgset( const std::function<int (unsigned long)> &callback)
{
	struct smapmsgset *msgsetp= &msgset;
	unsigned long n;
	int rc;

	while (msgsetp)
	{
		unsigned i;

		for (i=0; i<msgsetp->nranges; i++)
		{
			for (n=msgsetp->range[i][0];
			     n <= msgsetp->range[i][1]; n++)
			{
				if (current_mailbox == NULL ||
				    n > current_maildir_info.msgs.size())
					break;
				rc=callback(n-1);
				if (rc)
					return rc;
			}
		}

		msgsetp=msgsetp->next;
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
	auto filename=maildir::filename(current_mailbox, "",
					current_maildir_info.msgs[n]
					.filename);

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

static int hashdr(const char *hdrList, const char *hdr)
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

			hbuf[0]=0;
			strncat(hbuf, hdr, 29);
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

		for (n=0; hdrList[n] && hdrList[n] != ',' && hdr[n]; n++)
		{
			char a=hdrList[n];
			char b=hdr[n];

			UC(b);
			if (a != b)
				break;
		}

		if ((hdrList[n] == 0 || hdrList[n] == ',') && hdr[n] == 0)
			return 1;

		hdrList += n;
		while (*hdrList && *hdrList != ',')
			++hdrList;
	}
	return 0;
}

static void writemimeid(struct rfc2045 *rfcp)
{
	if (rfcp->parent)
	{
		writemimeid(rfcp->parent);
		writes(".");
	}
	writen(rfcp->pindex);
}

static int dump_hdrs(int fd, unsigned long n,
		     struct rfc2045 *rfcp, const char *hdrs,
		     const char *type)
{
	struct rfc2045src *src;
	struct rfc2045headerinfo *h;
	char *header;
	char *value;
	int rc;
        off_t start_pos, end_pos, dummy, start_body;
	off_t nbodylines;
	int get_flags=RFC2045H_NOLC;

	rc=0;

	if (type && strcmp(type, "RAWHEADERS") == 0)
		get_flags |= RFC2045H_KEEPNL;

	if (!rfcp)
	{
		struct stat stat_buf;

		if (fstat(fd, &stat_buf))
			end_pos=8000; /* Heh */
		else
			end_pos=stat_buf.st_size;
		start_pos=0;
		start_body=0;
	}
	else rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body, &dummy,
			     &nbodylines);

	writes("{.");
	writen(start_body - start_pos);
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
		writen(nbodylines);
		writes(" SIZE=");
		writen(end_pos-start_body);
		writes(" \"MIME.ID=");

		if (rfcp->parent)
		{
			writemimeid(rfcp);
			writes("\" \"MIME.PARENT=");
			if (rfcp->parent->parent)
				writemimeid(rfcp->parent);
		}
		writes("\"\n");
	}

	src=rfc2045src_init_fd(fd);
	h=src ? rfc2045header_start(src, rfcp):NULL;

	while (h &&
	       (rc=rfc2045header_get(h, &header, &value, get_flags)) == 0
	       && header)
	{
		if (hashdr(hdrs, header))
		{
			if (*header == '.')
				writes(".");
			writes(header);
			writes(": ");
			writes(value);
			writes("\n");

			header_count += strlen(header)+strlen(value)+3;
		}
	}
	writes(".\n");

	if (h)
		rfc2045header_end(h);
	else
		rc= -1;
	if (src)
		rfc2045src_deinit(src);
	return rc;
}

static int dump_body(FILE *fp, unsigned long msgNum,
		     struct rfc2045 *rfcp, int dump_all)
{
	char buffer[SMAP_BUFSIZ];
        off_t start_pos, end_pos, dummy, start_body;
	int i;
	int first;

	if (!rfcp)
	{
		struct stat stat_buf;

		if (fstat(fileno(fp), &stat_buf) < 0)
			return -1;

		if (dump_all)
		{
			start_pos=start_body=0;
		}
		else
		{
			if (fseek(fp, 0L, SEEK_SET) < 0)
				return -1;

			if (!(rfcp=rfc2045_alloc()))
				return -1;

			do
			{
				i=fread(buffer, 1, sizeof(buffer), fp);

				if (i < 0)
				{
					rfc2045_free(rfcp);
					return -1;
				}

				if (i == 0)
					break;
				rfc2045_parse(rfcp, buffer, i);
			} while (rfcp->workinheader);

			rfc2045_mimepos(rfcp, &start_pos, &end_pos,
					&start_body, &dummy,
					&dummy);
			rfc2045_free(rfcp);

			start_pos=0;
		}
		end_pos=stat_buf.st_size;
	}
	else rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body, &dummy,
			     &dummy);

	if (dump_all)
		start_body=start_pos;

	if (fseek(fp, start_body, SEEK_SET) < 0)
		return -1;

	first=1;
	do
	{
		int n=sizeof(buffer);

		if (n > end_pos - start_body)
			n=end_pos - start_body;

		for (i=0; i<n; i++)
		{
			int ch=getc(fp);

			if (ch == EOF)
			{
				errno=EIO;
				return -1;
			}
			buffer[i]=ch;
		}

		if (first)
		{
			if (start_body == end_pos)
			{
				writes("{.0} FETCH ");
				writen(msgNum+1);
				writes(" CONTENTS\n.");
			}
			else
			{
				writes("{");
				writen(i);
				writes("/");
				writen(end_pos - start_body);
				writes("} FETCH ");
				writen(msgNum+1);
				writes(" CONTENTS\n");
			}
		}
		else
		{
			writen(i);
			writes("\n");
		}

		first=0;
		writemem(buffer, i);

		start_body += i;
		body_count += i;
	} while (start_body < end_pos);
	writes("\n");
	return 0;
}

struct decodeinfo {
	char buffer[SMAP_BUFSIZ];
	size_t bufptr;

	int first;
	unsigned long msgNum;
	off_t estSize;
};

static void do_dump_decoded_flush(struct decodeinfo *);

static struct rfc2045 *decodeCreateRfc(FILE *fp);
static int do_dump_decoded(const char *, size_t, void *);

static int dump_decoded(FILE *fp, unsigned long msgNum,
			struct rfc2045 *rfcp)
{
	struct decodeinfo myDecodeInfo;
	const char *content_type;
	const char *content_transfer_encoding;
	const char *charset;
        off_t start_pos, end_pos, dummy, start_body;

	struct rfc2045src *src;
	struct rfc2045 *myrfcp=NULL;
	int fd;
	int i;

	if (!rfcp)
	{
		rfcp=myrfcp=decodeCreateRfc(fp);
		if (!rfcp)
			return -1;
	}

	if ((fd=dup(fileno(fp))) < 0)
	{
		if (myrfcp)
			rfc2045_free(myrfcp);
		return -1;
	}

	myDecodeInfo.first=1;
	myDecodeInfo.msgNum=msgNum;
	myDecodeInfo.bufptr=0;

	rfc2045_mimeinfo(rfcp, &content_type, &content_transfer_encoding,
			 &charset);
	rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body, &dummy,
			&dummy);
	myDecodeInfo.estSize=end_pos - start_body;

	if (content_transfer_encoding
	    && strlen(content_transfer_encoding) == 6)
	{
		char buf[7];

		strcpy(buf, content_transfer_encoding);
		up(buf);

		if (strcmp(buf, "BASE64") == 0)
			myDecodeInfo.estSize = myDecodeInfo.estSize / 4 * 3;

		/* Better estimate of base64 content */
	}

	src=rfc2045src_init_fd(fd);

	i=src ? rfc2045_decodemimesection(src, rfcp, &do_dump_decoded,
					  &myDecodeInfo):-1;

	do_dump_decoded_flush(&myDecodeInfo);

	if (src)
		rfc2045src_deinit(src);

	close(fd);

	if (i == 0 && myDecodeInfo.first) /* Empty body, punt */
	{
		writes("{.0} FETCH ");
		writen(msgNum+1);
		writes(" CONTENTS\n.");
	}
	writes("\n");
	if (myrfcp)
		rfc2045_free(myrfcp);
	return i;
}

/* Dummy up a rfc2045 structure for retrieving the entire msg body */

static struct rfc2045 *decodeCreateRfc(FILE *fp)
{
	char buffer[SMAP_BUFSIZ];
	struct stat stat_buf;
	int i;
	struct rfc2045 *myrfcp;

	if (fstat(fileno(fp), &stat_buf) < 0)
		return NULL;

	if (fseek(fp, 0L, SEEK_SET) < 0)
		return NULL;

	if (!(myrfcp=rfc2045_alloc()))
		return NULL;

	do
	{
		i=fread(buffer, 1, sizeof(buffer), fp);

		if (i < 0)
		{
			rfc2045_free(myrfcp);
			return NULL;
			}

		if (i == 0)
			break;
		rfc2045_parse(myrfcp, buffer, i);
	} while (myrfcp->workinheader);

	myrfcp->endpos=stat_buf.st_size;
	return myrfcp;
}

static int do_dump_decoded(const char *chunk, size_t chunkSize,
			   void *vp)
{
	struct decodeinfo *myDecodeInfo=(struct decodeinfo *) vp;

	while (chunkSize)
	{
		size_t n;

		if (myDecodeInfo->bufptr >= sizeof(myDecodeInfo->buffer))
			do_dump_decoded_flush(myDecodeInfo);

		n=sizeof(myDecodeInfo->buffer)-myDecodeInfo->bufptr;

		if (n > chunkSize)
			n=chunkSize;
		memcpy(myDecodeInfo->buffer + myDecodeInfo->bufptr, chunk, n);
		myDecodeInfo->bufptr += n;
		chunk += n;
		chunkSize -= n;
	}
	return 0;
}

static void do_dump_decoded_flush(struct decodeinfo *myDecodeInfo)
{
	size_t chunkSize= myDecodeInfo->bufptr;

	myDecodeInfo->bufptr=0;

	if (chunkSize == 0)
		return;

	if (myDecodeInfo->first)
	{
		myDecodeInfo->first=0;
		writes("{");
		writen(chunkSize);
		writes("/");
		writen(myDecodeInfo->estSize);
		writes("} FETCH ");
		writen(myDecodeInfo->msgNum+1);
		writes(" CONTENTS\n");
	}
	else
	{
		writen(chunkSize);
		writes("\n");
	}
	writemem(myDecodeInfo->buffer, chunkSize);
	body_count += chunkSize;
}

static int mime(int fd, unsigned long n,
		struct rfc2045 *rfcp, const char *hdrs)
{
	int rc=dump_hdrs(fd, n, rfcp, hdrs, NULL);

	if (rc)
		return rc;

	for (rfcp=rfcp->firstpart; rfcp; rfcp=rfcp->next)
		if (!rfcp->isdummy)
		{
			rc=mime(fd, n, rfcp, hdrs);
			if (rc)
				return rc;
		}

	return 0;
}

/*
** Find the specified MIME id.
*/

static struct rfc2045 *findmimeid(struct rfc2045 *rfcp,
				  const char *mimeid)
{
	unsigned long n;

	while (mimeid && *mimeid)
	{
		const char *d;

		n=0;

		if (strchr(digit, *mimeid) == NULL)
			return NULL;

		while (*mimeid && (d=strchr(digit, *mimeid)) != NULL)
		{
			n=n * 10 + d-digit;
			mimeid++;
		}

		while (rfcp)
		{
			if (!rfcp->isdummy && rfcp->pindex == n)
				break;
			rfcp=rfcp->next;
		}

		if (!rfcp)
			return NULL;

		if (*mimeid == '.')
		{
			++mimeid;
			rfcp=rfcp->firstpart;
		}
	}
	return rfcp;
}

static int do_fetch(unsigned long n, smapfetchinfo &fi)
{
	FILE *fp=open_cached_fp(n);
	int rc=0;

	if (!fp)
		return -1;

	if (strcmp(fi.entity, "MIME") == 0)
	{
		struct rfc2045 *rfcp=fetch_alloc_rfc2045(n, fp);
		int fd;

		if (!rfcp)
			return -1;

		fd=dup(fileno(fp));
		if (fd < 0)
			return -1;

		rc=mime(fd, n, rfcp, fi.hdrs);
		close(fd);
	}
	else if (strcmp(fi.entity, "HEADERS") == 0 ||
		 strcmp(fi.entity, "RAWHEADERS") == 0)
	{
		int fd;
		struct rfc2045 *rfcp;

		fd=dup(fileno(fp));
		if (fd < 0)
			return -1;

		if (!fi.mimeid || !*fi.mimeid)
			rfcp=NULL;
		else
		{
			rfcp=fetch_alloc_rfc2045(n, fp);

			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				close(fd);
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_hdrs(fd, n, rfcp, fi.hdrs, fi.entity);
		close(fd);
	}
	else if (strcmp(fi.entity, "BODY") == 0
		 || strcmp(fi.entity, "ALL") == 0)
	{
		struct rfc2045 *rfcp;

		if (!fi.mimeid || !*fi.mimeid)
			rfcp=NULL;
		else
		{
			rfcp=fetch_alloc_rfc2045(n, fp);

			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_body(fp, n, rfcp, fi.entity[0] == 'A');
	}
	else if (strcmp(fi.entity, "BODY.DECODED") == 0)
	{
		struct rfc2045 *rfcp;

		if (!fi.mimeid || !*fi.mimeid)
			rfcp=NULL;
		else
		{
			rfcp=fetch_alloc_rfc2045(n, fp);

			rfcp=findmimeid(rfcp, fi.mimeid);

			if (!rfcp)
			{
				errno=EINVAL;
				return -1;
			}
		}

		rc=dump_decoded(fp, n, rfcp);
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
					&flags, fileno(fp));
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
			FILE *fp=open_cached_fp(n);
			struct stat stat_buf;

			if (fp && fstat(fileno(fp), &stat_buf) == 0)
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
		FILE *fp=open_cached_fp(n);

		if (fp && fstat(fileno(fp), &stat_buf) == 0)
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

struct add_rcptlist {
	struct add_rcptlist *next;
	char *rcptto;
};

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

static void senderr(char *errmsg)
{
	writes("-ERR ");
	writes(errmsg);
	writes("\n");
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

	fd=imapscan_openfile(current_mailbox, &current_maildir_info, n);
	if (fd < 0)	return (0);

	if (fstat(fd, &stat_buf) < 0)
	{
		close(fd);
		return (0);
	}

	get_message_flags(&current_maildir_info.msgs.at(n), 0, &new_flags);

	fp=maildir_mkfilename(cqinfo->destmailbox,
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

	auto filename=maildir::filename(current_mailbox, "",
					current_maildir_info.msgs.at(n).filename);

	if (filename.empty())
		return 0;

	std::string newfilename;

	newfilename.reserve(strlen(cqinfo->destmailbox) + sizeof("/cur")-1
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

		if (!ismsgset_str(n->as.c_str()))
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
		n->as=getword(ptr);
	}
	else if (strcmp(w, "HEADER") == 0)
	{
		n=cs.alloc_search();
		n->type=search_header;
		n->cs=getword(ptr);
		up(n->cs);
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

static int do_copyto(const char *toFolder,
		     int (*do_func)(unsigned long, void *),
		     const char *acls)
{
	int has_quota=0;
	struct copyquotainfo cqinfo;
	struct maildirsize quotainfo;

	cqinfo.destmailbox=toFolder;
	cqinfo.nbytes=0;
	cqinfo.nfiles=0;
	cqinfo.acls=acls;

	if (maildirquota_countfolder(toFolder))
	{
		if (maildir_openquotafile(&quotainfo, ".") == 0)
		{
			if (quotainfo.fd >= 0)
				has_quota=1;
			maildir_closequotafile(&quotainfo);
		}

		if (has_quota > 0 && applymsgset(
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
		[&]
		(unsigned long n)
		{
			return do_func(n, &cqinfo);
		});
}

static int copyto(const char *toFolder, int do_move, const char *acls)
{
	if (!do_move)
		return do_copyto(toFolder, &do_copymsg, acls);

	if (!current_mailbox_shared &&
	    maildirquota_countfolder(current_mailbox) ==
	    maildirquota_countfolder(toFolder))
	{
		if (do_copyto(toFolder, do_movemsg, acls))
			return -1;

		doNoop(0);
		return(0);
	}

	if (do_copyto(toFolder, &do_copymsg, acls))
		return -1;

	applymsgset(
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

	setacl_info(char **fn, char **ptr)
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

static maildir::info checkacl(char **folder,
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
	struct imapflags add_flags;
	int in_add=0;
	char *add_from=NULL;
	std::string add_folder;
	time_t add_internaldate=0;
	char *add_notify=NULL;
	unsigned add_rcpt_count=0;
	mail::keywords::list addKeywords;

	struct add_rcptlist *add_rcpt_list=NULL;

	char rights_buf[40];

	enabled_utf8=1;
	memset(&add_flags, 0, sizeof(add_flags));

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
			char **argvec;
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
					if (add_from)
						free(add_from);
					if ((add_from=strdup(q)) == NULL)
					{
						writes("-ERR ");
						writes(strerror(errno));
						writes("\n");
						break;
					}
					okmsg="MAIL FROM set";
				}

				if (strcmp(p, "NOTIFY") == 0 && q)
				{
					if (add_notify)
						free(add_notify);
					if ((add_notify=strdup(q)) == NULL)
					{
						writes("-ERR ");
						writes(strerror(errno));
						writes("\n");
						break;
					}
					okmsg="NOTIFY set";
				}

				if (strcmp(p, "RCPTTO") == 0 && q)
				{
					struct add_rcptlist *rcpt=
						(add_rcptlist *)
						malloc(sizeof(struct
							      add_rcptlist));

					if (rcpt == NULL ||
					    (rcpt->rcptto=strdup(q)) == NULL)
					{
						if (rcpt)
							free(rcpt);
						writes("-ERR ");
						writes(strerror(errno));
						writes("\n");
						break;
					}
					rcpt->next=add_rcpt_list;
					add_rcpt_list=rcpt;
					++add_rcpt_count;
					okmsg="RCPT TO set";
				}

				if (strcmp(p, "FLAGS") == 0 && q)
				{
					memset(&add_flags, 0,
					       sizeof(add_flags));
					*(q=comma)='=';
					parseflags(q, &add_flags);

					if (strchr(rights_buf,
						   ACL_SEEN[0])
					    == NULL)
						add_flags.seen=0;
					if (strchr(rights_buf,
						   ACL_DELETEMSGS[0])
					    == NULL)
						add_flags.deleted=0;
					if (strchr(rights_buf,
						   ACL_WRITE[0])
					    == NULL)
						add_flags.answered=
							add_flags.flagged=
							add_flags.drafts=0;

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

					argvec=NULL;

					if (add_rcpt_count > 0 && n)
					{
						argvec=(char **)malloc(sizeof(char *)
							      * (add_rcpt_count
								 +10));

						if (!argvec)
							n=0;
					}

					if (argvec)
					{
						int i=1;
						struct add_rcptlist *l;

						// TODO
						static char arg1[]="-oi";
						argvec[i++]=arg1;

						static char arg2[]="-f";
						argvec[i++]=arg2;
						argvec[i++]=add_from
							? add_from:
							(char *)
							defaultSendFrom();

						if (add_notify)
						{
							static char arg3[]="-N";
							argvec[i++]=arg3;
							argvec[i++]=add_notify;
						}

						for (l=add_rcpt_list; l;
						     l=l->next)
						{
							argvec[i++]=l->rcptto;
						}
						argvec[i]=0;

						i=imapd_sendmsg(
							tmpname.c_str(), argvec,
							&senderr);
						free(argvec);
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
			struct add_rcptlist *l;

			while ((l=add_rcpt_list) != NULL)
			{
				add_rcpt_list=l->next;
				free(l->rcptto);
				free(l);
			}
			memset(&add_flags, 0, sizeof(add_flags));
			if (add_from)
				free(add_from);
			add_folder.clear();
			if (add_notify)
				free(add_notify);

			addKeywords.clear();

			in_add=0;
			add_from=NULL;
			add_internaldate=0;
			add_notify=NULL;
			add_rcpt_count=0;
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
			char **fn=fn_fromwords(&ptr);
			int cnt;

			if (!fn)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			auto minfo=maildir::info_smap_find(
				fn,
				getenv("AUTHENTICATED"));

			if (!minfo)
			{
				maildir_smapfn_free(fn);
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			maildir::aclt_list aclt_list;

			if (!read_acls(aclt_list, minfo))
			{
				maildir_smapfn_free(fn);
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
				maildir_smapfn_free(fn);
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
			maildir_smapfn_free(fn);
			writes("+OK ACLs retrieved\n");
			continue;
		}

		if (strcmp(p, "SETACL") == 0 ||
		    strcmp(p, "DELETEACL") == 0)
		{
			char **fn=fn_fromwords(&ptr);

			if (!fn)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			setacl_info sainfo{fn, &ptr};

			if (!sainfo.minfo)
			{
				maildir_smapfn_free(fn);
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

			maildir_smapfn_free(fn);
			continue;
		}

		if (strcmp(p, "LIST") == 0)
		{
			struct list_hier *hier=NULL;

			listcmd(&hier, &hier, &ptr);
			continue;
		}

		if (strcmp(p, "STATUS") == 0)
		{
			imapscaninfo loaded_info,
				*infoptr;

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

			if (current_mailbox && t == current_mailbox)
			{
				infoptr= &current_maildir_info;
			}
			else
			{
				infoptr=&loaded_info;

				if (imapscan_maildir(infoptr,
						     t.c_str(), 1, 1, NULL))
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
			char **fn;
			std::string t;

			fn=fn_fromwords(&ptr);

			if (fn)
			{
				auto minfo=maildir::info_smap_find(
					fn, getenv("AUTHENTICATED"));

				if (minfo)
				{
					maildir_smapfn_free(fn);

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
							accessdenied(
								ACL_DELETEFOLDER
							);
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
				else
				{
					maildir_smapfn_free(fn);
				}
			}

			if (!t.empty() && current_mailbox &&
			    t == current_mailbox)
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
			char **fnsrc, **fndst;
			size_t i;
			char *save;
			const char *errmsg;

			if ((fnsrc=fn_fromwords(&ptr)) == NULL)
			{
				writes("-ERR ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			if ((fndst=fn_fromwords(&ptr)) == NULL)
			{
				maildir_smapfn_free(fnsrc);
				writes("-ERR ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			for (i=0; fndst[i]; i++)
				;

			if (i == 0)
			{
				maildir_smapfn_free(fnsrc);
				maildir_smapfn_free(fndst);
				writes("-ERR Invalid destination folder name\n");
				continue;
			}

			auto msrc=checkacl(fnsrc, ACL_DELETEFOLDER);

			if (!msrc)
			{
				maildir_smapfn_free(fnsrc);
				maildir_smapfn_free(fndst);
				accessdenied(ACL_DELETEFOLDER);
				continue;
			}
			save=fndst[--i];
			fndst[i]=NULL;

			auto mdst=checkacl(fndst, ACL_CREATE);

			if (!mdst)
			{
				fndst[i]=save;
				maildir_smapfn_free(fnsrc);
				maildir_smapfn_free(fndst);
				accessdenied(ACL_CREATE);
				continue;
			}

			fndst[i]=save;

			mdst=maildir::info_smap_find(fndst,
						     getenv("AUTHENTICATED"));

			if (!mdst)
			{
				maildir_smapfn_free(fnsrc);
				maildir_smapfn_free(fndst);
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

			maildir_smapfn_free(fnsrc);
			maildir_smapfn_free(fndst);
			continue;
		}

		if (strcmp(p, "OPEN") == 0 ||
		    strcmp(p, "SOPEN") == 0)
		{
			char **fn;
			const char *snapshot=0;

			if (current_mailbox)
			{
				free(current_mailbox);
				current_maildir_info=imapscaninfo{};
				current_mailbox=0;
			}
			if (current_mailbox_acl)
				free(current_mailbox_acl);
			current_mailbox_acl=NULL;
			current_mailbox_shared=0;

			fetch_free_cache();

			if (p[0] == 'S')
				snapshot=getword(&ptr);

			fn=fn_fromwords(&ptr);

			if (!fn)
			{
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			auto minfo=maildir::info_smap_find(
				fn,
				getenv("AUTHENTICATED"));

			if (!minfo)
			{
				maildir_smapfn_free(fn);
				writes("-ERR Invalid folder: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			maildir::aclt_list aclt_list;

			if (!read_acls(aclt_list, minfo))
			{
				maildir_smapfn_free(fn);
				writes("-ERR Unable to read"
				       " existing ACLS: ");
				writes(strerror(errno));
				writes("\n");
				continue;
			}

			auto q=compute_myrights(aclt_list, minfo.owner);
			maildir_smapfn_free(fn);

			if (q.find(ACL_READ[0]) == q.npos)
			{
				accessdenied(ACL_READ);
				continue;
			}
			current_mailbox_acl=my_strdup(q.c_str());
			current_mailbox=my_strdup(
				maildir::name2dir(minfo.homedir,
						  minfo.maildir
				).c_str()
			);

			snapshot_select(snapshot != NULL);

			if (snapshot_init(current_mailbox, snapshot))
			{
				writes("* SNAPSHOTEXISTS ");
				smapword(snapshot);
				writes("\n");
				smap1_noop(0);
				continue;
			}

			if (imapscan_maildir(&current_maildir_info,
					     current_mailbox, 0, 0, NULL) == 0)
			{
				snapshot_init(current_mailbox, NULL);
				writes("* EXISTS ");
				writen(current_maildir_info.msgs.size());
				writes("\n+OK Folder opened\n");
				continue;
			}

			writes("-ERR Cannot open the folder: ");
			writes(strerror(errno));
			writes("\n");

			free(current_mailbox);
			current_mailbox=NULL;
			continue;
		}

		if (strcmp(p, "CLOSE") == 0)
		{
			if (current_mailbox)
			{
				free(current_mailbox);
				current_maildir_info=imapscaninfo{};
				current_mailbox=0;
			}
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
			if (imapenhancedidle())
				imapidle();

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

		if (!current_mailbox)
		{
			static char null[]="";
			// TODO
			p=null; /* FALLTHROUGH */
		}

		if (strcmp(p, "EXPUNGE") == 0)
		{
			int hasSet;

			p=markmsgset(&ptr, &hasSet);

			if (p)
			{
				if (strchr(current_mailbox_acl,
					   ACL_EXPUNGE[0]) == NULL)
				{
					accessdenied(ACL_EXPUNGE);
					continue;
				}

				if (hasSet)
				{
					if (strchr(current_mailbox_acl,
						   ACL_DELETEMSGS[0]) == NULL)
					{
						accessdenied(ACL_DELETEMSGS);
						continue;
					}
					applymsgset(
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

			memset(&si.flags, 0, sizeof(si.flags));

			p=markmsgset(&ptr, &dummy);

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
				else if (strncmp(p, "+FLAGS=", 7) == 0 ||
					 strncmp(p, "-FLAGS=", 7) == 0)
				{
					up(p);
					si.plusminus=p[0];
					parseflags(p, &si.flags);
					if ((dummy=applymsgset(
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
				else if ((strncmp(p, "+KEYWORDS=", 10) == 0 ||
					  strncmp(p, "-KEYWORDS=", 10) == 0) &&
					 keywords())
				{
					si.plusminus=p[0];
					parsekeywords(p, si.keywords);
					dummy=applymsgset(
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
			int dummy;
			struct smapfetchinfo fi;
			int fetch_items=0;

			for (p=markmsgset(&ptr, &dummy);
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
			int dummy;
			int domove= *p == 'M';

			p=markmsgset(&ptr, &dummy);

			if (dummy && *p == 0)
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

					if (copyto(p.c_str(), domove,
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
