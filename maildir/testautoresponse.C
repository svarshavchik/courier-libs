/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildirmisc.h"
#include	"autoresponse.h"
#include	<filesystem>
#include	<array>
#include	<string_view>
#include	<iostream>
#include	<fstream>
#include	<algorithm>

#define AUTORESPONSEQUOTA "/dev/null"
#include "autoresponse.C"

static constexpr char TESTDIR[]="autoresponse.tst";

template<typename ...Strings>
void verify_autoreplies(Strings && ...strings)
{
	auto list=mail::autoresponse::list(TESTDIR);

	std::sort(list.begin(), list.end());

	if (list.size() == sizeof...(strings))
	{
		if constexpr (sizeof...(strings) == 0)
			return;
		else
		{
			auto b=list.begin();

			if ( (... && (*b++ == strings)) )
				return;
		}
	}

	std::cerr << "Unexpected result of mail::autoresponse::list\n"
		"Expected:\n";

	if constexpr (sizeof...(strings) != 0)
	{
		auto b=list.begin();

		(..., (std::cerr << "    \"" << strings << "\"\n"));
	}
	std::cerr << "Actual:\n";

	for (auto &a:list)
	{
		std::cerr << "    \"" << a << "\"\n";
	}
	exit(1);
}

void rmrf()
{
	std::error_code ec;

	std::filesystem::remove_all(TESTDIR, ec);
}


int main(int argc, char **argv)
{
	rmrf();
	maildir::make(TESTDIR, 0700, 0700, false);
	verify_autoreplies();
	if (!mail::autoresponse::validate(TESTDIR, "ok") ||
	    mail::autoresponse::validate(TESTDIR, "not.ok"))
	{
		std::cerr <<
			"mail::autoresponse::validate semantics are wrong.\n";
		exit(1);
	}

	mail::autoresponse::create(
		TESTDIR, "001",
		[&]
		(std::ostream &o)
		{
			verify_autoreplies();

			o << "Lorem\n";
		}
	);

	{
		std::ifstream i;

		mail::autoresponse::open(i, TESTDIR, "001");

		std::string str;

		std::getline(i, str);

		if (str != "Lorem")
		{
			std::cerr <<
				"mail::autoresponse::open test 1 failed.\n";
			exit(1);
		}
	}

	verify_autoreplies("001");

	mail::autoresponse::create(
		TESTDIR, "0011",
		[&]
		(std::ostream &o)
		{
			o << "Lorem Ipsum\n";
		}
	);

	verify_autoreplies("001", "0011");

	mail::autoresponse::create(
		TESTDIR, "001",
		[&]
		(std::ostream &o)
		{
			o << "Ipsum\n";

			verify_autoreplies("001", "0011");

			std::string buffer;

			std::ifstream i;
			mail::autoresponse::open(i, TESTDIR, "001");

			std::getline(i, buffer);

			if (buffer != "Lorem")
			{
				std::cerr <<
					"mail::autoresponse::open test 2 "
					"failed.\n";
				exit(1);
			}
		}
	);

	{
		std::ifstream i;

		mail::autoresponse::open(i, TESTDIR, "001");

		std::string buffer;

		std::getline(i, buffer);

		if (buffer != "Ipsum")
		{
			std::cerr <<
				"mail::autoresponse::open test 3 failed.\n";
			exit(1);
		}
	}

	verify_autoreplies("001", "0011");

	mail::autoresponse::create(
		TESTDIR, "001",
		[&]
		(std::ostream &o)
		{
			mail::autoresponse::remove(TESTDIR, "001");
			verify_autoreplies("0011");
		}
	);
	verify_autoreplies("0011");

	{
		std::string autoresponsequota{TESTDIR};
		autoresponsequota += "/autoresponsesquota";

		std::ofstream o{autoresponsequota};

		o << "C3S300";
	}

	if (mail::autoresponse::create(
		    TESTDIR, "001",
		    [&]
		    (std::ostream &o)
		    {
			    o << std::string(500, ' ');

			    if (!mail::autoresponse::create(
					TESTDIR, "002",
					[]
					(std::ostream &o)
					{
					})
			    )
			    {
				    std::cerr << "autoresponse quota test 1"
					    " failure.\n";
				    exit(1);
			    }
		    }))
	{
		std::cerr << "autoresponse quota test 2 failure.\n";
		exit(1);
	}

	if (!mail::autoresponse::create(
		    TESTDIR, "001",
		    []
		    (std::ostream &o)
		    {
		    }))
	{
		std::cerr << "autoresponse quota test 3 failure.\n";
		exit(1);
	}

	if (mail::autoresponse::create(
		    TESTDIR, "003",
		    []
		    (std::ostream &o)
		    {
		    })
	)
	{
		std::cerr << "autoresponse quota test 4 failure.\n";
		exit(1);
	}
	rmrf();
	return (0);
}
