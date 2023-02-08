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

	const char *c_str() const { return (const char *)buf; }
	size_t	size() const { return (buflength); }
	void	resize(size_t l) { if (l < (size_t)buflength) buflength=l; }

	Buffer(const Buffer &);		// UNDEFINED
	Buffer &operator=(const Buffer &);

	void	push_back(int c) { if (buflength < bufsize)
				{
					buf[buflength]=c;
					++buflength;
				}
				else	append(c); }
	void	pop_back() { --buflength; }
	char	back() { return buf[buflength-1]; }
	void	clear() { buflength=0; }

private:
	void	append(int);
	void	replace(const char *);
public:

	template<typename iter>
	void    append(iter b, iter e)
	{
		while (b != e)
		{
			push_back(*b);
			++b;
		}
	}

	Buffer	&operator=(const char *p) { replace(p); return (*this); }
	Buffer	&operator += (const Buffer &p) { append(p.buf, p.buf+p.buflength); return (*this); }
	Buffer	&operator += (const char *p) { append(p, p+strlen(p)); return (*this); }
	void    push_back_0() { push_back(0); }
	int	compare(const Buffer &) const;
	int	compare(const char *) const;
	int	operator==(const Buffer &b) const
		{ return ( compare(b) == 0); }
	int	operator==(const char *b) const
		{ return ( compare(b) == 0); }
} ;

void	add_number(Buffer &buf, double val);
void	add_integer(Buffer &buf, unsigned long n);
inline void	set_integer(Buffer &buf, unsigned long n)
{
	Buffer b;

	add_integer(b, n);
	buf=b;
}

int	extract_int(const Buffer &buf, const char * =0);

#endif
