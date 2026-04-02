/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"config.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<errno.h>
#include	"sqwebmail.h"
#include	"maildir.h"
#include	"folder.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"cgi/cgi.h"
#include	"pref.h"
#include	"sqconfig.h"
#include	"dbobj.h"
#include	"auth.h"
#include	"acl.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirrequota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirinfo.h"
#include	"maildir/maildiraclt.h"
#include	"maildir/maildirsearch.h"
#include	"htmllibdir.h"

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<filesystem>
#include	<system_error>

namespace fs = std::filesystem;

#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UTIME_H
#include	<utime.h>
#endif

#include	<courier-unicode.h>

#include	"strftime.h"

#include <fstream>
#include <optional>
#include <set>
#include <unordered_map>

static time_t	current_time;

extern time_t rfc822_parsedt(const char *);
extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_mailboxid;

static std::string folderdatname;	/* Which folder has been cached */
static struct dbobj folderdat;
static const char *folderdatmode;	/* "R" or "W" */

static time_t cachemtime;	/* Modification time of the cache file */
static size_t new_cnt, all_cnt;
static int isoldestfirst;
static char sortorder;

static void createmdcache(const char *, const char *);

static void maildir_getfoldermsgs(const char *);

/* Change timestamp on a file */

static const char *parse_ul(const char *p, unsigned long *ul)
{
int	err=1;

	while (p && isspace((int)(unsigned char)*p))	++p;
	*ul=0;
	while (p && *p >= '0' && *p <= '9')
	{
		*ul *= 10;
		*ul += (*p-'0');
		err=0;
		++p;
	}
	if (err)	return (0);
	return (p);
}

static void change_timestamp(const char *filename, time_t t)
{
#if	HAVE_UTIME
struct utimbuf	ut;

	ut.actime=ut.modtime=t;
	utime(filename, &ut);
#else
#if	HAVE_UTIMES
struct timeval tv;

	tv.tv_sec=t;
	tv.tv_usec=0;
	utimes(filename, &tv);
#else
#error	You do not have utime or utimes function.  Upgrade your operating system.
#endif
#endif
}

/* Translate folder name into directory name */

static std::string xlate_mdir(const char *foldername)
{
	auto minfo=maildir::info_imap_find(foldername, login_returnaddr());
	if (!minfo)
	{
		cginocache();
		printf("Content-Type: text/plain\n\n"
		       "Unable to translate mailbox: %s\n",
		       foldername);
		cleanup();
		fake_exit(1);
	}

	if (!minfo.regular_maildir())
	{
		cginocache();
		printf("Content-Type: text/plain\n\n"
		       "Mailbox \"%s\" is not supported.\n",
		       foldername);
		cleanup();
		fake_exit(1);
		enomem();
	}

	return maildir::name2dir(minfo.homedir, minfo.maildir);
}

static std::string xlate_shmdir(const char *foldername)
{
	auto minfo=maildir::info_imap_find(foldername, login_returnaddr());

	if (!minfo)
	{
		printf("Content-Type: text/plain\n\n%s\n", foldername);
		enomem();
	}

	if (minfo.mailbox_type == MAILBOXTYPE_OLDSHARED)
		return maildir::shareddir(".", strchr(foldername, '.')+1);

	if (!minfo.regular_maildir())
	{
		enomem();
	}

	return maildir::name2dir(minfo.homedir, minfo.maildir);
}

/* Display message size in meaningfull form */

static void cat_n(char *buf, unsigned long n)
{
char	bb[MAXLONGSIZE+1];
char	*p=bb+sizeof(bb)-1;

	*p=0;
	do
	{
		*--p = "0123456789"[n % 10];
		n=n/10;
	} while (n);
	strcat(buf, p);
}

const char *showsize(unsigned long n)
{
static char	sizebuf[MAXLONGSIZE+10];

	/* If size is less than 1K bytes, display it as 0.xK */

	if (n < 1024)
	{
		strcpy(sizebuf, "0.");
		cat_n(sizebuf, (int)(10 * n / 1024 ));
		strcat(sizebuf, "K");
	}
	/* If size is less than 1 meg, display is as xK */

	else if (n < 1024 * 1024)
	{
		*sizebuf=0;
		cat_n(sizebuf, (unsigned long)(n+512)/1024);
		strcat(sizebuf, "K");
	}

	/* Otherwise, display in megabytes */

	else
	{
	unsigned long nm=(double)n / (1024.0 * 1024.0) * 10;

		*sizebuf=0;
		cat_n( sizebuf, nm / 10);
		strcat(sizebuf, ".");
		cat_n( sizebuf, nm % 10);
		strcat(sizebuf, "M");
	}
	return (sizebuf);
}

/*
** char *maildir_find(const char *maildir, const char *filename)
**	- find a message in a maildir
**
** Return the full path to the indicated message.  If the message flags
** in filename have changed, we search for the given message.
*/

std::string maildir_find(const char *folder, const char *filename)
{
	auto d=xlate_shmdir(folder);
	int	fd;

	if (d.empty())	return ("");
	auto p=maildir::filename(d, "", filename);

	std::string ret;

	if ((fd=open(p.c_str(), O_RDONLY)) >= 0)
	{
		close(fd);
		ret=p;
	}
	return ret;
}

/*
** char *maildir_basename(const char *filename)
**
** - return base name of the file (strip off cur or new, strip of trailing :)
*/

std::string maildir_basename(const char *filename)
{
	const char *q=strrchr(filename, '/');

	if (q)	++q;
	else	q=filename;
	std::string p{q};
	auto r=p.find(':');
	if (r != std::string::npos)
		p.erase(r);

	return (p);
}

/* Display message creation time.  If less than one week old (more or less)
** show day of the week, and time of day, otherwise show day, month, year
*/

static char *displaydate(time_t t)
{
struct tm *tmp=localtime(&t);
static char	datebuf[40];
const char *date_yfmt;
const char *date_wfmt;


	date_yfmt = getarg ("DSPFMT_YDATE");
	if (*date_yfmt == 0)
		date_yfmt = "%d %b %Y";

	date_wfmt = getarg ("DSPFMT_WDATE");
	if (*date_wfmt == 0)
		date_wfmt = "%a %I:%M %p";

	datebuf[0]='\0';
	if (tmp)
	{
		strftime(datebuf, sizeof(datebuf)-1,
			(t < current_time - 6 * 24 * 60 * 60 ||
			t > current_time + 12 * 60 * 60
			? date_yfmt : date_wfmt), tmp);
		datebuf[sizeof(datebuf)-1]=0;
	}
	return (datebuf);
}

/*
** Add a flag to a maildir filename
*/

static std::string maildir_addflagfilename(std::string_view filename, char flag)
{
	std::string new_filename;
	/* We can possibly add as many as four character */

	new_filename.reserve(filename.size() + 4);
	new_filename = filename;
	size_t p=new_filename.rfind('/');
	if (p == std::string::npos)
		p=0;
	size_t q=new_filename.find(':', p);
	if (q == std::string::npos)
		new_filename += ":2,";
	else if (new_filename[q+1] != '2' && new_filename[q+2] != ',')
		new_filename.replace(q, new_filename.size()-q, ":2,");
	p=new_filename.find(':', p);
	if (new_filename.find(flag, p) != std::string::npos)
	{
		return ("");		/* Already set */
	}

	p += 2;
	while (p < new_filename.size() && new_filename[p] < flag) p++;
	new_filename.insert(p, 1, flag);
	return (new_filename);
}

static void closedb()
{
	if (!folderdatname.empty())
	{
		dbobj_close(&folderdat);
		folderdatname.clear();
	}
}

static std::string foldercachename(std::string_view folder)
{
	std::string f;

	if (folder.find('/') != folder.npos)
	{
		enomem();
		return NULL;
	}

	f.reserve(sizeof(MAILDIRCURCACHE "/" DBNAME ".") - 1 + folder.size());
	f = MAILDIRCURCACHE "/" DBNAME ".";
	f += folder;

	return f;
}

static bool opencache(const char *folder, const char *mode)
{
	size_t	l;
	char	*p;
	std::string q;

	if (xlate_shmdir(folder).empty())
		return (false);

	auto cachename=foldercachename(folder);

	if (cachename.empty())
		return (false);

	if (folderdatname == cachename)
	{
		if (strcmp(mode, "W") == 0 &&
			strcmp(folderdatmode, "W"))
			;
			/*
			** We want to open for write, folder is open for
			** read
			*/
		else
		{
			return (true);
				/* We already have this folder cache open */
		}
	}
	closedb();
	folderdatmode=mode;

	dbobj_init(&folderdat);
	if (dbobj_open(&folderdat, cachename.c_str(), mode))	return (false);
	folderdatname=cachename;

	if ((p=dbobj_fetch(&folderdat, "HEADER", 6, &l, "")) == 0)
		return (true);
	q.reserve(l);
	q.assign(p, l);
	free(p);

	cachemtime=0;
	new_cnt=0;
	all_cnt=0;
	isoldestfirst=0;
	sortorder=0;

	std::string_view header{q};
	while (!header.empty())
	{
		std::string_view line;

		auto nl=header.find('\n');
		if (nl == header.npos)
		{
			line=header;
			header="";
		}
		else {
			line=header.substr(0, nl);
			header.remove_prefix(nl+1);
		}

		auto r=line.find('=');
		if (r == line.npos)	continue;
		std::string_view key=line.substr(0, r);
		std::string_view value=line.substr(r+1);
		const char *s=value.data();
		const char *t=s+value.size();

		if (key == "SAVETIME")
			std::from_chars(s, t, cachemtime);
		else if (key == "COUNT")
			std::from_chars(s, t, all_cnt);
		else if (key == "NEWCOUNT")
			std::from_chars(s, t, new_cnt);
		else if (key == "SORT")
		{
			auto res=std::from_chars(s, t, isoldestfirst);
			if (res.ec == std::errc())
			{
				s=res.ptr;
				if (s < t)
					sortorder=*s;
			}
		}
	}
	return (true);
}

static std::optional<MSGINFO> get_msginfo(unsigned long n)
{
	char	namebuf[MAXLONGSIZE+40];
	char	*p;
	size_t	len;

	std::string buf;

	std::optional<MSGINFO> ret;

	sprintf(namebuf, "REC%lu", n);

	p=dbobj_fetch(&folderdat, namebuf, strlen(namebuf), &len, "");
	if (!p)	return ret;

	buf.assign(p, len);
	free(p);

	MSGINFO &msginfo_buf=ret.emplace();

	std::string_view buf_parse{buf};

	while (!buf_parse.empty())
	{
		std::string_view line;

		auto nl=buf_parse.find('\n');
		if (nl == buf_parse.npos)
		{
			line=buf_parse;
			buf_parse="";
		}
		else {
			line=buf_parse.substr(0, nl);
			buf_parse.remove_prefix(nl+1);
		}

		auto r=line.find('=');
		if (r == line.npos) continue;
		std::string_view key=line.substr(0, r);
		std::string_view value=line.substr(r+1);
		const char *s=value.data();
		const char *t=s+value.size();

		if (key == "FILENAME")
			msginfo_buf.filename=value;
		else if (key == "FROM")
			msginfo_buf.from_s=value;
		else if (key == "SUBJECT")
			msginfo_buf.subject_s=value;
		else if (key == "SIZES")
			msginfo_buf.size_s=value;
		else if (key == "DATE")
		{
			auto res=std::from_chars(s, t, msginfo_buf.date_n);
			if (res.ec == std::errc())
				msginfo_buf.date_s=displaydate(msginfo_buf.date_n);
		}
		else if (key == "SIZEN")
		{
			std::from_chars(s, t, msginfo_buf.size_n);
		}
		else if (key == "TIME")
		{
			std::from_chars(s, t, msginfo_buf.mi_mtime);
		}
		else if (key == "INODE")
		{
			std::from_chars(s, t, msginfo_buf.mi_ino);
		}
	}

	return ret;
}

static void put_msginfo(const MSGINFO &m, unsigned long n)
{
	char	namebuf[MAXLONGSIZE+40];
	std::string rec;

	rec.reserve(m.filename.size()+m.from_s.size()+m.subject_s.size()
				+m.size_s.size()+MAXLONGSIZE*4+
				sizeof("FILENAME=\nFROM=\nSUBJECT=\nSIZES=\nDATE=\n"
				"SIZEN=\nTIME=\nINODE=\n")+100);

	rec.append("FILENAME=").append(m.filename).append("\nFROM=").append(m.from_s)
		.append("\nSUBJECT=").append(m.subject_s).append("\nSIZES=").append(m.size_s)
		.append("\nDATE=");

	*std::to_chars(namebuf, namebuf+sizeof(namebuf)-1, m.date_n).ptr=0;
	rec.append(namebuf);
	rec.append("\nSIZEN=");
	*std::to_chars(namebuf, namebuf+sizeof(namebuf)-1, m.size_n).ptr=0;
	rec.append(namebuf);
	rec.append("\nTIME=");
	*std::to_chars(namebuf, namebuf+sizeof(namebuf)-1, m.mi_mtime).ptr=0;
	rec.append(namebuf);
	rec.append("\nINODE=");
	*std::to_chars(namebuf, namebuf+sizeof(namebuf)-1, m.mi_ino).ptr=0;
	rec.append(namebuf);

	sprintf(namebuf, "REC%lu", n);
	if (dbobj_store(&folderdat, namebuf, strlen(namebuf),
		rec.c_str(), rec.size(), "R"))
		enomem();
}

static void update_foldermsgs(
	const char *folder,
	std::string_view newname,
	size_t pos
)
{
	std::string_view n{newname.substr(newname.rfind('/')+1)};

	if (opencache(folder, "W") )
	{
		auto p=get_msginfo(pos);

		if (p)
		{
			p->filename=std::string{n.begin(), n.end()};
			put_msginfo(*p, pos);
			return;
		}
	}

	error("Internal error in update_foldermsgs");
}

static void maildir_markflag(const char *folder, size_t pos, char flag)
{
	if (opencache(folder, "W") )
	{
		auto p=get_msginfo(pos);
		if (p)
		{
			auto filename=maildir_find(folder, p->filename.c_str());
			if (filename.empty())
				return;

			std::string new_filename;
			if (!(new_filename=maildir_addflagfilename(filename, flag)).empty())
			{
				rename(filename.c_str(), new_filename.c_str());
				update_foldermsgs(
					folder,
					std::string_view{new_filename.c_str(), new_filename.size()},
					pos
				);
			}
			return;
		}
	}

	error("Internal error in maildir_markflag");
}

void maildir_markread(const char *folder, size_t pos)
{
	char acl_buf[2];

	strcpy(acl_buf, ACL_SEEN);
	acl_computeRightsOnFolder(folder, acl_buf);
	if (acl_buf[0])
		maildir_markflag(folder, pos, 'S');
}

void maildir_markreplied(const char *folder, const char *message)
{
	std::string new_filename;
	char acl_buf[2];

	strcpy(acl_buf, ACL_WRITE);
	acl_computeRightsOnFolder(folder, acl_buf);

	if (acl_buf[0] == 0)
		return;

	auto filename=maildir_find(folder, message);

	if (!filename.empty() &&
	    (new_filename=maildir_addflagfilename(filename, 'R')).empty())
	{
		rename(filename.c_str(), new_filename.c_str());
	}
}

std::string maildir_posfind(const char *folder, size_t *pos)
{
	if (opencache(folder, "R"))
	{
		auto p=get_msginfo( *pos);
		if (p)
		{
			return maildir_find(folder, p->filename.c_str());
		}
	}

	error("Internal error in maildir_posfind");
	return "";
}


int maildir_name2pos(const char *folder, const char *filename, size_t *pos)
{
	std::string p;
	char *q;
	size_t len;

	maildir_reload(folder);
	if (!opencache(folder, "R"))
	{
		error("Internal error in maildir_name2pos");
		return (0);
	}

	p.reserve(strlen(filename)+10);
	p.append("FILE").append(filename);
	auto colon=p.find(':');
	if (colon != p.npos)
		p.resize(colon);

	q=dbobj_fetch(&folderdat, p.c_str(), p.size(), &len, "");

	if (!q)
		return (-1);

	*pos=0;
	for (auto p=q; len; --len, p++)
	{
		if (isdigit((int)(unsigned char)*p))
			*pos = *pos * 10 + (*p-'0');
	}
	free(q);
	return (0);
}

void maildir_msgpurge(const char *folder, size_t pos)
{
	auto filename=maildir_posfind(folder, &pos);

	if (!filename.empty())
	{
		unlink(filename.c_str());
	}
}

void maildir_msgpurgefile(const char *folder, const char *msgid)
{
	auto filename=maildir_find(folder, msgid);

	if (!filename.empty())
	{
		auto d=xlate_shmdir(folder);

		if (!d.empty())
		{
			if (strncmp(folder, SHARED ".", sizeof(SHARED))
			    && maildirquota_countfolder(d.c_str()) &&
			    maildirquota_countfile(filename.c_str()))
			{
				unsigned long filesize=0;

				if (maildir_parsequota(filename.c_str(),
						       &filesize))
				{
					struct stat stat_buf;

					if (stat(filename.c_str(), &stat_buf)
					    == 0)
						filesize=stat_buf.st_size;
				}

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}
		}
		unlink(filename.c_str());
	}
}

/*
** A message is moved to a different folder as follows.
** The message is linked to the destination folder, then marked with a 'T'
** flag in the original folder.  Later, all T-marked messages are deleted.
*/

static int msgcopy(int fromfd, int tofd)
		/* ... Except moving to/from sharable folder actually
		** involves copying.
		*/
{
char	buf[8192];
int	i, j;
char	*p;

	while ((i=read(fromfd, buf, sizeof(buf))) > 0)
	{
		p=buf;
		while (i)
		{
			j=write(tofd, p, i);
			if (j <= 0)	return (-1);
			p += j;
			i -= j;
		}
	}
	return (i);
}

static int do_msgmove(const char *from,
		      const char *file, const char *dest, size_t pos,
		      int check_acls)
{
	const char *p;
	struct stat stat_buf;
	std::string new_filename;
	unsigned long	filesize=0;
	int	no_link=0;
	struct maildirsize quotainfo;
	bool from_shared, dest_shared;
	char acl_buf[4];

	if (stat(file, &stat_buf) || stat_buf.st_nlink != 1)
	{
		unlink(file); /* Already moved, or crashed in the middle of
			      ** moving the file, so clean up.
			      */
		return (0);
	}


	/* Update quota */

	auto destdir=xlate_shmdir(dest);
	if (destdir.empty())	enomem();

	auto fromdir=xlate_shmdir(from);
	if (fromdir.empty())	enomem();

	from_shared=strncmp(from, SHARED ".", sizeof(SHARED)) == 0;
	dest_shared=strncmp(dest, SHARED ".", sizeof(SHARED)) == 0;

	strcpy(acl_buf, ACL_SEEN ACL_WRITE ACL_DELETEMSGS);
	if (check_acls)
		acl_computeRightsOnFolder(dest, acl_buf);

	if (strcmp(from, dest))
	{
		if (maildir_parsequota(file, &filesize))
			filesize=stat_buf.st_size;
			/* Recover from possible corruption */

		if ((dest_shared
		     || !maildirquota_countfolder(destdir.c_str())) &&
		    maildirquota_countfile(file))
		{

			if (!from_shared)
				maildir_quota_deleted(".", -(int64_t)filesize, -1);
		}
		else if (!dest_shared && maildirquota_countfolder(destdir.c_str()) &&
			 (from_shared || !maildirquota_countfolder(fromdir.c_str()))
			 )
			/* Moving FROM trash */
		{

			if (maildir_quota_add_start(".", &quotainfo,
						    filesize, 1, NULL))
			{
				return (-1);
			}

			maildir_quota_add_end(&quotainfo, filesize, 1);
		}
	}

	if (from_shared || dest_shared)
	{
		maildir::tmpcreate_info createInfo;
		int	fromfd, tofd;
		char	*l;

		if (dest_shared)	/* Copy to the sharable folder */
		{
			destdir += "/shared";
		}

		createInfo.maildir=destdir;
		createInfo.uniq="copy";
		createInfo.doordie=true;

		if ( dest_shared )
			umask (0022);

		if ((tofd=createInfo.fd()) < 0)
		{
			error(strerror(errno));
		}

		if (dest_shared)
		/* We need to copy it directly into /cur of the dest folder */
		{
			auto p=createInfo.newname.rfind('/');

			createInfo.newname[p-3]='c';
			createInfo.newname[p-2]='u';
			createInfo.newname[p-1]='r';
			/* HACK!!!!!!!!!!!! */
		}

		if ((fromfd=maildir_semisafeopen(file, O_RDONLY, 0)) < 0)
		{
			int terrno = errno;
			close(tofd);
			unlink(createInfo.tmpname.c_str());
			error3(__FILE__, __LINE__, "Failed to open for read:",
				file, terrno);
		}

		umask (0077);
		if (msgcopy(fromfd, tofd))
		{
			int terrno = errno;
			close(fromfd);
			close(tofd);
			unlink(createInfo.tmpname.c_str());
			error3(__FILE__, __LINE__, "Failed to copy message",
				"", terrno);
		}
		close(fromfd);
		close(tofd);

	/*
	** When we attempt to DELETE a message in the sharable folder,
	** attempt to remove the UNDERLYING message
	*/

		if (from_shared && (l=maildir_getlink(file)) != 0)
		{
			if (unlink(l))
			{
				/* Not our message */

				if (strcmp(dest, INBOX "." TRASH) == 0)
				{
					free(l);
					unlink(createInfo.tmpname.c_str());
					return (0);
				}
			}
			else {
				// Danglink link, delete it.
				unlink(file);
			}
			free(l);
		}

		if (strchr(acl_buf, ACL_DELETEMSGS[0]) == 0)
			maildir::remflagname(createInfo.newname, 'T');
		if (strchr(acl_buf, ACL_SEEN[0]) == 0)
			maildir::remflagname(createInfo.newname, 'S');
		if (strchr(acl_buf, ACL_WRITE[0]) == 0)
		{
			maildir::remflagname(createInfo.newname, 'F');
			maildir::remflagname(createInfo.newname, 'D');
			maildir::remflagname(createInfo.newname, 'R');
		}

		if (maildir::movetmpnew(createInfo.tmpname, createInfo.newname))
		{
			unlink(createInfo.tmpname.c_str());
			error(strerror(errno));
		}
		no_link=1;	/* Don't call link(), below */
	}

	p=strrchr(file, '/');
	if (p)	++p;
	else	p=file;

	std::string basename{p};
	maildir::remflagname(basename, 'T');	/* Remove any deleted flag for new name */

	if (strchr(acl_buf, ACL_SEEN[0]) == 0)
		maildir::remflagname(basename, 'S');
	if (strchr(acl_buf, ACL_WRITE[0]) == 0)
	{
		maildir::remflagname(basename, 'F');
		maildir::remflagname(basename, 'D');
		maildir::remflagname(basename, 'R');
	}
	auto newname=destdir + "/cur/" + basename;

	/* When DELETE is called for a message in TRASH, from and dest will
	** be the same, so we just mark the file as Trashed, to be removed
	** in checknew.
	*/

	if (no_link == 0 && strcmp(from, dest))
	{
		if (link(file, newname.c_str()))
		{
			return (-1);
		}
	}

	if (!(new_filename=maildir_addflagfilename(file, 'T')).empty())
	{
		rename(file, new_filename.c_str());
		update_foldermsgs(from, new_filename.c_str(), pos);
	}
	return (0);
}

void maildir_msgdeletefile(const char *folder, const char *file, size_t pos)
{
	auto filename=maildir_find(folder, file);

	if (!filename.empty())
	{
		(void)do_msgmove(folder, filename.c_str(), INBOX "." TRASH,
				 pos, 0);
	}
}

int maildir_msgmovefile(const char *folder, const char *file, const char *dest,
	size_t pos)
{
	auto filename=maildir_find(folder, file);
	int	rc;

	if (filename.empty())	return (0);
	rc=do_msgmove(folder, filename.c_str(), dest, pos, 1);
	return (rc);
}

static std::string foldercountfilename(const char *folder)
{
	return std::string(MAILDIRCURCACHE "/cnt.") + folder;
}

/*
** Grab new messages from new.
*/

static void maildir_checknew(const char *folder, const char *dir)
{
	struct	stat	stat_buf;
	char	acl_buf[2];

	/* Delete old files in tmp */

	maildir_purgetmp(dir);

	/* Move everything from new to cur */

	maildir_getnew(dir, 0, NULL, NULL);

	/* Look for any messages mark as deleted.  When we delete a message
	** we link it into the Trash folder, and mark the original with a T,
	** which we delete when we check for new messages.
	*/

	auto dirbuf=std::string(dir) + "/cur";

	if (stat(dirbuf.c_str(), &stat_buf))
	{
		return;
	}

	/* If the count cache file is still current, the directory hasn't
	** changed, so we don't need to scan it for deleted messages.  When
	** a message is deleted, the rename bumps up the timestamp.
	**
	** This depends on dodirscan() being called after this function,
	** which updates MAILDIRCOUNTCACHE
	*/

	{
		auto f=foldercountfilename(folder);
		struct stat c_stat_buf;

		if (stat(f.c_str(), &c_stat_buf) == 0 && c_stat_buf.st_mtime >
		    stat_buf.st_mtime)
			return;
	}

	strcpy(acl_buf, ACL_EXPUNGE);
	acl_computeRightsOnFolder(folder, acl_buf);

	std::error_code ec;

	for (const auto &entry : fs::directory_iterator(dirbuf, ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename[0] == '.')	continue;

		if (maildirfile_type(filename.c_str()) == MSGTYPE_DELETED &&
		    acl_buf[0])
		{
			auto p=dirbuf + "/" + filename;

			/*
			** Because of the funky way we do things,
			** if we were compiled with --enable-trashquota,
			** purging files from Trash should decrease the
			** quota
			*/

			if (strcmp(folder, INBOX "." TRASH) == 0 &&
			    maildirquota_countfolder(dir))
			{
				struct stat stat_buf;
				unsigned long filesize=0;

				if (maildir_parsequota(filename.c_str(),
						       &filesize))
					if (stat(p.c_str(), &stat_buf) == 0)
						filesize=stat_buf.st_size;

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}

			maildir_unlinksharedmsg(p.c_str());
				/* Does The Right Thing if this is a shared
				** folder
				*/
		}
	}
}

/*
** Automatically purge deleted messages.
*/

static bool goodcache(const char *foldername)
{
	struct stat stat_buf;

	auto minfo=maildir::info_imap_find(foldername, login_returnaddr());
	if (!minfo)
		return false;

	if (!minfo.regular_maildir())
		return false;

	auto folderdir=xlate_shmdir(foldername);

	if (folderdir.empty() || stat(folderdir.c_str(), &stat_buf) < 0)
	{
		return false;
	}
	return true;
}

void maildir_autopurge()
{
	struct	stat	stat_buf;
	char buffer[80];
	size_t n, i;
	FILE *fp;

	/* This is called when logging in.  Version 0.18 supports maildir
	** quotas, so automatically upgrade all folders.
	*/

	std::error_code ec;

	for (const auto &entry : fs::directory_iterator(".", ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename[0] != '.')	continue;

		auto f=filename + "/maildirfolder";

		close(open(f.c_str(), O_RDWR|O_CREAT, 0644));

		/* Eliminate obsoleted cache files */

		f=filename + "/" MAILDIRCOUNTCACHE;

		unlink(f.c_str());

		f=filename + "/" MAILDIRCURCACHE;

		unlink(f.c_str());

		f=filename + "/" MAILDIRCURCACHE "." DBNAME;
		unlink(f.c_str());
	}

	/* Version 0.24 top level remove */

	unlink(MAILDIRCURCACHE);

	/* Version 4 top level remove */

	unlink(MAILDIRCURCACHE "." DBNAME);
	mkdir (MAILDIRCURCACHE, 0700);



	/*
	** Periodically purge stale cache files of nonexistent folders.
	** This is done by using a counter that runs from 0 up until the
	** # of files in MAILDIRCURCACHE.  At each login, we check if
	** file #n is for an existing folder.  If not, the stale file is
	** removed.
	*/

	n=0;

	if ((fp=fopen(MAILDIRCURCACHE "/.purgecnt", "r")) != NULL)
	{
		if (fgets(buffer, sizeof(buffer), fp) != NULL)
			n=atoi(buffer);
		fclose(fp);
	}

	i=0;

	std::string folderdir;

	for (const auto &entry : fs::directory_iterator(MAILDIRCURCACHE, ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename[0] == '.')
			continue;

		if (filename.compare(0, sizeof(DBNAME), DBNAME ".") == 0 ||
		    filename.compare(0, 4, "cnt.") == 0)
		{
			if (i == n)
			{
				if (!goodcache(strchr(filename.c_str(), '.')+1))
				{
					folderdir = std::string(MAILDIRCURCACHE "/") + filename;
					unlink(folderdir.c_str());
				}
			}
			++i;
			continue;
		}

		folderdir = std::string(MAILDIRCURCACHE "/") + filename;
		unlink(folderdir.c_str());
	}

	if (i == n)
		++i;
	else
		i=0;


	if ((fp=fopen(MAILDIRCURCACHE "/.purgecnt.tmp", "w")) == NULL ||
	    fprintf(fp, "%lu\n", (unsigned long)i) < 0 ||
	    fflush(fp) < 0)
		enomem();

	fclose(fp);
	if (rename(MAILDIRCURCACHE "/.purgecnt.tmp",
		   MAILDIRCURCACHE "/.purgecnt") < 0)
		enomem();

	auto trashdir=xlate_mdir(INBOX "." TRASH);

	/* Delete old files in tmp */

	time(&current_time);
	auto dirbuf=trashdir + "/cur";

	for (const auto &entry : fs::directory_iterator(dirbuf, ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename[0] == '.')	continue;
		auto p=dirbuf + "/" + filename;

		if (stat(p.c_str(), &stat_buf) == 0 &&
		    pref_autopurge &&
		    stat_buf.st_ctime < current_time
		    - pref_autopurge * 24 * 60 * 60)
		{
			if (maildirquota_countfolder(dirbuf.c_str()) &&
			    maildirquota_countfile(p.c_str()))
			{
				unsigned long filesize=0;

				if (maildir_parsequota(filename.c_str(), &filesize))
					filesize=stat_buf.st_size;

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}

			unlink(p.c_str());
		}
	}

	maildir_purgemimegpg();
}

/*
** MIME-GPG decoding creates a temporary file in tmp, which is preserved
** (in the event of subsequent multipart/related accesses).  The filenames
** include ':', to mark them as used for this purpose.  Rather than wait
** 36 hours for them to get cleaned up, as part of a normal maildir tmp
** purge, we can blow them off right now.
*/

void maildir_purgemimegpg()
{
	std::error_code ec;

	for (const auto &entry: fs::directory_iterator("tmp", ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename.find(":mimegpg:") == std::string::npos &&
		    filename.find(":calendar:") == std::string::npos)	continue;

		std::string p = std::string("tmp/") + filename;

		unlink(p.c_str());
	}
}

/* Ditto for search results */

void maildir_purgesearch()
{
	std::error_code ec;

	for (const auto &entry: fs::directory_iterator("tmp", ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename.find(":search:") == std::string::npos)	continue;

		std::string p = std::string("tmp/") + filename;
		unlink(p.c_str());
	}
}

/*
** Messages supposed to be arranged in the reverse chronological order of
** arrival.
**
** Instead of stat()ing every file in the directory, we depend on the
** naming convention that are specified for the Maildir.  Therefore, we rely
** on Maildir writers observing the required naming conventions.
*/

namespace {
	struct messagecmp {
		bool operator()(const MSGINFO &a, const MSGINFO &b) const;
	};
}

bool messagecmp::operator()(const MSGINFO &a, const MSGINFO &b) const
{
	bool gt=false, lt=true;
	int	n;

	if (pref_flagisoldest1st)
	{
		gt= true;
		lt= false;
	}

	switch (pref_flagsortorder)	{
	case 'F':
		n=a.from_s.compare(b.from_s);
		if (n)	return (n < 0);
		break;
	case 'S':
		n=a.subject_s.compare(b.subject_s);
		if (n)	return (n < 0);
		break;
	}
	if (a.date_n < b.date_n)	return (gt);
	if (a.date_n > b.date_n)	return (lt);

	if (a.mi_ino < b.mi_ino)	return (gt);
	if (a.mi_ino > b.mi_ino)	return (lt);
	return (0);
}

/*
** maildirfile_type(directory, filename) - return one of the following:
**
**   MSGTYPE_NEW - new message
**   MSGTYPE_DELETED - trashed message
**   '\0' - all other kinds
*/

char maildirfile_type(const char *p)
{
const char *q=strrchr(p, '/');
int	seen_trash=0, seen_r=0, seen_s=0;

	if (q)	p=q;

	if ( !(p=strchr(p, ':')) || *++p != '2' || *++p != ',')
		return (MSGTYPE_NEW);		/* No :2,info */
				;
	++p;
	while (p && isalpha((int)(unsigned char)*p))
		switch (*p++)	{
		case 'T':
			seen_trash=1;
			break;
		case 'R':
			seen_r=1;
			break;
		case 'S':
			seen_s=1;
			break;
		}

	if (seen_trash)
		return (MSGTYPE_DELETED);	/* Trashed message */
	if (seen_s)
	{
		if (seen_r)	return (MSGTYPE_REPLIED);
		return (0);
	}

	return (MSGTYPE_NEW);
}

static int docount(const char *fn, size_t &new_cnt, size_t &other_cnt)
{
const char *filename=strrchr(fn, '/');
char	c;

	if (filename)	++filename;
	else		filename=fn;

	if (*filename == '.')	return (0);	/* We don't want this one */

	c=maildirfile_type(filename);

	if (c == MSGTYPE_NEW)
		++new_cnt;
	else
		++other_cnt;
	return (1);
}

maildir_contents_t maildir_read(const char *dirname, size_t nfiles,
				size_t &starting_pos,
				bool &morebefore, bool &moreafter)
{
	maildir_contents_t msginfo;

	msginfo.reserve(nfiles);

	if (!opencache(dirname, "W"))	return (msginfo);

	if (nfiles > all_cnt)	nfiles=all_cnt;
	if (starting_pos + nfiles > all_cnt)
		starting_pos=all_cnt-nfiles;

	morebefore = starting_pos > 0;

	size_t i;
	for (i=0; i<nfiles; i++)
	{
		if (starting_pos + i >= all_cnt)	break;
		auto p=get_msginfo(starting_pos + i);
		if (!p)
			break;

		p->msgnum=starting_pos+i;
		msginfo.emplace_back(
			*p,
			std::vector<MATCHEDSTR>{}
		);
	}
	moreafter= starting_pos + i < all_cnt;

	return (msginfo);
}

#define save_int(n, fp) do {						\
		size_t i;						\
		size_t cnt=sizeof((n));					\
		putc(cnt, (fp));					\
		for (i=0; i<cnt; i++)					\
			putc((n) >> (cnt-1-i)*8, (fp));			\
	} while(0);

static void save_str(const char *str, FILE *fp)
{
	size_t l=strlen(str);

	save_int(l, fp);
	fprintf(fp, "%s", str);
}

#define load_int(n, fp) do {					\
	int i;							\
	int cnt=getc(fp);					\
	(n)=0;							\
	if (cnt != EOF)						\
		for (i=0; i<cnt; ++i)				\
			(n)=(n) << 8 | (unsigned char)getc(fp);	\
	} while(0);

static std::string load_str(FILE *fp)
{
	size_t l;
	size_t i;

	load_int(l, fp);

	if (feof(fp))
		l=0;

	std::string str;
	str.reserve(l);

	for (i=0; i<l; i++)
	{
		char c=getc(fp);

		str.push_back(c);
	}

	return str;
}

static void save_msginfo(
	const MSGINFO &p,
	const std::vector<MATCHEDSTR> &context,
	FILE *fp
)
{
	save_int(p.msgnum, fp);
	save_str(p.filename.c_str(), fp);
	save_str(p.date_s.c_str(), fp);
	save_str(p.from_s.c_str(), fp);
	save_str(p.subject_s.c_str(), fp);
	save_str(p.size_s.c_str(), fp);
	save_int(p.date_n, fp);
	save_int(p.size_n, fp);
	save_int(p.mi_mtime, fp);
	save_int(p.mi_ino, fp);

	save_int(context.size(), fp);

	for (const auto &ms: context)
	{
		save_str(ms.prefix.c_str(), fp);
		save_str(ms.match.c_str(), fp);
		save_str(ms.suffix.c_str(), fp);
	}
}

static std::tuple<MSGINFO, std::vector<MATCHEDSTR>> load_msginfo(
	FILE *fp
)
{
	std::tuple<MSGINFO, std::vector<MATCHEDSTR>> ret;

	auto &[p, context] = ret;
	size_t context_cnt;

	load_int(p.msgnum, fp);
	p.filename=load_str(fp);
	p.date_s=load_str(fp);
	p.from_s=load_str(fp);
	p.subject_s=load_str(fp);
	p.size_s=load_str(fp);
	load_int(p.date_n, fp);
	load_int(p.size_n, fp);
	load_int(p.mi_mtime, fp);
	load_int(p.mi_ino, fp);
	load_int(context_cnt, fp);

	context.reserve(context_cnt);

	for (size_t i=0; i<context_cnt; i++)
	{
		MATCHEDSTR ms;

		ms.prefix=load_str(fp);
		ms.match=load_str(fp);
		ms.suffix=load_str(fp);

		context.push_back(std::move(ms));
	}

	return ret;
}

static maildir_contents_t execute_maildir_search(
	const char *dirname,
	size_t pos,
	const char *searchtxt,
	const char *charset,
	size_t nfiles,
	size_t &last_message_searched);

#define SEARCHFORMATVER 1

void maildir_search(
	const char *dirname,
	size_t pos,
	const char *searchtxt,
	const char *charset,
	size_t nfiles
)
{
	struct maildir_tmpcreate_info createInfo;

	static std::string filename;
	FILE *fp;

	size_t last_message_searched=0;

	auto p=execute_maildir_search(
		dirname, pos, searchtxt, charset,
		nfiles, last_message_searched
	);

	maildir_purgesearch();

	maildir_tmpcreate_init(&createInfo);

	createInfo.uniq=":search:";
	createInfo.doordie=1;

	if ((fp=maildir_tmpcreate_fp(&createInfo)) == NULL)
		error("Can't create new file.");

	filename=createInfo.tmpname;
	maildir_tmpcreate_free(&createInfo);

	chmod(filename.c_str(), 0600);

	{
		char ver=SEARCHFORMATVER;

		save_int(ver, fp);
		save_int(last_message_searched, fp);
	}

	for (auto &[info, matches]: p)
		save_msginfo(info, matches, fp);
	fflush(fp);
	if (ferror(fp) || fclose(fp))
		error("Cannot create temp file");

	cgi_put(SEARCHRESFILENAME, strrchr(filename.c_str(), '/')+1);
}

maildir_contents_t maildir_loadsearch(
	size_t nfiles,
	size_t &last_message_searched
)
{
        maildir_contents_t ret;

	size_t i;
	const char *filename;
	FILE *fp;
	char ver;

	ret.reserve(nfiles);
	filename=cgi(SEARCHRESFILENAME);
	CHECKFILENAME(filename);

	std::string buf;
	buf.reserve(strlen(filename)+5);
	buf="tmp/";
	buf += filename;
	fp=fopen(buf.c_str(), "r");

	if (fp)
	{
		load_int(ver, fp);

		if (ver == SEARCHFORMATVER)
		{
			load_int(last_message_searched, fp);
			for (i=0; i<nfiles; ++i)
			{
				int ch=getc(fp);

				if (ch == EOF)
					break;
				ungetc(ch, fp);

				ret.push_back(load_msginfo(fp));
			}
		}
		fclose(fp);
	}

	for (auto &[msginfo, matches]: ret)
	{
		auto p=get_msginfo(msginfo.msgnum);

		if (p && p->mi_ino == msginfo.mi_ino) /* Safety first */
			msginfo.filename=p->filename;
	}
	return ret;
}

static const char spaces[]=" \t\r\n";

#define SEARCH_MATCH_CONTEXT_LEN	20

/* After matching, save surrounding context here */

struct searchresults_match_context {

	std::u32string match_context_before;
	std::u32string match_context;
	std::u32string match_context_after;
};

struct searchresults {

	char prevchar;
	size_t foundcnt;

	int finished;

	char utf8buf[512];
	size_t utf8buf_cnt;

	std::unique_ptr<char32_t[]> context_buf;
	size_t context_buf_len;

	size_t context_buf_head;
	size_t context_buf_tail;

	std::vector<searchresults_match_context> matched_context;

	struct maildir_searchengine *se;

};

static std::vector<MATCHEDSTR> creatematches(struct searchresults *sr);

static int searchresults_init(struct searchresults *sr,
			      struct maildir_searchengine *se);

static int do_maildir_search(const char *filename,
			     struct searchresults *sr);


static std::vector<std::tuple<MSGINFO, std::vector<MATCHEDSTR>>> execute_maildir_search(
	const char *dirname,
	size_t pos,
	const char *searchtxt,
	const char *charset,
	size_t nfiles,
	size_t &last_message_searched)
{
	std::vector<std::tuple<MSGINFO, std::vector<MATCHEDSTR>>> msginfo;
	size_t i;
	char *utf8str;
	char *p, *q;

	msginfo.reserve(nfiles);

	if (!opencache(dirname, "W"))	return msginfo;

	if (pos >= all_cnt) return msginfo;

	utf8str=unicode_convert_toutf8(searchtxt, charset, NULL);

	if (!utf8str)
		return msginfo;

	/* Normalize whitespace in the search string */

	p=q=utf8str;

	while (*p && strchr(spaces, *p))
		++p;

	while (*p)
	{
		while (*p && !strchr(spaces, *p))
		{
			*q++ = *p++;
		}

		while (*p && strchr(spaces, *p))
			++p;

		if (*p)
			*q++=' ';
	}
	*q=0;

	if (*utf8str)
	{
		struct maildir_searchengine se;
		int rc=-1;
		char32_t *ustr;
		size_t ustr_size;
		unicode_convert_handle_t h;

		maildir_search_init(&se);

		h=unicode_convert_tou_init("utf-8",
					     &ustr,
					     &ustr_size,
					     1);

		if (h)
		{
			unicode_convert(h, utf8str, strlen(utf8str));
			if (unicode_convert_deinit(h, NULL) == 0)
			{
				size_t n;

				for (n=0; ustr[n]; ++n)
					ustr[n]=unicode_lc(ustr[n]);

				rc=maildir_search_start_unicode(&se, ustr);
				free(ustr);
			}
		}

		for (i=0; rc == 0 && pos+i<all_cnt && msginfo.size()<nfiles; ++i)
		{
			auto info=get_msginfo(pos+i);

			if (!info)
				continue;

			last_message_searched= pos+i;

			auto filename=maildir_find(
				dirname,
				info->filename.c_str()
			);

			maildir_search_reset(&se);

			if (!filename.empty())
			{
				struct searchresults sr;

				info->msgnum=pos+i;

				if (searchresults_init(&sr, &se) == 0)
				{
					if (do_maildir_search(filename.c_str(),
							      &sr))
					{
						msginfo.emplace_back(
							std::move(*info),
							creatematches(&sr)
						);
					}
				}
			}
		}
		maildir_search_destroy(&se);
	}

	free(utf8str);
	return msginfo;
}

#define sr_context_buf_index_inc(sr,n) ( ( (n)+1 ) % ( (sr)->context_buf_len))
#define sr_context_buf_index_dec(sr,n) ( ( (n)+ (sr)->context_buf_len - 1 ) % ( (sr)->context_buf_len))

static int searchresults_init(struct searchresults *sr,
			      struct maildir_searchengine *se)
{
	sr->prevchar=0;
	sr->foundcnt=0;
	sr->finished=0;
	sr->se=se;
	sr->utf8buf_cnt=0;

	sr->context_buf_len=maildir_search_len(se)+SEARCH_MATCH_CONTEXT_LEN*2+1;
	sr->context_buf=std::unique_ptr<char32_t[]>(new char32_t[sr->context_buf_len]);

	sr->context_buf_head=0;
	sr->context_buf_tail=0;

	return 0;
}

/* Save context before, and the matched context */

static void search_found_save_context(struct searchresults *sr)
{
	auto &c=sr->matched_context.emplace_back();

	c.match_context_before.reserve(SEARCH_MATCH_CONTEXT_LEN);
	c.match_context.reserve(maildir_search_len(sr->se));
	c.match_context_after.reserve(SEARCH_MATCH_CONTEXT_LEN);

	/*
	** Subtract from the head of the context buffer to arrive at the
	** start of the matched context
	*/

	size_t n=sr->context_buf_head;

	for (size_t i=maildir_search_len(sr->se); i > 0; --i)
	{
		if (n == sr->context_buf_tail)
			break; /* Shouldn't happen */

		n=sr_context_buf_index_dec(sr, n);
	}

	/* From here to the head is the matched context */

	for (size_t i=n; i != sr->context_buf_head; )
	{
		c.match_context.push_back(sr->context_buf[i]);
		i=sr_context_buf_index_inc(sr, i);
	}

	/* Now, look before the start of the matched context */

	size_t i=n;
	for (size_t j=0; j<SEARCH_MATCH_CONTEXT_LEN; ++j)
	{
		if (i == sr->context_buf_tail)
			break; /* Possible */

		i=sr_context_buf_index_dec(sr, i);
	}

	while (i != n)
	{
		c.match_context_before.push_back(sr->context_buf[i]);
		i=sr_context_buf_index_inc(sr, i);
	}
}

static int do_search_utf8(struct searchresults *res)
{
	char32_t *uc=NULL;
	size_t n;
	unicode_convert_handle_t h;

	if (res->utf8buf_cnt == 0)
		return 0;

	if (res->finished)
		return -1;

	auto utf8_len=res->utf8buf_cnt;
	res->utf8buf_cnt=0;

	h=unicode_convert_tou_init("utf-8",
				     &uc,
				     &n,
				     1);

	if (h)
	{
		unicode_convert(h, res->utf8buf, utf8_len);
		if (unicode_convert_deinit(h, NULL) == 0)
			;
		else
			uc=NULL;
	}

	for (n=0; uc && uc[n]; n++)
	{
		char32_t origch=uc[n];
		char32_t ch=unicode_lc(origch);

		maildir_search_step_unicode(res->se, ch);

		/* Record the context, one char at a time */

		res->context_buf[res->context_buf_head]=origch;
		res->context_buf_head =
			sr_context_buf_index_inc(res, res->context_buf_head);

		if (res->context_buf_head == res->context_buf_tail)
			res->context_buf_tail =
				sr_context_buf_index_inc(res, res->context_buf_tail);
		/* Accumulate post-match context for matched hits */

		for (auto &c: res->matched_context)
		{
			if (c.match_context_after.size() < SEARCH_MATCH_CONTEXT_LEN)
				c.match_context_after.push_back(origch);
		}

		if (maildir_search_found(res->se))
		{
			search_found_save_context(res);
			if (++res->foundcnt > 3)
			{
				res->finished=1;
				break;
			}

			maildir_search_reset(res->se);
		}
	}

	if (uc)
		free(uc);

	if (res->finished)
		return 1;

	return 0;
}

static int do_search(const char *str, size_t n, void *arg)
{
	struct searchresults *res=(struct searchresults *)arg;

	int rc=0;

	while (n && rc == 0)
	{
		char ch=*str++;
		--n;

		if (strchr(spaces, ch))
		{
			ch=' ';

			if (res->prevchar == ' ')
				continue;
		}
		res->prevchar=ch;

		if (res->utf8buf_cnt >= sizeof(res->utf8buf)-1)
		{
			size_t n;
			char save_ch;
			size_t save_n;

			for (n=res->utf8buf_cnt-1;
			     n > sizeof(res->utf8buf_cnt)/2; --n)
				if ((unsigned char)((res->utf8buf[n] ^ 0xC0)
						    & 0xC0))
					break;

			save_n=res->utf8buf_cnt;
			save_ch=res->utf8buf[n];

			res->utf8buf_cnt=n;

			rc=do_search_utf8(res);

			if (rc)
				return rc;

			res->utf8buf[n]=save_ch;

			while (n < save_n)
			{
				res->utf8buf[res->utf8buf_cnt++]=
					res->utf8buf[n];
				++n;
			}
		}

		res->utf8buf[res->utf8buf_cnt]=ch;
		++res->utf8buf_cnt;
	}

	return rc;
}

static int do_maildir_search(const char *filename,
			     struct searchresults *sr)
{
	struct rfc2045src *src;
	struct rfc2045_decodemsgtoutf8_cb cb;
	int fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	struct rfc2045 *rfc2045p;

	if (fd < 0)
		return 0;

	rfc2045p=rfc2045_fromfd(fd);

	if (rfc2045p == NULL)
	{
		close(fd);
		return 0;
	}

	memset(&cb, 0, sizeof(cb));
	cb.output_func=do_search;
	cb.arg=sr;

	if ((src=rfc2045src_init_fd(fd)) != NULL)
	{
		if (rfc2045_decodemsgtoutf8(src, rfc2045p, &cb) == 0)
			do_search_utf8(sr);
		rfc2045src_deinit(src);
	}

	rfc2045_free(rfc2045p);
	close(fd);
	return sr->foundcnt ? 1:0;
}

static std::vector<MATCHEDSTR> creatematches(struct searchresults *sr)
{
	size_t n=sr->matched_context.size();
	std::vector<MATCHEDSTR> retval;

	retval.reserve(n+1);

	for (auto &c: sr->matched_context)
	{
		MATCHEDSTR ms;

		ms.prefix=unicode::iconvert::fromu::convert(
			c.match_context_before,
			unicode::utf_8
		).first;
		ms.match=unicode::iconvert::fromu::convert(
			c.match_context,
			unicode::utf_8
		).first;
		ms.suffix=unicode::iconvert::fromu::convert(
			c.match_context_after,
			unicode::utf_8
		).first;

		retval.push_back(ms);
	}

	return retval;
}

static void dodirscan(const char *, const char *, size_t &, size_t &);

void maildir_count(const char *folder,
		   size_t &new_ptr,
		   size_t &other_ptr)
{
	std::string dir;

	new_ptr=0;
	other_ptr=0;

	auto minfo=maildir::info_imap_find(folder, login_returnaddr());

	if (!minfo)
		return;

	if (minfo.mailbox_type == MAILBOXTYPE_OLDSHARED)
	{
		dir=maildir::shareddir(".", strchr(folder, '.')+1);

		if (dir.empty())
			return;

		maildir_shared_sync(dir.c_str());
	}
	else
	{
		if (!minfo.regular_maildir())
			return;

		dir=maildir::name2dir(
			minfo.homedir,
			minfo.maildir
		);

		if (dir.empty())
			return;
	}

	maildir_checknew(folder, dir.c_str());
	dodirscan(folder, dir.c_str(), new_ptr, other_ptr);
}

size_t maildir_countof(const char *folder)
{
	maildir_getfoldermsgs(folder);
	return (all_cnt);
}

static void dodirscan(const char *folder,
		      const char *dir, size_t &new_cnt,
		      size_t &other_cnt)
{
	struct stat cur_stat;
	struct stat c_stat;
	const	char *p;
	char	cntbuf[MAXLONGSIZE*2+4];
	FILE	*fp;
	struct maildir_tmpcreate_info createInfo;

	new_cnt=0;
	other_cnt=0;
	auto curname=std::string(dir) + "/cur";

	if (stat(curname.c_str(), &cur_stat))
		return;

	auto cntfilename=foldercountfilename(folder);
	fp=fopen(cntfilename.c_str(), "r");

	if (fp)
	{
		char buf[BUFSIZ];

		if (fstat(fileno(fp), &c_stat) == 0 &&
		    c_stat.st_mtime > cur_stat.st_mtime &&
		    fgets(buf, sizeof(buf), fp))
		{
			size_t n;
			size_t o;

			if ((p=parse_ul(buf, &n)) && (p=parse_ul(p, &o)))
			{
				new_cnt=n;
				other_cnt=o;
				fclose(fp);
				return;	/* Valid cache of count */
			}
		}
		fclose(fp);
	}

	{
		std::error_code ec;

		for (const auto &entry : fs::directory_iterator(curname, ec))
			docount(entry.path().filename().string().c_str(), new_cnt, other_cnt);
	}

	auto fmtret=std::to_chars(cntbuf, cntbuf+sizeof(cntbuf)-2, new_cnt);
	*fmtret.ptr++=' ';
	fmtret=std::to_chars(fmtret.ptr, cntbuf+sizeof(cntbuf)-2, other_cnt);
	*fmtret.ptr++='\n';
	*fmtret.ptr++='\0';

	maildir_tmpcreate_init(&createInfo);
	createInfo.maildir=".";
	createInfo.uniq="count";
	createInfo.doordie=1;

	fp=maildir_tmpcreate_fp(&createInfo);
	if (!fp)
	{
		maildir_tmpcreate_free(&createInfo);
		return;
	}

	fprintf(fp, "%s", cntbuf);
	fclose(fp);

	if (rename(createInfo.tmpname, cntfilename.c_str()) < 0 ||
	    stat(cntfilename.c_str(), &c_stat) < 0)
	{
		unlink(cntfilename.c_str());
		maildir_tmpcreate_free(&createInfo);
		return;
	}
	maildir_tmpcreate_free(&createInfo);


	if (c_stat.st_mtime != cur_stat.st_mtime)
	{
		struct stat stat2;

		if (stat(curname.c_str(), &stat2)
		    || stat2.st_mtime != cur_stat.st_mtime)
		{
			/* cur directory changed while we were there, punt */

			c_stat.st_mtime=cur_stat.st_mtime;
			/* Reset it below */
		}
	}

	if (c_stat.st_mtime == cur_stat.st_mtime)
		/* Potential race condition */
	{
		change_timestamp(cntfilename.c_str(), c_stat.st_mtime-1);
				/* ... So rebuild it next time */
	}
}

/*****************************************************************************

The MSGINFO structure contains the summary of the headers found in all
messages in the cur directory.

Instead of opening each message every time we need to serve the directory
contents, the messages are scanned once, and a cache file is built
containing the contents.

*****************************************************************************/


/* Initialize a MSGINFO structure by reading the message headers */

static MSGINFO maildir_ngetinfo(const char *filename,
				std::string &orig_subject)
{
	struct stat stat_buf;
	const char *p;
	int	is_sent_header=0;
	std::string fromheader;

	/* Hack - see if we're reading a message from the Sent or Drafts
		folder */

	p=strrchr(filename, '/');
	if ((p && (size_t)(p - filename) >=
		sizeof(SENT) + 5 && strncmp(p - (sizeof(SENT) + 5),
			"/." SENT "/", sizeof(SENT)+2) == 0)
		|| strncmp(filename, "." SENT "/", sizeof(SENT)+1) == 0
		|| strncmp(filename, "./." SENT ".", sizeof(SENT)+3) == 0
		|| strncmp(filename, "." SENT ".", sizeof(SENT)+1) == 0)
		is_sent_header=1;
	if ((p && (size_t)(p - filename) >=
		sizeof(DRAFTS) + 5 && strncmp(p-(sizeof(DRAFTS) + 5),
			"/." DRAFTS "/", sizeof(DRAFTS)+2) == 0)
		|| strncmp(filename, "." DRAFTS "/", sizeof(DRAFTS)+1) == 0)
		is_sent_header=1;

	MSGINFO mi;

	rfc822::fdstreambuf fp{maildir_semisafeopen(filename, O_RDONLY, 0)};

	if (fp.error())
	{
		return (mi);
	}

	/* mi->filename shall be the base filename, normalized as :2, */

	if ((p=strrchr(filename, '/')) != NULL)
		p++;
	else	p=filename;

	mi.filename=p;

	if (fstat(fp.fileno(), &stat_buf) == 0)
	{
		mi.mi_mtime=stat_buf.st_mtime;
		mi.mi_ino=stat_buf.st_ino;
		mi.size_n=stat_buf.st_size;
		mi.size_s=showsize(stat_buf.st_size);
		mi.date_n=mi.mi_mtime;	/* Default if no Date: */
	}
	else
	{
		return (mi);
	}

	rfc2045::entity::line_iter<false>::headers headers{fp};

	headers.name_lc=true;

	do
	{
		const auto &[hdr, val]=headers.name_content();
		if (hdr == "subject")
		{
			// We're going to place the core subject into msginfo,
			// and store the original, decoded, subject into
			// orig_subject. The core subject will be used for
			// sorting and we'll restore it before saving it.

			auto [s, flags] = rfc822::coresubj(val);

			if (flags)
			{
				s += " (fwd)"; // Just for sorting purposes.
			}

			mi.subject_s=std::move(s);

			rfc822::display_header(
				"subject",
				val,
				unicode::utf_8,
				std::back_inserter(orig_subject)
			);
		}

		if (hdr == "date" && mi.date_s.empty())
		{
			std::optional<time_t> t=rfc822::parse_date(val);

			if (t)
			{
				mi.date_n=*t;
				mi.date_s=displaydate(mi.date_n);
			}
		}

		if ((is_sent_header ?
			hdr == "to" || hdr == "cc":
			hdr == "from") && fromheader.empty())
		{
			rfc822::tokens t{val};
			rfc822::addresses a{t};

			for (auto &addr:a)
			{
				if (addr.address.empty())
					continue;

				if (!fromheader.empty())
				{
					fromheader += "...";
					break;
				}

				addr.display_name(
					unicode::utf_8,
					std::back_inserter(fromheader)
				);
			}

		}

		if (!mi.date_s.empty() && !fromheader.empty() && !mi.subject_s.empty())
			break;
	} while (headers.next());

	mi.from_s=fromheader;
	std::replace(mi.from_s.begin(), mi.from_s.end(), '\n', ' ');
	std::replace(mi.subject_s.begin(), mi.subject_s.end(), '\n', ' ');
	if (mi.date_s.empty())
		mi.date_s=displaydate(mi.date_n);

	return (mi);
}

/************************************************************************/

/* Save cache file */

static unsigned long save_cnt, savenew_cnt;
static time_t	save_time;

static std::string save_dbname;
static std::string save_tmpdbname;
struct dbobj	tmpdb;

static void maildir_save_start(const char *folder,
			       const char *maildir, time_t t)
{
	int fd;
	struct maildir_tmpcreate_info createInfo;

	save_dbname=foldercachename(folder);

	save_time=t;
#if 1
	{
		auto tmpfname = std::string(maildir) + "/" MAILDIRCURCACHE ".nfshack";

		int f = open(tmpfname.c_str(), O_CREAT|O_WRONLY, 0600);

		if (f != -1) {
			struct stat s;
			if (write(f, ".", 1) != 1)
				; /* ignore */
			fsync(f);
			if (fstat(f, &s) == 0)
				save_time = s.st_mtime;
			close(f);
			unlink(tmpfname.c_str());
		}
	}
#endif

	maildir_tmpcreate_init(&createInfo);
	createInfo.maildir=maildir;
	createInfo.uniq="sqwebmail-db";
	createInfo.doordie=1;

	if ((fd=maildir_tmpcreate_fd(&createInfo)) < 0)
	{
		fprintf(stderr, "ERR: Can't create cache file %s: %s\n",
		       maildir, strerror(errno));
		error(strerror(errno));
	}
	close(fd);

	save_tmpdbname=createInfo.tmpname;
	maildir_tmpcreate_free(&createInfo);

	dbobj_init(&tmpdb);

	if (dbobj_open(&tmpdb, save_tmpdbname.c_str(), "N")) {
		fprintf(stderr, "ERR: Can't create cache file |%s|: %s\n",
			save_tmpdbname.c_str(), strerror(errno));
		error("Can't create cache file.");
	}

	save_cnt=0;
	savenew_cnt=0;
}

static void maildir_saveinfo(const MSGINFO &m)
{
	std::string rec;
	char	recnamebuf[MAXLONGSIZE+40];

	rec.reserve(m.filename.size()+m.from_s.size()+
		       m.subject_s.size()+m.size_s.size()+MAXLONGSIZE*4+
		       sizeof("FILENAME=\nFROM=\nSUBJECT=\nSIZES=\nDATE=\n"
			      "SIZEN=\nTIME=\nINODE=\n")+100);

	rec="FILENAME=";
	rec += m.filename;
	rec += "\nFROM=";
	rec += m.from_s;
	rec += "\nSUBJECT=";
	rec += m.subject_s;
	rec += "\nSIZES=";
	rec += m.size_s;

	char buf[MAXLONGSIZE+1];
	*std::to_chars(buf, buf+sizeof(buf)-1, m.date_n).ptr=0;
	rec+="\nDATE=";
	rec+=buf;
	rec+="\nSIZEN=";
	*std::to_chars(buf, buf+sizeof(buf)-1, m.size_n).ptr=0;
	rec+=buf;
	rec+="\nTIME=";
	*std::to_chars(buf, buf+sizeof(buf)-1, m.mi_mtime).ptr=0;
	rec+=buf;
	rec+="\nINODE=";
	*std::to_chars(buf, buf+sizeof(buf)-1, m.mi_ino).ptr=0;
	rec+=buf;
	rec+="\n";
	sprintf(recnamebuf, "REC%lu", (unsigned long)save_cnt);

	if (dbobj_store(&tmpdb, recnamebuf, strlen(recnamebuf),
		rec.c_str(), rec.size(), "R"))
		enomem();

	/* Reverse lookup */
	rec.clear();
	rec.reserve(m.filename.size()+10);
	rec="FILE";
	rec += m.filename;
	auto p=rec.find(':');
	if (p != std::string::npos)
		rec.erase(p);
	sprintf(recnamebuf, "%lu", (unsigned long)save_cnt);

	if (dbobj_store(&tmpdb, rec.c_str(), rec.size(),
			recnamebuf, strlen(recnamebuf), "R"))
		enomem();

	save_cnt++;
	if (maildirfile_type(m.filename.c_str()) == MSGTYPE_NEW)
		savenew_cnt++;
}

static void maildir_save_end(const char *maildir)
{
	std::string rec;

	rec.reserve(MAXLONGSIZE*4+sizeof(
					"SAVETIME=\n"
					"COUNT=\n"
					"NEWCOUNT=\n"
					"SORT=\n")+100);

	char buf[MAXLONGSIZE+1];

	rec="SAVETIME=";

	*std::to_chars(buf, buf+sizeof(buf)-1, save_time).ptr=0;
	rec+=buf;
	rec+="\nCOUNT=";
	*std::to_chars(buf, buf+sizeof(buf)-1, save_cnt).ptr=0;
	rec+=buf;
	rec+="\nNEWCOUNT=";
	*std::to_chars(buf, buf+sizeof(buf)-1, savenew_cnt).ptr=0;
	rec+=buf;
	rec+="\nSORT=";
	*std::to_chars(buf, buf+sizeof(buf)-1, pref_flagisoldest1st).ptr=0;
	rec+=buf;
	rec += (char)pref_flagsortorder;
	rec+="\n";

	if (dbobj_store(&tmpdb, "HEADER", 6, rec.c_str(), rec.size(), "R"))
		enomem();
	dbobj_close(&tmpdb);

	rename(save_tmpdbname.c_str(), save_dbname.c_str());
	unlink(save_tmpdbname.c_str());

	save_dbname.clear();
	save_tmpdbname.clear();
}

void maildir_savefoldermsgs(const char *folder)
{
}

/************************************************************************/

static void createmdcache(const char *folder, const char *maildir)
{
	std::set<MSGINFO, messagecmp> msgset;

	auto curname=std::string(maildir) + "/cur";

	time(&current_time);

	maildir_save_start(folder, maildir, current_time);

	std::error_code ec;
	std::unordered_map<const MSGINFO *, std::string> orig_subjects;

	for (const auto &entry : fs::directory_iterator(curname, ec))
	{
		std::string filename = entry.path().filename().string();

		if (filename[0] == '.')
			continue;

		auto f = curname + "/" + filename;

		std::string orig_subject;

		auto mi=maildir_ngetinfo(f.c_str(), orig_subject);

		orig_subjects.emplace( &*msgset.insert(std::move(mi)).first,
				       orig_subject );
	}

	// Before saving it, restore the original subject.
	for (auto &m:msgset)
	{
		auto info=m;

		info.subject_s=std::move(orig_subjects[&m]);
		maildir_saveinfo(info);
	}
	maildir_save_end(maildir);
}

static int chkcache(const char *folder)
{
	if (!opencache(folder, "W"))	return (-1);

	if (isoldestfirst != pref_flagisoldest1st)	return (-1);
	if (sortorder != pref_flagsortorder)		return (-1);

	return (0);
}

static void	maildir_getfoldermsgs(const char *folder)
{
	auto dir=xlate_shmdir(folder);

	if (dir.empty())	return;

	mkdir (MAILDIRCURCACHE, 0700);

	while ( chkcache(folder) )
	{
		closedb();
		createmdcache(folder, dir.c_str());
	}
}

void	maildir_remcache(const char *folder)
{
	auto cachename=foldercachename(folder);

	if (cachename.empty())
		return;
	unlink(cachename.c_str());
	if (folderdatname == cachename)
		closedb();
}

void	maildir_reload(const char *folder)
{
	auto	dir=xlate_shmdir(folder);
	struct	stat	stat_buf;

	if (dir.empty())	return;

	auto curname=dir + "/cur";

	time(&current_time);

	/* Remove old cache file when: */

	if (opencache(folder, "W"))
	{
		if ( stat(curname.c_str(), &stat_buf) != 0 ||
			stat_buf.st_mtime >= cachemtime)
		{
			closedb();
			createmdcache(folder, dir.c_str());
		}
	}
	maildir_getfoldermsgs(folder);
}

/*
**  Return a sorted list of folders.
**
*/

struct add_shared_info {
	std::vector<std::string> *folders;
	const char *inbox_pfix;
};

static void list_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	std::string o;

	while (*n)
	{
		if (*n == '.')
			break;
		++n;
	}

	o.reserve(strlen(i->inbox_pfix)+strlen(n));
	o=i->inbox_pfix;
	o += n;

	i->folders->push_back(o);
}

static void list_shared_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	std::string o;

	o.reserve(sizeof(SHARED ".")-1 + strlen(n));
	o=SHARED ".";
	o += n;

	i->folders->push_back(o);
}

static void list_sharable_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	std::string o;

	o.reserve(sizeof(SHARED ".")-1 + strlen(n));
	o=SHARED ".";
	o += n;

	for (auto &f: *i->folders)
		if (f == o)
			return;

	i->folders->push_back(o);
}

static bool shcomparefunc( const std::string &a, const std::string &b)
{
	return (strcasecmp(a.c_str(), b.c_str()) < 0);
}

std::vector<std::string> maildir_listfolders(
	const char *inbox_pfix,
	const char *homedir
)
{
	struct add_shared_info info;
	std::vector<std::string> folders;

	info.folders=&folders;
	info.inbox_pfix=inbox_pfix;

	if (!homedir)
		homedir=".";

	/*
	** Sort unsubscribed folders AFTER all subscribed folders.
	** This is done by grabbing INBOX, then shared subscribed folders
	** first, memorizing the folder cnt, adding unsubscribed folders,
	** then sorting the unsubscribed folders separately.
	*/

	maildir_list(homedir, list_callback, &info);

	if (strcmp(homedir, ".") == 0)
		maildir_list_shared(".", list_shared_callback, &info);

	auto sh_cnt=folders.size();
	if (strcmp(homedir, ".") == 0)
		maildir_list_sharable(".", list_sharable_callback, &info);

	std::sort(folders.begin(), folders.begin()+sh_cnt, shcomparefunc);
	std::sort(folders.begin()+sh_cnt, folders.end(), shcomparefunc);

	return (folders);
}

int maildir_create(const char *foldername)
{
	int	rc= -1;

	auto dir=xlate_mdir(foldername);
	if (dir.empty())
		return 0;

	if (mkdir(dir.c_str(), 0700) == 0)
	{
		auto tmp=dir + "/tmp";

		if (mkdir(tmp.c_str(), 0700) == 0)
		{
			auto tmp2=dir + "/new";

			if (mkdir(tmp2.c_str(), 0700) == 0)
			{
				auto tmp3=dir + "/cur";

				if (mkdir(tmp3.c_str(), 0700) == 0)
				{
					auto tmp4=dir + "/maildirfolder";

					close(open(tmp4.c_str(), O_RDWR|O_CREAT, 0600));
					rc=0;
				}
			}
			if (rc)	rmdir(tmp2.c_str());
		}
		if (rc)	rmdir(tmp.c_str());
	}
	if (rc)	rmdir(dir.c_str());
	return (rc);
}

int maildir_delete(const char *foldername, int deletecontent)
{
	int	rc=0;

	auto minfo=maildir::info_imap_find(foldername, login_returnaddr());

	if (!minfo)
		return -1;

	std::string dir;

	if (minfo.maildir == INBOX ||
	    minfo.maildir == INBOX "." SENT ||
	    minfo.maildir == INBOX "." TRASH ||
	    minfo.maildir == INBOX "." DRAFTS ||
	    (dir=maildir::name2dir(minfo.homedir.c_str(),
					   minfo.maildir.c_str())).empty())
		return (-1);

	auto tmp=dir + "/tmp";
	auto cur=dir + "/cur";
	auto newp=dir + "/new";

	if (!deletecontent)
	{
		if (rmdir(newp.c_str()) || rmdir(cur.c_str()))
		{
			mkdir(newp.c_str(), 0700);
			mkdir(cur.c_str(), 0700);
			rc= -1;
		}
	}

	if (rc == 0 && maildir_del(dir.c_str()))
		rc= -1;

	if (rc == 0)
		maildir_acl_delete(minfo.homedir.c_str(),
				   strchr(minfo.maildir.c_str(), '.'));

	return (rc);
}

/* ------------------------------------------------------------------- */

/* Here's where we create a new message in a maildir.  First maildir_createmsg
** is called.  Then, the message contents are defined via maildir_writemsg,
** then maildir_closemsg is called. */

static char writebuf[BUFSIZ];
static char *writebufptr;
static int writebufcnt, writebufleft;
static int writeerr;
off_t writebufpos;
int	writebuf8bit;

int	maildir_createmsg(
	const char *foldername,
	const char *seq,
	std::string &retname
)
{
	char	*p;
	auto	dir=xlate_mdir(foldername);
	char	*filename;
	int	n;
	struct maildir_tmpcreate_info createInfo;

	/* Create a new file in the tmp directory. */

	maildir_tmpcreate_init(&createInfo);

	createInfo.maildir=dir.c_str();
	createInfo.uniq=seq;
	createInfo.doordie=1;

	if ((n=maildir_tmpcreate_fd(&createInfo)) < 0)
	{
		error("maildir_createmsg: cannot create temp file.");
	}

	/*
	** Ok, new maildir semantics: filename in new is different than in tmp.
	** Originally this whole scheme was designed with the filenames being
	** the same.  We can fix it like this:
	*/

	close(n);

	memcpy(strrchr(createInfo.newname, '/')-3, "tmp", 3); /* Hack */

	if (rename(createInfo.tmpname, createInfo.newname) < 0 ||
	    (n=open(createInfo.newname, O_RDWR)) < 0)
	{
		error("maildir_createmsg: cannot create temp file.");
	}

	filename=createInfo.newname;

	p=strrchr(filename, '/');
	retname=p+1;
	maildir_tmpcreate_free(&createInfo);

	/* Buffer writes */

	writebufcnt=0;
	writebufpos=0;
	writebuf8bit=0;
	writebufleft=0;
	writeerr=0;
	return (n);
}


/* Like createmsg, except we're rewriting the contents of this message here,
** so we might as well use the same name. */

int maildir_recreatemsg(
	const char *folder,
	const char *name,
	std::string &baseptr
)
{
	auto	dir=xlate_mdir(folder);
	int	n;

	auto base=maildir_basename(name);
	auto p=dir + "/tmp/" + base;

	baseptr=base;
	n=maildir_safeopen(p.c_str(), O_CREAT|O_RDWR|O_TRUNC, 0644);
	writebufcnt=0;
	writebufleft=0;
	writeerr=0;
	writebufpos=0;
	writebuf8bit=0;
	return (n);
}

/* Flush write buffer */

static void writeflush(int n)
{
const char	*q=writebuf;
int	c;

	/* Keep calling write() until there's an error, or we're all done */

	while (!writeerr && writebufcnt)
	{
		c=write(n, q, writebufcnt);
		if ( c <= 0)
			writeerr=1;
		else
		{
			q += c;
			writebufcnt -= c;
		}
	}

	/* We have an empty buffer now */

	writebufcnt=0;
	writebufleft=sizeof(writebuf);
	writebufptr=writebuf;
}

	/* Add whatever we have to the buffer */

/* Write to the message file.  The writes are buffered, and we will set a
** flag if there's error writing to the message file.
*/

void maildir_writemsgstr(int n, const char *p)
{
	maildir_writemsg(n, p, strlen(p));
}

void	maildir_writemsg(int n, const char *p, size_t cnt)
{
int	c;
size_t	i;

	writebufpos += cnt;	/* I'm optimistic */
	for (i=0; i<cnt; i++)
		if (p[i] & 0x80)	writebuf8bit=1;

	while (cnt)
	{
		/* Flush buffer if it's full.  No need to flush if we've
		** already had an error before. */

		if (writebufleft == 0)	writeflush(n);

		c=writebufleft;
		if ((size_t)c > cnt)	c=cnt;
		memcpy(writebufptr, p, c);
		writebufptr += c;
		p += c;
		cnt -= c;
		writebufcnt += c;
		writebufleft -= c;
	}
}

/* The new message has been written out.  Move the new file from the tmp
** directory to either the new directory (if 'new' is set), or to the
** cur directory.
**
** The caller might have encountered an error condition.  If 'isok' is zero,
** we just delete the file.  If we had a write error, we delete the file
** as well.  We return -1 in both cases, or 0 if the new file has been
** succesfully moved into its final resting place.
*/

int	maildir_writemsg_flush(int n )
{
	writeflush(n);
	return (writeerr);
}

static int maildir_closemsg_common(int n,
	const char *folder,
	const std::string &retname,
	int isok,
	unsigned long prevsize
);

int	maildir_closemsg(int n,	/* File descriptor */
	const char *folder,	/* Folder */
	const std::string &retname,	/* Filename in folder */
	int isok,	/* 0 - discard it (I changed my mind),
			   1 - keep it
			  -1 - keep it even if we'll exceed the quota
			*/
	unsigned long prevsize	/* Prev size of this msg, used in quota calc */
		)
{
	int r=maildir_closemsg_common(n, folder, retname, isok, prevsize);

	close(n);
	return r;
}

int	maildir_closemsg(
	rfc822::fdstreambuf &n,
	const char *folder,
	const std::string &retname,
	int isok,
	unsigned long prevsize
)
{
	int r=maildir_closemsg_common(n.fileno(), folder, retname, isok,
				      prevsize);

	n=rfc822::fdstreambuf{};
	return r;
}

static int	maildir_closemsg_common(
	int n,
	const char *folder,
	const std::string &retname,
	int isok,
	unsigned long prevsize
)
{
	auto	dir=xlate_mdir(folder);
	auto oldname=dir + "/tmp/" + retname;
	struct	stat	stat_buf;

	writeflush(n);	/* If there's still anything in the buffer */
	if (fstat(n, &stat_buf))
	{
		unlink(oldname.c_str());
		enomem();
	}

	auto newname=maildir_find(folder, retname.c_str());
		/* If we called recreatemsg before */

	if (newname.empty())
	{
		newname=dir;
		newname += "/cur/";
		newname += retname;
		newname += ":2,S";
	}

	if (writeerr)
	{
		unlink(oldname.c_str());
		enomem();
	}

	if (isok)
	{
		if ((off_t)prevsize < stat_buf.st_size)
		{
			struct maildirsize info;

			if (maildir_quota_add_start(".", &info,
						    stat_buf.st_size-prevsize,
						    prevsize == 0 ? 1:0,
						    NULL))
			{
				if (isok < 0) /* Force it */
				{
					maildir_quota_deleted(".", (int64_t)
							      (stat_buf.st_size
							       -prevsize),
							      prevsize == 0
							      ? 1:0);
					isok= -2;
				}
				else
					isok=0;
			}
			maildir_quota_add_end(&info, stat_buf.st_size-prevsize,
					      prevsize == 0 ? 1:0);
		}
		else if ((off_t)prevsize != stat_buf.st_size)
		{
			maildir_quota_deleted(".", (int64_t)
					      (stat_buf.st_size-prevsize),
					      prevsize == 0 ? 1:0);
		}
	}

	if (isok)
		rename(oldname.c_str(), newname.c_str());

	unlink(oldname.c_str());

	if (isok)
	{
		char	*realnewname=maildir_requota(newname.c_str(),
						     stat_buf.st_size);

		if (newname != realnewname)
			rename(newname.c_str(), realnewname);
	}
	return (isok && isok != -2? 0:-1);
}

static void	maildir_deletenewmsg_common(const char *folder,
					    const std::string &filename);

void	maildir_deletenewmsg(
	int n,
	const char *folder,
	const std::string &filename
)
{
	close(n);
	maildir_deletenewmsg_common(folder, filename);
}

void	maildir_deletenewmsg(rfc822::fdstreambuf &n,
			     const char *folder, const std::string &filename)
{
	n={};
	maildir_deletenewmsg_common(folder, filename);
}

static void	maildir_deletenewmsg_common(
	const char *folder, const std::string &filename
)
{
	auto	dir=xlate_mdir(folder);
	auto	oldname=dir + "/tmp/" + filename;

	unlink(oldname.c_str());
}

void maildir_cleanup()
{
	closedb();
}

/*
** Convert folder names to modified-UTF7 encoding.
*/

std::string folder_toutf8(const char *foldername)
{
	char *p;
	int converr;

	p=unicode_convert_tobuf(foldername, sqwebmail_content_charset,
				  unicode_x_smap_modutf8, &converr);

	if (p && converr)
	{
		free(p);
		p=NULL;
	}

	if (p)
	{
		std::string s{p};
		free(p);
		return s;
	}

	return foldername;
}

char *folder_fromutf8(const char *foldername)
{
	char *p;
	int converr;

	p=unicode_convert_tobuf(foldername,
				  unicode_x_smap_modutf8,
				  sqwebmail_content_charset,
				  &converr);

	if (p && converr)
	{
		free(p);
		p=NULL;
	}

	if (p)
		return (p);

	p=strdup(foldername);
	if (!p)
		enomem();
	return (p);
}
