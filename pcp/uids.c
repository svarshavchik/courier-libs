/*
** Copyright 2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcp.h"
#include "uids.h"

const char *pcpuid()
{
	return uid;
}

const char *pcpgid()
{
	return gid;
}
