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

static int do_imapscan_maildir2(imapscaninfo *, const std::string &,
				int, int, struct uidplus_info *);
void imapscanfail(const char *p);

#if SMAP
extern int smapflag;
#endif

extern int keywords();
extern void set_time(const char *tmpname, time_t timestamp);

static void imapscan_readKeywords(const char *maildir,
				  imapscaninfo *scaninfo);

extern bool imapmaildirlock(imapscaninfo *scaninfo,
			    const std::string &maildir,
			    const std::function< bool() >&callback);

imapscaninfo_base::imapscaninfo_base()
{
	if ((keywordList=(libmail_kwHashtable *)malloc(sizeof(*keywordList))) == NULL)
		write_error_exit(0);

	libmail_kwhInit(keywordList);
}

imapscaninfo::imapscaninfo(imapscaninfo &&other)
{
	operator=(std::move(other));
}

imapscaninfo &imapscaninfo::operator=(imapscaninfo &&other)
{
	// TODO: fix this
	auto watcher1=watcher;
	auto watcher2=other.watcher;
	auto msgs1=msgs;
	auto msgs2=other.msgs;
	auto keywords1=keywordList;
	auto keywords2=other.keywordList;

	watcher=nullptr;
	other.watcher=nullptr;
	msgs=nullptr;
	other.msgs=nullptr;
	keywordList=nullptr;
	other.keywordList=nullptr;

	std::swap(static_cast<imapscaninfo_base &>(*this),
		  static_cast<imapscaninfo_base &>(other));

	watcher=watcher2;
	other.watcher=watcher1;
	msgs=msgs2;
	other.msgs=msgs1;
	keywordList=keywords2;
	other.keywordList=keywords1;
	return *this;
}

struct libmail_kwMessage *imapscan_createKeyword(imapscaninfo *a,
					      unsigned long n)
{
	if (n >= a->nmessages)
		return NULL;

	if (a->msgs[n].keywordMsg == NULL)
	{
		struct libmail_kwMessage *m=libmail_kwmCreate();

		if (!m)
			write_error_exit(0);

		m->u.userNum=n;
		a->msgs[n].keywordMsg=m;
	}

	return a->msgs[n].keywordMsg;
}

int imapscan_maildir(imapscaninfo *scaninfo,
		     const std::string &dir, int leavenew, int ro,
		     struct uidplus_info *uidplus)
{
	return imapmaildirlock(
		scaninfo, dir,
		[&]
		{
			return do_imapscan_maildir2(
				scaninfo,
				dir,
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

static char *readbuf;
static unsigned readbufsize=0;

char *readline(unsigned i, FILE *fp)
{
int	c;

	for (;;)
	{
		if (i >= 10000)
			--i;	/* DOS check */

		if (i >= readbufsize)
		{
			char	*p= readbuf ? (char *)realloc(readbuf, readbufsize+256):
				(char *)malloc(readbufsize+256);

			if (!p)	write_error_exit(0);
			readbuf=p;
			readbufsize += 256;
		}

		c=getc(fp);
		if (c == EOF || c == '\n')
		{
			readbuf[i]=0;
			return (c == EOF ? 0:readbuf);
		}
		readbuf[i++]=c;
	}
}

static int do_imapscan_maildir2(imapscaninfo *scaninfo,
				const std::string &dir,
				int leavenew, int ro,
				struct uidplus_info *uidplus)
{
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

	scaninfo->msgs=0;
	if (tempinfo_array.size() &&
	    (scaninfo->msgs=(struct imapscanmessageinfo *)
	     malloc(tempinfo_array.size() * sizeof(*scaninfo->msgs))) == 0)
		write_error_exit(0);
	scaninfo->nmessages=tempinfo_array.size();
	scaninfo->uidv=uidv;
	scaninfo->left_unseen=left_unseen;
	scaninfo->nextuid=nextuid+left_unseen;

	for (size_t i=0; i<tempinfo_array.size(); i++)
	{
		scaninfo->msgs[i].uid=tempinfo_array[i].uid;
		scaninfo->msgs[i].filename=
			my_strdup(tempinfo_array[i].filename.c_str());
		scaninfo->msgs[i].keywordMsg=NULL;
		scaninfo->msgs[i].copiedflag=0;
#if SMAP
		if (smapflag)
			scaninfo->msgs[i].recentflag=0;
		else
#endif
			scaninfo->msgs[i].recentflag=
				tempinfo_array[i].isrecent;
		scaninfo->msgs[i].changedflags=0;
	}

	imapscan_readKeywords(dir.c_str(), scaninfo);


	return (0);
}

static int try_maildir_open(const char *dir, struct imapscanmessageinfo *n)
{
int	fd;
char	*filename=maildir_filename(dir, 0, n->filename);
char	*p;

	if (!filename)
	{
		return (-1);
	}

	p=strrchr(filename, '/')+1;

	if (strcmp(p, n->filename))
	{
		n->changedflags=1;
		free(n->filename);
		n->filename=(char *)malloc(strlen(p)+1);
		if (!n->filename)	write_error_exit(0);
		strcpy(n->filename, p);
	}

	fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	free(filename);
	return (fd);
}

int imapscan_openfile(const char *dir, imapscaninfo *i, unsigned j)
{
struct imapscanmessageinfo *n;

	if (j >= i->nmessages)
	{
		errno=EINVAL;
		return (-1);
	}

	n=i->msgs+j;

	return (try_maildir_open(dir, n));
}

imapscaninfo_base::~imapscaninfo_base()
{
	unsigned	n;

	if (watcher)
	{
		maildirwatch_free(watcher);
	}

	if (msgs)
	{
		for (n=0; n<nmessages; n++)
		{
			if (msgs[n].filename)
				free(msgs[n].filename);

			if (msgs[n].keywordMsg)
				libmail_kwmDestroy(msgs[n].keywordMsg);

		}
		free(msgs);
	}

	if (keywordList)
	{
		if (libmail_kwhCheck(keywordList) < 0)
			write_error_exit("INTERNAL ERROR: Keyword hashtable "
					 "memory corruption.");

		free(keywordList);
	}
}

/*
** Keyword-related stuff  See README.imapkeywords.html for more information.
*/

extern char *current_mailbox;

int imapscan_updateKeywords(const char *filename,
			    struct libmail_kwMessage *newKeyword)
{
	char *tmpname, *newname;
	int rc;

	if (maildir_kwSave(current_mailbox, filename, newKeyword,
			   &tmpname, &newname, 0))
	{
		perror("maildir_kwSave");
		return -1;
	}

	rc=rename(tmpname, newname);

	if (rc)
	{
		perror(tmpname);
		unlink(tmpname);
	}
	free(tmpname);
	free(newname);
	return rc;
}

static unsigned long hashFilename(const char *fn, imapscaninfo *info)
{
	unsigned long hashBucket=0;

	while (*fn && *fn != MDIRSEP[0])
	{
		hashBucket=(hashBucket << 1) ^ (hashBucket & 0x8000 ? 0x1301:0)
			^ (unsigned char)*fn++;
	}
	hashBucket=hashBucket & 0xFFFF;

	return hashBucket % info->nmessages; /* Cannot get here if its zero */
}

struct imapscanReadKeywordInfo {
	struct maildir_kwReadInfo ri;

	imapscaninfo *messages;
	int hashedFilenames;
};

static struct libmail_kwMessage **findMessageByFilename(const char *filename,
						     int autocreate,
						     size_t *indexNum,
						     void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;

	size_t l;
	struct imapscanmessageinfo *i;

	imapscaninfo *scaninfo=info->messages;

	if (!info->hashedFilenames)
	{
		unsigned long n;

		for (n=0; n<scaninfo->nmessages; n++)
			scaninfo->msgs[n].firstBucket=NULL;

		for (n=0; n<scaninfo->nmessages; n++)
		{
			unsigned long bucket=hashFilename(scaninfo->msgs[n]
							  .filename,
							  scaninfo);

			scaninfo->msgs[n].nextBucket=
				scaninfo->msgs[bucket].firstBucket;

			scaninfo->msgs[bucket].firstBucket=scaninfo->msgs+n;
		}
		info->hashedFilenames=1;
	}

	l=strlen(filename);

	for (i= scaninfo->nmessages ?
		     scaninfo->msgs[hashFilename(filename, scaninfo)]
		     .firstBucket:NULL; i; i=i->nextBucket)
	{
		if (strncmp(i->filename, filename, l))
			continue;

		if (i->filename[l] == 0 ||
		    i->filename[l] == MDIRSEP[0])
			break;
	}

	if (!i)
		return NULL;

	if (indexNum)
		*indexNum= i-scaninfo->msgs;

	if (!i->keywordMsg && autocreate)
		imapscan_createKeyword(info->messages, i-scaninfo->msgs);

	return &i->keywordMsg;
}

static size_t getMessageCount(void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;

	return info->messages->nmessages;
}

static const char *getMessageFilename(size_t n, void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;

	if (n >= info->messages->nmessages)
		return NULL;

	return info->messages->msgs[n].filename;
}

static void updateKeywords(size_t n, struct libmail_kwMessage *kw,
			   void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;

	if (n >= info->messages->nmessages)
		return;

	if (info->messages->msgs[n].keywordMsg)
		libmail_kwmDestroy(info->messages->msgs[n].keywordMsg);

	kw->u.userNum=n;
	info->messages->msgs[n].keywordMsg=kw;
}

static struct libmail_kwHashtable * getKeywordHashtable(void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;

	return info->messages->keywordList;
}

static struct libmail_kwMessage **findMessageByIndex(size_t indexNum,
						  int autocreate,
						  void *voidarg)
{
	struct imapscanReadKeywordInfo *info=
		(struct imapscanReadKeywordInfo *)voidarg;
	struct imapscanmessageinfo *i;

	if (indexNum >= info->messages->nmessages)
		return NULL;

	i= &info->messages->msgs[indexNum];

	if (!i->keywordMsg && autocreate)
		imapscan_createKeyword(info->messages, indexNum);

	return &i->keywordMsg;
}

static void initri(struct imapscanReadKeywordInfo *rki)
{
	memset(rki, 0, sizeof(*rki));

	rki->ri.findMessageByFilename= &findMessageByFilename;
	rki->ri.getMessageCount= &getMessageCount;
	rki->ri.findMessageByIndex= &findMessageByIndex;
	rki->ri.getKeywordHashtable= &getKeywordHashtable;
	rki->ri.getMessageFilename= &getMessageFilename;
	rki->ri.updateKeywords= &updateKeywords;
	rki->ri.voidarg= rki;
}

void imapscan_readKeywords(const char *maildir,
			   imapscaninfo *scaninfo)
{
	struct imapscanReadKeywordInfo rki;

	initri(&rki);

	do
	{
		unsigned long i;

		for (i=0; i<scaninfo->nmessages; i++)
			if (scaninfo->msgs[i].keywordMsg)
			{
				libmail_kwmDestroy(scaninfo->msgs[i]
						      .keywordMsg);
				scaninfo->msgs[i].keywordMsg=NULL;
			}

		rki.messages=scaninfo;

		if (maildir_kwRead(maildir, &rki.ri) < 0)
			write_error_exit(0);

	} while (rki.ri.tryagain);
}

int imapscan_restoreKeywordSnapshot(FILE *fp, imapscaninfo *scaninfo)
{
	struct imapscanReadKeywordInfo rki;

	initri(&rki);

	rki.messages=scaninfo;
	return maildir_kwImport(fp, &rki.ri);
}

int imapscan_saveKeywordSnapshot(FILE *fp, imapscaninfo *scaninfo)
{
	struct imapscanReadKeywordInfo rki;

	initri(&rki);

	rki.messages=scaninfo;
	return maildir_kwExport(fp, &rki.ri);
}
