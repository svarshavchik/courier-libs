/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<signal.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/time.h>

#include	"gpg.h"
#include	"gpglib.h"

#include	"rfc2045/rfc2045.h"

struct rfc2045 *libmail_gpgmime_is_multipart_signed(const struct rfc2045 *q)
{
	struct rfc2045 *p;

	if (!q->content_type || strcmp(q->content_type, "multipart/signed"))
		return (0);

	for (p=q->firstpart; p && p->isdummy; p=p->next)
		;

	if (p && p->next && p->next->content_type &&
	    strcmp(p->next->content_type, "application/pgp-signature") == 0)
		return (p);

	return (NULL);
}

struct rfc2045 *libmail_gpgmime_is_multipart_encrypted(const struct rfc2045 *q)
{
	struct rfc2045 *p;

	if (!q->content_type || strcmp(q->content_type, "multipart/encrypted"))
		return (0);

	for (p=q->firstpart; p && p->isdummy; p=p->next)
		;

	if (p && p->content_type && p->next && p->next->content_type &&
	    strcmp(p->content_type, "application/pgp-encrypted") == 0 &&
	    strcmp(p->next->content_type, "application/octet-stream") == 0)
		return (p->next);

	return (NULL);
}

int libmail_gpgmime_has_mimegpg(const struct rfc2045 *q)
{
	if (libmail_gpgmime_is_multipart_signed(q) ||
	    libmail_gpgmime_is_multipart_encrypted(q))
		return (1);

	for (q=q->firstpart; q; q=q->next)
	{
		if (q->isdummy)
			continue;
		if (libmail_gpgmime_has_mimegpg(q))
			return (1);
	}
	return (0);
}

int libmail_gpgmime_is_decoded(const struct rfc2045 *q, int *retcode)
{
	const char *p;

	if (!q->content_type || strcasecmp(q->content_type,
					   "multipart/x-mimegpg"))
		return (0);

	p=rfc2045_getattr(q->content_type_attr, "xpgpstatus");
	if (!p)
		return (0);

	*retcode=atoi(p);
	return (1);
}

struct rfc2045 *libmail_gpgmime_decoded_content(const struct rfc2045 *q)
{
	for (q=q->firstpart; q; q=q->next)
	{
		if (q->isdummy)
			continue;

		return (q->next);
	}
	return (NULL);
}

struct rfc2045 *libmail_gpgmime_signed_content(const struct rfc2045 *p)
{
	struct rfc2045 *q;

	for (q=p->firstpart; q; q=q->next)
	{
		if (q->isdummy)
			continue;

		return (q);
	}
	return (NULL);
}

