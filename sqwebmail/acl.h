/*
*/
#ifndef	acl_h
#define	acl_h

/*
** Copyright 2004 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	"config.h"

#include	"maildir/maildiraclt.h"
#include	"maildir/maildirinfo.h"

int acl_read(maildir_aclt_list *l, const char *folder,
	     char **owner);
int acl_read2(maildir_aclt_list *l,
	      struct maildir_info *minfo,
	      char **owner);
void acl_computeRights(maildir_aclt_list *l, char *rights,
		       const char *owner);

void acl_computeRightsOnFolder(const char *folder, char *rights);

#endif
