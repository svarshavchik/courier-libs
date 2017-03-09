/*
** Copyright 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/

#include	"cgi.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>

extern void cgi_output_unicode_escapes(const char32_t *value,
				       const char *escapes,
				       void (*output_func)(const char *,
							   size_t,
							   void *),
				       void *output_arg);

static void do_cgi_textarea(const char *name,
			    int rows,
			    int cols,
			    const char32_t *value,
			    const char *opts,
			    const char *wrap,
			    void (*output_func)(const char *, size_t,
						void *),
			    void *output_arg)
{
	(*output_func)("<textarea name='", 0, output_arg);
	(*output_func)(name, 0, output_arg);
	(*output_func)("'", 0, output_arg);

	if (strchr(opts, 'r'))
		(*output_func)(" readonly='readonly'", 0, output_arg);
	if (strchr(opts, 'd'))
		(*output_func)(" disabled='disabled'", 0, output_arg);

	(*output_func)("'", 0, output_arg);

	if (rows)
	{
		char buf[100];

		sprintf(buf, " rows='%d'", rows);

		(*output_func)(buf, 0, output_arg);
	}

	if (cols)
	{
		char buf[100];

		sprintf(buf, " cols='%d'", cols);

		(*output_func)(buf, 0, output_arg);
	}

	if (wrap)
	{
		(*output_func)(" wrap='", 0, output_arg);
		(*output_func)(wrap, 0, output_arg);
		(*output_func)("'", 0, output_arg);
	}

	(*output_func)(">", 0, output_arg);

	cgi_output_unicode_escapes(value, "<>'&", output_func, output_arg);

	(*output_func)("</textarea>", 0, output_arg);
}

static void cnt_bytes(const char *str, size_t cnt, void *arg)
{
	if (!cnt)
		cnt=strlen(str);

	*(size_t *)arg += cnt;
}

static void save_bytes(const char *str, size_t cnt, void *arg)
{
	char **p=(char **)arg;

	if (!cnt)
		cnt=strlen(str);

	memcpy(*p, str, cnt);

	*p += cnt;
}

char *cgi_textarea(const char *name,
		   int rows,
		   int cols,
		   const char32_t *value,
		   const char *wrap,
		   const char *opts)
{
	size_t cnt=1;
	char *buf;
	char *ptr;

	if (!opts)
		opts="";

	do_cgi_textarea(name, rows, cols, value, opts, wrap, cnt_bytes, &cnt);

	buf=malloc(cnt);

	if (!buf)
		return NULL;

	ptr=buf;
	do_cgi_textarea(name, rows, cols, value, opts, wrap, save_bytes, &ptr);
	*ptr=0;
	return buf;
}
