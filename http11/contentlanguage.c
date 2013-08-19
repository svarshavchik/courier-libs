/*
** Copyright 1998 - 2002 Double Precision, Inc.
** See COPYING for distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	"http11.h"
#include	"../rfc2045/rfc2045charset.h"

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

extern void error(const char *);

static void enomem()
{
	error("Out of memory.");
}

static const char defaultlang[] = HTTP11_DEFAULTLANG;

/*
** Based upon Accept-Language: header, figure out which directory in
** HTMLLIBDIR matches it.
*/

FILE *http11_open_langfile(const char *libdir, const char *subdir,
		const char *file)
{
char	*p=malloc(strlen(libdir)+strlen(subdir)+strlen(file)+3);
FILE	*fp;

	if (!p)	return (0);
	strcat(strcat(strcat(strcat(strcpy(p, libdir), "/"), subdir), "/"),
		file);

	fp=fopen(p, "r");
	free(p);
	return (fp);
}

/**************************************************************************/

/* Parse Accept-Language: header */

static size_t parse_accept_string(const char *acc_lang, char **languages,
			double *weights)
{
char *p=strdup(acc_lang ? acc_lang:"");
size_t	cnt=0;
char	*q, *r;
int	has_weights=0;
double	*save_weights=weights;

	if (!p)	enomem();
	for (q=p; (q=strtok(q, ", ")) != 0; q=0)
	{
		if (languages)
		{
			q=strdup(q);
			if (!q)	enomem();
			*languages++=q;
		}
		if (weights)	*weights=1;	/* Until further notice */
		for (r=q; *r; r++)
			*r=tolower(*r);
		if ((r=strchr(q, ';')) != 0)
		{
			*r++=0;
			if (*r == 'q' && r[1] == '=')
			{
			double weight=atof(r+2);

				if (weights)	*weights=weight;
				has_weights=1;
			}
		}
		if (weights)	++weights;
		++cnt;
	}
	free(p);
	if (!has_weights && weights)
	{
	size_t	i;
	double weight=1;

	/*
	** Broken HTTP/1.1 clients do not specify quality factors, and expect
	** the server to pick the first one on the list
	*/
		for (i=cnt; i; )
		{
			--i;
			save_weights[i]=weight;
			weight = weight + 1;
		}
	}
	
	return (cnt);
}

char *http11_best_content_language(const char *libdir, const char *acc_lang)
{
size_t	naccept=parse_accept_string(acc_lang, 0, 0);
char **languages=malloc(naccept ? sizeof(char *)*naccept:1);
double *weights=malloc(naccept ? sizeof(double)*naccept:1);
DIR	*p;
struct dirent *de;
size_t	i;
#if 0
double	missweight=1;
#endif
char	*bestlang=0;
double	bestweight=0;
int	found_nondefault_match=0;

	if (!languages || !weights)
	{
		if (languages)	free(languages);
		if (weights)	free(weights);
		enomem();
	}
	(void)parse_accept_string(acc_lang, languages, weights);
#if 0
	for (i=0; i<naccept; i++)
		if (strcmp(languages[i], "*") == 0)	missweight=weights[i];
		/* Default weight */
#endif
	p=opendir(libdir);
	while (p && (de=readdir(p)) != 0)
	{
	FILE	*fp;

		if (*de->d_name == '.')	continue;

		if ((fp=http11_open_langfile(libdir, de->d_name, "LOCALE"))
				!= 0)
		{
#if 0
		double	myweight=missweight;
#else
		double  myweight=0;
#endif

			fclose(fp);
			for (i=0; i<naccept; i++)
				if (strcmp(languages[i], de->d_name) == 0)
				{
					myweight=weights[i];
					break;
				}
			if (!bestlang || myweight > bestweight)
			{
				if (bestlang)	free(bestlang);
				bestlang=strdup(de->d_name);
				if (!bestlang)	enomem();
				bestweight=myweight;
				if (i < naccept)
					found_nondefault_match=1;
			}
		}
	}
	if (p)	closedir(p);
	if (!bestlang || !found_nondefault_match)
	{
		if (bestlang)	free(bestlang);
		if ((bestlang=malloc(sizeof(defaultlang))) == 0) enomem();
		strcpy(bestlang, defaultlang);
	}
	for (i=0; i<naccept; i++)
		free(languages[i]);
	free(languages);
	free(weights);
	return (bestlang);
}

static const char *get_http11(const char *libdir, const char *subdir,
		char *buf,
		const char *file, const char *def)
{
FILE	*fp=http11_open_langfile(libdir, subdir, file);

	if (fp != 0)
	{
	size_t	n=fread(buf, 1, 79, fp);

		if (n <= 0)	n=0;
		buf[n]=0;
		fclose(fp);
		return (strtok(buf, "\r\n"));
	}
	return (def);
}

const char *http11_content_language(const char *libdir, const char *acc_lang)
{
static char	buf[80];

	return (get_http11(libdir, acc_lang, buf, "LANGUAGE", defaultlang));
}

const char *http11_content_locale(const char *libdir, const char *acc_lang)
{
static char buf[80];
const char *p=get_http11(libdir, acc_lang, buf, "LOCALE", "C");

	return (p);
}

const char *http11_content_ispelldict(const char *libdir, const char *acc_lang)
{
static char buf[80];

	return (get_http11(libdir, acc_lang, buf, "ISPELLDICT", defaultlang));
}

const char *http11_content_charset(const char *libdir, const char *acc_lang)
{
	char buf[80];
	static char buf2[80];
	size_t naccept;
	char **charsets;
	double *weights;
	const char *p;
	size_t i;

	strcpy(buf2, get_http11(libdir, acc_lang, buf, "CHARSET",
				RFC2045CHARSET));

	p=getenv("HTTP_ACCEPT_CHARSET");

	if (!p) p="";

	naccept=parse_accept_string(p, 0, 0);
	charsets=malloc(naccept ? sizeof(char *)*naccept:1);
	weights=malloc(naccept ? sizeof(double)*naccept:1);

	if (!charsets || !weights)
	{
		if (charsets)	free(charsets);
		if (weights)	free(weights);
		enomem();
	}

	(void)parse_accept_string(p, charsets, weights);
	strcpy(buf, buf2);

	for (p=strtok(buf, ", \t\r\n"); p != NULL; p=strtok(NULL, ", \t\r\n"))
	{
		for (i=0; i<naccept; i++)
			if (strcmp(charsets[i], p) == 0)
			{
				strcpy(buf2, charsets[i]);
				for (i=0; i<naccept; i++)
					free(charsets[i]);
				free(charsets);
				free(weights);
				return buf2;
			}
	}

	p=strtok(buf2, ", \t\r\n");
	if (!p)
		p=RFC2045CHARSET;
	for (i=0; i<naccept; i++)
		free(charsets[i]);
	free(charsets);
	free(weights);
	return p;
}
