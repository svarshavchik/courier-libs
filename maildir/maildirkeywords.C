/*
** Copyright 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<ctype.h>
#include	<unistd.h>
#include	<fstream>
#include	"maildirkeywords.h"
#include	"maildircreate.h"
#include	"maildirwatch.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirwatch.h"
#include	"numlib/numlib.h"
#if	HAVE_UTIME_H
#include	<utime.h>
#endif
#if TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
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
#include <unordered_map>
#include <vector>
#include <algorithm>

const char *mail::keywords::verbotten_chars=nullptr;

// To lowercase map.

static const unsigned char lowermap[256]={
	0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
	32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
	48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
	64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
	80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
	96,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
	80,81,82,83,84,85,86,87,88,89,90,123,124,125,126,127,
	128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
	144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
	160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
	176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
	192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
	208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
	224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
	240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};

size_t mail::keywords::keywordhash::operator()(std::string s) const
{
	for (char &c:s)
	{
		c=lowermap[(unsigned char)c];
	}
	return std::hash<std::string>{}(s);
}

bool mail::keywords::keywordeq::operator()(const std::string &a,
					   const std::string &b) const
{
	size_t n=a.size();

	if (n != b.size())
		return false;

	for (size_t i=0; i<n; i++)
	{
		if (lowermap[(unsigned char)a[i]] !=
		    lowermap[(unsigned char)b[i]])
		{
			return false;
		}
	}
	return true;
}

bool mail::keywords::save_keywords_from(
	const std::string &maildir,
	const std::string &filename,
	const std::function<void (FILE *)> &saver,
	std::string &tmpname,
	std::string &newname,
	bool try_atomic)
{
	maildir::tmpcreate_info create_info;

	newname.reserve(maildir.size()+filename.size()+10+
			sizeof(KEYWORDDIR));

	newname=maildir;

	newname += "/" KEYWORDDIR "/";

	auto p=newname.size();

	newname += filename;

	p=newname.find(MDIRSEP[0], p);

	if (p != newname.npos)
		newname.resize(p);

	create_info.maildir=maildir;

	const char *hostname=getenv("HOSTNAME");
	if (!hostname) hostname="";
	create_info.hostname=hostname;

	auto fp=create_info.fp();

	if (!fp)
		return false;

	saver(fp);

	errno=EIO;

	if (fflush(fp) < 0 || ferror(fp) || fsync(fileno(fp)) < 0)
	{
		fclose(fp);
		unlink(create_info.tmpname.c_str());
		return false;
	}

	fclose(fp);

	tmpname=create_info.tmpname;

	if (try_atomic)
	{
		char timeBuf[NUMBUFSIZE];

		std::string n;

		n.reserve(tmpname.size()
			  + sizeof(KEYWORDDIR) + NUMBUFSIZE+10);

		n=maildir;

		n += "/" KEYWORDDIR "/.tmp.";

		n += libmail_str_time_t(time(NULL), timeBuf);

		n += tmpname.substr(tmpname.rfind('/')+1);

		if (rename( tmpname.c_str(), n.c_str()) < 0)
		{
			maildir::create_keyword_dir(maildir);

			if (rename(tmpname.c_str(), n.c_str()) < 0)
				return false;
		}
		tmpname=n;
	}
	return true;
}

void mail::keywords::read_keywords_from_file(
	std::istream &i,
	const std::function<void (const std::string &,
				  mail::keywords::list &keywords)> &set)
{
	// The first part of the keyword file, the index.

	std::vector<std::string> keyword_index;

	std::string s;

	// If the file doesn't exist we'll do nothing here, and hurry up
	// to the grand finale.

	// Read the saved list of keywords.

	while (std::getline(i, s))
	{
		if (s.empty())
			break;

		if (mail::keywords::verbotten_chars)
			std::transform(
				s.begin(),
				s.end(),
				s.begin(),
				[]
				(char c)
				{
					if (strchr(mail::keywords::verbotten_chars, c))
						c='_';
					return c;
				});
		keyword_index.push_back(s);
	}

	while (std::getline(i, s))
	{
		auto e=s.end();
		auto b=std::find(s.begin(), e, MDIRSEP[0]);

		// Read the saved message and it keywords.

		mail::keywords::list keywords;

		auto p=b;

		while (b != e)
		{
			++b;

			if (*b < '0' || *b > '9')
				continue;

			size_t n=0;

			while (b != e && *b >= '0' && *b <= '9')
			{
				n = n * 10 + *b - '0';

				++b;
			}
			if (n < keyword_index.size())
				keywords.insert(keyword_index[n]);
		}
		set(std::string{s.begin(), p}, keywords);
	}
}

namespace {
#if 0
}
#endif

// When attempting to load keywords, aged update files follow the naming
// convention ".N.messagename".
//
// Construct this filename.

std::string versioned_name(const std::string &keyworddir,
			   const std::string &messagename,
			   size_t version)
{
	char b[NUMBUFSIZE];

	libmail_str_size_t(version, b);

	std::string n;

	n.reserve(keyworddir.size() + messagename.size() + NUMBUFSIZE+4);

	n=keyworddir;
	n += "/.";
	n += b;
	n += ".";
	n += messagename;
	return n;
}

// Keeps track of each message as we load it.

struct messagestatus {

	size_t most_recent=0;	// The most recent aged updated that was found
	bool found_newest=false;	// The most recent update was found

	// Check if we have a more recent aged update.

	template<typename T>
	void found_aged(T &d,
			const std::string &messagename,
			size_t n)
	{
		if (!most_recent)
		{
			most_recent=n;
			return;
		}

		// We can safely remove 2nd most recent aged update

		if (most_recent < n)
		{
			d.remove(versioned_name(
					 d.keyworddir,
					 messagename,
					 most_recent
				 ));
			most_recent=n;
			return;
		}
		d.remove(versioned_name(
				 d.keyworddir,
				 messagename,
				 n
			 ));
		return;
	}

	mail::keywords::list keywords;
};

// RAII wrapper for DIR *.

struct update_dir {
	std::string keyworddir;
	DIR *dirp;

	update_dir(const std::string &keyworddir)
		: keyworddir{keyworddir},
		  dirp{opendir(keyworddir.c_str())}
	{
	}

	~update_dir()
	{
		if (dirp)
			closedir(dirp);
	}

	update_dir(const update_dir &)=delete;
	update_dir &operator=(const update_dir &)=delete;

	const char *next()
	{
		if (!dirp)
			return nullptr;

		auto p=readdir(dirp);

		if (!p)
			return nullptr;

		return p->d_name;
	}

	static void remove(const std::string &fullfilename)
	{
		unlink(fullfilename.c_str());
	}

	static void delete_if_aged(const std::string &fullfilename,
				   time_t now)
	{
		struct stat stat_buf;

		if (stat(fullfilename.c_str(), &stat_buf) == 0
		    && stat_buf.st_mtime < now-15 * 60)
		{
			unlink(fullfilename.c_str());
		}
	}

	typedef std::ifstream stream_type;

	static bool try_open(std::ifstream &i, const std::string &filename)
	{
		i.open(filename);
		return i.is_open();
	}

	static void rename_newest(const std::string &from,
				  const std::string &to)
	{
		utime(from.c_str(), nullptr);
		rename(from.c_str(), to.c_str());
	}
};

// Scan the keyword directory for ".N.message" update files, and find the
// most recent one for each message. Also find all "message" files.
//
// A unit test supplies as a mock update_dir_t

template<typename update_dir_t>
bool scan_updates(
	update_dir_t &d,

	// Current time
	time_t t,

	// Time-based chunk
	time_t tn,

	// Statuses get loaded here.
	std::unordered_map<std::string, messagestatus> &statuses,

	// load()'s set callback.
	const std::function<bool (const std::string,
				  mail::keywords::list &keywords)> &set,
	bool &save_required
)
{
	const char *p;

	while ((p=d.next()) != nullptr)
	{
		std::string name=p;

		// Skip ., .., and :lst

		if (name == "." ||
		    name == ".." ||
		    name == ":list")
			continue;

		std::string fullfilename;
		fullfilename.reserve(d.keyworddir.size() + name.size()+1);

		fullfilename=d.keyworddir;
		fullfilename += "/";
		fullfilename += name;

		// Now, figure out what to do with everything else.

		if (*p == '.')
		{
			// Parse .N.messagename.

			size_t n=0;

			char first_digit='1';

			while (*++p && *p != '.')
			{
				if (*p < first_digit || *p > '9')
					break;

				first_digit='0';
				size_t prev_n=n;
				n = n * 10 + (*p-'0');

				if (n < prev_n)
					break; // Not good.
			}

			if (n && *p == '.')
			{
				// Found .N.messagename.

				std::string messagename=++p;

				statuses[messagename].found_aged(
					d,
					messagename,
					n
				);
				continue;
			}
		}

		// If we do not recognize ".something", it could
		// be .tmp., so clean them up, eventually.
		if (*name.c_str() == '.')
		{
			d.delete_if_aged(fullfilename, t);
			continue;
		}

		statuses[name].found_newest=true;
		save_required=true;
	}

	// Now, read all the keyword update files.

	for (auto &status:statuses)
	{
		typename update_dir_t::stream_type i;

		std::string messagename=d.keyworddir + "/" + status.first;

		// Attempt to open it.

		bool opened;

		if (status.second.found_newest)
		{
			opened=d.try_open(i, messagename);
		}
		else if (status.second.most_recent)
		{
			opened=d.try_open(i,
					  versioned_name(
						  d.keyworddir,
						  status.first,
						  status.second.most_recent
					  ));
		}
		else continue; // Shouldn't happen.

#ifdef MAILDIRKW_MOCKTIME2
		MAILDIRKW_MOCKTIME2()
#endif

		// We should've opened the update file.

		if (!opened)
			return true; // We'll try again.

		// Load the updated_keywords.
		std::string keyword;

		while (std::getline(i, keyword))
		{
			if (mail::keywords::verbotten_chars)
				std::transform(
					keyword.begin(),
					keyword.end(),
					keyword.begin(),
					[]
					(char c)
					{
						if (strchr(mail::keywords::verbotten_chars,
							   c))
							c='_';
						return c;
					});
			if (!keyword.empty())
				status.second.keywords.insert(keyword);
		}
	}

	// Successfully read all keyword updates. Deliver them.

	for (auto &status:statuses)
	{
		if (!set(status.first, status.second.keywords))
			save_required=true;
	}

	return false;
}

// Now that all the updates have been read, we can read the main keyword :list.

static void read_keyword_list(
	std::istream &i,
	std::unordered_map<std::string, messagestatus> &updates,
	const std::function<bool (const std::string &,
				  mail::keywords::list &keywords)> &set,
	bool &save_required
)
{
	mail::keywords::read_keywords_from_file(
		i,
		[&]
		(const std::string &name,
		 mail::keywords::list &keywords)
		{
			if (keywords.empty())
			{
				// Shouldn't happen, zap it out.
				save_required=true;
				return;
			}

			// Did we already load an updated keyword list for
			// this one?

			auto iter=updates.find(name);

			if (iter != updates.end())
				return;

			if (!set(name, keywords))
				save_required=true;
		}
	);
}

template<typename T>
static void cleanup(T &&d,
		    std::unordered_map<std::string, messagestatus> &statuses,
		    time_t t,
		    time_t tn)
{
	for (auto &status:statuses)
	{
		if (status.second.found_newest)
		{
			size_t age=tn+1;

			if (age < status.second.most_recent)
				age=status.second.most_recent;

			d.rename_newest(d.keyworddir + "/" + status.first,
					versioned_name(
						d.keyworddir,
						status.first,
						age));
			continue;
		}

		d.delete_if_aged(versioned_name(
					 d.keyworddir,
					 status.first,
					 status.second.most_recent), t);
	}
}

// Atempt to load keywords. Returns true if a race condition is detected,
// and we'll try again.

static bool attempt_load(
	const std::string &maildir,
	const std::function<bool (const std::string &,
				  mail::keywords::list &keywords)> &set,
	const std::function<bool ()> &end_of_keywords,
	const std::function<void (FILE *)> &save)
{
	// Construct a lookup from each filename to its index number.

	std::unordered_map<std::string, messagestatus> statuses;

	time_t t=time(NULL);
	time_t tn;
#ifdef MAILDIRKW_MOCKTIME
	MAILDIRKW_MOCKTIME();
#endif

	tn=t/300;

	std::string keyworddir=maildir + "/" KEYWORDDIR;

	update_dir d{keyworddir};

	// Scan keyword directory for updates.

	if (!d.dirp)
		// Maybe it's time to create it.
		maildir::create_keyword_dir(maildir, d.keyworddir);

	bool save_required=false;

	if (scan_updates(d, t, tn, statuses, set, save_required))
		return true;

	std::ifstream i{(keyworddir + "/:list").c_str()};
	read_keyword_list(i, statuses, set, save_required);
	i.close();

	if (end_of_keywords())
		save_required=true;

	if (save_required)
	{
		std::string tmpname;
		std::string newname;

		if (!mail::keywords::save_keywords_from(maildir, "", save,
							tmpname,
							newname, false) ||
		    rename(tmpname.c_str(),
			   (keyworddir + "/:list").c_str()) < 0)
			throw std::runtime_error(
				"An error occurred while saving keywords");
	}

	cleanup(d, statuses, t, tn);

	return false;
}
#if 0
{
#endif
}

// Helper called from load(), to save a new keyword list.
//
// Go through all the loaded keywords, save them as the keyword index.
//
// As we save them, they get numbered at the same time.

mail::keywords::save_keywords::save_keywords(
	std::unordered_map<std::string, std::string> &lookup,
	FILE *fp)
	: lookup{lookup}, fp{fp}
{
	size_t n=0;
	char buf[NUMBUFSIZE];

	for (auto &kw:lookup)
	{
		libmail_str_size_t(n++, buf);
		kw.second=buf;

		fprintf(fp, "%s\n", kw.first.c_str());
	}
	fprintf(fp, "\n");
}

// Now save each message's keywords, one at a time.

void mail::keywords::save_keywords::operator()(std::string &filename,
					       const list &keywords)
{
	if (keywords.empty())
		return;

	auto p=filename.rfind(MDIRSEP[0]);

	if (p != filename.npos)
		filename.resize(p);

	fprintf(fp, "%s:", filename.c_str());

	const char *sep="";

	for (auto &kw:keywords)
	{
		auto iter=lookup.find(kw);

		if (iter == lookup.end())
			throw std::runtime_error("unexpected keyword");

		fprintf(fp, "%s%s", sep, iter->second.c_str());
		sep=" ";
	}
	fprintf(fp, "\n");
}

void mail::keywords::load_impl(
	const std::string &maildir,
	const std::function<bool (const std::string &,
				  mail::keywords::list &keywords)> &set,
	const std::function<bool ()> &end_of_keywords,
	const std::function<void (FILE *)> &save)
{
	size_t i=0;

	while (attempt_load(maildir, set, end_of_keywords, save))
	{
		if (++i > 1000)
			throw std::runtime_error(
				"internal error - unable to load message "
				"keywords"
			);
	}
}

bool mail::keywords::update(const std::string &maildir,
			    const std::string &filename,
			    const list &keywords)
{
	std::string tmpname;
	std::string newname;

	if (!save_keywords_from(
		    maildir,
		    filename,
		    [&]
		    (FILE *fp)
		    {
			    for (auto &keyword:keywords)
				    fprintf(fp, "%s\n", keyword.c_str());
		    },
		    tmpname,
		    newname,
		    true))
	{
		throw std::runtime_error(
			"error saving an updated keyword list");
	}

	struct stat stat_buf;

	if (link(tmpname.c_str(), newname.c_str()) < 0 ||
	    stat(tmpname.c_str(), &stat_buf) < 0 ||
	    stat_buf.st_nlink != 2)
	{
		unlink(tmpname.c_str());
		return false;
	}
	unlink(tmpname.c_str());
	return true;
}
