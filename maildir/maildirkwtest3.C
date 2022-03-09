/*
** Copyright 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include "maildirkeywords.C"
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <algorithm>

struct logger {
	std::string pfix;

	template<typename T>
	std::ostream &operator<<(T && t)
	{
		std::cout << pfix << ": " << std::forward<T>(t);

		return std::cout;
	};
};

logger l;

struct mock_update_dir {
	std::string keyworddir="courierimapkeywords";

	const char * const *ptr;
	int left;

	mock_update_dir(const char *const *ptr) : ptr{ptr}
	{
	}

	const char *next()
	{
		if (!*ptr)
			return nullptr;

		return *ptr++;
	}

	void remove(const std::string &filename)
	{
		l << filename << ": remove\n";
	}

	void delete_if_aged(const std::string &filename, time_t now)
	{
		l << filename << ": delete_if_aged\n";
	}

	typedef std::istringstream stream_type;

	bool try_open(std::istringstream &i, const std::string &filename)
	{
		l << filename << ": open\n";
		i.str("keyword1\n");
		return true;
	}

	void rename_newest(const std::string &from,
			   const std::string &to)
	{
		l << "rename(" << from << ", " << to << ")\n";
	}
};

void testscanupdates()
{
	static const char * const fakedir[]={
		".",
		"..",
		":list",
		".4.message1",
		".5.message1",
		".5.message2",
		".4.message2",
		".4.message3",
		".5.message3",
		"message3",
		"message4",
		".5.message4",
		".4.message4",
		"message5",
		".tmp.message6",
		"message6",
		".4.message7",
		".9.message8",
		nullptr
	};

	l.pfix = "1";

	mock_update_dir mock{fakedir};

	std::map<std::string, mail::keywords::list> files;

	files["message1"];
	files["message2"];
	files["message3"];
	files["message4"];
	files["message5"];
	files["message8"];
	files["message9"];
	files["messagea"];

	std::unordered_map<std::string, messagestatus> statuses;

	bool save_required=false;

	auto save_updates=[&]
		(const std::string &name,
		 mail::keywords::list &keywords)
	{
		auto iter=files.find(name);

		if (iter == files.end())
			return false;

		iter->second=std::move(keywords);

		return true;
	};

	scan_updates(mock, 3000, 10, statuses, save_updates, save_required);

	l.pfix = "2";
	l << "save_required: " << save_required << "\n";
	l.pfix = "3";
	auto dump=[&]
	{
		for (const auto &f:files)
		{
			auto iter=statuses.find(f.first);

			auto &o=l << f.first << ":";

			std::set<std::string> sorted_set{
				f.second.begin(),
				f.second.end()
			};

			for (const auto &kw:sorted_set)
				o << " " << kw;

			if (iter != statuses.end())
			{
				o << ", most_recent="
					  << iter->second.most_recent
					  << ", found_newest="
					  << iter->second.found_newest;
			}
			o << "\n";
		}
	};

	dump();

	l.pfix = "4";

	std::istringstream i;

	i.str("keyword2\n"
	      "keyword3\n"
	      "\n"
	      "message1:0,1\n"
	      "message2:0,1\n"
	      "message3:0,1\n"
	      "message4:0,1\n"
	      "message5:0,1\n"
	      "message6:0,1\n"
	      "message7:0,1\n"
	      "message8:0,1\n"
	      "message9:0,1\n"
	      "messagec:0,1\n"
	);

	save_required=false;
	read_keyword_list(i, statuses, save_updates, save_required);
	l.pfix = "5";
	dump();
	l.pfix = "6";
	l << "save_required: " << save_required << "\n";
	l.pfix = "7";
	cleanup(mock, statuses, 1800, 6);
}

int main(int argc, const char **argv)
{
	testscanupdates();
	exit (0);
}
