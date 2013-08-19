/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "mimegpgheader.h"

void libmail_readheader_init(struct read_header_context *cts)
{
	cts->continue_header=0;
	cts->header_len=0;
	cts->first=0;
	cts->last=0;
}

int libmail_readheader(struct read_header_context *cts, const char *buf)
{
	int l;
	char *s;
	struct header *p;

	l=strlen(buf);
	p=0;

	if (cts->continue_header)
		p=cts->last;
	else if (isspace((int)(unsigned char)buf[0]))
		p=cts->last;

	if (!p)
	{
		p=(struct header *)malloc(sizeof(*p));
		if (!p)
			return -1;

		p->next=0;
		p->header=0;
		cts->header_len=0;
		if (cts->last)
			cts->last->next=p;
		else
			cts->first=p;
		cts->last=p;
	}

	s=p->header ? realloc(p->header, cts->header_len + l + 1)
		:malloc(l+1);

	if (!s)
		return (-1);

	strcpy(s+cts->header_len, buf);
	cts->header_len += l;
	p->header=s;

	cts->continue_header=!l || buf[l-1] != '\n';

	return 0;
}

struct header *libmail_readheader_finish(struct read_header_context *cts)
{
	struct header *last;

	for (last=cts->first; last; last=last->next)
	{
		char *q, *r;

		if (!last->header)
			continue;

		for (q=r=last->header; *r; r++)
		{
			if (*r == '\r')
				continue;
			*q++ = *r;
		}
		*q=0;
	}
	return (cts->first);
}

void libmail_header_free(struct header *p)
{
	struct header *q;

	while (p)
	{
		q=p->next;
		if (p->header)
			free(p->header);
		free(p);
		p=q;
	}
}

struct header *libmail_header_find(struct header *p, const char *n)
{
	int l=strlen(n);

	while (p)
	{
		if (p->header && strncasecmp(p->header, n, l) == 0)
			return (p);
		p=p->next;
	}
	return (p);
}

const char *libmail_header_find_txt(struct header *p, const char *n)
{
	p=libmail_header_find(p, n);
	if (p)
		return (p->header+strlen(n));
	return (NULL);
}

void libmail_mimeheader_free(struct mime_header *m)
{
	struct mime_attribute *a;

	while ((a=m->attr_list) != 0)
	{
		m->attr_list=a->next;
		if (a->name)
			free(a->name);
		if (a->value)
			free(a->value);
		free(a);
	}
	free(m->header_name);
	free(m);
}

static void striptrlspc(char *p)
{
	char *q;

	for (q=p; *p; p++)
		if (!isspace((int)(unsigned char)*p))
			q=p+1;
	*q=0;
}

struct mime_header *libmail_mimeheader_parse(const char *field)
{
	struct mime_header *header=0;
	struct mime_attribute *last_attr=0;

	while (*field || header == 0)
	{
		const char *p;

		if (*field && isspace((int)(unsigned char)*field))
		{
			++field;
			continue;
		}
		for (p=field; *p; p++)
		{
			if (*p == ';')
				break;
			if (*p == '=' && header)
				break;
		}

		if (!header)
		{
			if ((header=(struct mime_header *)
			     malloc(sizeof(*header))) == 0)
				return NULL;

			if ((header->header_name=malloc(p-field+1)) == 0)
			{
				free(header);
				return (NULL);
			}
			memcpy(header->header_name, field, p-field);
			header->header_name[p-field]=0;
			header->attr_list=NULL;
			striptrlspc(header->header_name);
			field=p;
		}
		else
		{
			struct mime_attribute *a=(struct mime_attribute *)
				malloc(sizeof(struct mime_attribute));
			int pass, len;
			char *s=0;

			if (!a)
			{
				libmail_mimeheader_free(header);
				return NULL;
			}
			if ((a->name=malloc(p-field+1)) == NULL)
			{
				free(a);
				libmail_mimeheader_free(header);
				return NULL;
			}
			memcpy(a->name, field, p-field);
			a->name[p-field]=0;
			striptrlspc(a->name);
			a->value=0;
			a->next=0;
			if (last_attr)
			{
				last_attr->next=a;
			}
			else
			{
				header->attr_list=a;
			}
			last_attr=a;

			if (*p == '=')
				++p;

			while (*p && isspace((int)(unsigned char)*p))
			{
				++p;
			}

			field=p;

			len=0;
			for (pass=0; pass<2; pass++)
			{
				int quote=0;

				if (pass)
				{
					s=a->value=malloc(len);
					if (!s)
					{
						libmail_mimeheader_free(header);
						return NULL;
					}
				}
				len=0;

				for (p=field; *p; )
				{
					if (*p == ';' && !quote)
					{
						break;
					}

					if (*p == '"')
					{
						quote= !quote;
						++p;
						continue;
					}

					if (*p == '\\' && p[1])
						++p;

					if (pass)
						s[len]= *p;
					++len;
					++p;
				}
				if (pass)
					s[len]=0;
				++len;
			}

			striptrlspc(a->value);
			field=p;
			if (a->value[0] == 0)
			{
				free(a->value);
				a->value=0;
			}
		}
		if (*field == ';')
			++field;
	}

	return (header);
}

const char *libmail_mimeheader_getattr(struct mime_header *m, const char *name)
{
	struct mime_attribute *a;

	for (a=m->attr_list; a; a=a->next)
	{
		if (strcasecmp(a->name, name) == 0)
			return (a->value);
	}
	return (0);
}

#if 0
void libmail_mimeheader_setattr(struct mime_header *m,
				const char *name, const char *value)
{
	struct mime_attribute *a, *lasta=0;

	for (a=m->attr_list; a; a=a->next)
	{
		if (strcasecmp(a->name, name) == 0)
		{
			if (a->value)
				free(a->value);
			a->value=0;
			if (value)
			{
				a->value=strdup(value);
				if (!a->value)
				{
					perror("strdup");
					exit(1);
				}
			}
			return;
		}
		lasta=a;
	}

	a=(struct mime_attribute *)malloc(sizeof(struct mime_attribute));
	if (!a)
	{
		perror("malloc");
		exit(1);
	}
	if (!(a->name=strdup(name)))
	{
		free(a);
		perror("malloc");
		exit(1);
	}

	a->value=0;
	a->next=0;
	if (value && !(a->value=strdup(value)))
	{
		free(a->name);
		free(a);
		perror("malloc");
		exit(1);
	}

	if (lasta)
		lasta->next=a;
	else
		m->attr_list=a;
}

void print_mime_header(FILE *f, struct mime_header *m)
{
	struct mime_attribute *a;

	fprintf(f, "%s", m->header_name);

	for (a=m->attr_list; a; a=a->next)
	{
		const char *p;

		fprintf(f, "; %s%s", a->name, a->value ? "=":"");
		if (!a->value)
			continue;

		for (p=a->value; *p; p++)
			if (strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_", *p) == NULL)
				break;

		if (!*p)
		{
			fprintf(f, "%s", a->value);
			continue;
		}
		putc('"', f);
		for (p=a->value; *p; p++)
		{
			if (*p == '"' || *p == '\\')
				putc('\\', f);
			putc(*p, f);
		}
		putc('"', f);
	}
}
#endif
