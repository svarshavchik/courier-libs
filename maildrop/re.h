#ifndef	re_h
#define	re_h


#include	"config.h"
#include	<sys/types.h>
#include	"funcs.h"
#include	"reeval.h"

class ReMatch;

///////////////////////////////////////////////////////////////////////////
//
//  The Re class represents a regular expression.   The regular expression
//  is translated into a non-deterministic automaton, stored as a list
//  of RegExpNodes.
//
//  Then, one or more strings are matched against the regular expression.
//
//  The Re object may dynamically allocate another Re object in order to
//  implement the ! operator.  Each ! operator introduces a dynamically-
//  allocated Re object, which contains the next chained regular expression.
//  Another ! operator causes another object to be allocated.
//
//  The ^ and $ anchors are implemented here.  The ABSENCE of a ^ anchor
//  causes a dummy "[.\n]*" expression to be created in the first Re object,
//  with the real expression being parsed in the 2nd Re object.
//
//  When a string is matched against a regular expression, when the current
//  state includes a FINAL state, and there is a chained Re object, the
//  remainder of the string gets matched against the chained Re object.
//  If the chained matched succeeds, the entire match succeeds, otherwise,
//  we continue matching the original string.
//
//  If a match is succesfull, MatchCount() may be called to return the number
//  of characters that were matched.  If an ! operator is used, the optional
//  argument to MatchCount(), if not null, can be used to call MatchCount()
//  to return the count that the next expression matched.
//
///////////////////////////////////////////////////////////////////////////

class	RegExpNode;

class Re {

	Re	*chainedre;		// Chained regular expression
	Re	*prevre;
	RegExpNode *nodes;		// Singly-linked list of nodes
	RegExpNode *first;		// Starting node
	RegExpNode *final;		// Final node
	unsigned nextid;		// When creating, next ID to assign

	RegExpNode	*allocnode();
	const	char *expr, *origexpr;

	// When matching:
	int	matched;
	off_t matchedpos;
	ReEval	*curstate, *nextstate;
	unsigned final_id;

	int	curchar() { return ((int)(unsigned char)*expr); }
	void	nextchar() { ++expr; }
	int	casesensitive;
	int	matchFull;
	int	isCaret;
	int	isDummy;
public:
	Re();
	~Re();

	int Compile(const char *, int, int &);
			// Compile regular expression
private:
	int CompileS(const char *, int, int &);


	void init();
	RegExpNode **CompileAtom(RegExpNode **);
	RegExpNode **CompileAtomString(RegExpNode **);
	RegExpNode **CompileOrClause(RegExpNode **);
	RegExpNode **CompileElement(RegExpNode **);
	void is_sets(RegExpNode *);

	int	parsechar();

// Evaluation

	ReEval	state1, state2;
	unsigned charsmatched;
public:
	int	Match(ReMatch &);
	unsigned MatchCount(Re **p =0) {
					if (p) *p=chainedre;
					return (charsmatched); }
	int	IsDummy()	{ return (isDummy); }
	int	IsAnchorStart()	{ return (isCaret); }
} ;

#endif
