/*
** Copyright 2001-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"autoresponse.h"
#ifndef AUTORESPONSEQUOTA
#include	"autoresponsequota.h"
#endif
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<errno.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include <sys/types.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#include	<filesystem>
#include	<string_view>
#include	<fstream>

namespace {
	struct maildir_autoresponse_quota {
		unsigned files=0;
		unsigned long bytes=0;
	} ;
}

std::vector<std::string> mail::autoresponse::list(std::string_view maildir)
{
	std::vector<std::string> autoresponses;

	if (maildir.empty())
		maildir=".";

	std::string d;

	d.reserve(maildir.size()+sizeof("/autoresponses")-1);

	d=maildir;
	d += "/autoresponses";

	std::error_code ec;
	std::filesystem::directory_iterator dirp{d, ec};

	if (!ec)
	{
		std::filesystem::directory_iterator dire;

		for (; dirp != dire; ++dirp)
		{
			std::string filename=dirp->path().filename();

			if (filename.find('.') == filename.npos)
				autoresponses.push_back(std::move(filename));
		}
	}
	return autoresponses;
}

static std::string afilename(std::string_view maildir,
			     std::string_view filename)
{
	std::string p;

	if (maildir.empty())
		maildir=".";

	if (filename.find('.') != filename.npos
	    || filename.find('/') != filename.npos
	    || filename.find('\'') != filename.npos
	    || filename.find('\"') != filename.npos
	    || filename.find('*') != filename.npos
	    || filename.find('?') != filename.npos
	    || filename.find('[') != filename.npos
	    || filename.find(']') != filename.npos
	    || filename.find(' ') != filename.npos
	    || filename.find('\n') != filename.npos
	    || filename.find('\t') != filename.npos
	    || filename.find('\r') != filename.npos
	    || filename.find('~') != filename.npos
	    || filename.empty())
	{
		errno=EINVAL;
		return p;
	}

	p.reserve(maildir.size()+filename.size()+
		  sizeof("/autoresponses/"));

	p=maildir;
	p += "/autoresponses/";
	p += filename;
	return p;
}

bool mail::autoresponse::validate(std::string_view maildir,
				  std::string_view filename)
{
	auto p=afilename(maildir, filename);

	if (p.empty())
		return false;
	return true;
}

/* Delete autoreply scratch file (optionally the autoreply file itself) */

static void deletefiles(std::string_view dir,
			std::string_view autoreply, bool deleteall)
{
	std::error_code ec;
	std::filesystem::directory_iterator dirp{dir, ec};

	if (!ec)
	{
		std::filesystem::directory_iterator dire;

		for (; dirp != dire; ++dirp)
		{
			std::string filename_str=dirp->path().filename();

			std::string_view filename{filename_str};

			if (filename.size() < autoreply.size())
				continue;

			if (filename.substr(0, autoreply.size()) !=
			    autoreply)
				continue;

			if (filename.size() == autoreply.size())
			{
				if (!deleteall)
					continue;
			}
			else if (filename.at(autoreply.size()) != '.')
				continue;

			std::error_code ec;
			std::filesystem::remove(dirp->path(), ec);
		}
	}
}

void mail::autoresponse::remove(std::string_view maildir,
				std::string_view filename)
{
	auto p=afilename(maildir, filename);

	auto q_pos=p.rfind('/');

	if (q_pos == p.npos)
		return;

	deletefiles(p.substr(0, q_pos),
		    p.substr(q_pos+1), true);
}

static void read_quota(maildir_autoresponse_quota &q,
		       std::string_view f)
{
	std::filesystem::path fp{f};

	std::string buf;

	std::ifstream i{fp};

	if (!i)
		return;

	if (!std::getline(i, buf))
		return;

	q={};
	for (auto p=buf.c_str(); *p; )
	{
		if (*p == 'C')
		{
			q.files=0;
			for ( ++p; *p; ++p)
			{
				if (!isdigit((int)(unsigned char)*p))
					break;
				q.files=q.files * 10 + (*p-'0');
			}
			continue;
		}

		if (*p == 'S')
		{
			q.bytes=0;
			for ( ++p; *p; ++p)
			{
				if (!isdigit((int)(unsigned char)*p))
					break;
				q.bytes=q.bytes * 10 + (*p-'0');
			}
			continue;
		}
		++p;
	}
}

static void get_quota(maildir_autoresponse_quota &q, std::string_view maildir)
{
	read_quota(q, AUTORESPONSEQUOTA);

	if (maildir.empty())
		maildir=".";

	std::string p;

	p.reserve(maildir.size()+sizeof("/autoresponsesquota"));

	p=maildir;
	p+="/autoresponsesquota";

	read_quota(q, p);
}

static void add_quota(maildir_autoresponse_quota &q,
		      std::string_view file, int sign)
{
	std::error_code ec;

	auto s=std::filesystem::file_size(file, ec);

	if (ec)
		return;

	q.files += sign;
	q.bytes += ((long)s)*sign;
}

static bool calc_quota(maildir_autoresponse_quota &q,
		       std::string_view maildir)
{
	if (maildir.empty())
		maildir=".";

	std::string p;

	p.reserve(maildir.size()+sizeof("/autoresponses"));

	p=maildir;
	p+="/autoresponses";

	std::error_code ec;

	std::filesystem::directory_iterator dirp{p, ec};
	if (ec)
		return false;

	for (std::filesystem::directory_iterator dire; dirp != dire; ++dirp)
	{
		std::string f=dirp->path().filename();

		if (f.find('.') != f.npos)
			continue;

		p=dirp->path();
		add_quota(q, p, 1);
	}
	return (true);
}

static bool check_quota(maildir_autoresponse_quota &setquota,
			maildir_autoresponse_quota &newquota)
{
	if (setquota.files > 0 && newquota.files > setquota.files)
		return false;
	if (setquota.bytes > 0 && newquota.bytes > setquota.bytes)
		return false;
	return true;
}

bool mail::autoresponse::create(
	std::string_view maildir, std::string_view filename,
	std::function<void (std::ostream &)> creator
)
{
	auto p=afilename(maildir, filename);

	std::string ptmp;

	ptmp.reserve(p.size()+4);

	ptmp=p;
	ptmp += ".tmp";

	std::ofstream o;

	o.open(ptmp);
	if (!o.is_open())
	{
		/* Perhaps we need to create the autoresponse dir? */

		auto dir=p.substr(0, p.rfind('/'));
		mkdir(dir.c_str(), 0700);
		o.open(ptmp);

		if (!o.is_open())
			return false;
	}

	creator(o);

	o.close();

	if (!o)
		return false;

	maildir_autoresponse_quota set_quota, new_quota;

	get_quota(set_quota, maildir);

	if (calc_quota(new_quota, maildir))
	{
		add_quota(new_quota, p, -1);
		add_quota(new_quota, ptmp, 1);
		if (!check_quota(set_quota, new_quota))
		{
			unlink(ptmp.c_str());
			errno=ENOSPC;
			return false;
		}
	}

	rename(ptmp.c_str(), p.c_str());
	return true;
}

void mail::autoresponse::open(
	std::ifstream &i,
	std::string_view maildir, std::string_view filename
)
{
	auto p=afilename(maildir, filename);

	i.open(p);
}
