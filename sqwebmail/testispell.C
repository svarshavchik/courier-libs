#include "config.h"

#define SPELLPROG "./testispell"
#include "ispell.C"
#include <iostream>
#include <sstream>

int main(int argc, char **argv)
{
	if (argc > 1 && std::string_view{argv[1]} == "-a")
	{
		std::cout << "# Ok\n" << std::flush;

		std::string line;
		while (std::getline(std::cin, line))
			;
		std::cout << "& quickk 2 6: world, w world\n"
			  << "+ ignore me\n"
			  << "# lazzy 3: not found\n" << std::flush;
		exit(0);
	}

	ispell speller{NULL, "abracadabra\n"};

	std::ostringstream o;

	for (auto &misspelling:speller.misspelled_words)
	{
		o << misspelling.misspelled_word << "@"
		  << misspelling.word_pos << ":";

		for (auto &suggestion:misspelling.suggestions)
		{
			o << " " << suggestion;
		}
		o << "\n";
	}
	if (o.str() != "quickk@6: world w\n"
	    "lazzy@3:\n")
	{
		std::cerr << "Unexpected result from the ispell parser:\n"
			  << o.str();
		exit(1);
	}
}
