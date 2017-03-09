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

void cgi_output_unicode_escapes(const char32_t *value,
				const char *escapes,
				void (*output_func)(const char *, size_t,
						    void *),
				void *output_arg)
{
	while (value && *value)
	{
		size_t i;

		for (i=0; value[i]; i++)
		{
			if (value[i] > 127 ||
			    strchr(escapes, value[i]))
				break;
		}

		while (i)
		{
			char buf[100];

			size_t n=sizeof(buf);
			size_t j;

			if (n > i)
				n=i;

			for (j=0; j<n; j++)
				buf[j]=value[j];

			(*output_func)(buf, j, output_arg);

			value += j;
			i -= j;
		}

		if (*value)
		{
			char buf[100];

			sprintf(buf, "&#%lu;", (unsigned long)value[i]);

			(*output_func)(buf, 0, output_arg);
			++value;
		}
	}
}


static void do_cgi_input(const char *name,
			 const char32_t *value,
			 int size,
			 int maxlength,
			 const char *flags,

			 void (*output_func)(const char *, size_t,
					     void *),
			 void *output_arg)
{
	(*output_func)("<input name='", 0, output_arg);
	(*output_func)(name, 0, output_arg);
	(*output_func)("'", 0, output_arg);

	if (strchr(flags, 'r'))
		(*output_func)(" readonly='readonly'", 0, output_arg);
	if (strchr(flags, 'd'))
		(*output_func)(" disabled='disabled'", 0, output_arg);

	(*output_func)("'", 0, output_arg);

	if (size)
	{
		char buf[100];

		sprintf(buf, " size=%d", size);

		(*output_func)(buf, 0, output_arg);
	}

	if (maxlength)
	{
		char buf[100];

		sprintf(buf, " maxlength=%d", maxlength);

		(*output_func)(buf, 0, output_arg);
	}

	(*output_func)(" value='", 0, output_arg);

	cgi_output_unicode_escapes(value, "<>'&", output_func, output_arg);

	(*output_func)("' />", 0, output_arg);
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

char *cgi_input(const char *name,
		const char32_t *value,
		int size,
		int maxlength,
		const char *flags)
{
	size_t cnt=1;
	char *buf;
	char *ptr;

	if (!flags)
		flags="";

	do_cgi_input(name, value, size, maxlength, flags, cnt_bytes, &cnt);

	buf=malloc(cnt);

	if (!buf)
		return NULL;

	ptr=buf;
	do_cgi_input(name, value, size, maxlength, flags, save_bytes, &ptr);
	*ptr=0;
	return buf;
}
