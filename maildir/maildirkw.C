/*
** Copyright 2003-2022 S. Varshavchik.
** See COPYING for distribution information.
*/


#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<errno.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include	"maildirkeywords.h"
#include	"maildirwatch.h"

#include	<string>
#include	<map>
#include	<iostream>
#include	<vector>

static void usage()
{
	printf("Usage: maildirkw [ options ] maildir [+/-]flag [+/-]flag...\n");
	exit(1);
}

static void load(const std::string &maildir,
		 mail::keywords::hashtable<std::string> &keywords,
		 std::map<std::string, mail::keywords::message<std::string>
		 > &messages)
{
	keywords.load(maildir,
		      []
		      (const std::string &filename)
		      {
			      return filename;
		      },
		      [&]
		      (const std::string &filename,
		       mail::keywords::list &list)
		      {
			      messages.emplace(
				      filename,
				      mail::keywords::message<std::string>{
					      keywords,
					      list,
					      filename
				      }
			      );
			      return true;
		      },
		      []
		      {
			      return false;
		      });
}

static bool doit_locked(const char *maildir,
			std::string filename,
			int plusminus,
			const mail::keywords::list &keywords)
{
	if (!plusminus)
		return mail::keywords::update(maildir, filename, keywords);

	auto p=filename.rfind(MDIRSEP[0]);

	if (p != filename.npos)
		filename.resize(p);

	mail::keywords::hashtable<std::string> existing_keywords;
	std::map<std::string, mail::keywords::message<std::string>> messages;

	load(maildir, existing_keywords, messages);

	auto new_keywords=messages[filename].keywords();

	if (plusminus == '+')
		new_keywords.insert(keywords.begin(), keywords.end());
	else
	{
		for (const auto &kw:keywords)
			new_keywords.erase(kw);
	}

	return mail::keywords::update(maildir, filename, new_keywords);
}

static int dolist(const std::string &maildir, int lockflag)
{
	if (lockflag)
	{
		maildir::watch::lock lock{maildir};

		return dolist(maildir, 0);
	}

	mail::keywords::hashtable<std::string> keywords;
	std::map<std::string, mail::keywords::message<std::string>> messages;

	load(maildir, keywords, messages);

	for (const auto &m:messages)
	{
		std::cout << *m.second;

		auto keywords=m.second.keywords();

		std::set<std::string> sorted_keywords{keywords.begin(),
			keywords.end()
		};

		for (const auto &kw:sorted_keywords)
			std::cout << " " << kw;
		std::cout << "\n";
	}
	return 0;
}

static bool doit(const char *maildir, const char *filename, int lockflag,
		 int plusminus,
		 const mail::keywords::list &keywords)
{
	if (lockflag)
	{
		maildir::watch w{maildir};

		maildir::watch::lock locked{w};

		return doit_locked(maildir, filename, plusminus, keywords);
	}

	return doit_locked(maildir, filename, plusminus, keywords);
}

int main(int argc, char *argv[])
{
	int lockflag=0;
	int optc;
	const char *maildir;
	const char *filename;
	int list=0;
	int plusminus=0;

	libmail_kwCaseSensitive=0;

	while ((optc=getopt(argc, argv, "arLlhc")) != -1)
		switch (optc) {
		case 'c':
			libmail_kwCaseSensitive=1;
			break;
		case 'l':
			lockflag=1;
			break;
		case 'L':
			list=1;
			break;
		case 'a':
			plusminus='+';
			break;
		case 'r':
			plusminus='-';
			break;
		default:
			usage();
		}

	if (optind >= argc)
		usage();

	maildir=argv[optind++];

	if (list)
	{
		exit (dolist(maildir, lockflag));
	}

	if (optind >= argc)
		usage();

	filename=argv[optind++];

	while (!doit(maildir, filename, lockflag, plusminus,
		     mail::keywords::list{argv+optind, argv+argc}))
	{
		mail::keywords::hashtable<std::string> existing_keywords;
		std::map<std::string, mail::keywords::message<std::string>
			 > messages;

		load(maildir, existing_keywords, messages);
	}

	return (0);
}
