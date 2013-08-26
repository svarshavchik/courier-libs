#ifndef	search_h
#define	search_h


#include	"buffer.h"

#if	HAVE_PCRE_H
#include	<pcre.h>
#else
#include	<pcre/pcre.h>
#endif

////////////////////////////////////////////////////////////////////////////
//
// The Search class encapsulates the entire functionality of matching
// patterns against the message.
//
// There are two main modes, both implemented by the overloaded find()
// function.  The first find() function matches a pattern against
// the message, the second find() function matches a pattern against
// text in memory.
//
// The find() function requires that the pattern, and pattern flags
// be already separated.
//
// The find() function returns -1 if there was an error in the format
// of the regular expression, 0 if the pattern was good, and it was
// successfully searched.
//
// The 'score' variable is set when find() returns 0.  If a pattern was
// found, it is set to 1, else it is set to 0.  If the pattern flags
// requested a weighted scoring search, the 'score' variable will
// contain the calculated score.
//
// If a weighted scoring is not requested, the find() function automatically
// sets the MATCH... variables (from the '!' operator).
//
////////////////////////////////////////////////////////////////////////////

class MessageInfo;
class Message;

class Search {

	pcre	*pcre_regexp;
	pcre_extra *pcre_regexp_extra;
	int	*pcre_vectors;
	size_t	pcre_vector_count;

	Buffer	current_line;
	Buffer	next_line;

	int	match_header, match_body;
	double	weight1, weight2;
	int	scoring_match;

	int init(const char *, const char *);

	void cleanup();

public:
	double	score;	// For weighted scoring.  Without scoring, this is
			// either 0, or 1.

	Search() : pcre_regexp(NULL), pcre_regexp_extra(NULL),
		pcre_vectors(NULL)	{}
	~Search()	{ cleanup(); }
	int find(Message &, MessageInfo &, const char *, const char *,
		Buffer *);
	int find(const char *, const char *, const char *, Buffer *);
private:
	int findinline(Message &, const char *, Buffer *);
	int findinsection(Message &, const char *, Buffer *);
	void init_match_vars(const char *, int, int *, Buffer *);

	Buffer search_expr;
	Buffer *foreachp_arg;
	static int search_cb(const char *ptr, size_t cnt, void *arg);
	int search_cb(const char *ptr, size_t cnt);
} ;
#endif
