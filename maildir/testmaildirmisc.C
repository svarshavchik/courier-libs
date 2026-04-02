/*
** Copyright 2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include "maildirmisc.h"
#include <iostream>

void testremflagname()
{
	static const struct {
		const char *orig;
		char flag;
		const char *result;
	} tests[]={
		{
			"./:2,xT/messagefile:2,RTV",
			'T',
			"./:2,xT/messagefile:2,RV"
		},

		{
			"./:2,xT/messagefile",
			'T',
			"./:2,xT/messagefile"
		},
		{
			"./:2,xT/messagefile:2,RTV",
			'X',
			"./:2,xT/messagefile:2,RTV"
		},


		{
			"messagefile:2,RTV",
			'T',
			"messagefile:2,RV"
		},

		{
			"messagefile",
			'e',
			"messagefile"
		},
		{
			"messagefile:2,RTV",
			'X',
			"messagefile:2,RTV"
		},

	};

	for (const auto &t:tests)
	{
		std::string filename{t.orig};

		maildir::remflagname(filename, t.flag);

		if (filename != t.result)
		{
			std::cerr << "maildir::remflagname(\""
				  << t.orig
				  << "\", '"
				  << t.flag
				  << "') failed:\n"
				  << "    Expected: "
				  << t.result
				  << "\n    Actual:   "
				  << filename
				  << "\n";
			exit(1);
		}
	}
}

int main()
{
	testremflagname();
	return 0;
}
