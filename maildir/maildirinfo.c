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

#include	<sys/types.h>
#include	<sys/stat.h>

#include	"config.h"
#include	"maildirinfo.h"

#include	"maildirmisc.h"
#include	"maildirnewshared.h"
#include	<courier-unicode.h>

void maildir_info_destroy(struct maildir_info *info)
{
	if (info->homedir)
		free(info->homedir);
	if (info->maildir)
		free(info->maildir);
	if (info->owner)
		free(info->owner);
	info->homedir=NULL;
	info->maildir=NULL;
	info->owner=NULL;
}

/***************************************************************************/

static int complex_flag;

void maildir_info_munge_complex(int f)
{
	complex_flag=f;
}

static size_t munge_complex(const char *, char *);

char *maildir_info_imapmunge(const char *name)
{
	char *n=unicode_convert_tobuf(name, "utf-8",
					unicode_x_imap_modutf7, NULL);
	char *p;
	size_t cnt;

	if (!n)
		return NULL;

	if (!complex_flag)
	{
		for (p=n; *p; p++)
		{
			if (*p == '.' || *p == '/')
				*p=' ';
		}

		return n;
	}

	cnt=munge_complex(n, NULL);
	p=malloc(cnt);
	if (!p)
	{
		free(n);
		return NULL;
	}

	munge_complex(n, p);

	free(n);
	return p;
}

static size_t munge_complex(const char *orig, char *n)
{
	size_t cnt=0;

	while (*orig)
	{
		switch (*orig) {
		case '.':
			if (n)
			{
				*n++='\\';
				*n++=':';
			}
			cnt += 2;
			break;
		case '/':
			if (n)
			{
				*n++='\\';
				*n++=';';
			}
			cnt += 2;
			break;
		case '\\':
			if (n)
			{
				*n++='\\';
				*n++='\\';
			}
			cnt += 2;
			break;
		default:
			if (n) *n++ = *orig;
			++cnt;
		}
		++orig;
	}

	if (n) *n=0;
	return cnt+1;
}
