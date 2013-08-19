#include	"setgroupid.h"


void	setgroupid(gid_t grpid)
{
gid_t g=grpid;

#if	HAVE_SETGROUPS
	setgroups(1, &g);
#endif

	setgid(g);
}
