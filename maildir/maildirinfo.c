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

/*
** Convert modified-UTF8 folder name into an array of UTF-8 words, that
** represent a folder name.
*/

char **maildir_smapfn_fromutf8(const char *modutf8)
{
	char *p=strdup(modutf8), *q;
	size_t n, i;
	char **fn;

	if (!p)
		return NULL;

	n=1;
	for (i=0; p[i]; i++)
		if (p[i] == '.' && p[i+1] && p[i+1] != '.')
		{
			++n;
		}

	fn=malloc((n+1)*sizeof(char *));

	if (!fn)
	{
		free(p);
		return NULL;
	}

	n=0;
	q=p;
	do
	{
		for (i=0; q[i]; i++)
			if (q[i] == '.' && q[i+1] && q[i+1] != '.')
			{
				q[i++]=0;
				break;
			}

		fn[n]=unicode_convert_tobuf(q,
					    unicode_x_smap_modutf8,
					    "utf-8", NULL);
		q += i;

		if (!fn[n])
		{
			while (n)
				free(fn[--n]);
			free(fn);
			free(p);
			return NULL;
		}
		n++;
	} while (*q);
	fn[n]=0;
	free(p);
	return fn;
}

void maildir_smapfn_free(char **fn)
{
	size_t i;

	for (i=0; fn[i]; i++)
		free(fn[i]);
	free(fn);
}

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
