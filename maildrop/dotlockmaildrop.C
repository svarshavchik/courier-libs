#include	"dotlock.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"xconfig.h"


unsigned DotLock::GetLockSleep()
{
	return extract_int(GetVar("LOCKSLEEP"), LOCKSLEEP_DEF);
}

unsigned DotLock::GetLockTimeout()
{
	return extract_int(GetVar("LOCKTIMEOUT"), LOCKTIMEOUT_DEF);
}

unsigned DotLock::GetLockRefresh()
{
	return extract_int(GetVar("LOCKREFRESH"), LOCKREFRESH_DEF);
}

std::string DotLock::GetLockExt()
{
	return GetVar("LOCKEXT");
}
