#include "config.h"
#include	"lexer.h"
#include	"funcs.h"
#include	"varlist.h"
#include	<ctype.h>


int	Lexer::Open(const char *filename_arg)
{
	linenum=1;
	lasttokentype=Token::semicolon;

int	fd;

	if ((fd=file.Open(filename_arg, O_RDONLY)) < 0)
		return (-1);
	filename=filename_arg;
	return (fd);
}

void	Lexer::error(const char *errmsg)
{
	merr.write(errmsg);
}

void	Lexer::token(Token &t)
{
	if ( file.fd() < 0)
		t.Type( Token::eof);
	else
	{
		token2(t);
		if (t.Type() == Token::eof)
			file.Close();
	}

	lasttokentype=t.Type();
	if (maildrop.embedded_mode)
		switch (lasttokentype)	{
		case Token::tokento:
		case Token::tokencc:
		case Token::btstring:
		case Token::tokenxfilter:
		case Token::tokensystem:
		case Token::dotlock:
		case Token::flock:
		case Token::logfile:
		case Token::log:
			{
			Buffer	errmsg;

				errmsg="maildrop: '";
				errmsg += t.Name();
				errmsg += "' disabled in embedded mode.\n";
				errmsg += '\0';
				error((const char *)errmsg);
				t.Type( Token::error );
				break;
			}
		default:
			break;
		}

	if (VerboseLevel() > 8)
	{
	Buffer	debug;

		debug="Tokenized ";
		debug += t.Name();
		debug += '\n';
		debug += '\0';
		error((const char *)debug);
	}
}

void	Lexer::token2(Token &t)
{
int	c;

	t.Type(Token::error);

	// Eat whitespace & comments

	for (;;)
	{
		while ((c=curchar()) >= 0 && isspace(c))
		{
			nextchar();
			if (c == '\n' || c == '\r')	// Treat as semicolon
			{
				t.Type(Token::semicolon);
				return;
			}
		}
		if (c == '\\')	// Continued line?
		{
			nextchar();
			c=curchar();
			if (c < 0 || !isspace(c))
			{
				return;	// Error
			}
			while (c >= 0 && c != '\n')
			{
				nextchar();
				c=curchar();
			}
			if (c == '\n')	nextchar();
			continue;
		}

		if (c != '#')	break;
		while ( (c=nextchar()) >= 0 && c != '\n')
			;
		if (c == '\n')
		{
			t.Type(Token::semicolon);
			return;
		}
	}

	if (c < 0)
	{
		t.Type(lasttokentype == Token::semicolon ? Token::eof
			: Token::semicolon);
		return;
	}

	// String, quoted by ", ', or `


Buffer	&pattern=t.String();
	pattern.reset();

	if (c == '\'' || c == '"' || c == '`')
	{
	Token::tokentype ttype=Token::qstring;
	int quote_char=c;

		if (c == '\'')	ttype=Token::sqstring;
		if (c == '`')	ttype=Token::btstring;

		nextchar();

	int	q;

		// Grab string until matching close is found.

		while ((q=curchar()) != c)
		{
			if (q < 0 || q == '\n' || q == '\r')
			{
missquote:
				error("maildrop: Missing ', \", or `.\n");
				return;
			}

			// Backslash escape

			if (q != '\\')
			{
				nextchar();
				pattern.push(q);
				continue;
			}
			nextchar();

			// Look what's after the backslash.
			// If it's whitespace, we may have a continuation
			// on the next line.

		int	qq=curchar();

			if (qq < 0)	goto missquote;
			if (!isspace(qq) && qq != '\r' && qq != '\n')
			{
				if (qq != quote_char && qq != '\\')
					pattern.push('\\');
				pattern.push(qq);
				nextchar();
				continue;
			}

			// If it's not a continuation, we need to dutifully
			// save the characters as the string.  So, save the
			// current length of the string, and backtrack if
			// necessary.

		int	l=pattern.Length();
			pattern.push('\\');

			// Collect all whitespace after the backslash,
			// not including newline characters.

			while ((q=curchar()) >= 0 && isspace(q) &&
				q != '\r' && q != '\n')
			{
				pattern.push(q);
				nextchar();
			}
			if (q < 0)	goto missquote;

			// If the next character is a newline char, or
			// a comment, we have a continuation.

			if (q != '#' && q != '\r' && q != '\n')	continue;
			pattern.Length(l);	// Discard padding
			while (q != '\n')
			{
				if (q < 0)	goto missquote;
				nextchar();
				q=curchar();
			}
			// Discard all whitespace at the beginning of the
			// next line.
			nextchar();
			while ( (q=curchar()) >= 0 && isspace(q))
				nextchar();
			if (q < 0)	goto missquote;
		}
		nextchar();
		t.Type(ttype);
		return;
	}

	// A pattern - "/", then arbitrary text, terminated by "/"

	if (c == '/' && lasttokentype != Token::equals &&
		lasttokentype != Token::tokento &&
		lasttokentype != Token::tokencc)
	{
		pattern.push(c);
		nextchar();
		c=curchar();
		if (c == '\r' || c == '\n' || c < 0 || isspace(c))
		{
			t.Type(Token::divi);
			return;
		}

		while ( (c=curchar()) != '/')
		{
			if (c < 0 || c == '\r' || c == '\n')
				return;	// Error token - let parser throw
					// an error
			if (c == '\\')
			{
				pattern.push(c);
				nextchar();
				c=curchar();
				if (c < 0 || c == '\r' || c == '\n')
					return;
			}

			pattern.push(c);
			nextchar();
		}
		pattern.push(c);
		nextchar();
		if ((c=curchar()) == ':')
		{
			pattern.push(c);
			nextchar();
			while ( (c=curchar()) >= 0 && (isalnum(c) ||
				c == '-' || c == '+' || c == '.' || c == ','))
			{
				pattern.push(c);
				nextchar();
			}
		}
		t.Type(Token::regexpr);
		return;
	}

// Letters, digits, -, ., :, /, can be in an unquoted string

#define	ISUNQSTRING(x)	(x >= 0 && (isalnum(x) || (x) == '_' || x == '-' || \
	(x) == '@' || (x) == '.' || x == ':' || x == SLASH_CHAR || x == '$' || \
        x == '{' || x == '}'))

// Unquoted string may not begin with {}

#define	ISLUNQSTRING(x)	(x >= 0 && (isalnum(x) || (x) == '_' || x == '-' || \
	(x) == '@' || (x) == '.' || x == ':' || x == SLASH_CHAR || x == '$'))

	if (ISLUNQSTRING(c))
	{
		do
		{
			nextchar();
			pattern.push(c);
			c=curchar();
		} while ( ISUNQSTRING(c) );

		while ( c >= 0 && isspace(c) && c != '\r' && c != '\n')
		{
			nextchar();
			c=curchar();
		}
		if (pattern.Length() == 2)
		{
		int	n= ((int)(unsigned char)*(const char *)pattern) << 8
				| (unsigned char)((const char *)pattern)[1];

			switch (n)	{
			case (('l' << 8) | 't'):
				t.Type(Token::slt);
				return;
			case (('l' << 8) | 'e'):
				t.Type(Token::sle);
				return;
			case (('g' << 8) | 't'):
				t.Type(Token::sgt);
				return;
			case (('g' << 8) | 'e'):
				t.Type(Token::sge);
				return;
			case (('e' << 8) | 'q'):
				t.Type(Token::seq);
				return;
			case (('n' << 8) | 'e'):
				t.Type(Token::sne);
				return;
			case (('t' << 8) | 'o'):
				t.Type(Token::tokento);
				return;
			case (('c' << 8) | 'c'):
				t.Type(Token::tokencc);
				return;
			}
		}
		if (pattern == "length")
			t.Type(Token::length);
		else if (pattern == "substr")
			t.Type(Token::substr);
		else if (pattern == "if")
			t.Type(Token::tokenif);
		else if (pattern == "elsif")
			t.Type(Token::tokenelsif);
		else if (pattern == "else")
			t.Type(Token::tokenelse);
		else if (pattern == "while")
			t.Type(Token::tokenwhile);
		else if (pattern == "exception")
			t.Type(Token::exception);
		else if (pattern == "echo")
			t.Type(Token::echo);
		else if (pattern == "xfilter")
			t.Type(Token::tokenxfilter);
		else if (pattern == "system")
			t.Type(Token::tokensystem);
		else if (pattern == "dotlock")
			t.Type(Token::dotlock);
		else if (pattern == "flock")
			t.Type(Token::flock);
		else if (pattern == "logfile")
			t.Type(Token::logfile);
		else if (pattern == "log")
			t.Type(Token::log);
		else if (pattern == "include")
			t.Type(Token::include);
		else if (pattern == "exit")
			t.Type(Token::exit);
		else if (pattern == "foreach")
			t.Type(Token::foreach);
		else if (pattern == "getaddr")
			t.Type(Token::getaddr);
		else if (pattern == "lookup")
			t.Type(Token::lookup);
		else if (pattern == "escape")
			t.Type(Token::escape);
		else if (pattern == "tolower")
			t.Type(Token::to_lower);
		else if (pattern == "toupper")
			t.Type(Token::to_upper);
		else if (pattern == "hasaddr")
			t.Type(Token::hasaddr);
		else if (pattern == "gdbmopen")
			t.Type(Token::gdbmopen);
		else if (pattern == "gdbmclose")
			t.Type(Token::gdbmclose);
		else if (pattern == "gdbmfetch")
			t.Type(Token::gdbmfetch);
		else if (pattern == "gdbmstore")
			t.Type(Token::gdbmstore);
		else if (pattern == "time")
			t.Type(Token::timetoken);
		else if (pattern == "import")
			t.Type(Token::importtoken);
		else if (pattern == "-")		// Hack
			t.Type(Token::minus);
		else if (pattern == "unset")
			t.Type(Token::unset);
		else
			t.Type(Token::qstring);
		return;
	}
	switch (c)	{
	case '&':
		nextchar();
		if ( curchar() == '&')
		{
			t.Type(Token::land);
			nextchar();
			return;
		}
		t.Type(Token::band);
		return;
	case '|':
		nextchar();
		if ( curchar() == '|')
		{
			t.Type(Token::lor);
			nextchar();
			return;
		}
		t.Type(Token::bor);
		return;
	case '{':
		t.Type(Token::lbrace);
		nextchar();
		return;
	case '}':
		t.Type(Token::rbrace);
		nextchar();
		return;
	case '(':
		t.Type(Token::lparen);
		nextchar();
		return;
	case ')':
		t.Type(Token::rparen);
		nextchar();
		return;
	case ';':
		t.Type(Token::semicolon);
		nextchar();
		return;
	case '+':
		t.Type(Token::plus);
		nextchar();
		return;
	case '*':
		t.Type(Token::mult);
		nextchar();
		return;
	case '~':
		t.Type(Token::bitwisenot);
		nextchar();
		return;
	case '<':
		nextchar();
		if ( curchar() == '=')
		{
			nextchar();
			t.Type(Token::le);
			return;
		}
		t.Type(Token::lt);
		return;
	case '>':
		nextchar();
		if ( curchar() == '=')
		{
			nextchar();
			t.Type(Token::ge);
			return;
		}
		t.Type(Token::gt);
		return;
	case '=':
		nextchar();
		if ( curchar() == '~')
		{
			nextchar();
			t.Type(Token::strregexp);
			return;
		}
		if ( curchar() != '=')
		{
			t.Type(Token::equals);
			return;
		}
		nextchar();
		t.Type(Token::eq);
		return;
	case '!':
		nextchar();
		if ( curchar() != '=')
		{
			t.Type(Token::logicalnot);
			return;
		}
		nextchar();
		t.Type(Token::ne);
		return;
	case ',':
		nextchar();
		t.Type(Token::comma);
		return;
	}
	nextchar();
	// Let the parser throw an error.
}

void	Lexer::errmsg(const char *emsg)
{
	errmsg(linenum, emsg);
}

void	Lexer::errmsg(unsigned long lnum, const char *emsg)
{
Buffer	errbuf;

	errbuf=filename;
	errbuf += "(";
	errbuf.append(lnum);
	errbuf += "): ";
	errbuf += emsg;
	errbuf += "\n";
	merr << errbuf;
}
