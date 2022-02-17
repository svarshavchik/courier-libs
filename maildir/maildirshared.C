/*
** Copyright 2000-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
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
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<stdio.h>
#include	<ctype.h>
#include	<errno.h>
#include	<fcntl.h>

#include	"maildirmisc.h"
#include	"maildircreate.h"
#include	"maildirsharedrc.h"

#include <algorithm>
#include <fstream>
#include <string>

/* Prerequisited for shared folder support */

#if	HAVE_READLINK
#if	HAVE_SYMLINK
#if	HAVE_DBOBJ

#define	YES_WE_CAN_DO_SHARED	1

#endif
#endif
#endif

#if	YES_WE_CAN_DO_SHARED

#include	"dbobj.h"

extern "C" void maildir_shared_fparse(char *, char **, char **);

namespace {

	// Helper class used by shared_subscribe, which creates a shared
	// maildir subdirectory. Each subdirectory that makes up the
	// maildir gets created here.

	struct shared_created {
		std::string name;
		bool done=false;

		// The constructor assembles the full pathname
		shared_created(const std::string &dir,
			       const std::string &suffix)
			: name{dir + "/" + suffix}
		{
		}

		// This operator overload tries to create the subdirectory
		// and returns true if it was created.

		operator bool()
		{
			done=mkdir(name.c_str(), 0700) == 0;

			return done;
		}

		// The destructor deletes the subdirectory, if the done
		// flag is set.
		//
		// if there's an error creating the subdirectory, the
		// destructor rmdirs it. If all subdirectories get created
		// the "done" flag gets cleared, leaving everything behind.

		~shared_created()
		{
			if (done)
				rmdir(name.c_str());
		}
	};
}

bool maildir::shared_subscribe(const std::string &maildir,
			       const std::string &folder)
{
	if (maildir.empty())
		return shared_subscribe(".", folder);

	auto namep=folder.find('.');

	if (namep == folder.npos)
	{
		errno=EINVAL;
		return false;
	}

	if (shareddir(maildir, folder).empty())	/* valid folder name? */
	{
		errno=EINVAL;
		return false;
	}

	bool found=false;
	std::string dir;

	for (int pass=0; pass<2; pass++)
	{
		std::ifstream i{
			pass ? maildir::shared_filename(maildir)
			: std::string{MAILDIRSHAREDRC}
		};

		std::string line;

		while (std::getline(i, line))
		{
			if (line.empty())
				continue;
			char *nameb, *namee, *dirb, *dire;

			maildir::shared_fparse(
				&line[0],
				&line[0]+line.size(),
				nameb, namee, dirb, dire);

			if (nameb < namee &&
			    (size_t)(namee - nameb) == namep &&
			    std::equal(nameb, namee, folder.begin()))
			{
				found=true;
				dir=std::string{dirb, dire};
				break;
			}
		}
		if (found)
			break;
	}

	if (found)
	{
		/*
		** We will create:
		**
		**  maildir/shared-folders/folder/(name)
		**
		**  there we'll have subdirs cur/new/tmp  and shared link
		*/

		std::string buf;

		buf.reserve(maildir.size()+folder.size() +
			    sizeof("/" SHAREDSUBDIR "//shared"));

		buf=maildir;
		buf += "/" SHAREDSUBDIR;

		mkdir(buf.c_str(), 0700);
		buf += "/";

		buf.insert(buf.end(), folder.begin(), folder.begin()+namep);
		mkdir(buf.c_str(), 0700);

		std::string name=folder.substr(namep+1);

		shared_created base_dir{buf, name},
			tmpsubdir{base_dir.name, "tmp"},
			cursubdir{base_dir.name, "cur"},
			newsubdir{base_dir.name, "new"};

		if (!base_dir || !tmpsubdir || !cursubdir || !newsubdir)
			return false;

		std::string link;

		link.reserve(dir.size()+name.size()+1);

		link=dir;
		link+="/.";
		link+=name;

		if (symlink(link.c_str(), (base_dir.name + "/shared").c_str()))
			return false;

		base_dir.done=false;
		tmpsubdir.done=false;
		cursubdir.done=false;
		newsubdir.done=false;
		return true;
	}
	errno=ENOENT;
	return false;
}

bool maildir::shared_unsubscribe(const std::string &maildir,
				 const std::string &folder)
{
	if (maildir.empty())
		return shared_unsubscribe(".", folder);

	auto dir=shareddir(maildir, folder);

	if (dir.empty())
	{
		errno=EINVAL;
		return false;
	}

	if (maildir_del(dir.c_str()))
		return false;

	dir.resize(dir.rfind('/')); /* Try to remove the whole folder dir */
	rmdir(dir.c_str());
	return true;
}

/*                    LET'S SYNC IT                  */

/* Step 1 - safely create a temporary database */

static bool create_db(struct dbobj *obj,
		      const std::string &dir,
		      std::string &dbname)
{
	maildir::tmpcreate_info createInfo;

	createInfo.maildir=dir;
	createInfo.uniq="sync";
	createInfo.doordie=true;

	auto fd=createInfo.fd();

	if (fd < 0)
	{
		perror(dir.c_str());
		return false;
	}
	close(fd);

	dbobj_init(obj);
	if (dbobj_open(obj, createInfo.tmpname.c_str(), "N") < 0)
	{
		perror(createInfo.tmpname.c_str());
		unlink(createInfo.tmpname.c_str());
		return false;
	}

	dbname=createInfo.tmpname;
	return true;
}

/*
** Populate the DB by building the db with the messages in the sharable
** folder's cur.  The key is the stripped message filename, the value is
** the complete message filename.
*/

static bool build_db(const std::string &shared, struct dbobj *obj)
{
	DIR	*dirp;
	struct	dirent *de;

	dirp=opendir((shared+"/cur").c_str());
	while (dirp && (de=readdir(dirp)) != 0)
	{

		if (de->d_name[0] == '.')
			continue;

		std::string a=de->d_name;
		std::string b=de->d_name;

		auto p=a.rfind(MDIRSEP[0]);

		if (p != a.npos)
			a.resize(p);

		if (dbobj_store(obj, a.c_str(), a.size(),
				b.c_str(), b.size(), "R"))
		{
			perror("dbobj_store");
			closedir(dirp);
			return false;
		}
	}
	if (dirp)	closedir(dirp);
	return true;
}

static bool update_link(const std::string &curdir,
			const std::string &linkname,
			const std::string &linkvalue,
			const std::string &shareddir,
			const char *msgfilename,
			size_t msgfilenamelen);

/*
**	Now, read our synced cur directory, and make sure that the soft
**	links are up to date.  Remove messages that have been deleted from
**	the sharable maildir, and make sure that the remaining links are
**	valid.
*/

static bool update_cur(const std::string &cur,
		       const std::string &shared, struct dbobj *obj)
{
	DIR	*dirp;
	struct	dirent *de;

	dirp=opendir(cur.c_str());
	while (dirp && (de=readdir(dirp)) != 0)
	{
		if (de->d_name[0] == '.')	continue;

		/*
		** Strip the maildir flags, and look up the message in the
		** db.
		*/

		std::string cur_base=de->d_name;

		size_t p=cur_base.rfind(MDIRSEP[0]);

		if (p != cur_base.npos)
			cur_base.resize(p);

		size_t cur_name_len;
		auto cur_name_ptr=dbobj_fetch(obj,
					      cur_base.c_str(),
					      cur_base.size(),
					      &cur_name_len, "");

		/* If it's there, delete the db entry. */

		if (cur_name_ptr)
			dbobj_delete(obj, cur_base.c_str(), cur_base.size());

		/*
		** We'll either delete this soft link, or check its
		** contents, so we better build its complete pathname in
		** any case.
		*/

		cur_base=cur;
		cur_base += "/";
		cur_base += de->d_name;

		if (!cur_name_ptr)	/* Removed from sharable dir */
		{
			unlink(cur_base.c_str());
			continue;
		}

		auto linked_name_len=shared.size()+strlen(de->d_name)+100;
			/* should be enough */

		auto linked_name_buf=new char[linked_name_len];

		auto n=readlink(cur_base.c_str(),
				linked_name_buf, linked_name_len);

		if (n < 0)
		{
			/* This is stupid, let's just unlink this nonsense */

			n=0;
		}

		if (n == 0 || (size_t)n >= linked_name_len ||
		    (linked_name_buf[n]=0,
		     !update_link(cur,
				  cur_base,
				  linked_name_buf, shared, cur_name_ptr,
				  cur_name_len)))
		{
			free(cur_name_ptr);
			unlink(cur_base.c_str());
			delete[] linked_name_buf;
			closedir(dirp);
			return false;
		}
		delete[] linked_name_buf;
		free(cur_name_ptr);
	}
	if (dirp)	closedir(dirp);
	return true;
}

/* Update the link pointer */

static bool update_link(const std::string &curdir,
			const std::string &linkname,
			const std::string &linkvalue,
			const std::string &shareddir,
			const char *msgfilename,
			size_t msgfilenamelen)
{
	std::string p;

	p.reserve(shareddir.size()+sizeof("/cur/")-1+msgfilenamelen);

	p=shareddir;
	p += "/cur/";
	p.insert(p.end(), msgfilename, msgfilename+msgfilenamelen);

	if (linkvalue == p)
	{
		/* the link is good */

		return (true);
	}

	/* Ok, we want this to be an atomic operation. */

	maildir::tmpcreate_info createInfo;

	createInfo.maildir=curdir;
	createInfo.uniq="relink";
	createInfo.doordie=true;

	auto fd=createInfo.fd();

	if (fd < 0)
		return false;

	close(fd);
	unlink(createInfo.tmpname.c_str());

	if (symlink(p.c_str(), createInfo.tmpname.c_str()) < 0 ||
	    rename(createInfo.tmpname.c_str(), linkname.c_str()) < 0)
	{
		perror(createInfo.tmpname.c_str());
		return false;
	}
	return true;
}

/* and now, anything that's left in the temporary db must be new messages */

static bool newmsgs(const std::string &cur,
		    const std::string &shared,
		    struct dbobj *obj)
{
	char	*key, *val;
	size_t	keylen, vallen;

	maildir::tmpcreate_info createInfo;

	createInfo.maildir=cur;
	createInfo.uniq="newlink";
	createInfo.doordie=true;

	auto fd=createInfo.fd();

	if (fd < 0)
		return false;

	close(fd);

	unlink(createInfo.tmpname.c_str());

	for (key=dbobj_firstkey(obj, &keylen, &val, &vallen); key;
		key=dbobj_nextkey(obj, &keylen, &val, &vallen))
	{

		std::string slink;

		slink.reserve(shared.size()+sizeof("/cur/")-1+vallen);

		slink=shared;
		slink+="/cur/";
		slink.insert(slink.end(), val, val+vallen);
		free(val);

		if (symlink(slink.c_str(), createInfo.tmpname.c_str()))
		{
			perror(createInfo.tmpname.c_str());
			return false;
		}

		slink.reserve(cur.size()+sizeof("/new/" MDIRSEP "2,")-1+keylen);

		slink=cur;
		slink+="/new/";
		slink.insert(slink.end(), key, key+keylen);
		slink += MDIRSEP "2,";

		if (rename(createInfo.tmpname.c_str(), slink.c_str()))
		{
			return false;
		}
	}
	return true;
}

void maildir::shared_sync(const std::string &dir)
{
	auto shared=maildir::getlink(dir + "/shared");

	if (shared.empty())
		return;

	struct	dbobj obj;
	struct	stat	stat1, stat2;
	int	fd;

	maildir_purgetmp(dir.c_str());	/* clean up after myself */
	maildir_getnew(dir.c_str(), 0, NULL, NULL);

	maildir_purgetmp(shared.c_str());
	maildir_getnew(shared.c_str(), 0, NULL, NULL);

	/* Figure out if we REALLY need to sync something */

	auto shared_update_name=dir + "/shared-timestamp";

	if (stat(shared_update_name.c_str(), &stat1) == 0)
	{
		if ( stat( (shared+"/new").c_str(), &stat2) == 0 &&
		     stat2.st_mtime < stat1.st_mtime &&
		     stat( (shared+"/cur").c_str(), &stat2) == 0 &&
		     stat2.st_mtime < stat1.st_mtime)
			return;
	}

	if ((fd=maildir::safeopen(shared_update_name, O_RDWR|O_CREAT, 0600))
	    >= 0)
	{
		if (write(fd, "", 1) < 0)
			perror("write");
		close(fd);
	}

	std::string dbname;

	if (!create_db(&obj, dir, dbname))	return;

	if (!build_db(shared, &obj))
	{
		dbobj_close(&obj);
		unlink(dbname.c_str());
		return;
	}

	auto cur=dir+"/cur";

	if (update_cur(cur, shared, &obj))
	{
		cur.resize(cur.size()-3);
		cur += "new";

		if (update_cur(cur, shared, &obj))
		{
			cur.resize(cur.rfind('/'));
			newmsgs(cur, shared, &obj);
		}
	}

	dbobj_close(&obj);
	unlink(dbname.c_str());
}

bool maildir::shared_isro(const std::string &maildir)
{
	if (access((maildir + "/shared/cur").c_str(), W_OK) == 0)
		return true;

	return false;
}

void maildir::unlinksharedmsg(const std::string &filename)
{
	std::string buf=getlink(filename);

	if (!buf.empty())
	{
		struct stat stat_buf;

		int rc=unlink(buf.c_str());

		/*
		** If we FAILED to unlink the real message in the real
		** sharable folder, but the message still exists, it means
		** that we do not have the permission to do so, so do not
		** purge this folder.  Instead, remove the T flag from
		** this message.
		*/

		if (rc && stat(buf.c_str(), &stat_buf) == 0)
		{
			std::string cpy=filename;

			size_t p=cpy.rfind(MDIRSEP[0]);

			if (p != cpy.npos && cpy.find('/', p) == cpy.npos
			    && cpy.size()-p > 2
			    && cpy[p+1] == '2' && cpy[p+2] == ','
			    && (p=cpy.find('T', p)) != cpy.npos)
			{
				cpy.erase(cpy.begin()+p);

				rename(filename.c_str(), cpy.c_str());
				fclose(fopen("/tmp/zz", "w"));

			}
			return;
		}
	}
	unlink(filename.c_str());
}

static void list_sharable2(const std::string &, const std::string &,
			   const std::function<void (const std::string &)> &);

void maildir::list_sharable(const std::string &maildir,
			    const std::function<void (const std::string &)> &cb)
{
	int pass;

	for (pass=0; pass<2; pass++)
	{
		std::ifstream fp{
			pass ? shared_filename(maildir.empty() ? ".":maildir)
			: MAILDIRSHAREDRC
		};

		if (!fp)	continue;

		std::string line;

		while (std::getline(fp, line))
		{
			if (line.empty())
				continue;

			char *b=&line[0];
			char *e=b+line.size();

			char *nameb, *namee, *dirb, *dire;

			shared_fparse(b, e, nameb, namee,
				      dirb, dire);

			if (nameb != namee)
				list_sharable2({nameb, namee},
					       {dirb, dire}, cb);
		}
	}
}

static void list_sharable2(const std::string &pfix,
			   const std::string &path,
			   const std::function<void (const std::string &)> &cb)
{
	DIR	*dirp;
	struct	dirent *de;
	struct	stat	stat_buf;

	dirp=opendir(path.c_str());
	while (dirp && (de=readdir(dirp)) != 0)
	{
		if (de->d_name[0] != '.')	continue;
		if (strcmp(de->d_name, ".") == 0 ||
			strcmp(de->d_name, "..") == 0)	continue;

		std::string z;

		z.reserve(path.size()+strlen(de->d_name)+12);

		z=path;

		z += "/";
		z += de->d_name;
		z += "/cur/.";

		if (stat(z.c_str(), &stat_buf))
		{
			continue;
		}

		z.reserve(pfix.size()+strlen(de->d_name)+1);

		z=pfix;
		z += de->d_name;
		cb(z);
	}
	if (dirp)	closedir(dirp);
}

void maildir::list_shared(const std::string &maildir,
			  const std::function<void (const std::string &)> &func)
{
	DIR	*dirp;
	struct	dirent *de;

	std::string sh;

	sh.reserve(maildir.size()+sizeof("/" SHAREDSUBDIR));

	sh=maildir;

	if (sh.empty())
		sh=".";

	sh += "/" SHAREDSUBDIR;

	dirp=opendir(sh.c_str());

	while (dirp && (de=readdir(dirp)) != 0)
	{
		DIR	*dirp2;
		struct	dirent *de2;

		if (de->d_name[0] == '.')	continue;

		std::string z;

		z.reserve(sh.size()+strlen(de->d_name)+1);

		z=sh;
		z+="/";
		z+=de->d_name;
		dirp2=opendir(z.c_str());

		while (dirp2 && (de2=readdir(dirp2)) != 0)
		{

			if (de2->d_name[0] == '.')	continue;

			std::string s;

			s.reserve(strlen(de->d_name)+strlen(de2->d_name)+1);

			s=de->d_name;
			s+=".";
			s+=de2->d_name;
			func(s);
		}
		if (dirp2)	closedir(dirp2);
	}
	if (dirp)	closedir(dirp);
}

#else

/* We cannot implement sharing */

bool maildir::shared_subscribe(const std::string &maildir,
			       const std::string &folder)
{
	errno=EINVAL;
	return false;
}

bool maildir::shared_unsubscribe(const std::string &maildir,
				 const std::string &folder)
{
	errno=EINVAL;
	return false;
}

void maildir::shared_sync(const std::string &dir)
{
}

bool maildir::shared_isro(const std::string &maildir)
{
	errno=EINVAL;
	return false;
}

void maildir::unlinksharedmsg(const std::string &filename)
{
}

/* We cannot implement sharing */

void maildir::list_sharable(const std::string &maildir,
			    const std::function<void (const std::string &)> &)
{
}

void maildir::list_shared(const std::string &maildir,
			  const std::function<void (const std::string &)> &)
{
}

#endif

int maildir_shared_subscribe(const char *maildir, const char *folder)
{
	return maildir::shared_subscribe(maildir ? maildir:"", folder)
		? 0:-1;
}

int maildir_shared_unsubscribe(const char *maildir, const char *folder)
{
	return maildir::shared_unsubscribe(maildir ? maildir:"", folder)
		? 0:-1;
}

void maildir_shared_sync(const char *dir)
{
	maildir::shared_sync(dir);
}

int maildir_sharedisro(const char *maildir)
{
	return maildir::shared_isro(maildir) ? 0:-1;
}

void maildir_unlinksharedmsg(const char *filename)
{
	maildir::unlinksharedmsg(filename);
}

void maildir_list_sharable(const char *maildir,
			   void (*func)(const char *, void *),
			   void *voidp)
{
	if (!maildir)
		maildir="";

	maildir::list_sharable(maildir,
			       [&]
			       (const std::string &s)
			       {
				       (*func)(s.c_str(), voidp);
			       });
}


void maildir_list_shared(const char *maildir,
	void (*func)(const char *, void *),
	void *voidp)
{
	if (!maildir)
		maildir="";

	maildir::list_shared(maildir,
			     [&]
			     (const std::string &s)
			     {
				     (*func)(s.c_str(), voidp);
			     });
}
