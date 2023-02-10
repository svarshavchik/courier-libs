#include "config.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"funcs.h"
#include	"maildrop.h"

#include	<string.h>
#include	<unordered_map>

std::unordered_map<std::string, std::string> varlist;

void UnsetVar(const std::string &var)
{
	varlist.erase(var);

	if (var == "VERBOSE")
	{
		maildrop.verbose_level=0;
	}

	return;
}

void SetVar(const std::string &var, const std::string &value)
{
	varlist[var]=value;

	if (var == "VERBOSE")
	{
		maildrop.verbose_level= extract_int(value, "0");
		if (maildrop.isdelivery)	maildrop.verbose_level=0;
	}
}

std::string GetVar(const std::string &var)
{
	auto iter=varlist.find(var);

	if (iter != varlist.end())
		return iter->second;

	return "";
}

// Create environment for a child process.

void ExportEnv(std::vector<std::vector<char>> &strings,
	       std::vector<char *> &pts)
{
	strings.clear();
	strings.reserve(varlist.size());

	std::string v;

	for (auto &kv:varlist)
	{
		v.clear();
		v.reserve(kv.first.size()+kv.second.size()+2);

		v=kv.first;
		v += "=";
		v += kv.second;

		std::vector<char> new_buf;

		new_buf.reserve(v.size()+1);

		new_buf.insert(new_buf.end(), v.begin(), v.end());
		new_buf.push_back(0);
		strings.push_back(std::move(new_buf));
	}

	pts.clear();
	pts.reserve(strings.size()+1);

	for (auto &s:strings)
	{
		pts.push_back(s.data());
	}
	pts.push_back(nullptr);
}
