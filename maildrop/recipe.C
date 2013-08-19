#include "config.h"
#include	"recipe.h"
#include	"lexer.h"
#include	"token.h"
#include	"funcs.h"


Recipe::Recipe() : firstNode(0), lastNode(0),
	topNode(0)
{
}

Recipe::~Recipe()
{
RecipeNode *n;

	while ((n=firstNode) != 0)
	{
		firstNode=n->nextNode;
		delete n;
	}
}

RecipeNode *Recipe::alloc(RecipeNode::RecipeNodeType t)
{
RecipeNode *n=new RecipeNode(t);

	if (!n)	outofmem();

	n->prevNode=lastNode;
	n->nextNode=0;

	if (lastNode)	lastNode->nextNode=n;
	else		firstNode=n;
	lastNode=n;
	n->linenum=lex->Linenum();
	return (n);
}

void Recipe::errmsg(RecipeNode &r, const char *emsg)
{
	lex->errmsg(r.linenum, emsg);
}

void Recipe::ExecuteRecipe()
{
Buffer	b;

	if (topNode)	topNode->Evaluate(*this, b);
}
