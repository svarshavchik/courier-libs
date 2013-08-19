#include	"dotlock.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"xconfig.h"


unsigned DotLock::GetLockSleep()
{
Buffer	b;

	b="LOCKSLEEP";

	return GetVar(b)->Int(LOCKSLEEP_DEF);
}

unsigned DotLock::GetLockTimeout()
{
Buffer	b;

	b="LOCKTIMEOUT";
	return (GetVar(b)->Int(LOCKTIMEOUT_DEF));
}

unsigned DotLock::GetLockRefresh()
{
Buffer	b;

	b="LOCKREFRESH";
	return (GetVar(b)->Int(LOCKREFRESH_DEF));
}

const	char *DotLock::GetLockExt()
{
Buffer varname;

	varname="LOCKEXT";

	return (GetVarStr(varname));
}
