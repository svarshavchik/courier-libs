/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildircreate.h"
#include	"maildirmisc.h"
#include	<sys/types.h>
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif

#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	"numlib/numlib.h"


FILE *maildir_tmpcreate_fp(struct maildir_tmpcreate_info *info)
{
	int fd=maildir_tmpcreate_fd(info);
	FILE *fp;

	if (fd < 0)
		return NULL;

	fp=fdopen(fd, "w+");

	if (fp == NULL)
	{
		close(fd);
		return NULL;
	}

	return fp;
}

static int maildir_tmpcreate_fd_do(struct maildir::tmpcreate_info &info);

#define KEEPTRYING	(60 * 60)
#define SLEEPFOR	3

int maildir_tmpcreate_fd(struct maildir_tmpcreate_info *info)
{
	maildir::tmpcreate_info cppinfo;

	cppinfo.maildir=info->maildir ? info->maildir:"";
	cppinfo.msgsize=info->msgsize;

	if (info->uniq)
		cppinfo.uniq=info->uniq;

	if (info->hostname)
		cppinfo.hostname=info->hostname;

	cppinfo.openmode=info->openmode;
	cppinfo.doordie=info->doordie;

	int fd=cppinfo.fd();

	if (fd < 0)
	{
		maildir_tmpcreate_free(info);
		return fd;
	}

	if ((info->tmpname=strdup(cppinfo.tmpname.c_str())) == NULL ||
	    (info->curname=strdup(cppinfo.curname.c_str())) == NULL ||
	    (info->newname=strdup(cppinfo.newname.c_str())) == NULL)
		 abort();
	return fd;
}

FILE *maildir::tmpcreate_info::fp()
{
	int nfd=fd();
	FILE *fp;

	if (nfd < 0)
		return NULL;

	fp=fdopen(nfd, "w+");

	if (fp == NULL)
	{
		close(nfd);
		return NULL;
	}

	return fp;
}

static int maildir_tmpcreate_fd_do(struct maildir::tmpcreate_info &info);

int maildir::tmpcreate_info::fd()
{
	int i;

	if (!doordie)
		return (maildir_tmpcreate_fd_do(*this));

	for (i=0; i<KEEPTRYING / SLEEPFOR; i++)
	{
		int fd=maildir_tmpcreate_fd_do(*this);

		if (fd >= 0 || errno != EAGAIN)
			return fd;

		sleep(SLEEPFOR);
	}

	return -1;
}

static int maildir_tmpcreate_fd_do(struct maildir::tmpcreate_info &info)
{
	char hostname_buf[256];
	char time_buf[NUMBUFSIZE];
	char usec_buf[NUMBUFSIZE];
	char pid_buf[NUMBUFSIZE];
	char len_buf[NUMBUFSIZE+3];
	char dev_buf[NUMBUFSIZE];
	char ino_buf[NUMBUFSIZE];
	struct timeval tv;

	struct stat stat_buf;
	int fd;

	if (info.maildir.empty())
		info.maildir=".";

	if (info.hostname.empty())
	{
		hostname_buf[sizeof(hostname_buf)-1]=0;
		if (gethostname(hostname_buf, sizeof(hostname_buf)-1) < 0)
			strcpy(hostname_buf, "localhost");
		info.hostname=hostname_buf;
	}

	gettimeofday(&tv, NULL);

	libmail_str_time_t(tv.tv_sec, time_buf);
	libmail_str_time_t(tv.tv_usec, usec_buf);
	libmail_str_pid_t(getpid(), pid_buf);
	len_buf[0]=0;
	if (info.msgsize > 0)
	{
		strcpy(len_buf, ",S=");
		libmail_str_size_t(info.msgsize, len_buf+3);
	}

	info.tmpname.reserve(info.maildir.size()+
			     info.uniq.size()+
			     info.hostname.size()+strlen(time_buf)+
			     strlen(usec_buf)+
			     strlen(pid_buf)+strlen(len_buf)+100);

	info.tmpname=info.maildir;
	info.tmpname += "/tmp/";
	info.tmpname += time_buf;
	info.tmpname += ".M";
	info.tmpname += usec_buf;
	info.tmpname += "P";
	info.tmpname += pid_buf;

	if (!info.uniq.empty())
	{
		info.tmpname += "_";
		info.tmpname += info.uniq;
	}

	info.tmpname += ".";
	info.tmpname += info.hostname;
	info.tmpname += len_buf;

	if (stat( info.tmpname.c_str(), &stat_buf) == 0)
	{
		errno=EAGAIN;
		return -1;
	}

	if (errno != ENOENT)
	{
		if (errno == EAGAIN)
			errno=EIO;
		return -1;
	}

	if ((fd=maildir_safeopen_stat(info.tmpname.c_str(),
				      O_CREAT|O_RDWR|O_TRUNC,
				      info.openmode, &stat_buf)) < 0)
	{
		return -1;
	}

	libmail_strh_dev_t(stat_buf.st_dev, dev_buf);
	libmail_strh_ino_t(stat_buf.st_ino, ino_buf);

	info.newname.reserve(info.tmpname.size()+strlen(ino_buf)+
			     strlen(dev_buf)+3);

	info.newname=info.maildir;

	info.newname += "/new/";
	info.newname += time_buf;
	info.newname += ".M";
	info.newname += usec_buf;
	info.newname += "P";
	info.newname += pid_buf;
	info.newname += "V";
	info.newname += dev_buf;
	info.newname += "I";
	info.newname += ino_buf;
	if (!info.uniq.empty())
	{
		info.newname += "_";
		info.newname += info.uniq;
	}

	info.newname += ".";
	info.newname += info.hostname;
	info.newname += len_buf;

	info.curname=info.newname;

	memcpy(&info.curname[info.maildir.size()+1], "cur", 3);

	return fd;
}

void maildir_tmpcreate_free(struct maildir_tmpcreate_info *info)
{
	if (info->tmpname)
		free(info->tmpname);
	info->tmpname=NULL;

	if (info->newname)
		free(info->newname);
	info->newname=NULL;

	if (info->curname)
		free(info->curname);
	info->curname=NULL;
}

int maildir_movetmpnew(const char *tmpname, const char *newname)
{
	return maildir::movetmpnew(tmpname, newname);
}

int maildir::movetmpnew(const std::string &tmpname, const std::string &newname)
{
	if (link(tmpname.c_str(), newname.c_str()) == 0)
	{
		unlink(tmpname.c_str());
		return 0;
	}

	if (errno != EXDEV)
		return -1;

	/* AFS? */

	return rename(tmpname.c_str(), newname.c_str());
}
