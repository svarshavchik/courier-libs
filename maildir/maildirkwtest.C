/*
** Copyright 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	"maildirkeywords.h"
#include	"maildirmisc.h"

static void name2dir_test()
{
	static struct {
		const char *homedir;
		const char *maildir;

		const char *path;
	} tests[]={
		{ "", "INBOX", "." },
		{ ".", "INBOX", "." },
		{ "/home/myself", "INBOX", "/home/myself" },
		{ NULL, "nope", NULL},
		{ NULL, "INBOX.x", "./.x" },
		{ NULL, "INBOX..x", NULL },
		{ NULL, "INBOX.x.", NULL },
		{ NULL, "INBOX/x", NULL },
	};

	size_t i;

	for (i=0; i<sizeof(tests)/sizeof(tests[0]); ++i)
	{
		char *p=maildir_name2dir(tests[i].homedir,
					 tests[i].maildir);

		if ( (p && !tests[i].path) ||
		     (!p && tests[i].path) ||
		     (p && strcmp(p, tests[i].path)))
		{
			fprintf(stderr, "name2dir test %u failed\n",
				(unsigned)i);
			exit(1);
		}
		free(p);
	}
}

static void folderdir_test()
{
	static struct {
		const char *homedir;
		const char *maildir;

		const char *path;
	} tests[]={
		{ 0, "INBOX", "." },
		{ ".", "INBOX", "." },
		{ "/home/myself", "INBOX", "/home/myself" },
		{ NULL, "yep", ".yep"},
		{ ".", "x", ".x" },
		{ NULL, ".x", NULL },
		{ NULL, "x..x", NULL },
		{ NULL, "x.", NULL },
		{ NULL, "yep/nope", NULL},
		{ "dir", 0, "dir"},
		{ "dir", "INBOX", "dir"},
		{ "dir", "x", "dir/.x"},
	};

	size_t i;

	for (i=0; i<sizeof(tests)/sizeof(tests[0]); ++i)
	{
		char *p=maildir_folderdir(tests[i].homedir,
					 tests[i].maildir);

		if ( (p && !tests[i].path) ||
		     (!p && tests[i].path) ||
		     (p && strcmp(p, tests[i].path)))
		{
			fprintf(stderr, "folderdir test %u failed\n",
				(unsigned)i);
			exit(1);
		}
		free(p);
	}
}

void keywordtest2()
{
	mail::keywords::hashtable<size_t> hashtable;

	mail::keywords::message<size_t> m{
		hashtable, {"alpha", "beta"}, size_t{3}
	};

	if (m.keywords() != mail::keywords::list{"alpha", "beta"})
	{
		fprintf(stderr, "keywordtest2 failed (1)\n");
		exit(1);
	}

	m.remove("beta");

	if (m.keywords() != mail::keywords::list{"alpha"})
	{
		fprintf(stderr, "keywordtest2 failed (2)\n");
		exit(1);
	}

	std::unordered_set<std::string> all_keywords;

	hashtable->enumerate_keywords([&]
				      (const std::string &kw)
	{
		all_keywords.insert(kw);
	});

	if (all_keywords != std::unordered_set<std::string>{"alpha"})
	{
		fprintf(stderr, "keywordtest2 failed (3)\n");
		exit(1);
	}

	std::unordered_set<size_t> all_messages;

	hashtable->enumerate_messages([&]
				      (size_t n)
	{
		all_messages.insert(n);
	});

	if (all_messages != std::unordered_set<size_t>{3})
	{
		fprintf(stderr, "keywordtest2 failed (4)\n");
		exit(1);
	}

	m=mail::keywords::message<size_t>{};

	all_keywords.clear();
	all_messages.clear();
	hashtable->enumerate_keywords([&]
				      (const std::string &kw)
	{
		all_keywords.insert(kw);
	});
	hashtable->enumerate_messages([&]
				      (size_t n)
	{
		all_messages.insert(n);
	});

	if (!all_keywords.empty() || !all_messages.empty())
	{
		fprintf(stderr, "keywordtest2 failed (5)\n");
		exit(1);
	}
	m=m;

	m.keywords(hashtable, {"gamma"}, size_t{3});

	hashtable->messages("alpha",
			    [&]
			    (size_t n)
	{
		all_messages.insert(n);
	});

	if (!all_messages.empty())
	{
		fprintf(stderr, "keywordtest2 failed (6)\n");
		exit(1);
	}

	hashtable->messages("gamma",
			    [&]
			    (size_t n)
	{
		all_messages.insert(n);
	});

	if (all_messages != std::unordered_set<size_t>{3})
	{
		fprintf(stderr, "keywordtest2 failed (7)\n");
		exit(1);
	}
}

int main()
{
	name2dir_test();
	folderdir_test();
	keywordtest2();
	return 0;
}
