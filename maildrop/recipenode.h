#ifndef recipenode_h
#define	recipenode_h


#include	"buffer.h"
class Recipe;

/////////////////////////////////////////////////////////////////////////////
//
// RecipeNode class - one "clause" in a recipe file, such as an "if"
// statement, or an expression.
//
// All RecipeNodes which represent a single recipe file are linked in a
// doubly-linked list anchored in their Recipe object.
//
// The RecipeNodes are arranged in a hierarchical format:
//
// prevNode/nextNode is the doubly-linked list anchored in the Recipe
// object.  All RecipeNodes dynamically allocated by the Recipe class
// are on this list.
//
// firstChild/lastChild is a doubly-linked list of RecipeNodes that are
// considered descendants of this node.  For example, a node that
// represents an "||" (logical or operation) will have two descendants,
// the left side, and the right side, of the expression.  This RecipeNode
// itself is on the descendant list of its parent.  All descendants of
// a RecipeNode have the parentNode pointer point to this RecipeNode.
// prevSibling/nextSibling is a doubly-linked list of all descendants of
// a single RecipeNode.
//
// This provides a generic linkage that is used to built a hierarchy that
// represents the logical layout of a recipe file.  The remaining fields
// store information pertaining to each individual kind of a RecipeNode
// object.  The nodeType field designates what kind of a RecipeNode object
// this is, which remaining fields are used depends on the nodeType field.
//
// The Recipe class calls the Evaluate() function of the first RecipeNode
// that represents that entire recipe file.  The set of Evaluate() functions
// are logically involved to actually execute the given recipe file.
//
/////////////////////////////////////////////////////////////////////////////

class RecipeNode {
	RecipeNode *prevNode, *nextNode;	// List of all nodes in
						// this recipe.

	RecipeNode *parentNode;			// Parent of this node.
	RecipeNode *prevSibling, *nextSibling;	// Siblings of this node.
	RecipeNode *firstChild, *lastChild;	// Its own children.

	Buffer	str;
	int	linenum;

	void dollarexpand(Recipe &, Buffer &);
	int dollarexpand(Recipe &, Buffer &, int);

public:
	friend class Recipe;

	enum RecipeNodeType {
		statementlist,
		assignment,
		qstring,
		sqstring,
		btstring,
		regexpr,
		add,
		subtract,
		multiply,
		divide,
		lessthan,
		lessthanoreq,
		greaterthan,
		greaterthanoreq,
		equal,
		notequal,
		concat,
		logicalor,
		logicaland,
		bitwiseor,
		bitwiseand,
		logicalnot,
		bitwisenot,
		strlessthan,
		strlessthanoreq,
		strgreaterthan,
		strgreaterthanoreq,
		strequal,
		strnotequal,
		strlength,
		strsubstr,
		strregexp,
		ifelse,
		whileloop,
		deliver,
		delivercc,
		exception,
		echo,
		xfilter,
		system,
		dotlock,
		flock,
		logfile,
		log,
		include,
		exit,
		foreach,
		getaddr,
		lookup,
		escape,
		to_lower,
		to_upper,
		hasaddr,
		gdbmopen,
		gdbmclose,
		gdbmfetch,
		gdbmstore,
		timetoken,
		importtoken,
		unset
		} nodeType;

	RecipeNode(RecipeNodeType);
	~RecipeNode()					{}
	void	Evaluate(Recipe &, Buffer &);
private:
	void	AppendSibling(RecipeNode *);
	void	EvaluateString(Recipe &r, Buffer &b);

	void EvaluateStrRegExp(Recipe &, Buffer &, Buffer *);
	void EvaluateRegExp(Recipe &, Buffer &, Buffer *);
static	void	ParseRegExp(const Buffer &, Buffer &, Buffer &);
static	int	boolean(const Buffer &);

	void rfc822getaddr(Buffer &);
	int rfc822hasaddr(Buffer &);
	int rfc822hasaddr(const char *, Buffer &);
	void SpecialEscape(Buffer &);
	int dolookup(Buffer &, Buffer &, Buffer &);
	} ;
#endif
