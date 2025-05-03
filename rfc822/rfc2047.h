#ifndef	rfc2047_h
#define	rfc2047_h

#include	<stdlib.h>
#include	<string.h>
#include	<courier-unicode.h>
/*
** Copyright 1998 - 2009 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#ifdef  __cplusplus
extern "C" {
#endif
#if 0
}
#endif

struct unicode_info;
extern const char rfc2047_xdigit[];
extern const unsigned char rfc2047_decode64tab[];

/*
** Raw RFC 2047 parser.
**
** rfc2047_decoder() repeatedly invokes the callback function, passing it
** the decoded RFC 2047 string that's given as an argument.
*/

int rfc2047_decoder(const char *text,
		    void (*callback)(const char *chset,
				     const char *lang,
				     const char *content,
				     size_t cnt,
				     void *dummy),
		    void *ptr);

/*
** rfc2047_print_unicodeaddr is like rfc822_print, except that it converts
** RFC 2047 MIME encoding to 8 bit text.
*/

struct rfc822a;

int rfc2047_print_unicodeaddr(const struct rfc822a *a,
			      const char *charset,
			      void (*print_func)(char, void *),
			      void (*print_separator)(const char *, void *),
			      void *ptr);


/*
** And now, let's encode something with RFC 2047.  Encode the following
** string in the indicated character set, into a malloced buffer.  Returns 0
** if malloc failed.
*/

char *rfc2047_encode_str(const char *str, const char *charset,
			 int (*qp_allow)(char c) /* See below */);


/* Potential arguments for qp_allow */

int rfc2047_qp_allow_any(char); /* Any character */
int rfc2047_qp_allow_comment(char); /* Any character except () */
int rfc2047_qp_allow_word(char); /* See RFC2047, bottom of page 7 */



/*
** rfc2047_encode_header allocates a buffer, and MIME-encodes a header.
**
** The name of the header, passed as the first parameter, should be
** "From", "To", "Subject", etc... It is not included in the encoded contents.
*/
char *rfc2047_encode_header_tobuf(const char *name, /* Header name */
				  const char *header, /* Header's contents */
				  const char *charset);

/*
** rfc2047_encode_header_addr allocates a buffer, and MIME-encodes an
** RFC822 address header.
**
*/
char *rfc2047_encode_header_addr(const struct rfc822a *a,
				 const char *charset);


/* Internal functions */
int rfc2047_encode_callback(const char32_t *uc,
			    size_t ucsize,
			    const char *charset,
			    int (*qp_allow)(char),
			    int (*func)(const char *, size_t, void *),
			    void *arg);

#if 0
{
#endif
#ifdef  __cplusplus
}

#include <string>
#include <utility>

namespace rfc2047 {
#if 0
}
#endif

// C++ version of rfc2047_encode_str
//
// The string is defined by a sequence specified by a beginning and an
// ending iterator.
//
// Returns a string am a bool flag that's true if there was an encoding
// error.

template<typename iter>
std::pair<std::string, bool> encode(iter b, iter e, const std::string &charset,
				    int (*qp_allow)(char))
{
	std::pair<std::string, bool> ret;

	std::get<1>(ret)=false;

	std::u32string ustr;

	unicode::iconvert::tou::convert(
		b, e, charset,
		std::get<1>(ret),
		std::back_inserter(ustr));

	if (ret.second)
		return ret;

	size_t length=0;

	if (rfc2047_encode_callback(
		    ustr.c_str(), ustr.size(), charset.c_str(),
		    qp_allow,
		    []
		    (const char *, size_t n, void *voidp) -> int
		    {
			    *static_cast<size_t *>(voidp) += n;
			    return 0;
		    },
		    &length))
	{
		ret.second=true;
		return ret;
	}

	ret.first.reserve(length);

	rfc2047_encode_callback(
		ustr.c_str(), ustr.size(), charset.c_str(),
		qp_allow,
		[]
		(const char *p, size_t n, void *voidp) -> int
		{
			auto ret=static_cast<std::string *>(voidp);

			ret->insert(ret->end(), p, p+n);
			return 0;
		},
		&ret.first);

	return ret;
}

inline std::pair<std::string, bool> encode(const std::string &s,
					   const std::string &charset,
					   int (*qp_allow)(char))
{
	return encode(s.begin(), s.end(), charset, qp_allow);
}

// Helper object used by decode_rfc2047()
//
// Keeps track of the begin/end iterator for the input sequence.
//
// Implements an undo buffer. decode_rfc2047() needs the ability to
// undo the read sequence.

template<typename in_iter> struct iter {
	in_iter &b, &e;

	std::string undo_buf;

	iter(in_iter &b, in_iter &e) : b{b}, e{e} {}

	// Next character, possibly cached. EOF signaled by -1.
	int peek() const
	{
		if (!undo_buf.empty())
			return static_cast<unsigned char>(undo_buf.back());

		if (b == e)
			return -1;

		return static_cast<unsigned char>(*b);
	}

	// Advance to the next character in the input sequence.

	void next()
	{
		if (!undo_buf.empty())
			undo_buf.pop_back();
		else
			++b;
	}

	// Undo read character from the input sequence.
	void undo(char c)
	{
		undo_buf.push_back(c);
	}
};

// Invoked from decoder(), decode an RFC-2047 atom after a "=?"
//
// inp defined the input sequence.
//
// When decode_rfc2047_atom reads "?=" it calls here.
//
// charset and language are passed by reference too, they are expected to be
// strings that are repeatedly used to parse atoms, for optimal memory usage.
//
// skip_buf is another buffer that's passed in by reference, for optimal
// memory usage.
//
// The callback and the error parameters are forwarded from decoder().
//
// Invokes the callback parameter with a closure. The closure must read the
// input sequence until the '?' and '=' was read from it, marking the end of the
// rfc 2047 atom. The first closure returns after calling the 2nd closure,
// and do_decode_rfc2047_atom returns after the first closure returns.
//
// Before returning the input sequence following the ?= is read ahead, and if
// a whitespace and another ?= follows true gets returned. Otherwise the
// read ahead sequece is undone and false gets returned.

template<typename in_iter, typename callback_closure, typename error_closure>
inline bool do_decode_rfc2047_atom(iter<in_iter> &inp,
				   std::string &charset,
				   std::string &language,
				   std::string &skip_buf,
				   callback_closure &&callback,
				   error_closure &&error)
{
	charset.clear();
	language.clear();

	// Expect to read charset(*language)?[QB]?....

	int c;
	while (1)
	{
		c=inp.peek();

		if (c < 0)
		{
			error(inp.b,
			      inp.e, "error parsing RFC 2047 character set");
			return false;
		}
		if (c == '?' || c == '*')
			break;

		charset.push_back(c);
		inp.next();
	}

	if (c == '*')
	{
		inp.next();
		while (1)
		{
			c=inp.peek();

			if (c < 0)
			{
				error(inp.b, inp.e, "error parsing RFC 2047 language");
				return false;
			}
			if (c == '?')
				break;
			language.push_back(c);
			inp.next();
		}
	}

	// Input sequence is now at ?[qB]?....
	//
	// Read the 2nd ?

	inp.next();

	int enc=inp.peek();

	if (enc < 0)
		;
	else
	{
		inp.next();

		if (inp.peek() == '?')
		{
			inp.next();
		}
		else
			enc=0;
	}

	switch (enc) {
	case 'q':
	case 'Q':
		callback(
			charset, language,
			[&]
			(auto &out)
			{
				while (1)
				{
					int c=inp.peek();

					if (c < 0)
						break;

					if (c == '?')
					{
						inp.next();

						int c2=inp.peek();
						if (c2 == '=')
						{
							inp.next();
							return;
						}

						if (c2 < 0)
							// Error no matter what
							break;

						inp.undo(c2);
					}

					inp.next();

					if (c != '=')
					{
						*out++=c;
						continue;
					}

					const char *hi, *lo;

					c=inp.peek();

					if (c < 0 ||
					    (hi=strchr(rfc2047_xdigit, c))
					    == NULL)
						break;

					inp.next();
					c=inp.peek();

					if (c < 0 ||
					    (lo=strchr(rfc2047_xdigit, c))
					    == NULL)
						break;
					inp.next();

					int h=hi-rfc2047_xdigit;
					int l=lo-rfc2047_xdigit;

					if (h > 15) h-=6;
					if (l > 15) l-=6;

					*out++=static_cast<char>(
						(h << 4) | l
					);
				}

				error(inp.b, inp.e, "qp decoding error");
			});
		break;
	case 'b':
	case 'B':
		callback(
			charset, language,
			[&]
			(auto &out)
			{
				while (1)
				{
					int wc=inp.peek();

					if (wc < 0)
						break;

					wc=static_cast<unsigned char>(wc);
					inp.next();

					if (wc == '?')
					{
						int c2=inp.peek();
						if (c2 == '=')
						{
							inp.next();
							return;
						}

						if (c2 < 0)
							// Error no matter what
							break;

						inp.undo(c2);
					}

					int xc=inp.peek();

					if (xc < 0 || xc == '?')
						break;
					xc=static_cast<unsigned char>(xc);

					inp.next();

					int yc=inp.peek();
					if (yc < 0 || yc == '?')
						break;

					yc=static_cast<unsigned char>(yc);

					inp.next();

					int zc=inp.peek();

					if (zc < 0 || zc == '?')
						break;
					zc=static_cast<unsigned char>(zc);

					inp.next();

					int w=rfc2047_decode64tab[wc];
					int x=rfc2047_decode64tab[xc];
					int y=rfc2047_decode64tab[yc];
					int z=rfc2047_decode64tab[zc];

					unsigned char g= (w << 2) | (x >> 4);
					unsigned char h= (x << 4) | (y >> 2);
					unsigned char i= (y << 6) | z;

					*out++=g;
					if (yc != '=')
						*out++=h;
					if (zc != '=')
						*out++=i;
					else if (inp.peek() != '?')
						break;
				}

				error(inp.b, inp.e, "base64 decoding error");
			});
		break;
	default:
		error(inp.b, inp.e, "unknown RFC 2047 encoding");
		return false;
	}

	// Check for another RFC 2047 atom.
	//
	// We'll always clean up skip_buf afterwards, so it's always
	// empty now.

	while (strchr(" \t\r\n", static_cast<char>(c=inp.peek())) != NULL)
	{
		skip_buf.push_back(c);
		inp.next();
	}

	if (c == '=')
	{
		skip_buf.push_back(c);
		inp.next();

		if (inp.peek() == '?')
		{
			inp.next();
			skip_buf.clear();
			return true;
		}
	}

	while (!skip_buf.empty())
	{
		inp.undo(skip_buf.back());

		skip_buf.pop_back();
	}
	return false;
}

// Extract consecutive RFC 2047 atoms

template<typename in_iter, typename callback_closure, typename error_closure>
inline void decode_rfc2047_atom(iter<in_iter> &inp,
				std::string &charset,
				std::string &language,
				std::string &skip_buf,
				callback_closure &&callback,
				error_closure &&error)
{
	while (do_decode_rfc2047_atom(inp, charset, language, skip_buf,
				      std::forward<callback_closure>(callback),
				      std::forward<error_closure>(error)))
		;
}

// C++ version of rfc2047_decoder, the raw RFC 2047 decoder.
//
// An input sequence is defined by a beginning and an ending iterator.
//
// The next parameter is a callback. The callback gets invoked, repeatedly,
// to define the decoded string. The callback gets invoked with three
// parameters: charset, language, and another closure, the 2nd closure.
//
// The callback must invoke the 2nd closure with a single parameter,
// a reference to an output iterator. The 2nd closure writes the decoded RFC
// 2047 string to the output iterator, advancing it.
//
// The last parameter is a closure that gets invoked in the event of an
// error. The closure receives current b and e, and an error message.
//
// Note that the 2nd closure can call the error closure too.

template<typename in_iter, typename callback_closure, typename error_closure>
void decode(in_iter &&b, in_iter &&e,
	    callback_closure &&callback,
	    error_closure &&error)
{
	std::string charset;
	std::string language;
	std::string skip_buf;
	iter<in_iter> inp{b, e};

	int c;

	while ((c=inp.peek()) >= 0)
	{
		if (c == '=')
		{
			inp.next();
			c=inp.peek();

			if (c == '?')
			{
				inp.next();

				decode_rfc2047_atom(
					inp,
					charset, language, skip_buf,
					std::forward<callback_closure>(
						callback
					),
					std::forward<error_closure>(
						error
					));
				continue;
			}
			inp.undo('=');
		}

		charset="utf-8";
		language.clear();

		callback(
			charset, language,
			[&]
			(auto &out)
			{
				while (1)
				{
					int c=inp.peek();

					if (c < 0)
						return;

					if (c == '=')
					{
						inp.next();
						int c2=inp.peek();
						inp.undo('=');

						if (c2 == '?')
						{
							// input is now at
							// ?=
							return;
						}
						c='=';
					}

					*out++=c;
					inp.next();
				}
			});
	}
}

#if 0
{
#endif
};

#endif

#endif
