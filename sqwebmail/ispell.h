#ifndef	ispell_h
#define	ispell_h

#include <string>
#include <string_view>
#include <list>
#include <tuple>

/*
** C++ interface to ispell.  Gimme a line of text, and I'll return
** list of mispelled words, plus their suggested derivations.
*/

struct ispell {
private:
	std::string buffer; // Internal buffer of ispell's response
public:
	struct misspelling {
		std::string_view misspelled_word;
		size_t word_pos{0}; // In the message
		std::list<std::string_view> suggestions;
	};

	std::list<misspelling> misspelled_words;

	ispell(const char *dict, std::string_view line);
	~ispell();

	ispell(const ispell &)=delete;
	ispell &operator=(const ispell &)=delete;
};

#endif
