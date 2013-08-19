#ifndef	reeval_h
#define	reeval_h


#include	"regexpnode.h"

///////////////////////////////////////////////////////////////////////////
//
//  For matching a regular expression, we keep track of the set of all the
//  current nodes we're currently on.  For speed, there are two ReEval
//  objects - current one, and the next one, and we switch between the two
//  on each step.
//
///////////////////////////////////////////////////////////////////////////


class ReEval {
public:
	RegExpNode **nodes;
	unsigned	numnodes;
	unsigned	*nodenums;	// For speed - lookup array of nodes
				// that are already in this set.
	ReEval() : nodes(0), numnodes(0), nodenums(0), arysize(0) {}
	~ReEval()	{ if (nodes)	delete[] nodes;
			if (nodenums)	delete[] nodenums;
			}
	void	init(unsigned maxsize);
private:
	unsigned	arysize;
} ;

#endif
