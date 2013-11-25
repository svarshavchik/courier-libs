#ifndef	setgroupid_h
#define	setgroupid_h


#include	"config.h"
#include	<sys/types.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#define	__USE_BSG
#include	<grp.h>

#ifdef  __cplusplus

extern "C"

#endif

int	setgroupid(gid_t grpid);

#endif
