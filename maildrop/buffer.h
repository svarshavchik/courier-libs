#ifndef	buffer_h
#define	buffer_h


#include	"config.h"
#include	<string.h>

///////////////////////////////////////////////////////////////////////////
//
// Generic text/data buffer.  Not null-terminated by default.
//
// This class is used to store arbitrary text strings.  It is used by
// the lexical scanner to build up text that's recognized as a token,
// and the rest of the code to store strings.
//
///////////////////////////////////////////////////////////////////////////

class Buffer {

	unsigned char *buf;
	int	bufsize;
	int	buflength;
public:
	Buffer() : buf(0), bufsize(0), buflength(0)	{}
	~Buffer()	{ if (buf)	delete[] buf; }
	const unsigned char *Ptr() const { return (buf); }
	operator const unsigned char *() const { return (buf); }
	operator const char *() const { return ((const char *)buf); }
	int	Int(const char * =0) const;
	int	Length() const { return (buflength); }
	void	Length(int l) { if (l < buflength) buflength=l; }

	Buffer(const Buffer &);		// UNDEFINED
	Buffer &operator=(const Buffer &);

	void	push(int c) { if (buflength < bufsize)
				{
					buf[buflength]=c;
					++buflength;
				}
				else	append(c); }
	int	pop() { return (buflength ? buf[--buflength]:0); }
	int	peek() { return (buflength ? buf[buflength-1]:0); }
	void	reset() { buflength=0; }

private:
	void	append(int);
public:
	void	append(const void *, int);
	void	set(const char *);
	void	append(const char *p) { append(p, strlen(p)); }
	void	set(unsigned long n) { buflength=0; append(n); }
	void	append(unsigned long n);
	void	append(double);
	Buffer	&operator=(const char *p) { set(p); return (*this); }
	Buffer	&operator += (const Buffer &p) { append(p.buf, p.buflength); return (*this); }
	Buffer	&operator += (const char *p) { append(p, strlen(p)); return (*this); }
	Buffer	&operator += (char c) { push(c); return (*this); }
	int	operator-(const Buffer &) const;
	int	operator-(const char *) const;
	int	operator==(const Buffer &b) const
		{ return ( operator-(b) == 0); }
	int	operator==(const char *b) const
		{ return ( operator-(b) == 0); }
} ;

#endif
