/*
** Copyright 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildiraclt.h"
#include	"maildirmisc.h"
#include	"maildircreate.h"
#include	"maildirnewshared.h"
#include	"numlib/numlib.h"
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>


int maildir_newshared_disabled=0;

int maildir_newshared_open(const char *indexfile,
			   struct maildir_newshared_enum_cb *info)
{
	info->indexfile=indexfile;
	if ((info->fp=fopen(maildir_newshared_disabled ?
			    "/dev/null":indexfile, "r")) == NULL)
		return -1;
	info->startingpos=0;
	info->linenum=0;
	return 0;
}

void maildir_newshared_close(struct maildir_newshared_enum_cb *info)
{
	if (info->fp)
		fclose(info->fp);
	info->fp=NULL;
}

int maildir_newshared_nextAt(struct maildir_newshared_enum_cb *info,
			     int *eof,
			     int (*cb_func)(struct maildir_newshared_enum_cb*,
					    void *),
			     void *cb_arg)
{
	int ret=0;

	*eof=maildir::newshared_nextAt(
		info,
		[&]
		{
			ret=(*cb_func)(info, cb_arg);
		}) ? 1:0;

	return ret;
}

int maildir_newshared_next(struct maildir_newshared_enum_cb *info,
			   int *eof,
			   int (*cb_func)(struct maildir_newshared_enum_cb *,
					  void *),
			   void *cb_arg)
{
	int ret=0;

	*eof=maildir::newshared_next(
		info,
		[&]
		{
			ret=(*cb_func)(info, cb_arg);
		}) ? 1:0;

	return ret;
}

bool maildir::newshared_nextAt(struct maildir_newshared_enum_cb *info,
			       const std::function<void ()> &callback)
{
	if (fseek(info->fp, info->startingpos, SEEK_SET) < 0)
		return true;
	info->linenum= -1;

	return newshared_next(info, callback);
}


bool maildir::newshared_next(struct maildir_newshared_enum_cb *info,
			     const std::function<void ()> &callback)
{
	char linebuf[BUFSIZ];
	char *p;
	const char *name;
	const char *homedir;
	const char *maildir;
	uid_t uid;
	gid_t gid;
	off_t nbytes;

#define CB_INIT(name_,homedir_,maildir_,uid_,gid_) \
	info->name=name_; info->homedir=homedir_; info->maildir=maildir_; \
	info->uid=uid_; info->gid=gid_;

	while (fgets(linebuf, sizeof(linebuf), info->fp) != NULL)
	{
		nbytes=strlen(linebuf);

		if (nbytes && linebuf[nbytes-1] == '\n')
			linebuf[nbytes-1]=0;

		p=strchr(linebuf, '#');
		if (p) *p=0;

		p=strchr(linebuf, '\t');
		++info->linenum;
		if (p)
		{
			name=linebuf;
			*p++=0;

			if (*p == '*')
			{
				p=strchr(p, '\t');
				if (p)
				{
					const char *q;
					size_t n;

					*p++=0;
					maildir=p;
					p=strchr(p, '\t');
					if (p) *p=0;

					q=strrchr(info->indexfile, '/');
					if (q)
						++q;
					else q=info->indexfile;

					n=strlen(info->indexfile)-strlen(q);

					p=(char *)malloc(n+strlen(maildir)+1);
					if (!p)
						return true;

					if (n)
						memcpy(p, info->indexfile, n);
					strcpy(p+n, maildir);


					CB_INIT(name, NULL, p, 0, 0);

					callback();

					free(p);
					info->startingpos += nbytes;
					return false;
				}
			}
			else
			{
				uid=libmail_atouid_t(p);
				p=strchr(p, '\t');
				if (uid && p)
				{
					*p++=0;
					gid=libmail_atogid_t(p);
					p=strchr(p, '\t');
					if (gid && p)
					{
						*p++=0;
						homedir=p;
						p=strchr(p, '\t');
						maildir="./Maildir";

						if (p)
						{
							*p++=0;
							if (*p && *p != '\t')
								maildir=p;
							p=strchr(p, '\t');
							if (p) *p=0;
						}

						CB_INIT(name, homedir,
							maildir,
							uid,
							gid);

						callback();
						info->startingpos += nbytes;
						return false;
					}
				}
			}
		}

		if (linebuf[0])
		{
			fprintf(stderr, "ERR: %s(%d): syntax error.\n",
				info->indexfile, (int)info->linenum);
		}
		info->startingpos += nbytes;
	}
	return true;
}

int maildir_newshared_enum(const char *indexfile,
			   int (*cb_func)(struct maildir_newshared_enum_cb *,
					  void *),
			   void *cb_arg)
{
	int rc=0;

	maildir::newshared_enum(
		indexfile,
		[&]
		(maildir_newshared_enum_cb *cb)
		{
			if (rc == 0)
				rc=cb_func(cb, cb_arg);
		});

	return rc;
}

void maildir::newshared_enum(
	const char *indexfile,
	const std::function<void (maildir_newshared_enum_cb *)> &callback)
{
	struct maildir_newshared_enum_cb cb;

	if (maildir_newshared_open(indexfile, &cb) < 0)
		return;

	while (!newshared_next(
		       &cb,
		       [&]
		       {
			       callback(&cb);
		       }))
		;

	maildir_newshared_close(&cb);
}
