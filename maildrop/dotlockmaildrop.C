#include	"dotlock.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"xconfig.h"


unsigned DotLock::GetLockSleep()
{
Buffer	b;

	b="LOCKSLEEP";

	return extract_int(*GetVar(b), LOCKSLEEP_DEF);
}

unsigned DotLock::GetLockTimeout()
{
Buffer	b;

	b="LOCKTIMEOUT";
	return extract_int(*GetVar(b), LOCKTIMEOUT_DEF);
}

unsigned DotLock::GetLockRefresh()
{
Buffer	b;

	b="LOCKREFRESH";
	return extract_int(*GetVar(b), LOCKREFRESH_DEF);
}

const	char *DotLock::GetLockExt()
{
Buffer varname;

	varname="LOCKEXT";

	return (GetVarStr(varname));
}
