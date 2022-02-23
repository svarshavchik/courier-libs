/*
** Copyright 1998 - 2011 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<fcntl.h>
#include	<time.h>
#include	<errno.h>
#include	"numlib/numlib.h"

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#include	<sys/stat.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
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

#include	"liblock/config.h"
#include	"liblock/liblock.h"
#include	"maildir/config.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirwatch.h"
#include	"liblock/mail.h"

#include	"imapscanclient.h"
#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"imapd.h"

#include	<fstream>
#include	<sstream>
#include	<iterator>
#include	<algorithm>
#include	<utility>
#include	<unordered_map>

/*
** RFC 2060: "A good value to use for the unique identifier validity value is a
** 32-bit representation of the creation date/time of the mailbox."
**
** Well, Y2038k is on the horizon, time to push to reset button.
**
*/

#ifndef IMAP_EPOCH
#define IMAP_EPOCH	1000000000
#endif

static int do_imapscan_maildir2(imapscaninfo *,
				int, int, struct uidplus_info *);
void imapscanfail(const char *p);
extern imapscaninfo current_maildir_info;

#if SMAP
extern int smapflag;
#endif

extern int keywords();
extern void set_time(const std::string &tmpname, time_t timestamp);

extern bool imapmaildirlock(imapscaninfo *scaninfo,
			    const std::string &maildir,
			    const std::function< bool() >&callback);

imapscaninfo::imapscaninfo(const std::string &current_mailbox)
	: imapscaninfo_base{current_mailbox},
	  watcher{current_mailbox}
{
}

imapscaninfo::imapscaninfo(imapscaninfo *prev)
	: imapscaninfo{prev->current_mailbox}
{
	current_mailbox_acl=prev->current_mailbox_acl;
}

imapscaninfo::imapscaninfo(imapscaninfo &&other) noexcept
	: imapscaninfo_base{""}, watcher{other.current_mailbox}
{
	operator=(std::move(other));
}

imapscaninfo &imapscaninfo::operator=(imapscaninfo &&other) noexcept
{
	auto watcher_save=std::move(watcher);

	watcher=std::move(other.watcher);

	other.watcher=std::move(watcher_save);

	std::swap(static_cast<imapscaninfo_base &>(*this),
		  static_cast<imapscaninfo_base &>(other));

	return *this;
}

int imapscan_maildir(imapscaninfo *scaninfo, int leavenew, int ro,
		     struct uidplus_info *uidplus)
{
	return imapmaildirlock(
		scaninfo, scaninfo->current_mailbox,
		[&]
		{
			return do_imapscan_maildir2(
				scaninfo,
				leavenew,
				ro,
				uidplus) == 0;
		}) ? 0:-1;
}

/* This structure is a temporary home for the filenames */

struct tempinfo {
	std::string filename;
	unsigned long uid;
	bool	found=false;
	bool	isrecent=false;

	template<typename T>
	tempinfo(T &&t, unsigned long uid, bool isrecent=false)
		: filename{std::forward<T>(t)}, uid{uid}, isrecent{isrecent}
	{
	}

	tempinfo(const tempinfo &)=default;
	tempinfo(tempinfo &&)=default;

	tempinfo &operator=(const tempinfo &)=default;
	tempinfo &operator=(tempinfo &&)=default;
} ;

static int fnamcmp(const char *a, const char *b)
{
	long ai, bi;
	char ca, cb;

	ai = atol(a);
	bi = atol(b);
	if(ai - bi)
		return ai - bi;

	do
	{
		ca= *a++;
		cb= *b++;

		if (ca == ':') ca=0;
		if (cb == ':') cb=0;
	} while (ca && cb && ca == cb);


	return ( (int)(unsigned char)ca - (int)(unsigned char)cb);
}

static bool sort_by_filename(const tempinfo &a, struct tempinfo &b)
{
	return fnamcmp(a.filename.c_str(), b.filename.c_str()) < 0;
}

static int sort_by_filename_status(const tempinfo &a, const tempinfo &b)
{
	if ( a.found && b.found )
	{
		if ( a.uid < b.uid )
			return (-1);
		if ( a.uid > b.uid )
			return (1);
		return (0);	/* What the fuck??? */
	}
	if ( a.found )	return (-1);
	if ( b.found )	return (1);

	return (fnamcmp( a.filename.c_str(), b.filename.c_str()));
}

/* Binary search on an array of tempinfos which is sorted by filenames */

struct search_by_filename {

	bool operator()(const tempinfo &a, const std::string &b) const
	{
		return fnamcmp(a.filename.c_str(), b.c_str()) < 0;
	}

	bool operator()(const std::string &a, const tempinfo &b)
	{
		return fnamcmp(a.c_str(), b.filename.c_str()) < 0;
	}
};

void imapscanfail(const char *p)
{
	fprintf(stderr, "ERR: Failed to create cache file: %s (%s)\n", p,
		getenv("AUTHENTICATED"));
#if	HAVE_STRERROR
	fprintf(stderr, "ERR: Error: %s\n", strerror(errno));
#endif
}

// Look up filenames using an unordered map of pointers to strings. Hash
// and compare the pointers by ignoring the filename suffix.

namespace {
	struct filename_hash {

		size_t operator()(const std::string *p) const
		{
			size_t n=p->rfind(MDIRSEP[0]);
			if (n == p->npos)
				n=p->size();
			return std::hash<std::string>{}(
				p->substr(0, n)
			);
		};
	};

	struct filename_cmp {
		bool operator()(const std::string *a,
				const std::string *b) const
		{
			return fnamcmp(a->c_str(), b->c_str()) == 0;
		}
	};
}

static int do_imapscan_maildir2(imapscaninfo *scaninfo,
				int leavenew, int ro,
				struct uidplus_info *uidplus)
{
	auto &dir=scaninfo->current_mailbox;
	std::string dbfilepath;
	std::string newdbfilepath;

	std::vector<tempinfo> tempinfo_array, newtempinfo_array;
	std::fstream fp;
	unsigned long uidv, nextuid;
	int	version;
	struct	stat	stat_buf;
	DIR	*dirp;
	struct	dirent *de;
	unsigned long left_unseen=0;
	int	dowritecache=0;

	if (is_sharedsubdir(dir.c_str()))
		maildir_shared_sync(dir.c_str());


	/* Step 0 - purge the tmp directory */

	maildir_purgetmp(dir.c_str());

	dbfilepath=dir;
	dbfilepath += "/" IMAPDB;

	/*
	** We may need to rebuild the UID cache file.  Create the new cache
	** file in the tmp subdirectory.
	*/

	{
		maildir::tmpcreate_info createInfo;

		createInfo.maildir=dir;

		const char *p=getenv("HOSTNAME");

		if (!p)
			p="";
		createInfo.hostname=p;
		createInfo.doordie=true;

		int fd;

		if ((fd=createInfo.fd()) < 0)
		{
			write_error_exit(0);
		}

		newdbfilepath=createInfo.tmpname;
	}

	/* Step 1 - read the cache file */
	std::string line;

	fp.open(dbfilepath, std::ios_base::in);

	version=0;

	if (fp.is_open() &&
	    std::getline(fp, line))
	{
		std::istringstream i{line};

		if (!(i >> version >> uidv >> nextuid))
			version=0;
	}

	if (version == IMAPDBVERSION)
	{
		while (std::getline(fp, line))
		{
			unsigned long uid;
			std::istringstream i{line};

			if (!(i >> uid >> std::ws)) continue;

			std::string filename{
				std::istreambuf_iterator<char>{i},
				std::istreambuf_iterator<char>{}
			};

			tempinfo_array.emplace_back(filename, uid);
		}
		fp.close();
	}
	else if(!ro)
	{

	/* First time - create the cache file */

		fp.close();
		nextuid=1;
		fp.open(newdbfilepath,
			std::ios_base::out | std::ios_base::trunc);

		if (!fp || stat(newdbfilepath.c_str(), &stat_buf) != 0)
		{
			fp.close();
			imapscanfail(newdbfilepath.c_str());

			/* bk: ignore error */
			unlink(newdbfilepath.c_str());
			unlink(dbfilepath.c_str());
			/*
			free(dbfilepath);
			unlink(newdbfilepath);
			free(newdbfilepath);
			return (-1);
			*/
		}
		uidv=stat_buf.st_mtime - IMAP_EPOCH;
		dowritecache=1;
	}
	else
	{
		nextuid=1;
		uidv=time(0) - IMAP_EPOCH;
	}

	while (uidplus)
	{
		if (uidplus->tmpkeywords)
			if (rename(uidplus->tmpkeywords,
				   uidplus->newkeywords) < 0)
			{
				struct libmail_kwGeneric g;

				/*
				** Maybe courierimapkeywords needs to be
				** created.
				*/

				libmail_kwgInit(&g);
				libmail_kwgReadMaildir(&g, dir.c_str());
				libmail_kwgDestroy(&g);

				rename(uidplus->tmpkeywords,
				       uidplus->newkeywords);
			}

		maildir_movetmpnew(uidplus->tmpfilename,
				   uidplus->curfilename);

		if (uidplus->mtime)
			set_time (uidplus->curfilename, uidplus->mtime);

		std::string s=strrchr(uidplus->curfilename, '/')+1;
		auto sp=s.rfind(MDIRSEP[0]);

		if (sp != s.npos)
			s.resize(sp);

		tempinfo_array.emplace_back(std::move(s), nextuid);
		uidplus->uid=nextuid;
		nextuid++;

		uidplus=uidplus->next;
		dowritecache=1;
	}

	if (!fp.is_open())
		fp.open(newdbfilepath,
			std::ios_base::out | std::ios_base::trunc);

	if (!fp.is_open())
	{
		imapscanfail(newdbfilepath.c_str());

		/* bk: ignore error */
		unlink(newdbfilepath.c_str());
		unlink(dbfilepath.c_str());
		/*
		free(dbfilepath);
		unlink(newdbfilepath);
		free(newdbfilepath);
		while (tempinfo_list)
		{
			tempinfoptr=tempinfo_list;
			tempinfo_list=tempinfoptr->next;
			free(tempinfoptr->filename);
			free(tempinfoptr);
		}
		return (-1);
		*/
	}

	/*
	** Sort the array by filename.
	*/

	std::sort(tempinfo_array.begin(), tempinfo_array.end(),
		  sort_by_filename);

	/* Step 2 - read maildir/cur.  Search the array.  Mark found files. */

	dirp=opendir((std::string{dir} + "/cur").c_str());

	while (dirp && (de=readdir(dirp)) != 0)
	{
		if (de->d_name[0] == '.')	continue;

		std::string p=de->d_name;
		std::string q=p;

		/* IMAPDB doesn't store the filename flags, so strip them */
		auto sp=p.rfind(MDIRSEP[0]);

		if (sp != p.npos)
			p.resize(sp);

		auto iter=std::lower_bound(
			tempinfo_array.begin(),
			tempinfo_array.end(),
			p,
			search_by_filename{});

		if (iter != tempinfo_array.end() &&
		    fnamcmp(iter->filename.c_str(), p.c_str()) == 0)
		{
			iter->found=true;
			iter->filename=q;
				/* Keep the full filename */
			continue;
		}

		newtempinfo_array.emplace_back(q, 0, true);
		dowritecache=1;
	}
	if (dirp)	closedir(dirp);

	/* Step 3 - purge messages that no longer exist in the maildir */

	tempinfo_array.erase(
		std::remove_if(
			tempinfo_array.begin(),
			tempinfo_array.end(),
			[&]
			(const tempinfo &a)
			{
				if (!a.found)
					dowritecache=1;

				return !a.found;
			}),
		tempinfo_array.end());

	/* Step 4 - add messages in cur that are not in the cache file */

	tempinfo_array.insert(tempinfo_array.end(),
			      newtempinfo_array.begin(),
			      newtempinfo_array.end());

	newtempinfo_array.clear();

	/* Step 5 - read maildir/new.  */

	if (leavenew)
	{
		std::string p=dir;

		p += "/new";

		dirp=opendir(p.c_str());
		while (dirp && (de=readdir(dirp)) != 0)
		{
			if (de->d_name[0] == '.')	continue;
			++left_unseen;
		}
		if (dirp)	closedir(dirp);
	}
	else
		/*
		** Some filesystems keel over if we delete files while
		** reading the directory where the files are.
		** Accomodate them by processing 20 files at a time.
		*/
	{
		bool keepgoing;
		std::string p=dir;

		p += "/new";

		do
		{
			keepgoing=false;

			std::vector<std::string> newbuf;
			std::vector<std::string> curbuf;

			dirp=opendir(p.c_str());
			while (dirp && (de=readdir(dirp)) != 0)
			{
				if (de->d_name[0] == '.')	continue;

				std::string z=de->d_name;

				newbuf.push_back(p + "/" + z);

				if (z.find(MDIRSEP[0]) == z.npos)
					z += + MDIRSEP "2,";

				std::string c=dir;

				c.reserve(c.size()+z.size()+5);

				c += "/cur/";
				c += z;
				curbuf.push_back(c);

				dowritecache=1;

				tempinfo_array.emplace_back(
					std::move(z), 0, true
				);

				if (curbuf.size() >= 20)
				{
					keepgoing=true;
					break;
				}
			}

			if (dirp)	closedir(dirp);

			for (size_t i=0; i<newbuf.size(); ++i)
			{
				if (rename(newbuf[i].c_str(),
					   curbuf[i].c_str()))
				{
					fprintf(stderr,
						"ERR: rename(%s,%s) failed:"
						" %s\n",
						newbuf[i].c_str(),
						curbuf[i].c_str(),
						strerror(errno));
					keepgoing=false;
					/* otherwise we could have infinite loop */
				}
			}
		} while (keepgoing);
	}

	/*
	** Step 6: sort existing messages by UIDs, new messages will
	** sort after all messages with UIDs, and new messages are
	** sorted by filename, so that they end up roughly in the order
	** they were received.
	*/

	std::sort(tempinfo_array.begin(),
		  tempinfo_array.end(),
		  []
		  (const tempinfo &a, const tempinfo &b)
		  {
			  return sort_by_filename_status(a, b) < 0;
		  });

	/* Assign new UIDs */

	for (auto &i:tempinfo_array)
		if ( !i.found )
		{
			i.uid= nextuid++;
			dowritecache=1;
		}

	/* bk: ignore if failed to open file */
	if (!ro && dowritecache && fp.is_open())
	{
	/* Step 7 - write out the new cache file */

		version=IMAPDBVERSION;

		fp << version << " " << uidv << " " << nextuid << "\n";

		for (auto &i:tempinfo_array)
		{
			size_t p=i.filename.rfind(MDIRSEP[0]);

			if (p == i.filename.npos)
				p=i.filename.size();
			fp << i.uid
			   << " " << i.filename.substr(0, p)
			   << "\n";
		}

		fp << std::flush;

		if (!fp.good() || (fp.close(), !fp.good()))
		{
			imapscanfail(dir.c_str());
			fp.close();
			/* bk: ignore if failed */
			unlink(newdbfilepath.c_str());
			unlink(dbfilepath.c_str());
			/*
			free(tempinfo_array);
			free(dbfilepath);
			unlink(newdbfilepath);
			free(newdbfilepath);
			while (tempinfo_list)
			{
				tempinfoptr=tempinfo_list;
				tempinfo_list=tempinfoptr->next;
				free(tempinfoptr->filename);
				free(tempinfoptr);
			}
			return (-1);
			*/
		}
		/* bk */
		else

			rename(newdbfilepath.c_str(), dbfilepath.c_str());
	}
	else
	{
		fp.close();
		unlink(newdbfilepath.c_str());
	}

	/* Step 8 - create the final scaninfo array */

	scaninfo->msgs.clear();
	scaninfo->msgs.resize(tempinfo_array.size());
	scaninfo->uidv=uidv;
	scaninfo->left_unseen=left_unseen;
	scaninfo->nextuid=nextuid+left_unseen;

	for (size_t i=0; i<tempinfo_array.size(); i++)
	{
		scaninfo->msgs[i].uid=tempinfo_array[i].uid;
		scaninfo->msgs[i].filename=
			tempinfo_array[i].filename;
#if SMAP
		if (smapflag)
			scaninfo->msgs[i].recentflag=0;
		else
#endif
			scaninfo->msgs[i].recentflag=
				tempinfo_array[i].isrecent;
	}

	// When loading keywords we'll need to look up the message by its
	// filename.
	std::unordered_map<const std::string *,
			   imapscanmessageinfo *, filename_hash, filename_cmp
			   > lookup;

	for (auto &msg:scaninfo->msgs)
	{
		lookup.emplace(&msg.filename, &msg);
	}

	scaninfo->keywords.load(
		dir,
		[&]
		(const keyword_meta &km)
		{
			return scaninfo->msgs.at(km.index).filename;
		},
		[&]
		(const std::string &filename,
		 mail::keywords::list &keywords)
		{
			auto iter=lookup.find(&filename);

			if (iter == lookup.end())
				return false;

			iter->second->keywords.keywords(
				scaninfo->keywords,
				keywords,
				(size_t)(iter->second-&scaninfo->msgs[0])
			);

			return true;
		},
		[]
		{
			return false;
		});
	return (0);
}

static int try_maildir_open(const std::string &dir, imapscanmessageinfo *n)
{
	auto filename=maildir::filename(dir, "", n->filename);

	if (filename.empty())
		return -1;

	auto p=filename.rfind('/')+1;

	if (n->filename != filename.c_str()+p)
	{
		n->changedflags=1;

		n->filename=filename.substr(p);
	}

	return maildir::semisafeopen(filename, O_RDONLY, 0);
}

int imapscan_openfile(imapscaninfo *i, unsigned j)
{
	if (j >= i->msgs.size())
	{
		errno=EINVAL;
		return (-1);
	}

	return (try_maildir_open(i->current_mailbox, &i->msgs[j]));
}

imapscaninfo_base::imapscaninfo_base(const std::string &current_mailbox)
	: current_mailbox{current_mailbox}
{
}

imapscaninfo_base::~imapscaninfo_base()=default;

/*
** Keyword-related stuff  See README.imapkeywords.html for more information.
*/

void imapscan_updateKeywords(const std::string &filename,
			     const mail::keywords::list &keywords)
{
	return imapscan_updateKeywords(
		current_maildir_info.current_mailbox, filename, keywords);
}

void imapscan_updateKeywords(const std::string &maildir,
			     const std::string &filename,
			     const mail::keywords::list &keywords)
{
	while (!mail::keywords::update(
		       maildir, filename, keywords))
	{
		std::unordered_map<std::string,
			mail::keywords::message<std::string>
				   > temp_keymap;

		mail::keywords::hashtable<std::string> temp_hashtable;

		temp_hashtable.load(
			maildir,
			[]
			(const std::string &s)
			{
				return s;
			},
			[&]
			(const std::string &filename,
			 mail::keywords::list &keywords)
			{
				temp_keymap[filename].keywords(
					temp_hashtable,
					keywords
				);
				return true;
			},
			[]
			{
				return false;
			});
	}
}

void imapscan_restoreKeywordSnapshot(std::istream &i,
				     imapscaninfo *scaninfo)
{
	std::unordered_map<const std::string *,
			   imapscanmessageinfo *, filename_hash, filename_cmp
			   > lookup;

	for (auto &msg:scaninfo->msgs)
	{
		lookup.emplace(&msg.filename, &msg);
	}

	mail::keywords::read_keywords_from_file(
		i,
		[&]
		(const std::string &filename,
		 mail::keywords::list &keywords)
		{
			auto iter=lookup.find(&filename);

			if (iter == lookup.end())
				return ;

			iter->second->keywords.keywords(
				scaninfo->keywords,
				keywords,
				(size_t)(iter->second-&scaninfo->msgs[0])
			);
		});
}

void imapscan_saveKeywordSnapshot(FILE *fp, imapscaninfo *scaninfo)
{
	scaninfo->keywords.save_keywords_to_file(
		fp,
		[&]
		(const keyword_meta &km)
		{
			return scaninfo->msgs.at(km.index).filename;
		});
}

unsigned long imapscaninfo::unseen() const
{
	auto n=left_unseen;

	for (const auto &msg:msgs)
	{
		auto p=msg.filename.rfind(MDIRSEP[0]);

		if (p != msg.filename.npos
		    && msg.filename.size() - p > 2 &&
		    msg.filename[p+1] == '2' &&
		    msg.filename[p+2] == ',' &&
		    msg.filename.find('S', p) !=
		    msg.filename.npos)
			continue;
		++n;
	}

	return n;
}
