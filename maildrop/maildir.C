/*
** Copyright 1998 - 2009 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"maildir.h"
#include	"mio.h"
#include	"alarmtimer.h"
#include	"alarmsleep.h"
#include	"config.h"
#include	"xconfig.h"
#include	"funcs.h"
#include	"varlist.h"
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<errno.h>
#include	<stdlib.h>
#include	"mytime.h"

#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	"../maildir/maildirquota.h"
#include	"../maildir/maildircreate.h"
#include	"../maildir/maildirmisc.h"
#include	"../maildir/maildirkeywords.h"


#include "../maildir/maildirquota.h"
extern int quota_warn_percent;
extern const char *quota_warn_message;

Maildir::Maildir() : is_open(0) , is_afs(0)
{
}

Maildir::~Maildir()
{
	MaildirAbort();
}

////////////////////////////////////////////////////////////////////////////
//
//  Attempt to detect if this is a Maildir directory.
//
////////////////////////////////////////////////////////////////////////////

int Maildir::IsMaildir(const char *name)
{
Buffer	dirname;
Buffer	subdirname;
struct	stat stat_buf;

int	c;

	if (!name || !*name)	return (0);	// Nope, not a Maildir
	dirname=name;
	c=dirname.pop();
	if (c != SLASH_CHAR)	dirname.push(c);	// Strip trailing /
	subdirname=dirname;
	subdirname += "/tmp";
	subdirname += '\0';
	if ( stat( (const char *)subdirname, &stat_buf ) ||
		! S_ISDIR(stat_buf.st_mode) )	return (0);
	subdirname=dirname;
	subdirname += "/new";
	subdirname += '\0';
	if ( stat( (const char *)subdirname, &stat_buf ) ||
		! S_ISDIR(stat_buf.st_mode) )	return (0);
	subdirname=dirname;
	subdirname += "/cur";
	subdirname += '\0';
	if ( stat( (const char *)subdirname, &stat_buf ) ||
		! S_ISDIR(stat_buf.st_mode) )	return (0);
	return (1);	// If it looks like a duck, walks like a duck...
}

int	Maildir::MaildirOpen(const char *dir, Mio &file, off_t s)
{
	Buffer	buf;
	struct maildirsize quotainfo;

	const char *quotap;

	Buffer quotabuf;

	quotabuf="MAILDIRQUOTA";	/* Reuse a convenient buffer */
	quotabuf= *GetVar(quotabuf);
	quotabuf += '\0';

	quotap=quotabuf;

	if (!*quotap)
		quotap=NULL;

	MaildirAbort();

AlarmTimer	abort_timer;
static long	counter=0;

	buf.set(counter++);
	buf += '\0';

	struct maildir_tmpcreate_info createInfo;

	maildir_tmpcreate_init(&createInfo);

	createInfo.maildir=dir;
	createInfo.uniq=(const char *)buf;
	createInfo.msgsize=s;
	createInfo.openmode=0666;

	abort_timer.Set( 24 * 60 * 60 );
	while (!abort_timer.Expired())
	{
		Buffer name_buf;

		name_buf="UMASK";
		const char *um=GetVarStr(name_buf);
		unsigned int umask_val=077;

		sscanf(um, "%o", &umask_val);

		umask_val=umask(umask_val);

		int f=maildir_tmpcreate_fd(&createInfo);
		umask(umask_val);

		if (f >= 0)
		{
			Buffer b;

			b="FLAGS";

			const char *flags=GetVarStr(b);

			tmpname=createInfo.tmpname;
			tmpname += '\0';

			if (flags)
			{
				const char *p=flags;

				while (*p)
				{
					if (strchr("DRSF", *p) == NULL)
					{
						f=0;
						break;
					}
					++p;
				}
			}

			if (flags && *flags)
			{
				newname=createInfo.curname;
				newname += ':';
				newname += '2';
				newname += ',';
				newname += flags;
			}
			else
			{
				newname=createInfo.newname;
			}

			newname += '\0';
			maildir_tmpcreate_free(&createInfo);

			file.fd(f);
			is_open=1;
			maildirRoot=dir;
			maildirRoot += '\0';

			if (maildir_quota_add_start(dir, &quotainfo, s,
						    1, quotap))
			{
				file.fd(-1);
				unlink( (const char *)tmpname );
				is_open=0;
				maildir_deliver_quota_warning(dir,
							      quota_warn_percent,
							      quota_warn_message);
				merr << "maildrop: maildir over quota.\n";
				return (-1);
			}

			maildir_quota_add_end(&quotainfo, s, 1);
			return (0);
		}

		if (errno != EAGAIN)
		{
			merr << "maildrop: " << dir << ": " << strerror(errno)
			     << "\n";
			return -1;
		}

		AlarmSleep	try_again(2);
	}

	merr << "maildrop: time out on maildir directory.\n";
	return (-1);
}

void	Maildir::MaildirSave()
{
	if (is_open)
	{
		Buffer keywords;

		keywords="KEYWORDS";
		keywords=*GetVar(keywords);
		keywords += '\0';

		const char *keywords_s=keywords;

		while (*keywords_s && *keywords_s == ',')
			++keywords_s;

		if (*keywords_s)
		{
			struct libmail_kwHashtable kwh;
			struct libmail_kwMessage *kwm;

			libmail_kwhInit(&kwh);

			if ((kwm=libmail_kwmCreate()) == NULL)
				throw strerror(errno);

			while (*keywords_s)
			{
				const char *p=keywords_s;

				while (*keywords_s && *keywords_s != ',')
					++keywords_s;

				char *n=new char [keywords_s - p + 1];

				if (!n)
				{
					libmail_kwmDestroy(kwm);
					throw strerror(errno);
				}

				memcpy(n, p, keywords_s - p);
				n[keywords_s - p]=0;

				while (*keywords_s && *keywords_s == ',')
					++keywords_s;

				if (libmail_kwmSetName(&kwh, kwm, n) < 0)
				{
					delete [] n;
					libmail_kwmDestroy(kwm);
					throw strerror(errno);
				}
				delete [] n;
			}

			char *tmpkname, *newkname;

			if (maildir_kwSave( (const char *)maildirRoot,
					    strrchr(newname, '/')+1, kwm,
					    &tmpkname, &newkname, 0) < 0)
			{
				libmail_kwmDestroy(kwm);
				throw "maildir_kwSave() failed.";
			}

			libmail_kwmDestroy(kwm);

			if (rename(tmpkname, newkname) < 0)
			{
				/* Maybe the keyword directory needs creating */

				struct stat stat_buf;

				if (stat(maildirRoot, &stat_buf) < 0)
				{
					free(tmpkname);
					free(newkname);
					throw strerror(errno);
				}

				char *keywordDir=strrchr(newkname, '/');

				*keywordDir=0;
				mkdir(newkname, 0700);
				chmod(newkname, stat_buf.st_mode & 0777);
				*keywordDir='/';

				if (rename(tmpkname, newkname) < 0)
				{
					free(tmpkname);
					free(newkname);
					throw strerror(errno);
				}
			}
			free(tmpkname);
			free(newkname);
		}

		Buffer dir;

		if (link( (const char *)tmpname, (const char *)newname) < 0)
		{
			if (errno == EXDEV){
				if(rename((const char *)tmpname, (const char *)newname) < 0)
					throw "rename() failed.";
				is_afs = 1;
			}
			else
			{
				throw "link() failed.";
			}
		}
		dir=newname;
		const char *p=dir;
		const char *q=strrchr(p, '/');

		if (q)
		{
			dir.Length(q-p);
			dir.push((char)0);

#if EXPLICITDIRSYNC
			int syncfd=open(dir, O_RDONLY);

			if (syncfd >= 0)
			{
				fsync(syncfd);
				close(syncfd);
			}
#endif

			dir.Length(q-p);
			dir += "/../";
			dir.push((char)0);

			maildir_deliver_quota_warning(dir, quota_warn_percent,
						      quota_warn_message);
		}
	}
}

void	Maildir::MaildirAbort()
{
	if (is_open && !is_afs)	unlink( (const char *)tmpname );
}
