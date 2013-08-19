#ifndef recipe_h
#define	recipe_h


#include	"config.h"
#include	"recipenode.h"
#include	"token.h"

class Lexer;
class Token;

//////////////////////////////////////////////////////////////////////////
//
// Class Recipe - parsed structure of a recipe file.
// This class reads tokens from the Lexer class, and arranges them in a
// logical structure that represents the recipe file.  The Recipe object
// maints a list of RecipeNode objects, which roughly represent individual
// statements, and elements of a recipe file.  There is more or less a
// one-to-one relationship between Tokens and Recipenodes.  Usually one
// RecipeNode is created for each token - but not always.  The RecipeNode
// objects are automatically created by the Recipe object when ParseRecipe()
// is called to translate the tokens returned by the Lexer class into
// the RecipeNode structure.  When the Recipe object is destroyed, it
// automatically destroys all RecipeNode objects it has allocated.
// The RecipeNode objects are created using a simple recursive-descent
// parser.
//
// The ExecuteRecipe() function actually starts the ball rolling by
// calling the Evaluate() function of the first RecipeNode object in the
// structure.
//
//////////////////////////////////////////////////////////////////////////


#include	"../dbobj.h"

class Recipe {

	RecipeNode *firstNode, *lastNode;	// All nodes in this recipe.
	RecipeNode *topNode;			// Topmost node.

	RecipeNode *alloc(RecipeNode::RecipeNodeType);

	Lexer	*lex;
	Token	cur_tok;

public:
	Recipe();
	~Recipe();

	int ParseRecipe(Lexer &);
	void ExecuteRecipe();
	void errmsg(RecipeNode &, const char *);

#ifdef	DbObj
	DbObj	gdbm_file;
#endif

private:
	// This is, essentially, a recursive-descent parser that builds
	// the RecipeNode tree.
	RecipeNode *ParseExpr()
		{
			return (ParseAssign());
		}
	RecipeNode *ParseAssign();
	RecipeNode *ParseLogicalOr();
	RecipeNode *ParseLogicalAnd();
	RecipeNode *ParseComparison();
	RecipeNode *ParseBitwiseOr();
	RecipeNode *ParseBitwiseAnd();
	RecipeNode *ParseAddSub();
	RecipeNode *ParseMultDiv();
	RecipeNode *ParseStrRegExp();
	RecipeNode *ParseStatementList();
	RecipeNode *ParseStatement();
	RecipeNode *ParseSubStatement();
	RecipeNode *ParseString();
	RecipeNode *ParseElement();
} ;

#endif
