/*
** Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"sqconfig.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"maildir/maildircreate.h"

/* Assume all configuration data fits in 256 char buffer. */

static char linebuf[256];

const char *read_sqconfig(const char *dir, const char *configfile, time_t *mtime)
{
char *p=malloc(strlen(dir) + strlen(configfile) + 2);
struct stat stat_buf;
FILE	*f;

	if (!p)	enomem();
	strcat(strcat(strcpy(p, dir), "/"), configfile);
	f=fopen(p, "r");
	free(p);
	if (!f)	return (0);
	if (fstat(fileno(f), &stat_buf) != 0 ||
		!fgets(linebuf, sizeof(linebuf), f))
	{
		fclose(f);
		return (0);
	}
	fclose(f);
	if (mtime)	*mtime=stat_buf.st_mtime;

	linebuf[sizeof(linebuf)-1]=0;
	if ((p=strchr(linebuf, '\n')) != 0)	*p=0;
	return (linebuf);
}

void write_sqconfig(const char *dir, const char *configfile, const char *val)
{
	char *p=malloc(strlen(dir) + strlen(configfile) + 2);

	struct maildir_tmpcreate_info createInfo;
	FILE *fp;

	if (!p)	enomem();

	strcat(strcat(strcpy(p, dir), "/"), configfile);
	if (!val)
	{
		unlink(p);
		free(p);
		return;
	}

	maildir_tmpcreate_init(&createInfo);

	createInfo.maildir=dir;
	createInfo.uniq="config";
	createInfo.doordie=1;

	fp=maildir_tmpcreate_fp(&createInfo);

	if (!fp)
		enomem();


	free(createInfo.newname);
	createInfo.newname=p;

	fprintf(fp, "%s\n", val);
	fflush(fp);
	if (ferror(fp))	eio("Error after write:",p);
	fclose(fp);

	/* Note - umask should already turn off the 077 bits, but
	** just in case someone screwed up previously, I'll fix it
	** myself */

	chmod(createInfo.tmpname, 0600);
	rename(createInfo.tmpname, createInfo.newname);
	maildir_tmpcreate_free(&createInfo);
}
