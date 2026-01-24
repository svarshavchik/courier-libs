/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"maildirfilter.h"

#include	"maildir/autoresponse.h"

#include	<cstring>

int maildir_filter_autoresp_info_init(struct maildir_filter_autoresp_info *i, const char *c)
{
	memset(i, 0, sizeof(*i));

	if (!mail::autoresponse::validate("", c))
		return (-1);
	i->name=strdup(c);
	if (!(i->name))
		return (-1);
	return (0);
}
