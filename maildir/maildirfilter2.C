/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildirfilter.h"
#include	"maildirfilterconfig.h"
#include	"maildircreate.h"
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include        "rfc822/rfc822.h"
#include	<errno.h>
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include <fstream>

static std::string maildir_filter_config(std::string_view maildir,
	std::string_view varname)
{
	std::string line;
	std::string filename;

	filename.reserve(maildir.size() + sizeof("/maildirfilterconfig")-1);
	filename.append(maildir);
	filename.append("/maildirfilterconfig");
	std::ifstream f{filename};
	if (!f)
	{
		f.open(MAILDIRFILTERCONFIG);
		if (!f)
			return "";
	}

	while (std::getline(f, line))
	{
		auto equals=line.find('=');
		if (equals == line.npos)
			continue;
		auto name=std::string_view{line}.substr(0, equals);
		if (name == varname)
		{
			return (line.substr(equals+1));
		}
	}
	return ("");
}

static std::string maildir_filter_config_maildirfilter(std::string_view maildir)
{
	std::string p=maildir_filter_config(maildir, "MAILDIRFILTER");

	if (p.empty())
	{
		errno=ENOENT;
		return "";
	}

	std::string q;
	if (*p.c_str() != '/')
	{
		q.append(maildir);
		q.append("/");
	}
	q.append(p);
	return (q);
}

static std::string maildir_filter_tmpname(std::string_view maildir)
{
	std::string newname;

	newname.reserve(maildir.size() + sizeof("/maildirfilter.tmp")-1);
	newname.append(maildir);
	newname.append("/maildirfilter.tmp");
	return (newname);
}

bool maildir::filter::import(std::string_view maildir)
{
	std::string p=maildir_filter_config(maildir, "MAILDIRFILTER");
	std::string maildirfiltername=maildir_filter_config_maildirfilter(maildir);
	std::ifstream i;
	maildir::tmpcreate_info createInfo;

	if (p.empty())
	{
		errno=ENOENT;
		return false;
	}

	if (maildirfiltername.empty())
	{
		return false;
	}

	createInfo.maildir=maildir;
	createInfo.uniq="maildirfilter-tmp";
	createInfo.doordie=true;

	rfc822::fdstreambuf o{
		createInfo.fd()
	};

	if (o.error())
		return false;

	createInfo.newname=maildir_filter_tmpname(maildir);

	i.open(maildirfiltername);

	if (!i)
	{
		maildirfilter mf;

		if (errno != ENOENT)
		{
			o={};
			unlink(createInfo.tmpname.c_str());
			return false;
		}

		o={};
		unlink(createInfo.tmpname.c_str());
		unlink(createInfo.newname.c_str());
		save(mf, maildir, "");
		/* write out a blank one */
	}
	else
	{
		std::ostream os{&o};

		os << i.rdbuf();
		o.pubsync();

		if (os.fail() || o.error())
		{
			o={};
			unlink(createInfo.tmpname.c_str());
			return false;
		}
		o={};
		if (chmod(createInfo.tmpname.c_str(), 0600)
		    || rename(createInfo.tmpname.c_str(), createInfo.newname.c_str()))
		{
			unlink(createInfo.tmpname.c_str());
			return false;
		}
	}

	return true;
}

bool maildir::filter::load(
	maildirfilter &mf,
	std::string_view maildir
)
{
	std::string newname=maildir_filter_tmpname(maildir);
	int	rc;

	rc=maildir_filter_loadrules(mf, newname);
	if (rc && rc != MF_LOADNOTFOUND)
		return false;

	return true;
}


bool maildir::filter::save(
	const maildirfilter &mf,
	std::string_view maildir,
	std::string_view from
)
{
	auto maildirpath=maildir_filter_config(maildir, "MAILDIR");
	maildir::tmpcreate_info createInfo;
	int fd;

	if (maildirpath.empty())
	{
		errno=EINVAL;
		return (false);
	}

	createInfo.maildir=maildir;
	createInfo.uniq="maildirfilter-tmp";
	createInfo.doordie=true;

	if ((fd=createInfo.fd()) < 0)
		return false;

	close(fd);
	unlink(createInfo.tmpname.c_str());

	createInfo.newname=maildir_filter_tmpname(maildir);

	if (!maildir_filter_saverules(
			mf,
			createInfo.tmpname,
			maildirpath,
			from
		)
	)
		return false;
	if (rename(createInfo.tmpname.c_str(), createInfo.newname.c_str()))
		return false;
	return true;
}

bool maildir::filter::commit(std::string_view maildir)
{
	auto maildirfilter=maildir_filter_config_maildirfilter(maildir);

	if (maildirfilter.empty())	return (false);

	std::string newname=maildir_filter_tmpname(maildir);
	return (rename(newname.c_str(), maildirfilter.c_str()) == 0);
}

bool maildir::filter::has(std::string_view maildir)
{
	auto maildirpath=maildir_filter_config(maildir, "MAILDIR");
	if (maildirpath.empty())	return (false);

	auto maildirfilter=maildir_filter_config(maildir, "MAILDIRFILTER");
	if (maildirfilter.empty())	return (false);
	return (true);
}

void maildir::filter::cancel(std::string_view maildir)
{
	auto maildirfilter=maildir_filter_config_maildirfilter(maildir);
	if (maildirfilter.empty())	return;

	std::string newname=maildir_filter_tmpname(maildir);
	unlink(newname.c_str());
}
