#include "config.h"
#include	"recipe.h"
#include	"lexer.h"
#include	"token.h"
#include	"funcs.h"


int Recipe::ParseRecipe(Lexer &l)
{
	lex= &l;
	lex->token(cur_tok);
	try
	{
		topNode=ParseStatementList();
	}
	catch (const char *p)
	{
		l.errmsg(p);
		return (-1);
	}
#if NEED_NONCONST_EXCEPTIONS
	catch (char *p)
	{
		l.errmsg(p);
		return (-1);
	}
#endif
	if (cur_tok.Type() != cur_tok.eof)
	{
		l.errmsg("Syntax error.");
		return (-1);
	}
	return (0);
}

RecipeNode *Recipe::ParseStatementList()
{
RecipeNode *n=alloc(RecipeNode::statementlist);
Token::tokentype type;

	while ( (type=cur_tok.Type()) != cur_tok.rbrace && type != cur_tok.eof)
	{
		if (type == cur_tok.semicolon)
		{
			lex->token(cur_tok);
			continue;
		}

	RecipeNode *o=ParseStatement();

		n->AppendSibling(o);
	}
	return (n);
}

RecipeNode *Recipe::ParseStatement()
{
RecipeNode *n, *o;

	switch (cur_tok.Type())	{
	case Token::tokenif:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Syntax error after if";

		lex->token(cur_tok);

		n=alloc(RecipeNode::ifelse);
		o=ParseExpr();

		n->AppendSibling(o);

		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";

		lex->token(cur_tok);
		if (cur_tok.Type() != Token::semicolon)	// Newline/semicolon
			throw "Syntax error after )";

		lex->token(cur_tok);
		n->AppendSibling(ParseSubStatement());
		if (cur_tok.Type() == Token::tokenelse)
		{
			lex->token(cur_tok);
			if (cur_tok.Type() != Token::semicolon)	// Newline/semicolon
				throw "Syntax error after else";

			lex->token(cur_tok);
			n->AppendSibling(ParseSubStatement());
		}
		else if (cur_tok.Type() == Token::tokenelsif)
		{
			cur_tok.Type(Token::tokenif);
			n->AppendSibling(ParseStatement());
		}
		return (n);
	case Token::tokenwhile:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Syntax error after while";

		lex->token(cur_tok);

		n=alloc(RecipeNode::whileloop);
		o=ParseExpr();
		n->AppendSibling(o);

		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";

		lex->token(cur_tok);
		if (cur_tok.Type() != Token::semicolon)	// Newline/semicolon
			throw "Syntax error after else";

		lex->token(cur_tok);
		n->AppendSibling(ParseSubStatement());
		return (n);
	case Token::lbrace:
		lex->token(cur_tok);
		n=ParseStatementList();
		if (cur_tok.Type() != Token::rbrace)
			throw "Missing }";

		lex->token(cur_tok);
		if (cur_tok.Type() != Token::semicolon)
		{
			throw "Syntax error after }";
		}
		lex->token(cur_tok);
		return (n);
	case Token::tokento:
		lex->token(cur_tok);
		n=alloc(RecipeNode::deliver);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";

		lex->token(cur_tok);
		return (n);
	case Token::tokencc:
		lex->token(cur_tok);
		n=alloc(RecipeNode::delivercc);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";

		lex->token(cur_tok);
		return (n);
	case Token::exception:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lbrace)
			throw "Syntax error.";

		n=alloc(RecipeNode::exception);
		o=ParseStatement();
		n->AppendSibling(o);
		return (n);
	case Token::echo:
		lex->token(cur_tok);
		n=alloc(RecipeNode::echo);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::tokenxfilter:
		lex->token(cur_tok);
		n=alloc(RecipeNode::xfilter);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::tokensystem:
		lex->token(cur_tok);
		n=alloc(RecipeNode::system);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::dotlock:
		lex->token(cur_tok);
		n=alloc(RecipeNode::dotlock);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::lbrace)
			throw "Syntax error.";
		n->AppendSibling( ParseStatement() );
		return (n);
	case Token::flock:
		lex->token(cur_tok);
		n=alloc(RecipeNode::flock);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::lbrace)
			throw "Syntax error.";
		n->AppendSibling( ParseStatement() );
		return (n);
	case Token::logfile:
		lex->token(cur_tok);
		n=alloc(RecipeNode::logfile);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::log:
		lex->token(cur_tok);
		n=alloc(RecipeNode::log);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::include:
		lex->token(cur_tok);
		n=alloc(RecipeNode::include);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::exit:
		lex->token(cur_tok);
		n=alloc(RecipeNode::exit);
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::foreach:
		lex->token(cur_tok);
		n=alloc(RecipeNode::foreach);
		n->AppendSibling( ParseExpr());
		if (n->firstChild->nodeType != RecipeNode::strregexp &&
			n->firstChild->nodeType != RecipeNode::regexpr)
			throw "Syntax error.";
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		n->AppendSibling( ParseSubStatement() );
		return (n);
	case Token::importtoken:
		lex->token(cur_tok);
		n=alloc(RecipeNode::importtoken);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	case Token::unset:
		lex->token(cur_tok);
		n=alloc(RecipeNode::unset);
		n->AppendSibling( ParseExpr());
		if (cur_tok.Type() != Token::semicolon)
			throw "Syntax error.";
		lex->token(cur_tok);
		return (n);
	default:
		break;
	}
	n=ParseExpr();
	if (cur_tok.Type() != Token::semicolon)
		throw "Syntax error.";
	lex->token(cur_tok);
	return (n);
}

RecipeNode *Recipe::ParseSubStatement()
{
	if (cur_tok.Type() == Token::semicolon)
	{
		lex->token(cur_tok);
		return alloc(RecipeNode::statementlist);
	}
	return ParseStatement();
}

RecipeNode *Recipe::ParseAssign()
{
RecipeNode *n=ParseLogicalOr();

	while (cur_tok.Type() == Token::equals)
	{
		lex->token(cur_tok);

		if (n->nodeType != RecipeNode::qstring)
			throw "Syntax error before =";

	RecipeNode *o=alloc(RecipeNode::assignment);
		o->AppendSibling(n);

		n=ParseLogicalOr();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseLogicalOr()
{
RecipeNode *n=ParseLogicalAnd();

	while (cur_tok.Type() == Token::lor)
	{
		lex->token(cur_tok);

	RecipeNode *o=alloc(RecipeNode::logicalor);

		o->AppendSibling(n);

		n=ParseLogicalAnd();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseLogicalAnd()
{
RecipeNode *n=ParseComparison();

	while (cur_tok.Type() == Token::land)
	{
		lex->token(cur_tok);

	RecipeNode *o=alloc(RecipeNode::logicaland);

		o->AppendSibling(n);

		n=ParseComparison();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseComparison()
{
RecipeNode *n=ParseBitwiseOr();
RecipeNode *o;

	switch (cur_tok.Type())	{
	case Token::lt:
		o=alloc(RecipeNode::lessthan);
		break;
	case Token::le:
		o=alloc(RecipeNode::lessthanoreq);
		break;
	case Token::gt:
		o=alloc(RecipeNode::greaterthan);
		break;
	case Token::ge:
		o=alloc(RecipeNode::greaterthanoreq);
		break;
	case Token::eq:
		o=alloc(RecipeNode::equal);
		break;
	case Token::ne:
		o=alloc(RecipeNode::notequal);
		break;
	case Token::slt:
		o=alloc(RecipeNode::strlessthan);
		break;
	case Token::sle:
		o=alloc(RecipeNode::strlessthanoreq);
		break;
	case Token::sgt:
		o=alloc(RecipeNode::strgreaterthan);
		break;
	case Token::sge:
		o=alloc(RecipeNode::strgreaterthanoreq);
		break;
	case Token::seq:
		o=alloc(RecipeNode::strequal);
		break;
	case Token::sne:
		o=alloc(RecipeNode::strnotequal);
		break;
	default:
		return (n);
	}

	lex->token(cur_tok);
	o->AppendSibling(n);
	n=ParseBitwiseOr();
	o->AppendSibling(n);
	return (o);
}

RecipeNode *Recipe::ParseBitwiseOr()
{
RecipeNode *n=ParseBitwiseAnd();

	while (cur_tok.Type() == Token::bor)
	{
		lex->token(cur_tok);

	RecipeNode *o=alloc(RecipeNode::bitwiseor);

		o->AppendSibling(n);

		n=ParseBitwiseAnd();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseBitwiseAnd()
{
RecipeNode *n=ParseAddSub();

	while (cur_tok.Type() == Token::band)
	{
		lex->token(cur_tok);

	RecipeNode *o=alloc(RecipeNode::bitwiseand);

		o->AppendSibling(n);

		n=ParseAddSub();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseAddSub()
{
RecipeNode *n=ParseMultDiv();
RecipeNode *o;

	for (;;)	{
		switch (cur_tok.Type())	{
		case Token::plus:
			o=alloc(RecipeNode::add);
			break;
		case Token::minus:
			o=alloc(RecipeNode::subtract);
			break;
		default:
			return (n);
		}

		lex->token(cur_tok);
		o->AppendSibling(n);
		n=ParseMultDiv();
		o->AppendSibling(n);
		n=o;
	}
}

RecipeNode *Recipe::ParseMultDiv()
{
RecipeNode *n=ParseStrRegExp();
RecipeNode *o;

	for (;;)	{
		switch (cur_tok.Type())	{
		case Token::mult:
			o=alloc(RecipeNode::multiply);
			break;
		case Token::divi:
			o=alloc(RecipeNode::divide);
			break;
		default:
			return (n);
		}

		lex->token(cur_tok);
		o->AppendSibling(n);
		n=ParseElement();
		o->AppendSibling(n);
		n=o;
	}
}

RecipeNode *Recipe::ParseStrRegExp()
{
RecipeNode	*n=ParseElement();

	while ( cur_tok.Type() == Token::strregexp)
	{
	RecipeNode	*o;

		o=alloc(RecipeNode::strregexp);
		lex->token(cur_tok);
		o->AppendSibling(n);
		if (cur_tok.Type() != Token::regexpr)
			throw "Syntax error after =~";

		n=ParseElement();
		o->AppendSibling(n);
		n=o;
	}
	return (n);
}

RecipeNode *Recipe::ParseElement()
{
RecipeNode	*n, *o;

	switch (cur_tok.Type())	{
	case Token::length:
		n=alloc(RecipeNode::strlength);
		lex->token(cur_tok);
		o=ParseElement();
		n->AppendSibling(o);
		return (n);
	case Token::substr:
		n=alloc(RecipeNode::strsubstr);
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Missing (";

		lex->token(cur_tok);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::comma)
			throw "Missing ,";

		lex->token(cur_tok);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() == Token::comma)
		{
			lex->token(cur_tok);
			o=ParseExpr();
			n->AppendSibling(o);
		}
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";

		lex->token(cur_tok);
		return (n);
	case Token::getaddr:
		lex->token(cur_tok);
		n=alloc(RecipeNode::getaddr);
		n->AppendSibling(ParseElement());
		return (n);
	case Token::escape:
		lex->token(cur_tok);
		n=alloc(RecipeNode::escape);
		n->AppendSibling(ParseElement());
		return (n);
	case Token::regexpr:
		n=alloc(RecipeNode::regexpr);
		n->str=cur_tok.String();
		lex->token(cur_tok);
		return (n);
	case Token::lparen:
		lex->token(cur_tok);
		n=ParseExpr();
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";

		lex->token(cur_tok);
		return (n);
	case Token::logicalnot:
		lex->token(cur_tok);
		n=alloc(RecipeNode::logicalnot);
		o=ParseElement();
		n->AppendSibling(o);
		return (n);
	case Token::bitwisenot:
		lex->token(cur_tok);
		n=alloc(RecipeNode::bitwisenot);
		o=ParseElement();
		n->AppendSibling(o);
		return (n);
	case Token::lookup:
		n=alloc(RecipeNode::lookup);
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Missing (";

		lex->token(cur_tok);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::comma)
			throw "Missing ,";

		lex->token(cur_tok);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() == Token::comma)
		{
			lex->token(cur_tok);
			o=ParseExpr();
			n->AppendSibling(o);
		}
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";
		lex->token(cur_tok);
		return (n);
	case Token::to_lower:
		lex->token(cur_tok);
		n=alloc(RecipeNode::to_lower);
		n->AppendSibling(ParseElement());
		return (n);
	case Token::to_upper:
		lex->token(cur_tok);
		n=alloc(RecipeNode::to_upper);
		n->AppendSibling(ParseElement());
		return (n);
	case Token::hasaddr:
		lex->token(cur_tok);
		n=alloc(RecipeNode::hasaddr);
		n->AppendSibling(ParseElement());
		return (n);
#ifdef	DbObj
	case Token::gdbmopen:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Missing (.";

		lex->token(cur_tok);
		n=alloc(RecipeNode::gdbmopen);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() == Token::comma)
		{
			lex->token(cur_tok);
			o=ParseExpr();
			n->AppendSibling(o);
		}
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";
		lex->token(cur_tok);
		return (n);
	case Token::gdbmclose:
		lex->token(cur_tok);
		return (alloc(RecipeNode::gdbmclose));
	case Token::gdbmfetch:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Missing (.";

		lex->token(cur_tok);
		n=alloc(RecipeNode::gdbmfetch);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() == Token::comma)
		{
			lex->token(cur_tok);
			o=ParseExpr();
			n->AppendSibling(o);
		}
		if (cur_tok.Type() == Token::comma)
		{
			lex->token(cur_tok);
			o=ParseExpr();
			n->AppendSibling(o);
		}
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";
		lex->token(cur_tok);
		return (n);
	case Token::gdbmstore:
		lex->token(cur_tok);
		if (cur_tok.Type() != Token::lparen)
			throw "Missing (.";

		lex->token(cur_tok);
		n=alloc(RecipeNode::gdbmstore);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::comma)
			throw "Missing ,.";

		lex->token(cur_tok);
		o=ParseExpr();
		n->AppendSibling(o);
		if (cur_tok.Type() != Token::rparen)
			throw "Missing )";
		lex->token(cur_tok);
		return (n);
#else
	case Token::gdbmopen:
	case Token::gdbmclose:
	case Token::gdbmfetch:
	case Token::gdbmstore:
		throw "GDBM/DB support is not available.";
#endif
	case Token::timetoken:
		lex->token(cur_tok);
		return (alloc(RecipeNode::timetoken));
	default:
		break;
	}
	return (ParseString());
}

////////////////////////////////////////////////////////////////////////////
//
//  Parse a string.  Consecutive strings are automatically concatenated.
//
////////////////////////////////////////////////////////////////////////////

RecipeNode *Recipe::ParseString()
{
RecipeNode	*n=NULL;
RecipeNode	*s=NULL;

	for (;;)
	{
		if (s && s->nodeType != RecipeNode::concat)
		{
			n=alloc(RecipeNode::concat);
			n->AppendSibling(s);
			s=n;
		}

		switch (cur_tok.Type())	{
		case Token::qstring:
			n=alloc(RecipeNode::qstring);
			break;
		case Token::sqstring:
			n=alloc(RecipeNode::sqstring);
			break;
		case Token::btstring:
			n=alloc(RecipeNode::btstring);
			break;
		default:
			throw "Syntax error.";
		}

		n->str=cur_tok.String();
		lex->token(cur_tok);
		if (s)	s->AppendSibling(n);
		else	s=n;

		switch (cur_tok.Type())	{
		case Token::qstring:
		case Token::sqstring:
		case Token::btstring:
			continue;
		default:
			break;
		}
		break;
	}
	return (s);
}
