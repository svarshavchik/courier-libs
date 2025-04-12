/*
** Copyright 1998 - 2022 S. Varshavchik.
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
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif

#include	<sys/types.h>
#include	<sys/stat.h>

#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"imapscanclient.h"

#include	"mysignal.h"
#include	"imapd.h"
#include	"fetchinfo.h"
#include	"searchinfo.h"
#include	"storeinfo.h"
#include	"mailboxlist.h"

#include	"maildir/config.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildiraclt.h"
#include	"maildir/maildirnewshared.h"
#include	"maildir/maildirinfo.h"
#include	<courier-unicode.h>
#include	"courierauth.h"
#include	<algorithm>


static const char hierchs[]={HIERCH, 0};

extern std::string decode_valid_mailbox(const std::string &, int);
extern dev_t homedir_dev;
extern ino_t homedir_ino;
/*
		LIST MAILBOXES
*/

static std::string create_maildir_shared_index_file()
{
	std::string s;

	const char *p=getenv("IMAP_SHAREDINDEXFILE");

	if (p && *p)
	{
		auto q=auth_getoptionenv("sharedgroup");

		s.reserve(strlen(p)+strlen(q ? q:""));

		s=p;
		if (q)
			s += q;

		if (q) free(q);
	}

	if (!s.empty())
	{
		struct stat stat_buf;

		if (stat(s.c_str(), &stat_buf))
		{
			fprintf(stderr, "ERR: ");
			perror(s.c_str());
		}
	}

	return s;
}

const char *maildir_shared_index_file()
{
	static std::string filenamep=create_maildir_shared_index_file();

	return filenamep.c_str();
}

static bool match_mailbox(const std::string &, const std::string &, int flags);
static void match_mailbox_prep(std::string &);

/* Check if a folder has any new messages */

static int hasnewmsgs2(const char *dir)
{
DIR	*dirp=opendir(dir);
struct	dirent *de;

	while (dirp && (de=readdir(dirp)) != 0)
	{
	char	*p;

		if (de->d_name[0] == '.')	continue;
		p=strrchr(de->d_name, MDIRSEP[0]);
		if (p == 0 || strncmp(p, MDIRSEP "2,", 3) ||
			strchr(p, 'S') == 0)
		{
			closedir(dirp);
			return (1);
		}
	}
	if (dirp)	closedir(dirp);
	return (0);
}

static int hasnewmsgs(const std::string &folder)
{
	auto dir=decode_valid_mailbox(folder, 0);

	if (dir.empty())	return (0);

	if (is_sharedsubdir(dir.c_str()))
		maildir_shared_sync(dir.c_str());

	auto subdir=dir+"/new";
	if (hasnewmsgs2(subdir.c_str()))
	{
		return (1);
	}

	subdir=dir+"/cur";
	if (hasnewmsgs2(subdir.c_str()))
	{
		return (1);
	}

	return (0);
}

/* Each folder is listed with the \Noinferiors tag.  Then, for every subfolder
** we've seen, we need to output a listing for all the higher-level hierarchies
** with a \Noselect tag.  Therefore, we need to keep track of all the
** hierarchies we've seen so far.
*/

struct hierlist {
	int flag;
	std::string hier;
	} ;

static bool add_hier(std::vector<hierlist> &h, const std::string &s)
{
	if (std::find_if(h.begin(), h.end(),
			 [&]
			 (const hierlist &h)
			 {
				 return h.hier == s;
			 }) != h.end())
	{
		return true;
		/* Seen this one already */
	}

	h.push_back({0, s});
	return (false);
}

static hierlist *search_hier(std::vector<hierlist> &h,
			     const std::string &s)
{
	auto iter=std::find_if(h.begin(), h.end(),
			       [&]
			       (const hierlist &h)
			       {
				       return h.hier == s;
			       });

	if (iter == h.end())
		return nullptr;

	return &*iter;
}

static void hier_entry(const std::string &folder,
		       std::vector<hierlist> &hierarchies);

static bool has_hier_entry(const std::string &folder,
			   std::vector<hierlist> &hierarchies);

static void folder_entry(std::string folder, const std::string &pattern,
			 int list_options,
			 std::vector<hierlist> &folders,
			 std::vector<hierlist> &hierarchies)
{
	size_t i;

	int need_add_hier;
	int need_add_folders;

	match_mailbox_prep(folder);

	/* Optimize away folders we don't care about */

	for (i=0; i < pattern.size(); i++)
	{
		if ((!(list_options & LIST_CHECK1FOLDER)) &&
		    (pattern[i] == '%' || pattern[i] == '*'))
		{
			while (i)
			{
				if (pattern[i] == HIERCH)
					break;
				--i;
			}
			break;
		}
	}

	if (folder.size() <= i)
	{
		if (!std::equal(folder.begin(), folder.end(), pattern.begin()))
			return;

		if (folder.size() != i && pattern[folder.size()] != HIERCH)
			return;
	}
	else if (i)
	{
		if (!std::equal(folder.begin(), folder.begin()+i,
				pattern.begin()))
			return;
		if (folder[i] != HIERCH)
			return;
	}

	need_add_folders=0;

	if (match_mailbox(folder, pattern, list_options))
		need_add_folders=1;

	need_add_hier=0;
	if (!has_hier_entry(folder, hierarchies))
		need_add_hier=1;

	if (!need_add_folders && !need_add_hier)
		return; /* Nothing to do */

	if (!acl_check_rights(folder, ACL_LOOKUP))
		return;

	if (need_add_folders)
		(void) add_hier(folders, folder);

	if (need_add_hier)
		hier_entry(folder, hierarchies);
}

static void hier_entry(const std::string &folder,
		       std::vector<hierlist> &hierarchies)
{
	size_t i;

	for (i=0; i<folder.size(); i++)
	{
		if (folder[i] != HIERCH)	continue;
		(void)add_hier(hierarchies, folder.substr(0, i));
	}
}

static bool has_hier_entry(const std::string &folder,
			   std::vector<hierlist> &hierarchies)
{
	size_t i;

	for (i=0; i < folder.size(); i++)
	{
		if (folder[i] != HIERCH)	continue;

		if (!search_hier(hierarchies, folder.substr(0, i)))
		{
			return false;
		}
	}
	return true;
}

struct list_sharable_info {
	std::string pattern;
	std::vector<hierlist> folders, hierarchies;
	int flags;
	mailbox_scan_cb_t callback_func;
	} ;

static void list_sharable(const std::string &n,
			  list_sharable_info &ip)

{
	std::string p;

	p.reserve(n.size()+sizeof("shared"));

	p="shared.";
	p+=n;

	folder_entry(p, ip.pattern, ip.flags,
		     ip.folders, ip.hierarchies);
}

static void list_subscribed(const std::string &hier,
			    int flags,
			    std::vector<hierlist> &folders,
			    std::vector<hierlist> &hierarchies)
{
char	buf[BUFSIZ];
FILE	*fp;

	fp=fopen(SUBSCRIBEFILE, "r");
	if (fp)
	{
		while (fgets(buf, sizeof(buf), fp) != 0)
		{
			char *q=strchr(buf, '\n');

			if (q)	*q=0;

			if (*hier.c_str() == '#')
			{
				if (*buf != '#')
					continue;
			}
			else
			{
				if (*buf == '#')
					continue;
			}

			folder_entry(buf, hier, flags,
				     folders, hierarchies);
		}
		fclose(fp);
	}
}

static void maildir_scan(const std::string &inbox_dir,
			 const std::string &inbox_name,
			 list_sharable_info &shared_info)
{
	DIR	*dirp;
	struct	dirent *de;

	/* Scan maildir, looking for .subdirectories */

	dirp=opendir(inbox_dir.size() ? inbox_dir.c_str():".");
	while (dirp && (de=readdir(dirp)) != 0)
	{

		if (de->d_name[0] != '.' ||
		    strcmp(de->d_name, "..") == 0)
			continue;

		std::string p;

		p.reserve(strlen(de->d_name)+inbox_name.size()+10);
		p=inbox_name;

		if (strcmp(de->d_name, "."))
			p += de->d_name;

		folder_entry(p, shared_info.pattern, shared_info.flags,
			     shared_info.folders,
			     shared_info.hierarchies);
	}

	if (dirp)	closedir(dirp);
}

/* List the #shared hierarchy */

struct list_newshared_info {
	const char *acc_pfix;
	const char *skipped_pattern;
	struct list_sharable_info *shared_info;
	struct maildir_shindex_cache *parentCache;
	int dorecurse;
};

static void list_newshared_cb(struct maildir_newshared_enum_cb *cb,
			      struct list_newshared_info *lni);
static int list_newshared_skipcb(struct maildir_newshared_enum_cb *cb,
				 struct list_newshared_info *lni);
static int list_newshared_skiplevel(struct maildir_newshared_enum_cb *cb,
				    struct list_newshared_info *lni);

static int list_newshared_shortcut(const char *skipped_pattern,
				   struct list_sharable_info *shared_info,
				   const char *current_namespace,
				   struct maildir_shindex_cache *parentCache,
				   const char *indexfile,
				   const char *subhierarchy);

static int list_newshared(const char *skipped_pattern,
			  struct list_sharable_info *shared_info)
{
	return list_newshared_shortcut(skipped_pattern, shared_info,
				       NEWSHARED,
				       NULL, NULL, NULL);
}

static int list_newshared_shortcut(const char *skipped_pattern,
				   struct list_sharable_info *shared_info,
				   const char *acc_pfix,
				   struct maildir_shindex_cache *parentCache,
				   const char *indexfile,
				   const char *subhierarchy)
{
	struct list_newshared_info lni;
	struct maildir_shindex_cache *curcache=NULL;

	lni.acc_pfix=acc_pfix;
	lni.skipped_pattern=skipped_pattern;
	lni.shared_info=shared_info;
	lni.dorecurse=1;

	/* Try for some common optimization, to avoid expanding the
	** entire #shared hierarchy, taking advantage of the cache list.
	*/

	for (;;)
	{
		const char *p;
		size_t i;
		bool eof;

		if (strcmp(skipped_pattern, "%") == 0)
		{
			lni.dorecurse=0;
			break;
		}

		if (strncmp(skipped_pattern, "%" HIERCHS,
			    sizeof("%" HIERCHS)-1) == 0)
		{
			curcache=maildir_shared_cache_read(parentCache,
							   indexfile,
							   subhierarchy);
			if (!curcache)
				return 0;

			lni.acc_pfix=acc_pfix;
			lni.skipped_pattern=skipped_pattern
				+ sizeof("%" HIERCHS)-1;
			lni.parentCache=curcache;

			for (i=0; i<curcache->nrecords; i++)
			{
				int rc=0;

				if (i == 0)
				{
					curcache->indexfile.startingpos=0;

					eof=maildir::newshared_nextAt(
						&curcache->indexfile,
						[&]
						{
							rc=list_newshared_skiplevel(
								&curcache->indexfile,
								&lni);
						});
				}
				else
					eof=maildir::newshared_next(
						&curcache->indexfile,
						[&]
						{
							rc=list_newshared_skiplevel(
								&curcache->indexfile,
								&lni);
						});
				if (rc || eof)
				{
					fprintf(stderr, "ERR:maildir_newshared_next failed: %s\n",
						strerror(errno));
					break;
				}
			}
			return 0;
		}

		for (p=skipped_pattern; *p; p++)
			if (*p == HIERCH ||
			    ((lni.shared_info->flags & LIST_CHECK1FOLDER) == 0
			     && (*p == '*' || *p == '%')))
				break;

		if (*p && *p != HIERCH)
			break;

		curcache=maildir_shared_cache_read(parentCache, indexfile,
						   subhierarchy);
		if (!curcache)
			return 0;

		for (i=0; i < curcache->nrecords; i++)
		{
			char *n=maildir_info_imapmunge(curcache->records[i]
						       .name);

			if (!n)
				write_error_exit(0);

			if (strlen(n) == (size_t)(p-skipped_pattern) &&
			    strncmp(n, skipped_pattern, p-skipped_pattern) == 0)
			{
				free(n);
				break;
			}
			free(n);
		}

		if (i >= curcache->nrecords) /* not found */
			return 0;

		if (*p)
			++p;


		std::string q;

		q.reserve(strlen(acc_pfix)+(p-skipped_pattern)+1);

		q=acc_pfix;
		q.insert(q.end(), skipped_pattern, p);

		lni.acc_pfix=q.c_str();
		lni.skipped_pattern=p;
		lni.parentCache=curcache;

		curcache->indexfile.startingpos=curcache->records[i].offset;

		int rc=0;

		maildir::newshared_nextAt(
			&curcache->indexfile,
			[&]
			{
				rc=list_newshared_skipcb(
					&curcache->indexfile,
					&lni
				);
			});
		return rc;

	}

	if (!indexfile)
		indexfile=maildir_shared_index_file();

	maildir::newshared_enum(
		indexfile,
		[&]
		(maildir_newshared_enum_cb *cb)
		{
			list_newshared_cb(cb, &lni);
		});

	return 0;
}

static void list_newshared_cb(struct maildir_newshared_enum_cb *cb,
			      struct list_newshared_info *lni)
{
	const char *name=cb->name;
	const char *homedir=cb->homedir;
	const char *maildir=cb->maildir;
	char *munged_name=maildir_info_imapmunge(name);

	if (!munged_name)
		write_error_exit(0);

	std::string n{munged_name};
	free(munged_name);

	if (homedir == NULL)
	{
		struct list_newshared_info new_lni= *lni;

		std::string new_pfix;

		new_pfix.reserve(strlen(lni->acc_pfix)+n.size()+1);

		new_pfix=lni->acc_pfix;
		new_pfix += n;

		n=new_pfix;
		new_lni.acc_pfix=n.c_str();
		add_hier(lni->shared_info->hierarchies, n);
		hier_entry(n, lni->shared_info->hierarchies);
		n += hierchs;
		if (lni->dorecurse)
		{
			maildir::newshared_enum(
				maildir,
				[&]
				(maildir_newshared_enum_cb *cb)
				{
					list_newshared_cb(cb, &new_lni);
				});
		}
	}
	else
	{
		std::string new_pfix;
		struct stat stat_buf;

		new_pfix=maildir::location(homedir, maildir);

		if (stat(new_pfix.c_str(), &stat_buf) < 0 ||
		    /* maildir inaccessible, perhaps another server? */

		    (stat_buf.st_dev == homedir_dev &&
		     stat_buf.st_ino == homedir_ino))
		    /* Exclude ourselves from the shared list */
		{
			return;
		}

		new_pfix.reserve(strlen(lni->acc_pfix)+n.size());

		new_pfix=lni->acc_pfix;
		new_pfix+= n;

		n=new_pfix;

		new_pfix.reserve(strlen(homedir)+strlen(maildir)+1);


		if (*maildir == '/')
			new_pfix=maildir;
		else
		{
			new_pfix=homedir;
			new_pfix += "/";
			new_pfix += maildir;
		}

		/*		if (lni->dorecurse) */

		maildir_scan(new_pfix, n, *lni->shared_info);
#if 0
		else
		{
			folder_entry(n, lni->shared_info->pattern,
				     lni->shared_info->flags,
				     lni->shared_info->folders,
				     lni->shared_info->hierarchies);
		}
#endif
	}
}

static int list_newshared_skiplevel(struct maildir_newshared_enum_cb *cb,
				    struct list_newshared_info *lni)
{
	char *munged_name=maildir_info_imapmunge(cb->name);
	std::string n{munged_name};
	free(munged_name);

	std::string p;

	p.reserve(strlen(lni->acc_pfix)+n.size()+sizeof(HIERCHS)-1);

	int rc;
	const char *save_skip;

	p=lni->acc_pfix;
	p += n;
	p += HIERCHS;

	save_skip=lni->acc_pfix;
	lni->acc_pfix=p.c_str();

	rc=list_newshared_skipcb(cb, lni);
	lni->acc_pfix=save_skip;
	return rc;
}

static int list_newshared_skipcb(struct maildir_newshared_enum_cb *cb,
				 struct list_newshared_info *lni)
{
	std::string dir, inbox_name;

	if (cb->homedir == NULL)
		return list_newshared_shortcut(lni->skipped_pattern,
					       lni->shared_info,
					       lni->acc_pfix,
					       lni->parentCache,
					       cb->maildir,
					       cb->name);

	inbox_name=lni->acc_pfix;

	if (!inbox_name.empty() && inbox_name.back() == HIERCH)
		inbox_name.pop_back();/* Strip trailing hier separator */

	dir.reserve(strlen(cb->homedir)+strlen(cb->maildir)+2);

	if (*cb->maildir == '/')
		dir=cb->maildir;
	else
	{
		dir=cb->homedir;
		dir += "/";
		dir += cb->maildir;
	}

	maildir_scan(dir, inbox_name, *lni->shared_info);
	return 0;
}
/*
**	IMAP sucks.  Here's why.
*/


bool mailbox_scan(const char *reference, const char *name,
		  int list_options,
		  const mailbox_scan_cb_t &callback_func)
{
	list_sharable_info shared_info;
	int	isnullname= *name == 0;

	shared_info.pattern.reserve(strlen(reference)+strlen(name)+2);

	shared_info.pattern=reference;

	if (!shared_info.pattern.empty() &&
	    shared_info.pattern.back() == HIERCH)
		shared_info.pattern.pop_back(); /* Strip trailing . for now */

	if (!shared_info.pattern.empty())
	{
		if (!maildir::info_imap_find(shared_info.pattern,
					     getenv("AUTHENTICATED")))
		{
			return (true); /* Invalid reference */
		}
	}

	/* Combine reference and name. */
	if (!shared_info.pattern.empty() && *name)
		shared_info.pattern += hierchs;

	shared_info.pattern += name;

	if (*name)
	{
		if (shared_info.pattern.back() == HIERCH)
			shared_info.pattern.pop_back();	/* strip trailing . */

	}

	int	found_hier=MAILBOX_NOSELECT;
	int	is_interesting;
	int	j,bad_pattern;

	const char *obsolete;
	int check_all_folders=0;
	char hiersepbuf[8];
	bool callback_rc=true;

	obsolete=getenv("IMAP_CHECK_ALL_FOLDERS");
	if (obsolete && atoi(obsolete))
		check_all_folders=1;

	obsolete=getenv("IMAP_OBSOLETE_CLIENT");

	if (obsolete && atoi(obsolete) == 0)
		obsolete=0;

	/* Allow up to ten wildcards */

	j=0;
	for (auto c:shared_info.pattern)
		if (c == '*' || c == '%')	++j;
	bad_pattern= j > 10;

	if (list_options & LIST_CHECK1FOLDER)
		bad_pattern=0;

	if (bad_pattern)
	{
		errno=EINVAL;
		return false;
	}

	match_mailbox_prep(shared_info.pattern);

	shared_info.flags=list_options;
	shared_info.callback_func=callback_func;

	if (!(list_options & LIST_SUBSCRIBED))
	{
		if (strncmp(shared_info.pattern.c_str(), NEWSHARED,
			    sizeof(NEWSHARED)-1) == 0)
		{
			list_newshared(shared_info.pattern.c_str() +
				       sizeof(NEWSHARED)-1,
				       &shared_info);
		}
		else
		{
			maildir_scan(".", INBOX, shared_info);

			/* List sharable maildirs */

			maildir::list_sharable(
				".",
				[&]
				(const std::string &n)
				{
					list_sharable(n, shared_info);
				});
		}
	}
	else
	{
		list_subscribed(shared_info.pattern, list_options,
				shared_info.folders,
				shared_info.hierarchies);

		/* List shared folders */

		maildir::list_shared(
			".",
			[&]
			(const std::string &n)
			{
				list_sharable(n, shared_info);
			});
	}

	for (auto &hp : shared_info.folders)
	{
		struct hierlist *d;
		int mb_flags;

		is_interesting= -1;

		if (hp.hier == INBOX || check_all_folders)
			is_interesting=hasnewmsgs(hp.hier);

		strcat(strcat(strcpy(hiersepbuf, "\""), hierchs), "\"");

		mb_flags=0;

		if (is_interesting == 0)
		{
			mb_flags|=MAILBOX_UNMARKED;
		}
		if (is_interesting > 0)
		{
			mb_flags|=MAILBOX_MARKED;
		}

		if (!(d=search_hier(shared_info.hierarchies, hp.hier)))
		{
			mb_flags |=
				obsolete ? MAILBOX_NOINFERIORS:MAILBOX_NOCHILDREN;
		}
		else
		{
			d->flag=1;
			if (!obsolete)
				mb_flags |= MAILBOX_CHILDREN;
		}

		if (isnullname)
			found_hier=mb_flags;
		else
			if (callback_rc)
				callback_rc=callback_func
					({hiersepbuf, hp.hier,
						 mb_flags | list_options});
	}

	for (auto &hp:shared_info.hierarchies)
	{
		match_mailbox_prep(hp.hier);

		if (match_mailbox(hp.hier, shared_info.pattern, list_options)
		    && hp.flag == 0)
		{
			int mb_flags=MAILBOX_NOSELECT;

			if (!obsolete)
				mb_flags |= MAILBOX_CHILDREN;

			if (isnullname)
				found_hier=mb_flags;
			else
			{
				strcat(strcat(strcpy(hiersepbuf, "\""),
					      hierchs), "\"");

				if (callback_rc)
					callback_rc=callback_func
						({hiersepbuf,
							 hp.hier,
							 mb_flags |
							 list_options});
			}
		}
	}

	if (isnullname)
	{
		const char *namesp="";

		if (strncmp(shared_info.pattern.c_str(), NEWSHARED,
			    sizeof(NEWSHARED)-1) == 0)
			namesp=NEWSHARED;

		strcat(strcat(strcpy(hiersepbuf, "\""), hierchs), "\"");

		if (callback_rc)
			callback_rc=callback_func(
				{
					hiersepbuf,
					namesp,
					found_hier | list_options
				});
	}
	return callback_rc;
}

static bool match_recursive(std::string::const_iterator, std::string::const_iterator,
			    std::string::const_iterator, std::string::const_iterator,
			    int);

static void match_mailbox_prep(std::string &name)
{
	/* First component, INBOX, is case insensitive */

	static const char inbox[]="INBOX";

	if (strncasecmp(name.c_str(), inbox, sizeof(inbox)-1) == 0)
		std::copy(inbox, inbox+sizeof(inbox)-1,
			  name.begin());

	/* ... except that "shared" should be lowercase ... */

	static const char shared[]="shared";

	if (strncasecmp(name.c_str(), shared, sizeof(shared)-1) == 0)
		std::copy(shared, shared+sizeof(shared)-1,
			  name.begin());
}

static bool match_mailbox(const std::string &name,
			  const std::string &pattern, int list_options)
{
        if (list_options & LIST_CHECK1FOLDER)
                return name==pattern;

	return (match_recursive(name.begin(), name.end(),
				pattern.begin(), pattern.end(), HIERCH));
}

static bool match_recursive(std::string::const_iterator nameb,
			    std::string::const_iterator namee,
			    std::string::const_iterator patternb,
			    std::string::const_iterator patterne,
			    int hierch)
{
	for (;;)
	{
		if (patternb != patterne && *patternb == '*')
		{
			while (1)
			{
				if (match_recursive(nameb, namee,
						    patternb+1, patterne,
						    hierch))
					return true;

				if (nameb == namee)
					break;
				++nameb;
			}
			return false;
		}
		if (patternb != patterne && *patternb == '%')
		{
			while (1)
			{
				if (match_recursive(nameb, namee,
						    patternb+1, patterne,
						    hierch))
					return true;

				if (*nameb == hierch)
					break;

				if (nameb == namee)
					break;
				++nameb;
			}
			return false;
		}
		if (nameb == namee && patternb == patterne)
			break;
		if (nameb == namee || patternb == patterne)
			return false;

		if (*nameb != *patternb)	return false;
		++nameb;
		++patternb;
	}
	return true;
}
