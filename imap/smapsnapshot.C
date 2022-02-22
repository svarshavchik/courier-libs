/*
** Copyright 2003 S. Varshavchik.
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
#include	"imapscanclient.h"
#include	"imapwrite.h"

#include	"maildir/config.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirrequota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirwatch.h"

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

#include <fstream>
#include <sstream>

extern "C" int keywords();

/*
** Implement SMAP snapshots.  A snapshot is implemented, essentially, by
** saving the current folder index, restoring it, then doing a noop().
**
** The snapshot file saves uids, not complete filenames.  The complete
** filenames are already in courierimapuiddb.  Filenames are long, and saving
** them can result in huge snapshot files for large folders.  So only uids
** are saved, and when the snapshot is restored the courierimapuiddb file is
** read to obtain the filenames.
*/

extern char *current_mailbox;
extern imapscaninfo current_maildir_info;
extern char *readline(unsigned i, FILE *);

static std::string snapshot_dir; /* Directory with snapshots */

static std::string snapshot_last; /* Last snapshot */
static std::string snapshot_cur;  /* Current snapshot */

static int index_dirty;
static int snapshots_enabled;

extern void set_time(const std::string &tmpname, time_t timestamp);
extern void smapword(const char *);

struct snapshot_list {
	struct snapshot_list *next;

	std::string filename;
	std::string prev;
	time_t mtime=0;
};

/*
** When cleaning up a snapshot directory, we need to know whether there's
** a later snapshot that claims that this snapshot is the previous
** snapshot (we can safely dump snapshots that were previous snapshots
** of previous snapshots).
*/

static struct snapshot_list *find_next_snapshot(struct snapshot_list *s,
						const char *n)
{
	const char *p;

	p=strrchr(n, '/');

	if (p)
		n=p+1;

	while (s)
	{
		size_t i=s->prev.rfind('/');

		if (i != s->prev.npos)
		{
			++i;

			if (s->prev.substr(i) == n)
				return s;
		}
		s=s->next;
	}
	return NULL;
}

/*
** Delete a snapshot structure, and the actual file
*/

static void delete_snapshot(struct snapshot_list *snn)
{
	std::string n;

	n.reserve(snapshot_dir.size()+snn->filename.size()+1);

	n=snapshot_dir;
	n+="/";
	n+=snn->filename;
	unlink(n.c_str());
	delete snn;
}

/*
** Restore a snapshot
*/

static int restore_snapshot2(const std::string &snapshot_dir,
			     std::istream &snapshot_fp,
			     imapscaninfo *new_index);

/*
** Part 1: process the first header line of a snapshot file, and allocate a
** new folder index list.
*/

static int restore_snapshot(const std::string &dir, std::istream &snapshot_fp,
			    std::string &last_snapshot)
{
	int format;
	unsigned long s_nmessages, s_uidv, s_nextuid;
	imapscaninfo new_index;

	std::string buf;

	if (!std::getline(snapshot_fp, buf))
		return 0;

	auto p=buf.find(':');
	if (p == buf.npos)
		p=buf.size();

	last_snapshot.clear();

	format=0;

	if (!(std::istringstream{buf.substr(0, p)}
				>> format >> s_nmessages >> s_uidv >> s_nextuid)
	|| format != SNAPSHOTVERSION)
		return 0; /* Don't recognize the header */

	/* Save the previous snapshot ID */

	if (p < buf.size())
	{
		last_snapshot.reserve(dir.size()+buf.size()-p+4);

		last_snapshot=dir;
		last_snapshot += "/";
		last_snapshot += buf.substr(p);
	}

	new_index.msgs.resize(s_nmessages);

	new_index.uidv=s_uidv;
	new_index.nextuid=s_nextuid;

	if (restore_snapshot2(dir, snapshot_fp, &new_index))
	{
		current_maildir_info=std::move(new_index);
		return 1;
	}

	last_snapshot.clear();

	return 0;
}

/*
** Part 2: combine the snapshot and courierimapuiddb, create a halfbaked
** index from the combination.
*/

static int restore_snapshot2(const std::string &snapshot_dir,
			     std::istream &fp,
			     imapscaninfo *new_index)
{
	int version;
	unsigned long uidv;
	unsigned long nextuid;
	std::string uid_line;
	unsigned long uid=0;

	std::ifstream courierimapuiddb{snapshot_dir + "/../" + IMAPDB};

	if (!courierimapuiddb)
		return 0; /* Can't open the uiddb file, no dice */

	version=0;
	if (!std::getline(courierimapuiddb, uid_line) ||
	    !(std::istringstream{uid_line} >> version >> uidv >> nextuid) ||
	    version != IMAPDBVERSION /* Do not recognize the uiddb file */

	    || uidv != new_index->uidv /* Something major happened, abort */ )
	{
		return 0;
	}

	if (std::getline(courierimapuiddb, uid_line))
	{
		if (!std::istringstream{uid_line} >> uid)
		{
			return 0;
		}
	}

	/*
	** Both the snapshot file and courierimapuiddb should be in sorted
	** order, by UIDs, rely on that and do what amounts to a merge sort.
	*/

	for (auto &msg:new_index->msgs)
	{
		unsigned long s_uid;
		std::string flag_buf;

		size_t p;

		if (!std::getline(fp, flag_buf) ||
		    !(std::istringstream{flag_buf} >> s_uid))
			/* Corrupted file */

		{
			return 0;
		}

		if ((p=flag_buf.find(':')) == uid_line.npos)
		{
			/* Corrupted file */
			return 0;
		}

		msg.uid=s_uid;

		/* Try to fill in the filenames to as much of an extent as
		** possible.  If IMAPDB no longer has a particular uid listed,
		** that's ok, because the message is now gone, so we just
		** insert an empty filename, which will be expunged by
		** noop() processing, after the snapshot is restored.
		*/

		while (courierimapuiddb && uid <= s_uid)
		{
			size_t uid_line_pos;

			if (uid == s_uid &&
			    (uid_line_pos=uid_line.find(' ')) != uid_line.npos)
				/* Jackpot */
			{
				msg.filename=uid_line.substr(uid_line_pos+1);

				msg.filename += MDIRSEP;
				msg.filename += flag_buf.substr(p+1);
			}

			if (std::getline(courierimapuiddb, uid_line))
			{
				if (!std::istringstream{uid_line} >> uid)
				{
					return 0;
				}
			}
		}
	}

	if (keywords())
		imapscan_restoreKeywordSnapshot(fp, new_index);
	return 1;
}

void snapshot_select(int flag)
{
	snapshots_enabled=flag;
}

/*
** Initialize snapshots for an opened folder.
**
** Parameters:
**
**     folder - the path to a folder that's in the process of opening.
**
**     snapshot - not NULL if the client requested a snapshot restore.
**
** Exit code:
**
** When a snapshot is requested, a non-zero exit code means that the
** snapshot has been succesfully restored, and current_mailbox is now
** initialized based on the snapshot.  A zero exit code means that the
** snapshot has not been restored, and snapshot_init() needs to be called
** again with snapshot=NULL in order to initialize the snapshot structures.
**
** When a snapshot is not requested, the exit code is always 0
*/

int snapshot_init(const char *folder, const char *snapshot)
{
	struct snapshot_list *sl=NULL;
	DIR *dirp;
	struct dirent *de;
	struct snapshot_list *snn, **ptr;
	int cnt;
	std::string new_dir;
	int rc=0;
	std::string new_snapshot_cur;
	std::string new_snapshot_last;

	new_dir=folder;
	new_dir += "/";
	new_dir += SNAPSHOTDIR;

	mkdir(new_dir.c_str(), 0755); /* Create, if doesn't exist */

	if (snapshot)
	{
		if (*snapshot == 0 || strchr(snapshot, '/') ||
		    *snapshot == '.') /* Monkey business */
		{
			return 0;
		}

		new_snapshot_cur.reserve(new_dir.size() +
					 strlen(snapshot) + 2);

		new_snapshot_cur=new_dir;
		new_snapshot_cur += "/";
		new_snapshot_cur += snapshot;

		std::ifstream fp{new_snapshot_cur};

		if (fp && restore_snapshot(new_dir, fp, new_snapshot_last))
		{
			set_time(new_snapshot_cur, time(NULL));
			rc=1; /* We're good to go.  Finish everything else */
		}

		fp.close();

		if (!rc) /* Couldn't get the snapshot, abort */
		{
			return 0;
		}
	}

	snapshot_last=new_snapshot_last;
	snapshot_cur=new_snapshot_cur;
	snapshot_dir=new_dir;

	index_dirty=1;

	/* Get rid of old snapshots as follows */

	/* Step 1, compile a list of snapshots, sorted in mtime order */

	dirp=opendir(snapshot_dir.c_str());

	while (dirp && (de=readdir(dirp)) != NULL)
	{
		struct stat stat_buf;

		if (de->d_name[0] == '.') continue;

		std::string n=snapshot_dir + "/" + de->d_name;

		std::ifstream fp{n};

		if (fp.is_open())
		{
			std::string buf;

			if (std::getline(fp, buf) &&
			    stat(n.c_str(), &stat_buf) == 0)
			{
				int fmt=0;

				if ((std::istringstream{buf} >> fmt) &&
				    fmt == SNAPSHOTVERSION)
				{
					snn=new snapshot_list;

					snn->filename=de->d_name;

					auto p=buf.find(':');

					if (p < buf.npos)
						snn->prev=buf.substr(p+1);

					snn->mtime=stat_buf.st_mtime;

					for (ptr= &sl; *ptr;
					     ptr=&(*ptr)->next)
					{
						if ( (*ptr)->mtime >
						     snn->mtime)
							break;
					}

					n.clear();

					snn->next= *ptr;
					*ptr=snn;
				}

			}
		}
		if (!n.empty())
		{
			unlink(n.c_str());
		}
	}
	if (dirp)
		closedir(dirp);

	/* Step 2: drop snapshots that are definitely obsolete */

	for (ptr= &sl; *ptr; )
	{
		if ((snn=find_next_snapshot(sl, (*ptr)->filename.c_str())) &&
		    find_next_snapshot(sl, snn->filename.c_str()))
		{
			snn= *ptr;

			*ptr=snn->next;

			delete_snapshot(snn);
		}
		else
			ptr=&(*ptr)->next;

	}

	/* If there are more than 10 snapshots, drop older snapshots */

	cnt=0;
	for (snn=sl; snn; snn=snn->next)
		++cnt;

	if (cnt > 10)
	{
		time_t now=time(NULL);

		while (sl && sl->mtime < now &&
		       (now - sl->mtime) > 60 * 60 * 24 * (7 + (cnt-10)*2))
		{
			snn=sl;
			sl=sl->next;
			delete_snapshot(snn);
			--cnt;
		}
	}

	/* All right, put a lid on 50 snapshots */

	while (cnt > 50)
	{
		snn=sl;
		sl=sl->next;
		delete_snapshot(snn);
		--cnt;
	}

	while (sl)
	{
		snn=sl;
		sl=sl->next;
		delete snn;
	}
	return rc;
}

/*
** Something changed in the folder, so next time snapshot_save() was called,
** take a snapshot.
*/

void snapshot_needed()
{
	index_dirty=1;
}

/*
** Save a snapshot, if the folder was changed.
*/

void snapshot_save()
{
	if (!index_dirty || !snapshots_enabled)
		return;

	index_dirty=0;

	maildir::tmpcreate_info createInfo;

	createInfo.maildir=current_mailbox;
	createInfo.uniq="snapshot";

	const char *h=getenv("HOSTNAME");

	if (h)
		createInfo.hostname=h;
	createInfo.doordie=true;

	auto fp=createInfo.fp();

	if (!fp)
	{
		perror("maildir_tmpcreate_fd");
		return;
	}

	createInfo.newname=snapshot_dir +
		createInfo.tmpname.substr(createInfo.tmpname.rfind('/'));

	fprintf(fp, "%d %lu %lu %lu", SNAPSHOTVERSION,
		(unsigned long)current_maildir_info.msgs.size(),
		current_maildir_info.uidv,
		current_maildir_info.nextuid);

	if (!snapshot_cur.empty())
		fprintf(fp, ":%s",
			snapshot_cur.substr(snapshot_cur.rfind('/')+1).c_str());
	fprintf(fp, "\n");

	for (auto &p:current_maildir_info.msgs)
	{
		auto q=p.filename.rfind(MDIRSEP[0]);

		fprintf(fp, "%lu:%s\n", p.uid,
			q == p.filename.npos ? ""
			: p.filename.c_str()+(q+1));
	}

	if (keywords())
		imapscan_saveKeywordSnapshot(fp, &current_maildir_info);

	if (fflush(fp) < 0 || ferror(fp) < 0)
	{
		fclose(fp);
		perror(createInfo.tmpname.c_str());
		unlink(createInfo.tmpname.c_str());
		return;
	}
	fclose(fp);
	if (rename(createInfo.tmpname.c_str(), createInfo.newname.c_str()) < 0)
	{
		perror(createInfo.tmpname.c_str());
		unlink(createInfo.tmpname.c_str());
		return;
	}
	if (!snapshot_last.empty())
	{
		unlink(snapshot_last.c_str()); /* Obsolete snapshot */
	}

	snapshot_last=snapshot_cur;
	snapshot_cur=createInfo.newname;

	writes("* SNAPSHOT ");
	smapword(snapshot_cur.substr(snapshot_cur.rfind('/')+1).c_str());
	writes("\n");
}
