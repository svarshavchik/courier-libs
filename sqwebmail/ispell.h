#ifndef	ispell_h
#define	ispell_h
/*
*/

/*
** C interface to ispell.  Gimme a line of text, and I'll return a link
** list of mispelled words, plus their suggested derivations.
*/

struct ispell_misspelled;
struct ispell_suggestion;

struct ispell {
	char *ispell_buf;
	struct ispell_misspelled *first_misspelled;
} ;

struct ispell_misspelled {
	struct ispell_misspelled *next;
	const char *misspelled_word;
	int word_pos;
	struct ispell_suggestion *first_suggestion;
} ;

struct ispell_suggestion {
	struct ispell_suggestion *next;
	const char *suggested_word;
} ;

struct ispell *ispell_run(const char *dictionary, const char *line);
void ispell_free(struct ispell *);
#endif
