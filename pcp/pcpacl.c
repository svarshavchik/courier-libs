/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdlib.h>
#include <string.h>
#include "pcp.h"

void pcp_acl_name(int flags, char *buf)
{
#define NAME(n,s) if (flags & (n)) {if (*buf) strcat(buf, " "); strcat(buf,s);}

	NAME(PCP_ACL_MODIFY, "MODIFY");
	NAME(PCP_ACL_CONFLICT, "CONFLICT");
	NAME(PCP_ACL_LIST, "LIST");
	NAME(PCP_ACL_RETR, "RETR");
	NAME(PCP_ACL_NONE, "NONE");
#undef NAME
}

int pcp_acl_num(const char *c)
{
#define NAME(n,s) if (strcasecmp(c, s) == 0) return (n)

	NAME(PCP_ACL_MODIFY, "MODIFY");
	NAME(PCP_ACL_CONFLICT, "CONFLICT");
	NAME(PCP_ACL_LIST, "LIST");
	NAME(PCP_ACL_RETR, "RETR");
	NAME(PCP_ACL_NONE, "NONE");
#undef NAME
	return (0);
}

