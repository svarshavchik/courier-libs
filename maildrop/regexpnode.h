#ifndef	regexpnode_h
#define regexpnode_h


#include	"config.h"

/////////////////////////////////////////////////////////////////////////////
//
// class RegExpNode represents a node in a non-deterministic automaton that
// represents a regular expression.
//
/////////////////////////////////////////////////////////////////////////////

class RegExpNode {
public:
	RegExpNode *next;	// List of all the nodes in the automaton
	unsigned id;		// Unique ID of this node.
	int	thechar;	// Character for this node, or one of the
				// following special constants:

#define	RENULL	-1		// Null transition
#define	RESET	-2		// This is a set
#define	REFINAL	-3		// Final node - acceptance


	RegExpNode *next1, *next2; // Up to two transitions for this node
				// (next2 is used only by RENULLs
	unsigned char *reset;	// Used by RESETs

	RegExpNode(unsigned i) : next(0), id(i), thechar(0),
			next1(0), next2(0), reset(0) {}
	~RegExpNode() { if (reset) delete[] reset; }
} ;

#endif
