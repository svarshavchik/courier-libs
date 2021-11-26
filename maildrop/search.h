#ifndef	search_h
#define	search_h


#include	"buffer.h"

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>

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

	pcre2_code *pcre_regexp;
	pcre2_match_data *match_data;

	Buffer	current_line;
	Buffer	next_line;

	int	match_top_header, match_other_headers, match_body;
	double	weight1, weight2;
	int	scoring_match;

	int init(const char *, const char *);

	void cleanup();

public:
	double	score;	// For weighted scoring.  Without scoring, this is
			// either 0, or 1.

	Search() : pcre_regexp(NULL),
		   match_data(NULL) {}
	~Search()	{ cleanup(); }
	int find(Message &, MessageInfo &, const char *, const char *,
		Buffer *);
	int find(const char *, const char *, const char *, Buffer *);
private:
	int findinline(Message &, const char *, Buffer *);
	int findinsection(Message &, const char *, Buffer *);
	void init_match_vars(const char *,
			     PCRE2_SIZE *,
			     uint32_t,
			     Buffer *);
	Buffer search_expr;
	Buffer *foreachp_arg;
	static int search_cb(const char *ptr, size_t cnt, void *arg);
	int search_cb(const char *ptr, size_t cnt);
} ;
#endif
