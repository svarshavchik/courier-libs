/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "mimegpgstack.h"

int libmail_mimestack_push(struct mimestack **s, const char *b)
{
	struct mimestack *ss=(struct mimestack *)
		malloc(sizeof(**s));

	if (!ss)
		return -1;

	if ((ss->boundary=strdup(b)) == NULL)
	{
		free(ss);
		return -1;
	}

	ss->next= *s;
	*s=ss;
	return 0;
}

void libmail_mimestack_pop(struct mimestack **p)
{
	struct mimestack *pp= *p;

	if (pp)
	{
		*p=pp->next;
		free(pp->boundary);
		free(pp);
	}
}

void libmail_mimestack_pop_to(struct mimestack **p, struct mimestack *s)
{
	while (*p)
	{
		int last=strcmp( (*p)->boundary, s->boundary) == 0;
		libmail_mimestack_pop(p);
		if (last)
			break;
	}
}

struct mimestack *libmail_mimestack_search(struct mimestack *p, const char *c)
{
	int l=strlen(c);

	while (p)
	{
		int ll=strlen(p->boundary);

		if (l >= ll && strncasecmp(p->boundary, c, ll) == 0)
			break;
		p=p->next;
	}
	return (p);
}


