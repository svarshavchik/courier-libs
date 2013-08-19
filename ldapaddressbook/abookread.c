/*
**
** Copyright 2003-2006, Double Precision Inc.
**
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"ldapaddressbook.h"

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

static void dequote(char *);

struct ldapabook *ldapabook_read(const char *filename)
{
char	buf[BUFSIZ];
FILE	*fp;
struct	ldapabook *list, *last;
char	*s;
char	*name;
char	*host;
char	*port;
char	*suffix;
char	*binddn;
char	*bindpw;

	if ((fp=fopen(filename, "r")) == 0)	return (0);

	list=last=0;
	while (fgets(buf, sizeof(buf), fp))
	{
	struct	ldapabook *p;

	struct	ldapabook_opts *opts=NULL, *lastopt=NULL;
	struct ldapabook_opts *nextopt;

		s=strchr(buf, '\n');
		if (s)	*s=0;
		if ((s=strchr(buf, '#')) != 0)	*s=0;
		name=buf;
		s=strchr(buf, '\t');
		if (!s)	continue;
		*s++=0;
		host=s;
		s=strchr(s, '\t');
		if (s) *s++=0;
		port=s;
		if (s)	s=strchr(s, '\t');
		if (s)	*s++=0;
		suffix=s;
		if (s)	s=strchr(s, '\t');
		if (s)	*s++=0;
		binddn=s;
		if (s)	s=strchr(s, '\t');
		if (s)	*s++=0;
		bindpw=s;
		if (!port || !*port)	port="389";
		if (!suffix)	suffix="";
		if (!binddn)	binddn="";
		if (!bindpw)	bindpw="";

		if (s)	s=strchr(s, '\t');
		if (s)	*s++=0;

		while (s && *s)
		{
			char *t;

			t=strchr(s, ',');
			if (t)
				*t++=0;

			if ((nextopt=(struct ldapabook_opts *)
			     malloc(sizeof(struct ldapabook_opts))) == NULL
			    || (nextopt->options=strdup(s)) == NULL)
			{
				if (nextopt)
					free(nextopt);
				ldapabook_free(list);
				fclose(fp);
				return (NULL);
			}

			dequote(nextopt->options);
			if (!lastopt)
				opts=nextopt;
			else
				lastopt->next=nextopt;
			nextopt->next=NULL;
			lastopt=nextopt;
			s=t;
		}

		if ((p=malloc(sizeof(struct ldapabook))) == 0)
		{
			struct ldapabook_opts *nextopt;

			while ((nextopt=opts) != NULL)
			{
				opts=nextopt->next;
				free(nextopt->options);
				free(nextopt);
			}

			ldapabook_free(list);
			fclose(fp);
			return (0);
		}

		memset(p, 0, sizeof(*p));
		p->opts=opts;

		if ( (p->name=strdup(name)) != 0)
		{
			if ((p->host=strdup(host)) != 0)
			{
				if ((p->port=strdup(port)) != 0)
				{
					if ((p->suffix=strdup(suffix)) != 0)
					{
						if ((p->binddn=strdup(binddn))
							!= 0)
						{
							if ((p->bindpw=strdup
								(bindpw)) != 0)
							{
								if (!list)
								   list=last=p;
								else
								   last->next=p;
								last=p;
								p->next=0;
								continue;
							}
							free(p->binddn);
						}
						free(p->suffix);
					}
					free(p->port);
				}
				free(p->host);
			}
			free(p->name);
		}
		free(p);
		while ((nextopt=opts) != NULL)
		{
			opts=nextopt->next;
			free(nextopt->options);
			free(nextopt);
		}
		ldapabook_free(list);
		fclose(fp);
		return (0);
	}
	fclose(fp);
	return (list);
}

void ldapabook_free(struct ldapabook *p)
{
	while (p)
	{
		struct ldapabook *n=p->next;
		struct ldapabook_opts *opts;

		while ((opts=p->opts) != NULL)
		{
			p->opts=opts->next;
			free(opts->options);
			free(opts);
		}

		free(p->bindpw);
		free(p->binddn);
		free(p->suffix);
		free(p->port);
		free(p->host);
		free(p->name);
		free(p);
		p=n;
	}
}

static const char hex[]="0123456789ABCDEF";

static int nybble(char c)
{
	char	*p=strchr(hex, c);

	if (p)	return (p-hex);
	return (0);
}

static void dequote(char *p)
{
	char *q;

	for (q=p; *q; q++)
	{
		if (*q == '+' && q[1] && q[2])
		{
			*p++=nybble(q[1])*16 + nybble(q[2]);
			q += 2;
			continue;
		}
		*p++=*q;
	}
	*p=0;
}

void ldapabook_writerec(const struct ldapabook *b, FILE *fp)
{
	struct	ldapabook_opts *opts;
	char	*sep="\t";

	fprintf(fp, "%s\t%s\t%s\t%s\t%s\t%s", b->name, b->host,
		b->port ? b->port:"", b->suffix,
		b->binddn ? b->binddn:"",
		b->bindpw ? b->bindpw:"");

	for (opts=b->opts; opts; opts=opts->next)
	{
		char *p;

		fprintf(fp, "%s", sep);
		sep=",";

		for (p=opts->options; *p; p++)
		{
			if (*p <= ' ' || *p >= 127 ||
			    *p == ',' || *p == '+')
			{
				fprintf(fp, "+%02X", (int)(unsigned char)*p);
				continue;
			}
			putc(*p, fp);
		}
	}
	fprintf(fp, "\n");
}
