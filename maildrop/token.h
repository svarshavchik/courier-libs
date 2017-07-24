#ifndef	token_h
#define	token_h


#include	"buffer.h"

///////////////////////////////////////////////////////////////////////////
//
// The Token class represents one syntactical element in a recipe file, such
// as a semicolon, a brace, or a text string.  The Lexer class creates
// one Token after another from the recipe file.
//
///////////////////////////////////////////////////////////////////////////

class	Token {

	Buffer	buf;

public:
	enum tokentype {
		error,
		eof,
		qstring,		// Quoted string
		sqstring,		// Sinqle-quoted string
		btstring,		// Backticked string
		equals,
		lbrace,
		rbrace,
		semicolon,
		regexpr,
                plus,
		minus,
		mult,
		divi,
		lt,
		le,
		gt,
		ge,
		eq,
		ne,
		lor,
		land,
		bor,
		band,
		lparen,
		rparen,
		logicalnot,
		bitwisenot,
		slt,
		sle,
		sgt,
		sge,
		seq,
		sne,
		length,
		substr,
		comma,
		strregexp,
		tokenif,
		tokenelse,
		tokenelsif,
		tokenwhile,
		tokento,
		tokencc,
		exception,
		echo,
		tokenxfilter,
		tokensystem,
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
		};
private:
	tokentype type;
public:
	Token() : type(error)	{}
	~Token()		{}
	Token(const Token &);	// UNDEFINED
	Token &operator=(const Token &t) { type=t.type; buf=t.buf; return (*this); }

	void Type(tokentype t) { type=t; }
	void Type(tokentype t, const Buffer &tbuf) { type=t; buf=tbuf; }
	tokentype Type() const { return (type); }
	const Buffer &String() const { return (buf); }
	Buffer &String() { return (buf); }

	const char *Name();
} ;
#endif
