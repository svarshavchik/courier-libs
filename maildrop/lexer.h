#ifndef	lexer_h
#define	lexer_h


#include	"mio.h"
#include	"buffer.h"
#include	"token.h"

////////////////////////////////////////////////////////////////////////////
//
// Lexer is a lexical scanner - translates a text file into individual
// tokens.  The Token class is used to represent an individual token, the
// Lexer class is used to transform a text file into a list of tokens.
//
// The scanning algorythm is quite involved.  We may read ahead in the file,
// then back up.  An 'undo' buffer is maintained.  The actual file is
// referred to indirectly via the Mio class.
//
// The concept of the 'current line number' and 'filename' is also recorded
// by the Lexer object.  An error message function is provided - given an
// error message, the filename, and the line number is printer to standard
// error, followed by the error message.
//
////////////////////////////////////////////////////////////////////////////

class	Lexer {

	Mio file;
	int linenum;
	Buffer filename;
	Token::tokentype lasttokentype;
	// curchar() represents the next character in the file.
	// Calling curchar() does NOT actual "read" the character, this
	// is a "peek" function.

	int	curchar() {	return (file.peek()); }

	// nextchar() is used to actually read the next character.

	int	nextchar() { int c=file.get();

				if (c == '\n')	++linenum;
				return (c);
			}
	void	error(const char *);

public:
	Lexer()	{}
	~Lexer()	{}
	int	Open(const char *);	// Open file to read
	void	token(Token &);		// Scan the next token in
private:
	void	token2(Token &);
public:
	void	errmsg(const char *);	// Show error message
	void	errmsg(unsigned long, const char *);
					// Show error message for a different
					// line
	int	Linenum() { return (linenum); }
					// Return current line number in recipe
					// file
} ;

#endif
