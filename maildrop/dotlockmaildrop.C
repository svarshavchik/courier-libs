#include	"dotlock.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"xconfig.h"


unsigned DotLock::GetLockSleep()
{
std::string	b;

	b="LOCKSLEEP";

	return extract_int(*GetVar(b), LOCKSLEEP_DEF);
}

unsigned DotLock::GetLockTimeout()
{
std::string	b;

	b="LOCKTIMEOUT";
	return extract_int(*GetVar(b), LOCKTIMEOUT_DEF);
}

unsigned DotLock::GetLockRefresh()
{
std::string	b;

	b="LOCKREFRESH";
	return extract_int(*GetVar(b), LOCKREFRESH_DEF);
}

const	char *DotLock::GetLockExt()
{
std::string varname;

	varname="LOCKEXT";

	return (GetVarStr(varname));
}
