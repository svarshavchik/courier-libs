/*
*/
#ifndef	acl_h
#define	acl_h

/*
** Copyright 2004 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include	"config.h"

#include	"maildir/maildiraclt.h"
#include	"maildir/maildirinfo.h"

void acl_computeRights(maildir_aclt_list *l, char *rights,
		       const char *owner);

void acl_computeRightsOnFolder(const char *folder, char *rights);

#endif
