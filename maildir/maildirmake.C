/*
** Copyright 1998 - 2026 S. Varshavchik.
** See COPYING for distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<ctype.h>
#include	<errno.h>

#include	"rfc822/rfc822.h"
#include	"maildircreate.h"
#include	"maildirmisc.h"
#include	"maildirinfo.h"
#include	"maildirsharedrc.h"
#include	"maildirquota.h"
#include	"maildirfilter.h"
#include	<courier-unicode.h>

#include	<string>
#include	<string_view>
#include	<vector>
#include	<fstream>

static void usage()
{
	printf("Usage: maildirmake [ options ] maildir\n");
	exit(1);
}

static void add(const std::string &dir, const std::string &name)
{
	auto equals=name.find('=');

	if (equals == name.npos)
		usage();

	auto s=name.substr(equals+1);
	std::string c=name.substr(0, equals);

	if (*s.c_str() != '/')
		usage();
	if (access(s.c_str(), R_OK))
	{
		perror(s.c_str());
		exit(1);
	}

	if (c.find('.') != c.npos || c.find('/') != c.npos)
	{
		fprintf(stderr, "%s: invalid name\n", c.c_str());
		exit(1);
	}

	if (chdir(dir.c_str()))
	{
		perror(dir.c_str());
		exit(1);
	}

	std::string line;

#if TESTMAILDIRMAKE

#else
	std::ifstream in(MAILDIRSHAREDRC);
	if (in)
	{
		while (std::getline(in, line))
		{
			auto [namep, dirp]=maildir::shared_fparse(line);

			if (!namep.empty())
			{
				if (namep == c)
				{
					fprintf(stderr,
						"%s: already defined as a sharable maildir in %s.\n",
						namep.c_str(), MAILDIRSHAREDRC);
					exit(2);
				}
			}
		}
	}
#endif
	std::fstream out{
		maildir::shared_filename("."),
		std::ios::in | std::ios::out | std::ios::app
	};
	if (!out)
	{
		perror(dir.c_str());
		exit(1);
	}

	while (std::getline(out, line))
	{
		auto [namep, dirp]=maildir::shared_fparse(line);

		if (namep.empty())
			continue;
		if (namep == c)
		{
			fprintf(stderr, "%s: already defined as a sharable maildir.\n",
				namep.c_str());
			exit(2);
		}
	}
	out.clear();
	out << c << "\t" << s << "\n";
	out.flush();
	if (out.fail())
	{
		perror(dir.c_str());
		exit(1);
	}
	exit(0);
}

#include <iostream>

static void del(const std::string &dir, const std::string &n)
{
	if (chdir(dir.c_str()))
	{
		perror(dir.c_str());
		exit(1);
	}

	std::ifstream fp{maildir::shared_filename(".")};
	std::string line;

	if (!fp)
	{
		perror(dir.c_str());
		exit(1);
	}

	maildir::tmpcreate_info createInfo;

	createInfo.uniq="shared";
	createInfo.doordie=true;

	rfc822::fdstreambuf sb{createInfo.fd()};
	if (sb.error())
	{
		perror(dir.c_str());
		exit(1);
	}
	std::ostream fp2{&sb};

	bool found=false;
	while (std::getline(fp, line))
	{
		auto [namep, dirp]=maildir::shared_fparse(line);

		if (namep == n)
		{
			found=true;
			continue;
		}
		fp2 << line << '\n';
	}

	fp2.flush();
	if (fp2.fail() || sb.error()
	    || (sb={},
		rename(createInfo.tmpname.c_str(), "shared-maildirs") < 0))
	{
		perror(createInfo.tmpname.c_str());
		unlink(createInfo.tmpname.c_str());
		exit(1);
	}
	if (!found)
	{
		fprintf(stderr, "%s: not found.\n", n.c_str());
		exit(1);
	}
	exit(0);
}

/*****************************************************************************

Convert modified-UTF7 folder names to UTF-8 (sort-of).

*****************************************************************************/

struct convertutf8_list {
	std::string rename_from;
	std::string rename_to;
};

struct convertutf8_status {
	std::vector<convertutf8_list> list;
	bool error=false;
};

/* Find folders that need to change */

static void convertutf8_build_list(const std::string &inbox_name,
	struct convertutf8_status *status)
{
	auto converted=maildir::imap_foldername_to_filename(false, inbox_name);

	if (converted.empty())
	{
		fprintf(stderr,
			"Error: %s: does not appear to be valid"
			" modified-UTF7\n",
			inbox_name.c_str());
		status->error=true;
		return;
	}

	if (converted == inbox_name)
	{
		return;
	}
	status->list.push_back({inbox_name, converted});
}

void convertutf8(const char *maildir, const char *mailfilter, int doit)
{
	struct convertutf8_status status;
	std::string courierimapsubscribed;
	std::string courierimapsubscribed_new;
	maildirfilter mf;
	int mf_status;
	std::string mailfilter_newname;

	printf("Checking %s:\n", maildir);

	maildir::list(maildir,
		      [&](const std::string &folder)
		      {
				convertutf8_build_list(folder, &status);
		      });

	if (status.error)
		exit(1);

	if (mailfilter)
	{
		/*
		** Try to convert folder references from mailfilter
		*/

		mailfilter_newname.reserve(strlen(mailfilter)+10);
		mailfilter_newname=mailfilter;
		mailfilter_newname += ".new";
		mf_status=maildir_filter_loadrules(mf, mailfilter);

		if (mf_status != MF_LOADOK && mf_status != MF_LOADNOTFOUND)
		{
			fprintf(stderr, "Error: cannot load %s\n",
				mailfilter);
		}

		for (auto &r: mf)
		{
			std::string converted;

			/* Look for deliveries to a folder */

			if (std::string_view{r.tofolder}.substr(0, 6) != "INBOX.")
				continue;

			converted=maildir::imap_foldername_to_filename(
				false,
				r.tofolder
			);

			if (converted.empty())
			{
				fprintf(stderr, "Error: %s: "
					"%s: does not appear to be valid "
					"modified-UTF7\n",
					mailfilter,
					r.tofolder.c_str());
				status.error=true;
			}

			if (converted == r.tofolder)
			{
				continue;
			}

			printf("Mail filter to %s updated to %s\n",
			       r.tofolder.c_str(), converted.c_str());

			r.tofolder=converted;
		}

		if (mf_status == MF_LOADOK)
		{
			std::ifstream fp(mailfilter);
			std::string detected_from;
			std::string detected_tomaildir;
			std::string buffer;
			struct stat st_buf;

			/*
			** We need to know the FROM address and the MAILDIR
			** reference. Attempt to ham-fistedly parse the
			** current filter file.
			*/

			detected_from.clear();
			detected_tomaildir.clear();

			if (!fp)
			{
				perror(mailfilter);
				exit(1);
			}

			while (std::getline(fp, buffer))
			{
				char *p, *q;

				if (buffer.find("FROM='") == 0)
				{
					size_t slash=buffer.find('\'', 6);

					if (slash == std::string::npos)
					{
						fprintf(stderr,
							"Cannot parse %s\n",
							mailfilter);
						status.error=true;
					}
					else
					{
						detected_from=std::string{
							buffer.data()+6,
							slash-6
						};

						/*
						  Unescape, because saverules()
						  escapes it.
						*/

						for (q=p=
							detected_from.data();
							 *p; p++)
						{
							if (*p == '\\' && p[1])
								++p;
							*q=*p;
							++q;
						}
						detected_from.resize(
							q-detected_from.data()
						);
					}
				}

				if (buffer.find("to \"") == 0)
				{
					size_t p=buffer.find("/.\"");

					if (p == std::string::npos)
					{
						fprintf(stderr,
							"Cannot parse %s\n",
							mailfilter);
						status.error=true;
					}
					else
					{
						detected_tomaildir=std::string{
							buffer.data()+4,
							p-4
						};
					}
				}
			}
			if (detected_from.empty() ||
			    detected_tomaildir.empty())
			{
				fprintf(stderr,
					"Failed to parse %s\n",
					mailfilter);
				status.error=true;
			}
			maildir_filter_saverules(mf, mailfilter_newname,
						 detected_tomaildir,
						 detected_from);

			if (stat(mailfilter, &st_buf) ||
			    chmod(mailfilter_newname.c_str(), st_buf.st_mode))
			{
				perror(mailfilter);
				exit(1);
			}
			/*
			** If we're root, preserve the ownership and permission
			*/
			if (geteuid() == 0)
			{
				if (chown(mailfilter_newname.c_str(),
						st_buf.st_uid,
						st_buf.st_gid))
				{
					perror(mailfilter_newname.c_str());
					exit(1);
				}
			}
		}
	}
	else
	{
		mf_status=MF_LOADNOTFOUND;
	}

	courierimapsubscribed.reserve(strlen(maildir)+100);
	courierimapsubscribed_new.reserve(strlen(maildir)+100);

	courierimapsubscribed=maildir;
	courierimapsubscribed_new=maildir;

	courierimapsubscribed += "/courierimapsubscribed";
	courierimapsubscribed_new += "/courierimapsubscribed.new";

	/*
	** Update folder references in the IMAP subscription file.
	*/

	std::ifstream courierimapsubscribed_fp(courierimapsubscribed.c_str());
	bool verified_subscribed=false;
	if (courierimapsubscribed_fp.is_open())
	{
		std::ofstream new_fp(courierimapsubscribed_new.c_str());
		if (!new_fp.is_open())
		{
			perror(courierimapsubscribed_new.c_str());
			exit(1);
		}

		std::string buffer;
		struct stat st_buf;

		while (std::getline(courierimapsubscribed_fp, buffer))
		{
			auto converted=maildir::imap_foldername_to_filename(
				false,
				buffer
			);

			if (converted.empty())
			{
				fprintf(stderr, "Error: %s: %s: does not appear to be "
					"valid modified-UTF7\n",
					courierimapsubscribed.c_str(),
					buffer.c_str());
				status.error=true;
				new_fp << buffer << '\n';
				continue;
			}
			new_fp << converted << '\n';

			if (buffer != converted)
			{
				printf("Subscription to %s changed to %s\n",
				       buffer.c_str(), converted.c_str());
			}
		}

		if (status.error)
		{
			new_fp.close();
			unlink(courierimapsubscribed_new.c_str());
			exit(1);
		}

		new_fp.close();
		courierimapsubscribed_fp.close();
		verified_subscribed=true;
		if (stat(courierimapsubscribed.c_str(), &st_buf) ||
		    chmod(courierimapsubscribed_new.c_str(), st_buf.st_mode))
		{
			perror(courierimapsubscribed.c_str());
			exit(1);
		}

		/*
		** If we're root, preserve the ownership and permission
		*/
		if (geteuid() == 0)
		{
			if (chown(courierimapsubscribed_new.c_str(),
					st_buf.st_uid,
					st_buf.st_gid))
			{
				perror(courierimapsubscribed_new.c_str());
				exit(1);
			}
		}
	}
	else
	{
		std::ofstream new_fp(courierimapsubscribed.c_str());

		if (!new_fp.is_open())
		{
			perror(courierimapsubscribed.c_str());
			exit(1);
		}
		new_fp.close();
	}

	if (status.error)
	{
		unlink(courierimapsubscribed_new.c_str());
		if (mf_status == MF_LOADOK)
			unlink(mailfilter_newname.c_str());
		exit(1);
	}

	for (auto &list : status.list)
	{
		printf("Rename %s to %s\n",
			list.rename_from.c_str(),
			list.rename_to.c_str());

		std::string frompath, topath;

		frompath.reserve(
			std::string_view{maildir}.size() + list.rename_from.size() + 2
		);
		topath.reserve(
			std::string_view{maildir}.size() + list.rename_to.size() + 2
		);

		frompath=maildir;
		topath=maildir;

		frompath += "/";
		frompath += std::string_view{list.rename_from}.substr(
			list.rename_from.find('.')
		);
		topath += "/";
		topath += std::string_view{list.rename_to}.substr(
			list.rename_to.find('.')
		);

		if (doit)
		{
			if (rename(frompath.c_str(), topath.c_str()))
			{
				fprintf(stderr,
					"FATAL ERROR RENAMING %s to %s: %s\n",
					frompath.c_str(), topath.c_str(),
					strerror(errno));
				status.error=true;
			}
		}
	}

	if (doit)
	{
		if (verified_subscribed)
		{
			printf("Updating %s\n", courierimapsubscribed.c_str());

			if (rename(courierimapsubscribed_new.c_str(),
				   courierimapsubscribed.c_str()))
			{
				fprintf(stderr,
					"FATAL ERROR RENAMING %s to %s: %s\n",
					courierimapsubscribed_new.c_str(),
					courierimapsubscribed.c_str(),
					strerror(errno));
				status.error=true;
			}
		}

		if (mf_status == MF_LOADOK)
		{
			printf("Updating %s\n", mailfilter);

			if (rename(mailfilter_newname.c_str(),
				   mailfilter))
			{
				fprintf(stderr,
					"FATAL ERROR RENAMING %s to %s: %s\n",
					mailfilter_newname.c_str(),
					mailfilter,
					strerror(errno));
				status.error=true;
			}
		}
	}
	else
	{
		if (verified_subscribed)
		{
			printf("Verified %s\n", courierimapsubscribed.c_str());
		}

		if (mf_status == MF_LOADOK)
		{
			printf("Verified %s\n", mailfilter);
		}
	}
	exit(status.error ? 1:0);
}

int main(int argc, char *argv[])
{
std::string maildir, folder;
int	argn;
int	perm=0700;
int	musthavefolder=0;
int	subdirperm;
char	*addshared=0, *delshared=0;
const char *quota=0;

	for (argn=1; argn<argc; argn++)
	{
		if (argv[argn][0] != '-')	break;
		if (strcmp(argv[argn], "-") == 0)	break;
		if (strncmp(argv[argn], "-f", 2) == 0)
		{
			folder=argv[argn]+2;
			if (folder.empty() && argn+1 < argc)
				folder=argv[++argn];
			continue;
		}
		if (strncmp(argv[argn], "-F", 2) == 0)
		{
			int converr;

			const char *p=argv[argn]+2;

			if (*p == 0 && argn+1 < argc)
				p=argv[++argn];

			folder=unicode_convert_tobuf(p,
						       unicode_default_chset(),
						       unicode_x_smap_modutf8,
						       &converr);

			if (converr || folder.empty())
			{
				fprintf(stderr, "Cannot convert %s to maildir encoding\n",
					p);
				exit(1);
			}
			continue;
		}
		if (strcmp(argv[argn], "-S") == 0)
		{
			perm=0755;
			continue;
		}

		if (strncmp(argv[argn], "-s", 2) == 0)
		{
		const char *p=argv[argn]+2;

			if (*p == 0 && argn+1 < argc)
				p=argv[++argn];

			perm=0755;
			for (; *p; ++p)
			{
				if (isspace((int)(unsigned char)*p) ||
					*p == ',')
					continue;
				if (*p == 'r')
					perm=0755;
				else if (*p == 'w')
					perm=0777 | S_ISVTX;
				else if (*p == 'g')
					perm &= ~0007;

				while (*p && !isspace((int)(unsigned char)*p)
					&& *p != ',')
					++p;
				if (!*p)	break;
			}
			musthavefolder=1;
			continue;
		}

		if (strncmp(argv[argn], "-q", 2) == 0)
		{
			const char *p=argv[argn]+2;

			if (*p == 0 && argn+1 < argc)
				p=argv[++argn];

			quota=p;
			continue;
		}

		if (strcmp(argv[argn], "--add") == 0 && argc-argn > 1)
		{
			addshared=argv[++argn];
			continue;
		}

		if (strcmp(argv[argn], "--del") == 0 && argc-argn > 1)
		{
			delshared=argv[++argn];
			continue;
		}

		if (strcmp(argv[argn], "--convutf8") == 0 && argc-argn > 1)
		{
			auto maildir=argv[++argn];
			auto mailfilter=argn < argc ? argv[++argn]:0;
			convertutf8(maildir, mailfilter, 1);
			exit(0);
		}

		if (strcmp(argv[argn], "--checkutf8") == 0 && argc-argn > 1)
		{
			auto maildir=argv[++argn];
			auto mailfilter=argn < argc ? argv[++argn]:0;
			convertutf8(maildir, mailfilter, 0);
			exit(0);
		}
		usage();
	}

	if (argn == argc)	usage();
	maildir=argv[argn];

	if (addshared)
	{
		add(maildir, addshared);
		exit (0);
	}

	if (delshared)
	{
		del(maildir, delshared);
		exit (0);
	}

	if (!folder.empty() && folder[0] == '.')
	{
		printf("Invalid folder name: %s\n", folder.c_str());
		exit(1);
	}

	if (!folder.empty())
	{
		maildir=maildir::folderdir(maildir, folder);
	}
	else	if (musthavefolder)
		usage();

	if (quota)
	{
		struct stat stat_buf;

		if (stat(maildir.c_str(), &stat_buf) < 0 && errno == ENOENT)
		{
			if (!maildir::make(maildir, perm & ~0022,
					 (perm & ~0022),
					 false))
			{
				perror(maildir.c_str());
				exit(1);
			}
		}

		maildir_quota_set(maildir.c_str(), quota);
		exit(0);
	}
	subdirperm=perm;
	if (folder.empty())	subdirperm=0700;
	umask(0);
	if (!maildir::make(maildir, perm & ~0022, subdirperm, !folder.empty()))
	{
		perror(("maildirmake: " + maildir).c_str());
		exit(1);
	}
	exit(0);
	return (0);
}
