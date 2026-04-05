#include "sqwebmail.h"
#include "pref.h"
#include <courier-unicode.h>
#include <iostream>
#include <optional>

const char *sqwebmail_content_charset=unicode::utf_8;

std::string savedconfigfile;

std::optional<std::string> read_sqconfig(const char *dir, const char *configfile, time_t *mtime)
{
	return savedconfigfile;
}

void write_sqconfig(const char *dir, const char *configfile, const char *val)
{
	savedconfigfile=val;
}

void validate(const char *expected)
{
	if (savedconfigfile != expected)
	{
		std::cerr << "Wrong config: " << savedconfigfile << " != " << expected << std::endl;
		exit(1);
	}
}

int main(int argc, char **argv)
{
	pref_init();
	pref_update();
	validate("SORT=D PAGESIZE=10 AUTOPURGE=7 NOFLOWEDTEXT=0 NOARCHIVE=0 NOAUTORENAMESENT=0 STARTOFWEEK=0 FROM= LDAP=");
	pref_flagisoldest1st=1;
	pref_update();
	validate("SORT=D PAGESIZE=10 AUTOPURGE=7 NOFLOWEDTEXT=0 NOARCHIVE=0 NOAUTORENAMESENT=0 STARTOFWEEK=0 OLDEST1ST FROM= LDAP=");

	pref_flagisoldest1st=0;
	pref_autopurge=30;
	pref_update();
	validate("SORT=D PAGESIZE=10 AUTOPURGE=30 NOFLOWEDTEXT=0 NOARCHIVE=0 NOAUTORENAMESENT=0 STARTOFWEEK=0 FROM= LDAP=");

	pref_init();
	pref_update();
	validate("SORT=D PAGESIZE=10 AUTOPURGE=30 NOFLOWEDTEXT=0 NOARCHIVE=0 NOAUTORENAMESENT=0 STARTOFWEEK=0 FROM= LDAP=");

	pref_from="Hèllo <nobody@example.com>";
	pref_ldap="ldap://localhost";
	pref_update();
	validate("SORT=D PAGESIZE=10 AUTOPURGE=30 NOFLOWEDTEXT=0 NOARCHIVE=0 NOAUTORENAMESENT=0 STARTOFWEEK=0 FROM=H+C3+A8llo+20<nobody@example.com> LDAP=ldap://localhost");
	pref_init();

	if (pref_from != "Hèllo <nobody@example.com>")
	{
		std::cerr << "Wrong decoding of pref_from: " << pref_from << std::endl;
		exit(1);
	}
	if (pref_ldap != "ldap://localhost")
	{
		std::cerr << "Wrong decoding of pref_ldap: " << pref_ldap << std::endl;
		exit(1);
	}

	return 0;
}
