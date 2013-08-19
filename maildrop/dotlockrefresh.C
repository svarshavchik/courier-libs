#include	"dotlockrefresh.h"
#include	"dotlock.h"
#include	"xconfig.h"


DotLockRefresh::DotLockRefresh(DotLock *p) : dotlock(p)
{
}

DotLockRefresh::~DotLockRefresh()
{
	Cancel();
}

void DotLockRefresh::handler()
{
	dotlock->dorefresh();
}
