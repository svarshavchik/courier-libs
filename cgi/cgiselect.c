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

static void do_cgi_select(const char *name,
			  const char *optvalues,
			  const char *optlabels,
			  const char *default_value,
			  size_t list_size,
			  const char *flags,

			  void (*output_func)(const char *, size_t, void *),
			  void *output_arg)
{
	(*output_func)("<select name='", 0, output_arg);
	(*output_func)(name, 0, output_arg);
	(*output_func)("'", 0, output_arg);

	if (strchr(flags, 'm'))
		(*output_func)(" multiple='multiple'", 0, output_arg);
	if (strchr(flags, 'd'))
		(*output_func)(" disabled='disabled'", 0, output_arg);

	(*output_func)("'>", 0, output_arg);

	if (!optvalues)
		optvalues="";

	while (*optlabels)
	{
		const char *label_start=optlabels;
		const char *value_start=optvalues;

		while (*optlabels)
		{
			if (*optlabels == '\n')
				break;
			++optlabels;
		}

		while (*optvalues)
		{
			if (*optvalues == '\n')
				break;
			++optvalues;
		}

		(*output_func)("<option", 0, output_arg);

		if (*value_start)
		{
			if (default_value &&
			    optvalues - value_start == strlen(default_value) &&
			    strncmp(value_start, default_value,
				    optvalues-value_start) == 0)
			{
				(*output_func)(" selected='selected'", 0,
					       output_arg);
			}

			(*output_func)(" value='", 0, output_arg);
			(*output_func)(value_start, optvalues-value_start,
				       output_arg);
			(*output_func)("'", 0, output_arg);
		}
		(*output_func)(">", 0, output_arg);
		(*output_func)(label_start, optlabels-label_start, output_arg);
		(*output_func)("</option>", 0, output_arg);
		if (*optlabels)
			++optlabels;
		if (*optvalues)
			++optvalues;
	}

	(*output_func)("</select>", 0, output_arg);
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

char *cgi_select(const char *name,
		 const char *optvalues,
		 const char *optlabels,
		 const char *default_value,
		 size_t list_size,
		 const char *flags)
{
	size_t cnt=1;
	char *buf;
	char *ptr;

	if (!flags)
		flags="";

	do_cgi_select(name, optvalues, optlabels, default_value,
		      list_size, flags, cnt_bytes, &cnt);

	buf=malloc(cnt);

	if (!buf)
		return NULL;

	ptr=buf;
	do_cgi_select(name, optvalues, optlabels, default_value,
		      list_size, flags, save_bytes, &ptr);
	*ptr=0;
	return buf;
}
