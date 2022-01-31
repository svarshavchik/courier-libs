#ifndef	imapd_h
#define	imapd_h

/*
** Copyright 1998 - 1999 S. Varshavchik.
** See COPYING for distribution information.
*/

#ifdef __cplusplus
extern "C" {
#endif


#define	HIERCH	'.'		/* Hierarchy separator char */
#define	HIERCHS	"."		/* Hierarchy separator char */

#define	NEWMSG_FLAG	'*'	/* Prefixed to mimeinfo to indicate new msg */

extern int enabled_utf8;

#define	is_sharedsubdir(dir) \
	(strncmp((dir), SHAREDSUBDIR "/", \
		 sizeof (SHAREDSUBDIR "/")-1) == 0)

#define	SUBSCRIBEFILE	"courierimapsubscribed"

#ifdef __cplusplus
}
#include <string>

extern void check_rights(const std::string &mailbox,
			 char *rights_buf);
#endif

#define CHECK_RIGHTSM(mailbox, varname, rights) \
	char varname[sizeof(rights)]; \
	strcpy(varname, rights); \
	check_rights(mailbox, varname);

#endif
