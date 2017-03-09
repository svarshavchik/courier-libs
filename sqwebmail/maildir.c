/*
** Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
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

#if	HAVE_DIRENT_H
#include	<dirent.h>
#define	NAMLEN(dirent)	strlen(dirent->d_name)
#else
#define	dirent	direct
#define	NAMLEN(dirent)	((dirent)->d_namlen)
#if	HAVE_SYS_NDIR_H
#include	<sys/ndir.h>
#endif
#if	HAVE_SYS_DIR_H
#include	<sys/dir.h>
#endif
#if	HAVE_NDIR_H
#include	<ndir.h>
#endif
#endif

#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UTIME_H
#include	<utime.h>
#endif

#include	<courier-unicode.h>

#include	"strftime.h"

static time_t	current_time;

extern time_t rfc822_parsedt(const char *);
extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_mailboxid;

static char *folderdatname=0;	/* Which folder has been cached */
static struct dbobj folderdat;
static const char *folderdatmode;	/* "R" or "W" */

static time_t cachemtime;	/* Modification time of the cache file */
static unsigned long new_cnt, all_cnt;
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

static char *xlate_mdir(const char *foldername)
{
	struct maildir_info minfo;
	char	*p;

	if (maildir_info_imap_find(&minfo, foldername,
				   login_returnaddr())<0)
	{
		cginocache();
		printf("Content-Type: text/plain\n\n"
		       "Unable to translate mailbox: %s\n",
		       foldername);
		cleanup();
		fake_exit(1);
	}

	if (minfo.homedir == NULL || minfo.maildir == NULL)
	{
		maildir_info_destroy(&minfo);
		cginocache();
		printf("Content-Type: text/plain\n\n"
		       "Mailbox \"%s\" is not supported.\n",
		       foldername);
		cleanup();
		fake_exit(1);
		enomem();
	}

	p=maildir_name2dir(minfo.homedir, minfo.maildir);

	maildir_info_destroy(&minfo);
	return p;
}

static char *xlate_shmdir(const char *foldername)
{
	struct maildir_info minfo;
	char	*p;

	if (maildir_info_imap_find(&minfo, foldername,
				   login_returnaddr())<0)
	{
		printf("Content-Type: text/plain\n\n%s\n", foldername);
		enomem();
	}

	if (minfo.mailbox_type == MAILBOXTYPE_OLDSHARED)
		return maildir_shareddir(".", strchr(foldername, '.')+1);

	if (minfo.homedir == NULL || minfo.maildir == NULL)
	{
		maildir_info_destroy(&minfo);
		enomem();
	}

	p=maildir_name2dir(minfo.homedir, minfo.maildir);

	maildir_info_destroy(&minfo);
	return p;
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

/* Put together a filename from up to three parts */

char *alloc_filename(const char *dir1, const char *dir2, const char *filename)
{
char	*p;

	if (!dir1)	dir1="";
	if (!dir2)	dir2="";

	p=malloc(strlen(dir1)+strlen(dir2)+strlen(filename)+3);

	if (!p)	enomem();

	strcpy(p, dir1);
	if (*dir2)
	{
		if (*p)	strcat(p, "/");
		strcat(p, dir2);
	}
	if (*filename)
	{
		if (*p)	strcat(p, "/");
		strcat(p, filename);
	}

	return (p);
}

/*
** char *maildir_find(const char *maildir, const char *filename)
**	- find a message in a maildir
**
** Return the full path to the indicated message.  If the message flags
** in filename have changed, we search for the given message.
*/

char *maildir_find(const char *folder, const char *filename)
{
char	*p;
char	*d=xlate_shmdir(folder);
int	fd;

	if (!d)	return (0);
	p=maildir_filename(d, 0, filename);
	free(d);

	if (!p)	enomem();

	if ((fd=open(p, O_RDONLY)) >= 0)
	{
		close(fd);
		return (p);
	}
	free(p);
	return (0);
}

/*
** char *maildir_basename(const char *filename)
**
** - return base name of the file (strip off cur or new, strip of trailing :)
*/

char *maildir_basename(const char *filename)
{
const char *q=strrchr(filename, '/');
char	*p, *r;

	if (q)	++q;
	else	q=filename;
	p=alloc_filename("", "", q);
	if ((r=strchr(p, ':')) != 0)	*r='\0';
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

static char *maildir_addflagfilename(const char *filename, char flag)
{
char	*new_filename=malloc(strlen(filename)+5);
		/* We can possibly add as many as four character */
char	*p;
char	*q;

	strcpy(new_filename, filename);
	p=strrchr(new_filename, '/');
	if (!p)	p=new_filename;
	if ((q=strchr(p, ':')) == 0)
		strcat(new_filename, ":2,");
	else if (q[1] != '2' && q[2] != ',')
		strcpy(p, ":2,");
	p=strchr(p, ':');
	if (strchr(p, flag))
	{
		free(new_filename);
		return (0);		/* Already set */
	}

	p += 2;
	while (*p && *p < flag)	p++;
	q=p+strlen(p);
	while ((q[1]=*q) != *p)
		--q;
	*p=flag;
	return (new_filename);
}

static void closedb()
{
	if (folderdatname)
	{
		dbobj_close(&folderdat);
		free(folderdatname);
		folderdatname=0;
	}
}

static char *foldercachename(const char *folder)
{
	char *f;

	if (strchr(folder, '/'))
	{
		enomem();
		return NULL;
	}

	f=malloc(sizeof(MAILDIRCURCACHE "/" DBNAME ".")+strlen(folder));
	if (!f)
		enomem();

	return strcat(strcpy(f, MAILDIRCURCACHE "/" DBNAME "."),
		      folder);
}

static int opencache(const char *folder, const char *mode)
{
char	*cachename;
size_t	l;
char	*p;
char	*q;
char	*r;
unsigned long ul;

	{
		char	*maildir=xlate_shmdir(folder);

		if (!maildir)	return (-1);
		free(maildir);
	}

	cachename=foldercachename(folder);

	if (folderdatname && strcmp(folderdatname, cachename) == 0)
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
			free(cachename);
			return (0);
				/* We already have this folder cache open */
		}
	}
	closedb();
	folderdatmode=mode;

	dbobj_init(&folderdat);
	if (dbobj_open(&folderdat, cachename, mode))	return (-1);
	folderdatname=cachename;

	if ((p=dbobj_fetch(&folderdat, "HEADER", 6, &l, "")) == 0)
		return (0);
	q=malloc(l+1);
	if (!q)	enomem();
	memcpy(q, p, l);
	q[l]=0;
	free(p);

	cachemtime=0;
	new_cnt=0;
	all_cnt=0;
	isoldestfirst=0;
	sortorder=0;

	for (p=q; (p=strtok(p, "\n")) != 0; p=0)
	{
		if ((r=strchr(p, '=')) == 0)	continue;
		*r++=0;
		if (strcmp(p, "SAVETIME") == 0 &&
			parse_ul(r, &ul))
			cachemtime=ul;
		else if (strcmp(p, "COUNT") == 0)
			parse_ul(r, &all_cnt);
		else if (strcmp(p, "NEWCOUNT") == 0)
			parse_ul(r, &new_cnt);
		else if (strcmp(p, "SORT") == 0)
		{
		unsigned long ul;
		const char *s;

			if ((s=parse_ul(r, &ul)) != 0)
			{
				isoldestfirst=ul;
				sortorder= *s;
			}
		}
	}
	free(q);
	return (0);
}

/* And, remove a flag */

static void maildir_remflagname(char *filename, char flag)
{
char	*p;

	p=strrchr(filename, '/');
	if (!p)	p=filename;
	if ((p=strchr(p, ':')) == 0)	return;
	else if (p[1] != '2' && p[2] != ',')
		return;

	p=strchr(p, ':');
	p += 3;

	while (*p && isalpha((int)(unsigned char)*p))
	{
		if (*p == flag)
		{
			while ( (*p=p[1]) != 0)
				p++;
			return;
		}
		p++;
	}
}

static MSGINFO *get_msginfo(unsigned long n)
{
char	namebuf[MAXLONGSIZE+40];
char	*p;
size_t	len;
unsigned long ul;

static	char *buf=0;
size_t	bufsize=0;
static	MSGINFO msginfo_buf;

	sprintf(namebuf, "REC%lu", n);

	p=dbobj_fetch(&folderdat, namebuf, strlen(namebuf), &len, "");
	if (!p)	return (0);

	if (!buf || len > bufsize)
	{
		buf= buf ? realloc(buf, len+1):malloc(len+1);
		if (!buf)	enomem();
		bufsize=len;
	}
	memcpy(buf, p, len);
	buf[len]=0;
	free(p);

	memset(&msginfo_buf, 0, sizeof(msginfo_buf));
	msginfo_buf.filename= msginfo_buf.date_s= msginfo_buf.from_s=
	msginfo_buf.subject_s= msginfo_buf.size_s="";

	for (p=buf; (p=strtok(p, "\n")) != 0; p=0)
	{
	char	*q=strchr(p, '=');

		if (!q)	continue;
		*q++=0;

		if (strcmp(p, "FILENAME") == 0)
			msginfo_buf.filename=q;
		else if (strcmp(p, "FROM") == 0)
			msginfo_buf.from_s=q;
		else if (strcmp(p, "SUBJECT") == 0)
			msginfo_buf.subject_s=q;
		else if (strcmp(p, "SIZES") == 0)
			msginfo_buf.size_s=q;
		else if (strcmp(p, "DATE") == 0 &&
			parse_ul(q, &ul))
		{
			msginfo_buf.date_n=ul;
			msginfo_buf.date_s=displaydate(msginfo_buf.date_n);
		}
		else if (strcmp(p, "SIZEN") == 0 &&
			parse_ul(q, &ul))
			msginfo_buf.size_n=ul;
		else if (strcmp(p, "TIME") == 0 &&
			parse_ul(q, &ul))
			msginfo_buf.mi_mtime=ul;
		else if (strcmp(p, "INODE") == 0 &&
			parse_ul(q, &ul))
			msginfo_buf.mi_ino=ul;
	}
	return (&msginfo_buf);
}

static MSGINFO *get_msginfo_alloc(unsigned long n)
{
MSGINFO	*msginfop=get_msginfo(n);
MSGINFO *p;

	if (!msginfop)	return (0);

	if ((p= (MSGINFO *) malloc(sizeof(*p))) == 0)
		enomem();

	memset(p, 0, sizeof(*p));
	if ((p->filename=strdup(msginfop->filename)) == 0 ||
		(p->date_s=strdup(msginfop->date_s)) == 0 ||
		(p->from_s=strdup(msginfop->from_s)) == 0 ||
		(p->subject_s=strdup(msginfop->subject_s)) == 0 ||
		(p->size_s=strdup(msginfop->size_s)) == 0)
		enomem();
	p->date_n=msginfop->date_n;
	p->size_n=msginfop->size_n;
	p->mi_mtime=msginfop->mi_mtime;
	p->mi_ino=msginfop->mi_ino;
	return (p);
}

static void put_msginfo(MSGINFO *m, unsigned long n)
{
char	namebuf[MAXLONGSIZE+40];
char	*rec;

	sprintf(namebuf, "REC%lu", n);

	rec=malloc(strlen(m->filename)+strlen(m->from_s)+
		strlen(m->subject_s)+strlen(m->size_s)+MAXLONGSIZE*4+
		sizeof("FILENAME=\nFROM=\nSUBJECT=\nSIZES=\nDATE=\n"
			"SIZEN=\nTIME=\nINODE=\n")+100);
	if (!rec)	enomem();

	sprintf(rec, "FILENAME=%s\nFROM=%s\nSUBJECT=%s\nSIZES=%s\n"
		"DATE=%lu\n"
		"SIZEN=%lu\n"
		"TIME=%lu\n"
		"INODE=%lu\n",
		m->filename,
		m->from_s,
		m->subject_s,
		m->size_s,
		(unsigned long)m->date_n,
		(unsigned long)m->size_n,
		(unsigned long)m->mi_mtime,
		(unsigned long)m->mi_ino);

	if (dbobj_store(&folderdat, namebuf, strlen(namebuf),
		rec, strlen(rec), "R"))
		enomem();
	free(rec);
}

static void update_foldermsgs(const char *folder, const char *newname, size_t pos)
{
MSGINFO	*p;
char *n;

	n=strrchr(newname, '/')+1;
	if (opencache(folder, "W") || (p=get_msginfo(pos)) == 0)
	{
		error("Internal error in update_foldermsgs");
		return;
	}

	p->filename=n;

	put_msginfo(p, pos);
}

static void maildir_markflag(const char *folder, size_t pos, char flag)
{
MSGINFO	*p;
char	*filename;
char	*new_filename;

	if (opencache(folder, "W") || (p=get_msginfo(pos)) == 0)
	{
		error("Internal error in maildir_markflag");
		return;
	}

	filename=maildir_find(folder, p->filename);
	if (!filename)	return;

	if ((new_filename=maildir_addflagfilename(filename, flag)) != 0)
	{
		rename(filename, new_filename);
		update_foldermsgs(folder, new_filename, pos);
		free(new_filename);
	}
	free(filename);
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
	char	*filename;
	char	*new_filename;
	char acl_buf[2];

	strcpy(acl_buf, ACL_WRITE);
	acl_computeRightsOnFolder(folder, acl_buf);

	if (acl_buf[0] == 0)
		return;

	filename=maildir_find(folder, message);

	if (filename &&
		(new_filename=maildir_addflagfilename(filename, 'R')) != 0)
	{
		rename(filename, new_filename);
		free(new_filename);
	}
	if (filename)	free(filename);
}

char *maildir_posfind(const char *folder, size_t *pos)
{
MSGINFO	*p;
char	*filename;

	if (opencache(folder, "R") || (p=get_msginfo( *pos)) == 0)
	{
		error("Internal error in maildir_posfind");
		return (0);
	}

	filename=maildir_find(folder, p->filename);
	return(filename);
}


int maildir_name2pos(const char *folder, const char *filename, size_t *pos)
{
	char *p, *q;
	size_t len;

	maildir_reload(folder);
	if (opencache(folder, "R"))
	{
		error("Internal error in maildir_name2pos");
		return (0);
	}

	p=malloc(strlen(filename)+10);
	if (!p)
		enomem();
	strcat(strcpy(p, "FILE"), filename);
	q=strchr(p, ':');
	if (q)
		*q=0;

	q=dbobj_fetch(&folderdat, p, strlen(p), &len, "");
	free(p);

	if (!q)
		return (-1);

	*pos=0;
	for (p=q; len; --len, p++)
	{
		if (isdigit((int)(unsigned char)*p))
			*pos = *pos * 10 + (*p-'0');
	}
	free(q);
	return (0);
}

void maildir_msgpurge(const char *folder, size_t pos)
{
char *filename=maildir_posfind(folder, &pos);

	if (filename)
	{
		unlink(filename);
		free(filename);
	}
}

void maildir_msgpurgefile(const char *folder, const char *msgid)
{
char *filename=maildir_find(folder, msgid);

	if (filename)
	{
		char *d=xlate_shmdir(folder);

		if (d)
		{
			if (strncmp(folder, SHARED ".", sizeof(SHARED))
			    && maildirquota_countfolder(d) &&
			    maildirquota_countfile(filename))
			{
				unsigned long filesize=0;

				if (maildir_parsequota(filename, &filesize))
				{
					struct stat stat_buf;

					if (stat(filename, &stat_buf) == 0)
						filesize=stat_buf.st_size;
				}

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}
			free(d);
		}
		unlink(filename);
		free(filename);
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
	char *destdir, *fromdir;
	const char *p;
	char *basename;
	struct stat stat_buf;
	char	*new_filename;
	unsigned long	filesize=0;
	int	no_link=0;
	struct maildirsize quotainfo;
	char *newname;
	int from_shared, dest_shared;
	char acl_buf[4];

	if (stat(file, &stat_buf) || stat_buf.st_nlink != 1)
	{
		unlink(file); /* Already moved, or crashed in the middle of
			      ** moving the file, so clean up.
			      */
		return (0);
	}


	/* Update quota */

	destdir=xlate_shmdir(dest);
	if (!destdir)	enomem();

	fromdir=xlate_shmdir(from);
	if (!fromdir)	enomem();

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
		     || !maildirquota_countfolder(destdir)) &&
		    maildirquota_countfile(file))
		{

			if (!from_shared)
				maildir_quota_deleted(".", -(int64_t)filesize, -1);
		}
		else if (!dest_shared && maildirquota_countfolder(destdir) &&
			 (from_shared || !maildirquota_countfolder(fromdir))
			 )
			/* Moving FROM trash */
		{

			if (maildir_quota_add_start(".", &quotainfo,
						    filesize, 1, NULL))
			{
				free(fromdir);
				return (-1);
			}

			maildir_quota_add_end(&quotainfo, filesize, 1);
		}
	}

	free(fromdir);

	if (from_shared || dest_shared)
	{
		struct maildir_tmpcreate_info createInfo;
		int	fromfd, tofd;
		char	*l;

		if (dest_shared)	/* Copy to the sharable folder */
		{
		char	*p=malloc(strlen(destdir)+sizeof("/shared"));

			if (!p)
			{
				free(destdir);
				enomem();
			}
			strcat(strcpy(p, destdir), "/shared");
			free(destdir);
			destdir=p;
		}

		maildir_tmpcreate_init(&createInfo);

		createInfo.maildir=destdir;
		createInfo.uniq="copy";
		createInfo.doordie=1;

		if ( dest_shared )
			umask (0022);

		if ((tofd=maildir_tmpcreate_fd(&createInfo)) < 0)
		{
			free(destdir);
			error(strerror(errno));
		}

		if (dest_shared)
		/* We need to copy it directly into /cur of the dest folder */
		{
			memcpy(strrchr(createInfo.newname, '/')-3, "cur", 3);
						/* HACK!!!!!!!!!!!! */
		}

		if ((fromfd=maildir_semisafeopen(file, O_RDONLY, 0)) < 0)
		{
			int terrno = errno;
			free(destdir);
			close(tofd);
			unlink(createInfo.tmpname);
			maildir_tmpcreate_free(&createInfo);
			error3(__FILE__, __LINE__, "Failed to open for read:",
				file, terrno);
		}

		umask (0077);
		if (msgcopy(fromfd, tofd))
		{
			int terrno = errno;
			close(fromfd);
			close(tofd);
			free(destdir);
			unlink(createInfo.tmpname);
			maildir_tmpcreate_free(&createInfo);
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
					unlink(createInfo.tmpname);
					maildir_tmpcreate_free(&createInfo);
					free(destdir);
					return (0);
				}
			}
			free(l);
		}

		if (strchr(acl_buf, ACL_DELETEMSGS[0]) == 0)
			maildir_remflagname(createInfo.newname, 'T');
		if (strchr(acl_buf, ACL_SEEN[0]) == 0)
			maildir_remflagname(createInfo.newname, 'S');
		if (strchr(acl_buf, ACL_WRITE[0]) == 0)
		{
			maildir_remflagname(createInfo.newname, 'F');
			maildir_remflagname(createInfo.newname, 'D');
			maildir_remflagname(createInfo.newname, 'R');
		}

		if (maildir_movetmpnew(createInfo.tmpname, createInfo.newname))
		{
			free(destdir);
			unlink(createInfo.tmpname);
			maildir_tmpcreate_free(&createInfo);
			error(strerror(errno));
		}
		no_link=1;	/* Don't call link(), below */
		maildir_tmpcreate_free(&createInfo);
	}

	p=strrchr(file, '/');
	if (p)	++p;
	else	p=file;

	if ( (basename=strdup(p)) == NULL)
		enomem();
	maildir_remflagname(basename, 'T');	/* Remove any deleted flag for new name */

	if (strchr(acl_buf, ACL_SEEN[0]) == 0)
		maildir_remflagname(basename, 'S');
	if (strchr(acl_buf, ACL_WRITE[0]) == 0)
	{
		maildir_remflagname(basename, 'F');
		maildir_remflagname(basename, 'D');
		maildir_remflagname(basename, 'R');
	}
	newname=alloc_filename(destdir, "cur", basename);
	free(destdir);
	free(basename);

	/* When DELETE is called for a message in TRASH, from and dest will
	** be the same, so we just mark the file as Trashed, to be removed
	** in checknew.
	*/

	if (no_link == 0 && strcmp(from, dest))
	{
		if (link(file, newname))
		{
			free(newname);
			return (-1);
		}
	}
	free(newname);

	if ((new_filename=maildir_addflagfilename(file, 'T')) != 0)
	{
		rename(file, new_filename);
		update_foldermsgs(from, new_filename, pos);
		free(new_filename);
	}
	return (0);
}

void maildir_msgdeletefile(const char *folder, const char *file, size_t pos)
{
char *filename=maildir_find(folder, file);

	if (filename)
	{
		(void)do_msgmove(folder, filename, INBOX "." TRASH, pos, 0);
		free(filename);
	}
}

int maildir_msgmovefile(const char *folder, const char *file, const char *dest,
	size_t pos)
{
	char *filename=maildir_find(folder, file);
	int	rc;

	if (!filename)	return (0);
	rc=do_msgmove(folder, filename, dest, pos, 1);
	free(filename);
	return (rc);
}

static char *foldercountfilename(const char *folder)
{
	char *f=malloc(sizeof(MAILDIRCURCACHE "/cnt.") + strlen(folder));

	if (!f)
		enomem();

	strcat(strcpy(f, MAILDIRCURCACHE "/cnt."), folder);
	return f;
}

/*
** Grab new messages from new.
*/

static void maildir_checknew(const char *folder, const char *dir)
{
	char	*dirbuf;
	struct	stat	stat_buf;
	DIR	*dirp;
	struct	dirent	*dire;
	char	acl_buf[2];

	/* Delete old files in tmp */

	maildir_purgetmp(dir);

	/* Move everything from new to cur */

	dirbuf=alloc_filename(dir, "new", "");

	for (dirp=opendir(dirbuf); dirp && (dire=readdir(dirp)) != 0; )
	{
	char	*oldname, *newname;
	char	*p;

		if (dire->d_name[0] == '.')	continue;

		oldname=alloc_filename(dirbuf, dire->d_name, "");

		newname=malloc(strlen(oldname)+4);
		if (!newname)	enomem();

		strcat(strcat(strcpy(newname, dir), "/cur/"), dire->d_name);
		p=strrchr(newname, '/');
		if ((p=strchr(p, ':')) != NULL)	*p=0;	/* Someone screwed up */
		strcat(newname, ":2,");
		rename(oldname, newname);
		free(oldname);
		free(newname);
	}
	if (dirp)	closedir(dirp);
	free(dirbuf);

	/* Look for any messages mark as deleted.  When we delete a message
	** we link it into the Trash folder, and mark the original with a T,
	** which we delete when we check for new messages.
	*/

	dirbuf=alloc_filename(dir, "cur", "");
	if (stat(dirbuf, &stat_buf))
	{
		free(dirbuf);
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
		char *f=foldercountfilename(folder);
		struct stat c_stat_buf;

		if (stat(f, &c_stat_buf) == 0 && c_stat_buf.st_mtime >
		    stat_buf.st_mtime)
		{
			free(f);
			free(dirbuf);
			return;
		}
		free(f);
	}

	strcpy(acl_buf, ACL_EXPUNGE);
	acl_computeRightsOnFolder(folder, acl_buf);

	for (dirp=opendir(dirbuf); dirp && (dire=readdir(dirp)) != 0; )
	{
	char	*p;

		if (dire->d_name[0] == '.')	continue;

		if (maildirfile_type(dire->d_name) == MSGTYPE_DELETED &&
		    acl_buf[0])
		{
			p=alloc_filename(dirbuf, "", dire->d_name);


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

				if (maildir_parsequota(dire->d_name,
						       &filesize))
					if (stat(p, &stat_buf) == 0)
						filesize=stat_buf.st_size;

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}

			maildir_unlinksharedmsg(p);
				/* Does The Right Thing if this is a shared
				** folder
				*/

			free(p);
		}
	}
	if (dirp)	closedir(dirp);
	free(dirbuf);
}

/*
** Automatically purge deleted messages.
*/

static int goodcache(const char *foldername)
{
	struct maildir_info minfo;
	char *folderdir;
	struct stat stat_buf;

	if (maildir_info_imap_find(&minfo, foldername, login_returnaddr())<0
	    || minfo.homedir == NULL || minfo.maildir == NULL)
		return 0;

	maildir_info_destroy(&minfo);

	folderdir=xlate_shmdir(foldername);

	if (stat(folderdir, &stat_buf) < 0)
	{
		free(folderdir);
		return 0;
	}
	free(folderdir);
	return 1;
}

void maildir_autopurge()
{
	char	*dir;
	char	*dirbuf;
	struct	stat	stat_buf;
	DIR	*dirp;
	struct	dirent	*dire;
	char	*filename;
	char buffer[80];
	size_t n, i;
	FILE *fp;

	/* This is called when logging in.  Version 0.18 supports maildir
	** quotas, so automatically upgrade all folders.
	*/

	for (dirp=opendir("."); dirp && (dire=readdir(dirp)) != 0; )
	{
		if (dire->d_name[0] != '.')	continue;
		if (strcmp(dire->d_name, "..") == 0)	continue;

		if (strcmp(dire->d_name, "."))
		{
			filename=alloc_filename(dire->d_name,
						"maildirfolder", "");
			if (!filename)	enomem();
			close(open(filename, O_RDWR|O_CREAT, 0644));
			free(filename);
		}

		/* Eliminate obsoleted cache files */

		filename=alloc_filename(dire->d_name, MAILDIRCOUNTCACHE, "");

		if (!filename)	enomem();
		unlink(filename);
		free(filename);

		filename=alloc_filename(dire->d_name, MAILDIRCURCACHE, "");

		if (!filename)	enomem();
		unlink(filename);
		free(filename);

		filename=alloc_filename(dire->d_name, "",
					MAILDIRCURCACHE "." DBNAME);
		if (!filename)	enomem();
		unlink(filename);
		free(filename);
	}

	/* Version 0.24 top level remove */

	unlink(MAILDIRCURCACHE);

	/* Version 4 top level remove */

	unlink(MAILDIRCURCACHE "." DBNAME);
	mkdir (MAILDIRCURCACHE, 0700);

	if (dirp)
		closedir(dirp);

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

	for (dirp=opendir(MAILDIRCURCACHE); dirp && (dire=readdir(dirp)); )
	{
		char *folderdir;

		if (dire->d_name[0] == '.')
			continue;

		if (strncmp(dire->d_name, DBNAME ".", sizeof(DBNAME)) == 0 ||
		    strncmp(dire->d_name, "cnt.", 4) == 0)
		{
			if (i == n)
			{
				if (!goodcache(strchr(dire->d_name, '.')+1))
				{
					folderdir=malloc(sizeof(MAILDIRCURCACHE
								"/")
							 + strlen(dire->
								  d_name));
					if (!folderdir)
						enomem();
					strcat(strcpy(folderdir,
						      MAILDIRCURCACHE "/"),
					       dire->d_name);
					unlink(folderdir);
					free(folderdir);
				}
				break;
			}
			++i;
			continue;
		}

		folderdir=malloc(sizeof(MAILDIRCURCACHE "/")
				 + strlen(dire->d_name));
		if (!folderdir)
			enomem();
		strcat(strcpy(folderdir, MAILDIRCURCACHE "/"), dire->d_name);
		unlink(folderdir);
		free(folderdir);
	}
	if (dirp)
		closedir(dirp);

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

	dir=xlate_mdir(INBOX "." TRASH);

	/* Delete old files in tmp */

	time(&current_time);
	dirbuf=alloc_filename(dir, "cur", "");
	free(dir);

	for (dirp=opendir(dirbuf); dirp && (dire=readdir(dirp)) != 0; )
	{
		if (dire->d_name[0] == '.')	continue;
		filename=alloc_filename(dirbuf, dire->d_name, "");
		if (stat(filename, &stat_buf) == 0 &&
		    pref_autopurge &&
		    stat_buf.st_ctime < current_time
		    - pref_autopurge * 24 * 60 * 60)
		{
			if (maildirquota_countfolder(dirbuf) &&
			    maildirquota_countfile(filename))
			{
				unsigned long filesize=0;

				if (maildir_parsequota(filename, &filesize))
					filesize=stat_buf.st_size;

				if (filesize > 0)
					maildir_quota_deleted(".",
							      -(int64_t)filesize,
							      -1);
			}

			unlink(filename);
		}

		free(filename);
	}
	if (dirp)	closedir(dirp);
	free(dirbuf);

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
	DIR	*dirp;
	struct	dirent	*dire;
	char *p;

	for (dirp=opendir("tmp"); dirp && (dire=readdir(dirp)) != 0; )
	{
		if (strstr(dire->d_name, ":mimegpg:") == 0 &&
		    strstr(dire->d_name, ":calendar:") == 0)	continue;

		p=malloc(sizeof("tmp/")+strlen(dire->d_name));

		if (p)
		{
			strcat(strcpy(p, "tmp/"), dire->d_name);
			unlink(p);
			free(p);
		}
	}

	if (dirp)
		closedir(dirp);
}

/* Ditto for search results */

void maildir_purgesearch()
{
	DIR	*dirp;
	struct	dirent	*dire;
	char *p;

	for (dirp=opendir("tmp"); dirp && (dire=readdir(dirp)) != 0; )
	{
		if (strstr(dire->d_name, ":search:") == 0)	continue;

		p=malloc(sizeof("tmp/")+strlen(dire->d_name));

		if (p)
		{
			strcat(strcpy(p, "tmp/"), dire->d_name);
			unlink(p);
			free(p);
		}
	}

	if (dirp)
		closedir(dirp);
}

static int subjectcmp(const char *a, const char *b)
{
	int	aisre;
	int	bisre;
	int	n;
	char	*as;
	char	*bs;

	as=rfc822_display_hdrvalue_tobuf("subject", a, "utf-8",
					 NULL, NULL);

	if (!as)
		as=strdup(a);

	bs=rfc822_display_hdrvalue_tobuf("subject", b, "utf-8",
					 NULL, NULL);

	if (!bs)
		bs=strdup(b);

	if (as)
	{
		char *p=rfc822_coresubj(as, &aisre);

		free(as);
		as=p;
	}

	if (bs)
	{
		char *p=rfc822_coresubj(bs, &bisre);

		free(bs);
		bs=p;
	}

	if (!as || !bs)
	{
		if (as)	free(as);
		if (bs)	free(bs);
		enomem();
	}

	n=strcasecmp(as, bs);
	free(as);
	free(bs);

	if (aisre)
		aisre=1;	/* Just to be sure */

	if (bisre)
		bisre=1;	/* Just to be sure */

	if (n == 0)	n=aisre - bisre;
	return (n);
}

/*
** Messages supposed to be arranged in the reverse chronological order of
** arrival.
**
** Instead of stat()ing every file in the directory, we depend on the
** naming convention that are specified for the Maildir.  Therefore, we rely
** on Maildir writers observing the required naming conventions.
*/

static int messagecmp(const MSGINFO **pa, const MSGINFO **pb)
{
int	gt=1, lt=-1;
int	n;
const MSGINFO *a= *pa;
const MSGINFO *b= *pb;

	if (pref_flagisoldest1st)
	{
		gt= -1;
		lt= 1;
	}

	switch (pref_flagsortorder)	{
	case 'F':
		n=strcasecmp(a->from_s, b->from_s);
		if (n)	return (n);
		break;
	case 'S':
		n=subjectcmp(a->subject_s, b->subject_s);
		if (n)	return (n);
		break;
	}
	if (a->date_n < b->date_n)	return (gt);
	if (a->date_n > b->date_n)	return (lt);

	if (a->mi_ino < b->mi_ino)	return (gt);
	if (a->mi_ino > b->mi_ino)	return (lt);
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

static int docount(const char *fn, unsigned *new_cnt, unsigned *other_cnt)
{
const char *filename=strrchr(fn, '/');
char	c;

	if (filename)	++filename;
	else		filename=fn;

	if (*filename == '.')	return (0);	/* We don't want this one */

	c=maildirfile_type(filename);

	if (c == MSGTYPE_NEW)
		++ *new_cnt;
	else
		++ *other_cnt;
	return (1);
}

MSGINFO **maildir_read(const char *dirname, unsigned nfiles,
		       size_t *starting_pos,
		       int *morebefore, int *moreafter)
{
MSGINFO	**msginfo;
size_t	i;

	if ((msginfo=malloc(sizeof(MSGINFO *)*nfiles)) == 0)
		enomem();
	for (i=0; i<nfiles; i++)
		msginfo[i]=0;

	if (opencache(dirname, "W"))	return (msginfo);

	if (nfiles > all_cnt)	nfiles=all_cnt;
	if (*starting_pos + nfiles > all_cnt)
		*starting_pos=all_cnt-nfiles;

	*morebefore = *starting_pos > 0;

	for (i=0; i<nfiles; i++)
	{
		if (*starting_pos + i >= all_cnt)	break;
		if ((msginfo[i]= get_msginfo_alloc(*starting_pos + i)) == 0)
			break;

		msginfo[i]->msgnum=*starting_pos+i;
	}
	*moreafter= *starting_pos + i < all_cnt;
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

static char *load_str(FILE *fp)
{
	size_t l;
	size_t i;
	char *str;

	load_int(l, fp);

	if (feof(fp))
		l=0;

	str=malloc(l+1);

	if (!str)
		enomem();

	for (i=0; i<l; i++)
	{
		char c=getc(fp);

		str[i]=c;
	}

	str[i]=0;
	return str;
}

static void save_msginfo(const MSGINFO *p, MATCHEDSTR *context, FILE *fp)
{
	size_t context_cnt;
	MATCHEDSTR *c;

	save_int(p->msgnum, fp);
	save_str(p->filename, fp);
	save_str(p->date_s, fp);
	save_str(p->from_s, fp);
	save_str(p->subject_s, fp);
	save_str(p->size_s, fp);
	save_int(p->date_n, fp);
	save_int(p->size_n, fp);
	save_int(p->mi_mtime, fp);
	save_int(p->mi_ino, fp);

	for (context_cnt=0, c=context; c && c->match; ++c, ++context_cnt)
		;

	save_int(context_cnt, fp);

	for (c=context; c && c->match; ++c)
	{
		save_str(c->prefix, fp);
		save_str(c->match, fp);
		save_str(c->suffix, fp);
	}
}

static void load_msginfo(MSGINFO **retinfo,
			 MATCHEDSTR **retmatches,
			 FILE *fp)
{
	MSGINFO *p=malloc(sizeof(MSGINFO));
	size_t context_cnt;
	MATCHEDSTR *c;

	if (p == 0)
		enomem();

	load_int(p->msgnum, fp);
	p->filename=load_str(fp);
	p->date_s=load_str(fp);
	p->from_s=load_str(fp);
	p->subject_s=load_str(fp);
	p->size_s=load_str(fp);
	load_int(p->date_n, fp);
	load_int(p->size_n, fp);
	load_int(p->mi_mtime, fp);
	load_int(p->mi_ino, fp);

	*retinfo=p;

	load_int(context_cnt, fp);

	*retmatches=NULL;

	if (context_cnt)
	{
		*retmatches=c=malloc(sizeof(MATCHEDSTR)*(context_cnt+1));

		for (; context_cnt; --context_cnt)
		{
			char *prefix=load_str(fp);
			char *match=load_str(fp);
			char *suffix=load_str(fp);

			if (c)
			{
				c->prefix=prefix;
				c->match=match;
				c->suffix=suffix;
				++c;
			}
			else
			{
				free(prefix);
				free(match);
				free(suffix);
			}
		}

		if (c)
		{
			c->prefix=NULL;
			c->match=NULL;
			c->suffix=NULL;
		}
	}
}

static void execute_maildir_search(const char *dirname,
				   size_t pos,
				   const char *searchtxt,
				   const char *charset,
				   unsigned nfiles,
				   MSGINFO ***retval,
				   MATCHEDSTR ***retcontext,
				   unsigned long *last_message_searched);

#define SEARCHFORMATVER 1

void maildir_search(const char *dirname,
		    size_t pos,
		    const char *searchtxt,
		    const char *charset,
		    unsigned nfiles)
{
	struct maildir_tmpcreate_info createInfo;

	MSGINFO **p;
	MATCHEDSTR **pcontext;
	char *filename;
	FILE *fp;
	unsigned i;

	unsigned long last_message_searched=0;

	execute_maildir_search(dirname, pos, searchtxt, charset,
			       nfiles, &p, &pcontext, &last_message_searched);

	maildir_purgesearch();

	maildir_tmpcreate_init(&createInfo);

	createInfo.uniq=":search:";
	createInfo.doordie=1;

	if ((fp=maildir_tmpcreate_fp(&createInfo)) == NULL)
	{
		if (p)
			maildir_free(p, nfiles);
		error("Can't create new file.");
	}

	filename=createInfo.tmpname;
	createInfo.tmpname=NULL;
	maildir_tmpcreate_free(&createInfo);

	chmod(filename, 0600);

	{
		char ver=SEARCHFORMATVER;

		save_int(ver, fp);
		save_int(last_message_searched, fp);
	}

	for (i=0; i<nfiles; i++)
		if (p[i])
			save_msginfo(p[i], pcontext[i], fp);
	fflush(fp);
	if (ferror(fp) || fclose(fp))
		error("Cannot create temp file");

	cgi_put(SEARCHRESFILENAME, strrchr(filename, '/')+1);

	if (p)
		maildir_free(p, nfiles);
	if (pcontext)
		matches_free(pcontext, nfiles);
}

void maildir_loadsearch(unsigned nfiles,
			MSGINFO ***retmsginfo,
			MATCHEDSTR ***retmatches,
			unsigned long *last_message_searched)
{
	MSGINFO	**msginfo;
	MATCHEDSTR **matches;

	unsigned i;
	const char *filename;
	char *buf;
	FILE *fp;
	char ver;

	if ((msginfo=malloc(sizeof(MSGINFO *)*nfiles)) == 0)
		enomem();

	if ((matches=malloc(sizeof(MATCHEDSTR *)*nfiles)) == 0)
	{
		free(msginfo);
		enomem();
	}

	for (i=0; i<nfiles; i++)
	{
		msginfo[i]=0;
		matches[i]=0;
	}

	filename=cgi(SEARCHRESFILENAME);
	CHECKFILENAME(filename);

	buf=malloc(strlen(filename)+5);

	if (!buf)
	{
		free(msginfo);
		enomem();
	}

	strcat(strcpy(buf, "tmp/"), filename);

	fp=fopen(buf, "r");
	free(buf);

	if (fp)
	{
		load_int(ver, fp);

		if (ver == SEARCHFORMATVER)
		{
			load_int(*last_message_searched, fp);
			for (i=0; i<nfiles; ++i)
			{
				int ch=getc(fp);

				if (ch == EOF)
					break;
				ungetc(ch, fp);

				load_msginfo(&msginfo[i], &matches[i], fp);
			}
		}
	}
	fclose(fp);

	for (i=0; i<nfiles; ++i)
	{
		MSGINFO *p;

		if (msginfo[i] == 0)
			continue;

		p=get_msginfo(msginfo[i]->msgnum);

		if (p && p->mi_ino == msginfo[i]->mi_ino) /* Safety first */
		{
			char *f=strdup(p->filename);
			if (f)
			{
				if (msginfo[i]->filename)
					free(msginfo[i]->filename);
				msginfo[i]->filename=f;
			}
		}
	}

	*retmsginfo=msginfo;
	*retmatches=matches;
}

static const char spaces[]=" \t\r\n";

#define SEARCH_MATCH_CONTEXT_LEN	20

/* After matching, save surrounding context here */

struct searchresults_match_context {
	struct searchresults_match_context *next;

	char32_t *match_context_before;
	char32_t *match_context;

	char32_t *match_context_after;
	size_t match_context_after_len;
	size_t match_context_after_max_len;
};

struct searchresults {

	char prevchar;
	size_t foundcnt;

	int finished;

	char utf8buf[512];
	size_t utf8buf_cnt;

	char32_t *context_buf;
	size_t context_buf_len;

	size_t context_buf_head;
	size_t context_buf_tail;

	struct searchresults_match_context *matched_context_head;
	struct searchresults_match_context **matched_context_tail;

	struct maildir_searchengine *se;

};

static MATCHEDSTR *creatematches(struct searchresults *sr);

static int searchresults_init(struct searchresults *sr,
			      struct maildir_searchengine *se);

static void searchresults_destroy(struct searchresults *sr);

static int do_maildir_search(const char *filename,
			     struct searchresults *sr);


static void execute_maildir_search(const char *dirname,
				   size_t pos,
				   const char *searchtxt,
				   const char *charset,
				   unsigned nfiles,
				   MSGINFO ***retval,
				   MATCHEDSTR ***retcontext,
				   unsigned long *last_message_searched)
{
	MSGINFO	**msginfo;
	MATCHEDSTR **matches;
	unsigned long i;
	unsigned j;
	char *utf8str;
	char *p, *q;

	if ((*retval=msginfo=malloc(sizeof(MSGINFO *)*nfiles)) == 0)
		enomem();

	if ((*retcontext=matches=malloc(sizeof(MATCHEDSTR *)*nfiles)) == 0)
	{
		free(msginfo);
		enomem();
	}

	for (i=0; i<nfiles; i++)
	{
		msginfo[i]=0;
		matches[i]=0;
	}

	if (opencache(dirname, "W"))	return;

	if (pos >= all_cnt) return;

	utf8str=unicode_convert_toutf8(searchtxt, charset, NULL);

	if (!utf8str)
		return;

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

		j=0;

		for (i=0, j=0; rc == 0 && pos+i<all_cnt && j<nfiles; ++i)
		{
			MSGINFO *info=get_msginfo_alloc(pos+i);
			char *filename;

			if (!info)
				continue;

			*last_message_searched= pos+i;

			filename=maildir_find(dirname, info->filename);

			maildir_search_reset(&se);

			if (filename)
			{
				struct searchresults sr;

				info->msgnum=pos+i;

				if (searchresults_init(&sr, &se) == 0)
				{
					if (do_maildir_search(filename, &sr))
					{
						msginfo[j]=info;
						matches[j]=creatematches(&sr);
						info=NULL;
						++j;
					}

					searchresults_destroy(&sr);
				}
				free(filename);
			}

			if (info)
				maildir_nfreeinfo(info);
		}
		maildir_search_destroy(&se);
	}

	free(utf8str);
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
	sr->context_buf=malloc(sr->context_buf_len * sizeof(char32_t));

	if (sr->context_buf == NULL)
		return -1;
	sr->context_buf_head=0;
	sr->context_buf_tail=0;

	sr->matched_context_head=NULL;
	sr->matched_context_tail=&sr->matched_context_head;

	return 0;
}

static void searchresults_destroy(struct searchresults *sr)
{
	while (sr->matched_context_head)
	{
		struct searchresults_match_context *c=sr->matched_context_head;

		sr->matched_context_head=c->next;

		free(c->match_context_before);
		free(c->match_context_after);
		free(c->match_context);
	}

	free(sr->context_buf);
}

/* Save context before, and the matched context */

static void search_found_save_context(struct searchresults *sr)
{
	struct searchresults_match_context *c=
		malloc(sizeof(struct searchresults_match_context));
	size_t n, i, j;;

	if (c == NULL)
		return;

	if ((c->match_context_before=malloc((SEARCH_MATCH_CONTEXT_LEN+1)
					    * sizeof(char32_t))) == NULL)
	{
		free(c);
		return;
	}

	if ((c->match_context=malloc((maildir_search_len(sr->se)+1)
				     * sizeof(char32_t))) == NULL)
	{
		free(c->match_context_before);
		free(c);
		return;
	}

	if ((c->match_context_after=malloc((SEARCH_MATCH_CONTEXT_LEN+1)
					   * sizeof(char32_t))) == NULL)
	{
		free(c->match_context);
		free(c->match_context_before);
		free(c);
		return;
	}

	c->next=NULL;

	*sr->matched_context_tail=c;
	sr->matched_context_tail= &c->next;

	/*
	** Subtract from the head of the context buffer to arrive at the
	** start of the matched context
	*/

	n=sr->context_buf_head;

	for (i=maildir_search_len(sr->se); i > 0; --i)
	{
		if (n == sr->context_buf_tail)
			break; /* Shouldn't happen */

		n=sr_context_buf_index_dec(sr, n);
	}

	/* From here to the head is the matched context */

	j=0;
	for (i=n; i != sr->context_buf_head; )
	{
		c->match_context[j++]=sr->context_buf[i];
		i=sr_context_buf_index_inc(sr, i);
	}
	c->match_context[j]=0;

	/* Now, look before the start of the matched context */

	for (i=n, j=0; j<SEARCH_MATCH_CONTEXT_LEN; ++j)
	{
		if (i == sr->context_buf_tail)
			break; /* Possible */

		i=sr_context_buf_index_dec(sr, i);
	}

	j=0;
	while (i != n)
	{
		c->match_context_before[j++]=sr->context_buf[i];
		i=sr_context_buf_index_inc(sr, i);
	}
	c->match_context_before[j]=0;

	c->match_context_after_len=0;
	c->match_context_after_max_len=SEARCH_MATCH_CONTEXT_LEN;
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

	res->utf8buf[res->utf8buf_cnt]=0;
	res->utf8buf_cnt=0;

	h=unicode_convert_tou_init("utf-8",
				     &uc,
				     &n,
				     1);

	if (h)
	{
		unicode_convert(h, res->utf8buf, strlen(res->utf8buf));
		if (unicode_convert_deinit(h, NULL) == 0)
			;
		else
			uc=NULL;
	}

	for (n=0; uc && uc[n]; n++)
	{
		struct searchresults_match_context *c;

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

		for (c=res->matched_context_head; c; c=c->next)
		{
			if (c->match_context_after_len <
			    c->match_context_after_max_len)
				c->match_context_after[c->match_context_after_len++]=origch;
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

static void savematch(struct searchresults_match_context *smc,
		      MATCHEDSTR **curptr);

static MATCHEDSTR *creatematches(struct searchresults *sr)
{
	size_t n=0;
	struct searchresults_match_context *smc;
	MATCHEDSTR *retval, *retptr;

	/* Count, allocate the array */
	for (smc=sr->matched_context_head; smc; smc=smc->next)
		++n;

	if ((retval=malloc(sizeof(MATCHEDSTR)*(n+1))) == NULL)
		return NULL;

	retptr=retval;

	for (smc=sr->matched_context_head; smc; smc=smc->next)
	{
		savematch(smc, &retptr);
	}

	/* Last one */

	retptr->prefix=NULL;
	retptr->match=NULL;
	retptr->suffix=NULL;
	return retval;
}

static char *match_conv(const char32_t *uc)
{
	char *cbuf;
	size_t csize;
	unicode_convert_handle_t h;
	size_t i;

	if ((h=unicode_convert_fromu_init("utf-8", &cbuf, &csize, 1)) == NULL)
		return NULL;

	for (i=0; uc[i]; ++i)
		;

	unicode_convert_uc(h, uc, i);

	if (unicode_convert_deinit(h, NULL))
		return NULL;
	return cbuf;
}

static void savematch(struct searchresults_match_context *smc,
		      MATCHEDSTR **curptr)
{
	if ( ((*curptr)->prefix=match_conv(smc->match_context_before))
	     == NULL)
		return;

	if ( ((*curptr)->match=match_conv(smc->match_context)) == NULL)
	{
		free( (*curptr)->prefix );
		return;
	}

	smc->match_context_after[smc->match_context_after_len]=0;

	if ( ((*curptr)->suffix=match_conv(smc->match_context_after))
	     == NULL)
	{
		free( (*curptr)->match );
		free( (*curptr)->prefix );
		return;
	}
	++ (*curptr);
}

static void dodirscan(const char *, const char *, unsigned *, unsigned *);

void maildir_count(const char *folder,
		   unsigned *new_ptr,
		   unsigned *other_ptr)
{
	struct maildir_info minfo;
	char *dir;

	*new_ptr=0;
	*other_ptr=0;

	if (maildir_info_imap_find(&minfo, folder,
				   login_returnaddr()) < 0)
		return;

	if (minfo.mailbox_type == MAILBOXTYPE_OLDSHARED)
	{
		dir=maildir_shareddir(".", strchr(folder, '.')+1);

		if (!dir)
		{
			maildir_info_destroy(&minfo);
			return;
		}

		maildir_shared_sync(dir);
	}
	else
	{
		if (minfo.homedir == NULL || minfo.maildir == NULL)
		{
			maildir_info_destroy(&minfo);
			return;
		}

		dir=maildir_name2dir(minfo.homedir, minfo.maildir);

		if (!dir)
		{
			maildir_info_destroy(&minfo);
			return;
		}
	}

	maildir_info_destroy(&minfo);
	maildir_checknew(folder, dir);
	dodirscan(folder, dir, new_ptr, other_ptr);
	free(dir);
}

unsigned maildir_countof(const char *folder)
{
	maildir_getfoldermsgs(folder);
	return (all_cnt);
}

static void dodirscan(const char *folder,
		      const char *dir, unsigned *new_cnt,
		      unsigned *other_cnt)
{
	DIR *dirp;
	struct dirent *de;
	char	*curname;
	struct stat cur_stat;
	struct stat c_stat;
	const	char *p;
	char	cntbuf[MAXLONGSIZE*2+4];
	char	*cntfilename;
	FILE	*fp;
	struct maildir_tmpcreate_info createInfo;

	*new_cnt=0;
	*other_cnt=0;
	curname=alloc_filename(dir, "cur", "");

	if (stat(curname, &cur_stat))
	{
		free(curname);
		return;
	}

	cntfilename=foldercountfilename(folder);
	fp=fopen(cntfilename, "r");

	if (fp)
	{
		char buf[BUFSIZ];

		if (fstat(fileno(fp), &c_stat) == 0 &&
		    c_stat.st_mtime > cur_stat.st_mtime &&
		    fgets(buf, sizeof(buf), fp))
		{
			unsigned long n;
			unsigned long o;

			if ((p=parse_ul(buf, &n)) && (p=parse_ul(p, &o)))
			{
				*new_cnt=n;
				*other_cnt=o;
				free(curname);
				fclose(fp);
				free(cntfilename);
				return;	/* Valid cache of count */
			}
		}
		fclose(fp);
	}

	dirp=opendir(curname);
	while (dirp && (de=readdir(dirp)) != NULL)
		docount(de->d_name, new_cnt, other_cnt);
	if (dirp)	closedir(dirp);
	sprintf(cntbuf, "%u %u\n", *new_cnt, *other_cnt);

	maildir_tmpcreate_init(&createInfo);
	createInfo.maildir=".";
	createInfo.uniq="count";
	createInfo.doordie=1;

	fp=maildir_tmpcreate_fp(&createInfo);
	if (!fp)
	{
		free(curname);
		free(cntfilename);
		maildir_tmpcreate_free(&createInfo);
		return;
	}

	fprintf(fp, "%s", cntbuf);
	fclose(fp);

	if (rename(createInfo.tmpname, cntfilename) < 0 ||
	    stat(cntfilename, &c_stat) < 0)
	{
		unlink(cntfilename);
		free(curname);
		free(cntfilename);
		maildir_tmpcreate_free(&createInfo);
		return;
	}
	maildir_tmpcreate_free(&createInfo);


	if (c_stat.st_mtime != cur_stat.st_mtime)
	{
		struct stat stat2;

		if (stat(curname, &stat2)
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
		change_timestamp(cntfilename, c_stat.st_mtime-1);
				/* ... So rebuild it next time */
	}
	free(curname);
	free(cntfilename);
}

void maildir_free(MSGINFO **files, unsigned nfiles)
{
unsigned i;

	for (i=0; i<nfiles; i++)
	{
		if ( files[i] )
			maildir_nfreeinfo( files[i] );
	}
	free(files);
}

static char *buf=0;
size_t bufsize=0, buflen=0;

static void addbuf(int c)
{
	if (buflen == bufsize)
	{
	char	*newbuf= buf ? realloc(buf, bufsize+512):malloc(bufsize+512);

		if (!newbuf)	enomem();
		buf=newbuf;
		bufsize += 512;
	}
	buf[buflen++]=c;
}

char *maildir_readline(FILE *fp)
{
int	c;

	buflen=0;
	while ((c=getc(fp)) != '\n' && c >= 0)
		if (buflen < 8192)
			addbuf(c);
	if (c < 0 && buflen == 0)	return (NULL);
	addbuf(0);
	return (buf);
}

char *maildir_readheader_nolc(FILE *fp, char **value)
{
	int c;

	buflen=0;

	while ((c=getc(fp)) != EOF)
	{
		if (c != '\n')
		{
			addbuf(c);
			continue;
		}
		c=getc(fp);
		if (c >= 0) ungetc(c, fp);
		if (c < 0 || c == '\n' || !isspace(c)) break;
		addbuf('\n');
	}
	addbuf(0);

	if (c == EOF && buf[0] == 0) return (0);

	for ( *value=buf; **value; (*value)++)
	{
		if (**value == ':')
		{
			**value='\0';
			++*value;
			break;
		}
	}
	while (**value && isspace((int)(unsigned char)**value))	++*value;
	return(buf);
}

char	*maildir_readheader_mimepart(FILE *fp, char **value, int preserve_nl,
		off_t *mimepos, const off_t *endpos)
{
	int	c;
	int	eatspaces=0;

	buflen=0;

	if (mimepos && *mimepos >= *endpos)	return (0);

	while (mimepos == 0 || *mimepos < *endpos)
	{
		if ((c=getc(fp)) != '\n' && c >= 0)
		{
			if (c != ' ' && c != '\t' && c != '\r')
				eatspaces=0;

			if (!eatspaces)
				addbuf(c);
			if (mimepos)	++ *mimepos;
			continue;
		}
		if ( c == '\n' && mimepos)	++ *mimepos;
		if (buflen == 0)	return (0);
		if (c < 0)	break;
		c=getc(fp);
		if (c >= 0)	ungetc(c, fp);
		if (c < 0 || c == '\n' || !isspace(c))	break;
		addbuf(preserve_nl ? '\n':' ');
		if (!preserve_nl)
			eatspaces=1;
	}
	addbuf(0);

	for ( *value=buf; **value; (*value)++)
	{
		if (**value == ':')
		{
			**value='\0';
			++*value;
			break;
		}
		**value=tolower(**value);
	}
	while (**value && isspace((int)(unsigned char)**value))	++*value;
	return(buf);
}

char	*maildir_readheader(FILE *fp, char **value, int preserve_nl)
{
	return (maildir_readheader_mimepart(fp, value, preserve_nl, 0, 0));
}

/*****************************************************************************

The MSGINFO structure contains the summary of the headers found in all
messages in the cur directory.

Instead of opening each message every time we need to serve the directory
contents, the messages are scanned once, and a cache file is built
containing the contents.

*****************************************************************************/

/* Deallocate an individual MSGINFO structure */

void maildir_nfreeinfo(MSGINFO *mi)
{
	if (mi->filename)	free(mi->filename);
	if (mi->date_s)	free(mi->date_s);
	if (mi->from_s)	free(mi->from_s);
	if (mi->subject_s)	free(mi->subject_s);
	if (mi->size_s)	free(mi->size_s);
	free(mi);
}

/* Initialize a MSGINFO structure by reading the message headers */

MSGINFO *maildir_ngetinfo(const char *filename)
{
FILE	*fp;
MSGINFO	*mi;
struct stat stat_buf;
char	*hdr, *val;
const char *p;
int	is_sent_header=0;
char	*fromheader=0;
int	fd;

	/* Hack - see if we're reading a message from the Sent or Drafts
		folder */

	p=strrchr(filename, '/');
	if ((p && p - filename >=
		sizeof(SENT) + 5 && strncmp(p - (sizeof(SENT) + 5),
			"/." SENT "/", sizeof(SENT)+2) == 0)
		|| strncmp(filename, "." SENT "/", sizeof(SENT)+1) == 0
		|| strncmp(filename, "./." SENT ".", sizeof(SENT)+3) == 0
		|| strncmp(filename, "." SENT ".", sizeof(SENT)+1) == 0)
		is_sent_header=1;
	if ((p && p - filename >=
		sizeof(DRAFTS) + 5 && strncmp(p-(sizeof(DRAFTS) + 5),
			"/." DRAFTS "/", sizeof(DRAFTS)+2) == 0)
		|| strncmp(filename, "." DRAFTS "/", sizeof(DRAFTS)+1) == 0)
		is_sent_header=1;

	if ((mi=(MSGINFO *)malloc(sizeof(MSGINFO))) == 0)
		enomem();

	memset(mi, '\0', sizeof(*mi));

	fp=0;
	fd=maildir_semisafeopen(filename, O_RDONLY, 0);
	if (fd >= 0)
		if ((fp=fdopen(fd, "r")) == 0)
			close(fd);

	if (fp == NULL)
	{
		free(mi);
		return (NULL);
	}

	/* mi->filename shall be the base filename, normalized as :2, */

	if ((p=strrchr(filename, '/')) != NULL)
		p++;
	else	p=filename;

	if (!(mi->filename=strdup(p)))
		enomem();

	if (fstat(fileno(fp), &stat_buf) == 0)
	{
		mi->mi_mtime=stat_buf.st_mtime;
		mi->mi_ino=stat_buf.st_ino;
		mi->size_n=stat_buf.st_size;
		mi->size_s=strdup( showsize(stat_buf.st_size));
		mi->date_n=mi->mi_mtime;	/* Default if no Date: */
		if (!mi->size_s)	enomem();
	}
	else
	{
		free(mi->filename);
		fclose(fp);
		free(mi);
		return (0);
	}


	while ((hdr=maildir_readheader(fp, &val, 0)) != 0)
	{
		if (strcmp(hdr, "subject") == 0)
		{
			char *uibuf=rfc822_display_hdrvalue_tobuf("subject",
								  val,
								  "utf-8",
								  NULL, NULL);

			if (mi->subject_s)	free(mi->subject_s);

			mi->subject_s=uibuf;
			if (!mi->subject_s)	enomem();
		}

		if (strcmp(hdr, "date") == 0 && mi->date_s == 0)
		{
			time_t t;

			if (rfc822_parsedate_chk(val, &t) == 0)
			{
				mi->date_n=t;
				mi->date_s=strdup(displaydate(mi->date_n));
				if (!mi->date_s)	enomem();
			}
		}

		if ((is_sent_header ?
			strcmp(hdr, "to") == 0 || strcmp(hdr, "cc") == 0:
			strcmp(hdr, "from") == 0) && fromheader == 0)
		{
			struct rfc822t *from_addr;
			struct rfc822a *from_addra;
			char	*p;
			int dotflag=0;
			int cnt;

			from_addr=rfc822t_alloc_new(val, NULL, NULL);
			if (!from_addr)	enomem();
			from_addra=rfc822a_alloc(from_addr);
			if (!from_addra)	enomem();

			p=NULL;

			for (cnt=0; cnt<from_addra->naddrs; ++cnt)
			{
				if (from_addra->addrs[cnt].tokens == NULL)
					continue;

				if (p)
				{
					dotflag=1;
					break;
				}

				p=rfc822_display_name_tobuf(from_addra, cnt,
							    "utf-8");
			}

			if (p)
			{
				if (fromheader)	free(fromheader);
				if ((fromheader=malloc(strlen(p)+7)) == 0)
					enomem();
				strcpy(fromheader, p);
				if (dotflag)
					strcat(fromheader, "...");

				free(p);
			}

			rfc822a_free(from_addra);
			rfc822t_free(from_addr);
		}

		if (mi->date_s && fromheader && mi->subject_s)
			break;
	}
	fclose(fp);

	mi->from_s=fromheader;
	if (!mi->date_s)
		mi->date_s=strdup(displaydate(mi->date_n));
	if (!mi->date_s)	enomem();
	if (!mi->from_s && !(mi->from_s=strdup("")))	enomem();
	if (!mi->subject_s && !(mi->subject_s=strdup("")))	enomem();
	return (mi);
}

/************************************************************************/

/* Save cache file */

static unsigned long save_cnt, savenew_cnt;
static time_t	save_time;

static char	*save_dbname;
static char	*save_tmpdbname;
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
	  int f = -1;
	  char *tmpfname = alloc_filename(maildir,
					   "", MAILDIRCURCACHE ".nfshack");
	  if (tmpfname) {
	    f = open(tmpfname, O_CREAT|O_WRONLY, 0600);
	    free(tmpfname);
	  }
	  if (f != -1) {
	    struct stat s;
	    if (write(f, ".", 1) != 1)
		    ; /* ignore */
	    fsync(f);
	    if (fstat(f, &s) == 0)
	      save_time = s.st_mtime;
	    close(f);
	    unlink(tmpfname);
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
	createInfo.tmpname=NULL;
	maildir_tmpcreate_free(&createInfo);

	dbobj_init(&tmpdb);

	if (dbobj_open(&tmpdb, save_tmpdbname, "N")) {
		fprintf(stderr, "ERR: Can't create cache file |%s|: %s\n", save_tmpdbname, strerror(errno));
		error("Can't create cache file.");
	}

	save_cnt=0;
	savenew_cnt=0;
}

static void maildir_saveinfo(MSGINFO *m)
{
char	*rec, *p;
char	recnamebuf[MAXLONGSIZE+40];

	rec=malloc(strlen(m->filename)+strlen(m->from_s)+
		strlen(m->subject_s)+strlen(m->size_s)+MAXLONGSIZE*4+
		sizeof("FILENAME=\nFROM=\nSUBJECT=\nSIZES=\nDATE=\n"
			"SIZEN=\nTIME=\nINODE=\n")+100);
	if (!rec)	enomem();

	sprintf(rec, "FILENAME=%s\nFROM=%s\nSUBJECT=%s\nSIZES=%s\n"
		"DATE=%lu\n"
		"SIZEN=%lu\n"
		"TIME=%lu\n"
		"INODE=%lu\n",
		m->filename,
		m->from_s,
		m->subject_s,
		m->size_s,
		(unsigned long)m->date_n,
		(unsigned long)m->size_n,
		(unsigned long)m->mi_mtime,
		(unsigned long)m->mi_ino);
	sprintf(recnamebuf, "REC%lu", (unsigned long)save_cnt);
	if (dbobj_store(&tmpdb, recnamebuf, strlen(recnamebuf),
		rec, strlen(rec), "R"))
		enomem();
	free(rec);

	/* Reverse lookup */
	rec=malloc(strlen(m->filename)+10);
	if (!rec)
		enomem();
	strcat(strcpy(rec, "FILE"), m->filename);
	if ((p=strchr(rec, ':')) != 0)
		*p=0;
	sprintf(recnamebuf, "%lu", (unsigned long)save_cnt);
	if (dbobj_store(&tmpdb, rec, strlen(rec),
			recnamebuf, strlen(recnamebuf), "R"))
		enomem();

	save_cnt++;
	if (maildirfile_type(m->filename) == MSGTYPE_NEW)
		savenew_cnt++;
}

static void maildir_save_end(const char *maildir)
{
char	*curname;
char	*rec;

	curname=alloc_filename(maildir, "", "cur");

	rec=malloc(MAXLONGSIZE*4+sizeof(
			"SAVETIME=\n"
			"COUNT=\n"
			"NEWCOUNT=\n"
			"SORT=\n")+100);

	if (!rec)	enomem();
	sprintf(rec,
		"SAVETIME=%lu\nCOUNT=%lu\nNEWCOUNT=%lu\nSORT=%d%c\n",
		(unsigned long)save_time,
		(unsigned long)save_cnt,
		(unsigned long)savenew_cnt,
			pref_flagisoldest1st,
			pref_flagsortorder);
	if (dbobj_store(&tmpdb, "HEADER", 6, rec, strlen(rec), "R"))
		enomem();
	dbobj_close(&tmpdb);
	free(rec);

	rename(save_tmpdbname, save_dbname);
	unlink(save_tmpdbname);

	free(curname);
	free(save_dbname);
	free(save_tmpdbname);
}

void maildir_savefoldermsgs(const char *folder)
{
}

/************************************************************************/

struct	MSGINFO_LIST {
	struct MSGINFO_LIST	*next;
	MSGINFO *minfo;
	} ;

static void createmdcache(const char *folder, const char *maildir)
{
char	*curname;
DIR *dirp;
struct dirent *de;
struct MSGINFO_LIST *milist, *newmi;
MSGINFO	*mi;
unsigned long cnt=0;

	curname=alloc_filename(maildir, "", "cur");

	time(&current_time);

	maildir_save_start(folder, maildir, current_time);

	milist=0;
	dirp=opendir(curname);
	while (dirp && (de=readdir(dirp)) != NULL)
	{
	char	*filename;

		if (de->d_name[0] == '.')
			continue;

		filename=alloc_filename(curname, "", de->d_name);
		mi=maildir_ngetinfo(filename);
		free(filename);
		if (!mi)	continue;

		if (!(newmi=malloc(sizeof(struct MSGINFO_LIST)))) enomem();
		newmi->next= milist;
		milist=newmi;
		newmi->minfo=mi;
		++cnt;
	}
	if (dirp)	closedir(dirp);
	free(curname);

	if (milist)
	{
	MSGINFO **miarray=malloc(sizeof(MSGINFO *) * cnt);
	unsigned long i;

		if (!miarray)	enomem();
		i=0;
		while (milist)
		{
			miarray[i++]=milist->minfo;
			newmi=milist;
			milist=newmi->next;
			free(newmi);
		}

		qsort(miarray, cnt, sizeof(*miarray),
			( int (*)(const void *, const void *)) messagecmp);
		for (i=0; i<cnt; i++)
		{
			maildir_saveinfo(miarray[i]);
			maildir_nfreeinfo(miarray[i]);
		}
		free(miarray);
	}

	maildir_save_end(maildir);
}

static int chkcache(const char *folder)
{
	if (opencache(folder, "W"))	return (-1);

	if (isoldestfirst != pref_flagisoldest1st)	return (-1);
	if (sortorder != pref_flagsortorder)		return (-1);
	return (0);
}

static void	maildir_getfoldermsgs(const char *folder)
{
char	*dir=xlate_shmdir(folder);

	if (!dir)	return;

	while ( chkcache(folder) )
	{
		closedb();
		createmdcache(folder, dir);
	}
	free(dir);
}

void	maildir_remcache(const char *folder)
{
	char	*dir=xlate_shmdir(folder);
	char	*cachename=foldercachename(folder);

	unlink(cachename);
	if (folderdatname && strcmp(folderdatname, cachename) == 0)
		closedb();
	free(cachename);
	free(dir);
}

void	maildir_reload(const char *folder)
{
char	*dir=xlate_shmdir(folder);
char	*curname;
struct	stat	stat_buf;

	if (!dir)	return;

	curname=alloc_filename(dir, "cur", ".");
	time(&current_time);

	/* Remove old cache file when: */

	if (opencache(folder, "W") == 0)
	{
		if ( stat(curname, &stat_buf) != 0 ||
			stat_buf.st_mtime >= cachemtime)
		{
			closedb();
			createmdcache(folder, dir);
		}
	}
	free(dir);
	maildir_getfoldermsgs(folder);
	free(curname);
}

/*
	maildir_listfolders(char ***) - read all the folders in the mailbox.
	maildir_freefolders(char ***) - deallocate memory
*/

static void addfolder(const char *name, char ***buf, size_t *size, size_t *cnt)
{
	if (*cnt >= *size)
	{
	char	**newbuf= *buf ? realloc(*buf, (*size + 10) * sizeof(char *))
			: malloc( (*size+10) * sizeof(char *));

		if (!newbuf)	enomem();
		*buf=newbuf;
		*size += 10;
	}

	(*buf)[*cnt]=0;
	if ( name && ((*buf)[*cnt]=strdup(name)) == 0)	enomem();
	++*cnt;
}

/*
**  Return a sorted list of folders.
**
*/

struct add_shared_info {
	char ***p;
	size_t *s;
	size_t *c;
	const char *inbox_pfix;
	} ;

static void list_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	char *o;

	while (*n)
	{
		if (*n == '.')
			break;
		++n;
	}

	o=malloc(strlen(i->inbox_pfix)+strlen(n)+1);
	if (!o)
		enomem();
	strcat(strcpy(o, i->inbox_pfix), n);

	addfolder(o, i->p, i->s, i->c);

	free(o);
}

static void list_shared_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	char *p=malloc(sizeof(SHARED ".") + strlen(n));

	if (!p)
		enomem();
	strcat(strcpy(p, SHARED "."), n);

	addfolder(p, i->p, i->s, i->c);
	free(p);
}

static void list_sharable_callback(const char *n, void *vp)
{
	struct add_shared_info *i=
		(struct add_shared_info *)vp;
	char *p=malloc(sizeof(SHARED ".") + strlen(n));
	size_t j;

	if (!p)
		enomem();
	strcat(strcpy(p, SHARED "."), n);

	for (j=0; j< *i->c; j++)
		if (strcmp( (*i->p)[j], p) == 0)
		{
			free(p);
			return;
		}

	addfolder(p, i->p, i->s, i->c);
	free(p);
}

static int shcomparefunc( char **a, char **b)
{
	char	*ca= *a, *cb= *b;

	return (strcasecmp(ca, cb));
}

void maildir_listfolders(const char *inbox_pfix,
			 const char *homedir,
			 char ***fp)
{
	size_t	fbsize=0;
	size_t	fbcnt=0;
	struct add_shared_info info;
	size_t	sh_cnt;

	*fp=0;

	info.p=fp;
	info.s= &fbsize;
	info.c= &fbcnt;
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

	sh_cnt=fbcnt;
	if (strcmp(homedir, ".") == 0)
		maildir_list_sharable(".", list_sharable_callback, &info);

	qsort( (*fp), sh_cnt, sizeof(**fp),
		(int (*)(const void *, const void *))shcomparefunc);

	qsort( (*fp)+sh_cnt, fbcnt-sh_cnt, sizeof(**fp),
		(int (*)(const void *, const void *))shcomparefunc);
	addfolder(NULL, fp, &fbsize, &fbcnt);
}

void maildir_freefolders(char ***fp)
{
size_t	cnt;

	for (cnt=0; (*fp)[cnt]; cnt++)
		free( (*fp)[cnt] );
	free(*fp);
}

int maildir_create(const char *foldername)
{
	char	*dir;
	int	rc= -1;

	dir=xlate_mdir(foldername);
	if (!dir)
		return 0;

	if (mkdir(dir, 0700) == 0)
	{
	char *tmp=alloc_filename(dir, "tmp", "");

		if (mkdir(tmp, 0700) == 0)
		{
		char *tmp2=alloc_filename(dir, "new", "");

			if (mkdir(tmp2, 0700) == 0)
			{
			char *tmp3=alloc_filename(dir, "cur", "");

				if (mkdir(tmp3, 0700) == 0)
				{
				char *tmp4=alloc_filename(dir, "maildirfolder",
					"");

					close(open(tmp4, O_RDWR|O_CREAT, 0600));
					rc=0;
					free(tmp4);
				}
				free(tmp3);
			}
			if (rc)	rmdir(tmp2);
			free (tmp2);
		}
		if (rc)	rmdir(tmp);
		free(tmp);
	}
	if (rc)	rmdir(dir);
	free(dir);
	return (rc);
}

int maildir_delete(const char *foldername, int deletecontent)
{
	char	*dir, *tmp, *new, *cur;
	int	rc=0;

	struct maildir_info minfo;

	if (maildir_info_imap_find(&minfo, foldername, login_returnaddr())<0)
		return -1;

	if (strcmp(minfo.maildir, INBOX) == 0 ||
	    strcmp(minfo.maildir, INBOX "." SENT) == 0 ||
	    strcmp(minfo.maildir, INBOX "." TRASH) == 0 ||
	    strcmp(minfo.maildir, INBOX "." DRAFTS) == 0 ||
	    (dir=maildir_name2dir(minfo.homedir, minfo.maildir)) == NULL)

	{
		maildir_info_destroy(&minfo);
		return (-1);
	}

	tmp=alloc_filename(dir, "tmp", "");
	cur=alloc_filename(dir, "cur", "");
	new=alloc_filename(dir, "new", "");

	if (!deletecontent)
	{
		if (rmdir(new) || rmdir(cur))
		{
			mkdir(new, 0700);
			mkdir(cur, 0700);
			rc= -1;
		}
	}

	if (rc == 0 && maildir_del(dir))
		rc= -1;

	if (rc == 0)
		maildir_acl_delete(minfo.homedir,
				   strchr(minfo.maildir, '.'));

	maildir_info_destroy(&minfo);
	free(tmp);
	free(new);
	free(cur);
	free(dir);
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

int	maildir_createmsg(const char *foldername, const char *seq,
		char **retname)
{
	char	*p;
	char	*dir=xlate_mdir(foldername);
	char	*filename;
	int	n;
	struct maildir_tmpcreate_info createInfo;

	/* Create a new file in the tmp directory. */

	maildir_tmpcreate_init(&createInfo);

	createInfo.maildir=dir;
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
	createInfo.newname=NULL;

	maildir_tmpcreate_free(&createInfo);

	p=strrchr(filename, '/');
	*retname=strdup(p+1);

	if (*retname == 0)
	{
		close(n);
		free(filename);
		enomem();
	}
	free(filename);

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

int maildir_recreatemsg(const char *folder, const char *name, char **baseptr)
{
char	*dir=xlate_mdir(folder);
char	*base;
char	*p;
int	n;

	base=maildir_basename(name);
	p=alloc_filename(dir, "tmp", base);

	free(dir);
	*baseptr=base;
	n=maildir_safeopen(p, O_CREAT|O_RDWR|O_TRUNC, 0644);
	if (n < 0)	free(base);
	free(p);
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
		if (c > cnt)	c=cnt;
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

int	maildir_closemsg(int n,	/* File descriptor */
	const char *folder,	/* Folder */
	const char *retname,	/* Filename in folder */
	int isok,	/* 0 - discard it (I changed my mind),
			   1 - keep it
			  -1 - keep it even if we'll exceed the quota
			*/
	unsigned long prevsize	/* Prev size of this msg, used in quota calc */
		)
{
	char	*dir=xlate_mdir(folder);
	char	*oldname=alloc_filename(dir, "tmp", retname);
	char	*newname;
	struct	stat	stat_buf;


	writeflush(n);	/* If there's still anything in the buffer */
	if (fstat(n, &stat_buf))
	{
		close(n);
		unlink(oldname);
		enomem();
	}

	newname=maildir_find(folder, retname);
		/* If we called recreatemsg before */

	if (!newname)
	{
		newname=alloc_filename(dir, "cur:2,S", retname);
		/* Hack of the century          ^^^^ */
		strcat(strcat(strcat(strcpy(newname, dir), "/cur/"),
			retname), ":2,S");
	}

	if (writeerr)
	{
		close(n);
		unlink(oldname);
		enomem();
	}

	close(n);

	if (isok)
	{
		if (prevsize < stat_buf.st_size)
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
		else if (prevsize != stat_buf.st_size)
		{
			maildir_quota_deleted(".", (int64_t)
					      (stat_buf.st_size-prevsize),
					      prevsize == 0 ? 1:0);
		}
	}

	if (isok)
		rename(oldname, newname);

	unlink(oldname);

	if (isok)
	{
	char	*realnewname=maildir_requota(newname, stat_buf.st_size);

		if (strcmp(newname, realnewname))
			rename(newname, realnewname);
		free(realnewname);
	}
	free(dir);
	free(oldname);
	free(newname);
	return (isok && isok != -2? 0:-1);
}

void	maildir_deletenewmsg(int n, const char *folder, const char *retname)
{
char	*dir=xlate_mdir(folder);
char	*oldname=alloc_filename(dir, "tmp", retname);

	close(n);
	unlink(oldname);
	free(oldname);
	free(dir);
}

void maildir_cleanup()
{
	closedb();
}

void matches_free(MATCHEDSTR **p, unsigned n)
{
	size_t i;

	for (i=0; p && i<n; ++i)
	{
		MATCHEDSTR *q;

		for (q=p[i]; q && q->match; ++q)
		{
			free(q->prefix);
			free(q->match);
			free(q->suffix);
		}
		if (p[i])
			free(p[i]);
	}
	if (p)
		free(p);
}

/*
** Convert folder names to modified-UTF7 encoding.
*/

char *folder_toutf7(const char *foldername)
{
	char *p;
	int converr;

	p=unicode_convert_tobuf(foldername, sqwebmail_content_charset,
				  unicode_x_imap_modutf7, &converr);

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

char *folder_fromutf7(const char *foldername)
{
	char *p;
	int converr;

	p=unicode_convert_tobuf(foldername,
				  unicode_x_imap_modutf7,
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
