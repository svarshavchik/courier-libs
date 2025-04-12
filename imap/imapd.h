#ifndef	imapd_h
#define	imapd_h

#include <string>
#include "imapflags.h"

/*
** Copyright 1998 - 1999 S. Varshavchik.
** See COPYING for distribution information.
*/

#define	HIERCH	'.'		/* Hierarchy separator char */
#define	HIERCHS	"."		/* Hierarchy separator char */

#define	NEWMSG_FLAG	'*'	/* Prefixed to mimeinfo to indicate new msg */

extern int enabled_utf8;

#define	is_sharedsubdir(dir) \
	(strncmp((dir), SHAREDSUBDIR "/", \
		 sizeof (SHAREDSUBDIR "/")-1) == 0)

#define	SUBSCRIBEFILE	"courierimapsubscribed"

class acl_check_rights {
	const char *rights_requested;
	std::string rights_granted;

public:
	acl_check_rights(const std::string &rights_granted)
		: rights_granted{rights_granted}
	{
		rights_requested=rights_granted.c_str();
	}

	acl_check_rights(const std::string &mailbox,
			 const char *rights_requested);

	operator bool() const
	{
		return rights_requested == rights_granted;
	}

	bool operator()(char c) const
	{
		return rights_granted.find(c) != rights_granted.npos;
	}

	bool operator>>(imapflags &flags) const;

	auto rights_granted_c_str() const {
		return rights_granted.c_str();
	}
};

#endif
