/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/
#include	"config.h"
#include	"maildircache.h"
#include	<filesystem>
#include	<map>
#include	<fstream>
#include	<string>
#include	<iostream>
#include	<sstream>
#include	<pwd.h>
#include	<unistd.h>

#define	UNIT_TEST 1
#include	"maildircache.C"

std::string dumpmaildircache()
{
	std::map<std::string, std::string> v;
	
	for (auto &e: std::filesystem::recursive_directory_iterator(
		"testmaildircache.dir"
	))
	{
		if (!e.is_regular_file())
			continue;

		std::ifstream i{e.path().string()};

		v[e.path().string()]=std::string{
			std::istreambuf_iterator<char>(i),
			std::istreambuf_iterator<char>()
		};
	}
	std::string s;
	for (auto &e: v)
		s+=e.first+"\n"+e.second+"\n\n";
	return s;
}

static bool search_cb(uid_t uid, gid_t gid, const std::string &homedir,
	std::string &s)
{
	std::ostringstream o;
	o << uid << " " << gid << " " << homedir;

	s=o.str();
	return true;
}

int main(int argc, char **argv)
{
	std::string me=getpwuid(geteuid())->pw_name;
	std::error_code ec;

	std::filesystem::remove_all("testmaildircache.dir", ec);
	std::filesystem::create_directory("testmaildircache.dir", ec);

	const char *a[] = {"AUTHUSER", NULL};

	maildir_cache_init(100, "testmaildircache.dir", me.c_str(), a);

	maildir_cache_start();
	setenv("AUTHUSER", "nobody1@example.com", 1);
	maildir_cache_save("nobody1", 20, "/home/nobody1", 999, 999);
	maildir_cache_start();
	setenv("AUTHUSER", "nobody2@example.com", 1);
	maildir_cache_save("nobody2", 50, "/home/nobody2", 999, 999);
	maildir_cache_start();
	setenv("AUTHUSER", "nobody3@example.com", 1);
	maildir_cache_save("nobody3", 150, "/home/nobody3", 999, 999);

	setenv("AUTHUSER", "", 1);

	std::ostringstream output;
	output << dumpmaildircache();

	std::string s;

	maildir_cache_search("nobody1", 20,
		[&](uid_t uid, gid_t gid, const std::string &homedir)
		{
			return search_cb(uid, gid, homedir, s);
		});
	output << s << " " << getenv("AUTHUSER") << "\n";

	maildir_cache_search("nobody2", 50,
		[&](uid_t uid, gid_t gid, const std::string &homedir)
		{
			return search_cb(uid, gid, homedir, s);
		});
	output << s << " " << getenv("AUTHUSER") << "\n";

	maildir_cache_search("nobody3", 150,
		[&](uid_t uid, gid_t gid, const std::string &homedir)
		{
			return search_cb(uid, gid, homedir, s);
		});
	output << s << " " << getenv("AUTHUSER") << "\n";

	output << "---\n";
	maildir_cache_purge(250);
	output << dumpmaildircache();
	std::filesystem::remove_all("testmaildircache.dir", ec);

	std::cout << output.str();
	return 0;
}