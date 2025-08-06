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
#include <iterator>
#include <type_traits>

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

template<typename iterb, typename itere>
std::pair<std::string, bool> encode(iterb &&b, itere &&e,
				    const std::string &charset,
				    int (*qp_allow)(char))
{
	std::pair<std::string, bool> ret;

	std::get<1>(ret)=false;

	std::u32string ustr;

	unicode::iconvert::tou::convert(
		std::forward<iterb>(b),
		std::forward<itere>(e),
		charset,
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

template<typename in_iterb, typename in_itere> struct iter {
	in_iterb &b;
	in_itere &e;

	std::string undo_buf;

	iter(in_iterb &b, in_itere &e) : b{b}, e{e} {}

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

/*
  Standalone quoted-printable and base64 decoders

  qpdecoder decoder{closure, true};
  base64decoder decoder{closure};

  The closure object is an object that's callable with two parameters,
  a const char * and a size_t. The object gets repeatedly called with decoded
  content, passing a chunk at a time. qpdecoder's second bool parameter is
  true if quoted-printable semantics in RFC 2047 are to be used (namely, _ is
  decoded to a space), and false for the plain RFC 2045 quoted-printable
  transfer encoding.

  Both qpdecoder and base64decoder are templates, and the default deduction
  guide takes care of the template parameters. The closure parameter may be
  passed in by reference or by value. If passed in by value it gets copied
  and stored in the decoder object. After decoding is complete, and just
  before the decoder object gets destroyed, the final value of the closure
  is extracted via an overloaded closure operator:

  closure=decoder;

  decoder is itself a callable object, with a const char * and a size_t
  parameter. This defines the quoted-printable or base64-encoded content,
  a chunk at a time.

  Note: the closure object can be called just before the decoder object is
  destroyed, to emit the final chunk, or chunks, of decoded contents, or
  it can be called when extracting the final value of the closure object from
  the decoder, for the same reason. Both the extraction operator overload, and
  a destructor, signals the end of the encoded content.

  If the encoded sequence is bad, the decoded content will have an obnoxious
  error message (but only on the first encounter of the badness, the others are
  swallowed silently). This can happen in the destructor or the extraction
  operator, if the encoded sequence is truncated in a manner that breaks the
  encoding.

*/

struct qpdecoder_base {
private:
	const bool mime_encoded_word;
	unsigned char nybble;
	size_t (qpdecoder_base::*handler)(const char *, size_t);
	size_t do_char(const char *, size_t);
	size_t do_prev_equal(const char *, size_t);
	size_t do_prev_equal_cr(const char *, size_t);
	size_t do_prev_equal_h1(const char *, size_t);

	bool error_occured{false};
	void report_error();

	virtual void emit(const char *, size_t)=0;

protected:
	void finished();
public:
	qpdecoder_base(bool mime_encoded_word);
	~qpdecoder_base();

	void process_char(const char *, size_t);
};

template<typename out_iter_type>
struct qpdecoder : qpdecoder_base {

private:
	std::conditional_t<
		std::is_same_v<out_iter_type, out_iter_type &>,
		out_iter_type,
		std::remove_cv_t<
			std::remove_reference_t<out_iter_type>>> out_iter;

	void emit(const char *ptr, size_t n) override
	{
		out_iter(ptr, n);
	}
public:
	template<typename T>
	qpdecoder(T &&out_iter, bool mime_encoded_word) :
		qpdecoder_base{mime_encoded_word},
		out_iter{
			std::forward<T>(out_iter)
		}
	{
	}

	~qpdecoder()
	{
		this->finished();
	}

	void operator()(const char *p, size_t n)
	{
		this->process_char(p, n);
	}

	operator decltype(out_iter)()
	{
		this->finished();

		return out_iter;
	}
};

template<typename out_iter_type>
qpdecoder(out_iter_type &&, bool) -> qpdecoder<out_iter_type>;;

template<typename out_iter_type>
qpdecoder(out_iter_type &, bool) -> qpdecoder<out_iter_type &>;

struct base64decoder_base {
private:
	unsigned char buffer[4];
	size_t bufferp{0};

	bool error_occured{false};

	virtual void emit(const char *, size_t)=0;
	bool seen_end{false};

protected:
	void finished();
public:
	base64decoder_base();
	~base64decoder_base();

	void report_error();
	void process_char(const char *, size_t);
};

template<typename out_iter_type>
struct base64decoder : base64decoder_base {

private:
	std::conditional_t<
		std::is_same_v<out_iter_type, out_iter_type &>,
		out_iter_type,
		std::remove_cv_t<
			std::remove_reference_t<out_iter_type>>> out_iter;

	void emit(const char *p, size_t n) override
	{
		out_iter(p, n);
	}
public:
	template<typename T>
	base64decoder(T &&out_iter)
		: out_iter{std::forward<T>(out_iter)}
	{
	}

	~base64decoder()
	{
		this->finished();
	}

	void operator()(const char *p, size_t n)
	{
		this->process_char(p, n);
	}

	operator decltype(out_iter)()
	{
		this->finished();

		return out_iter;
	}
};

template<typename out_iter_type>
base64decoder(out_iter_type &&) -> base64decoder<out_iter_type>;;

template<typename out_iter_type>
base64decoder(out_iter_type &) -> base64decoder<out_iter_type &>;

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
inline bool do_decode_rfc2047_atom(in_iter &inp,
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

	const char *error_msg=nullptr;

	switch (enc) {
	case 'q':
	case 'Q':
		callback(
			charset, language,
			[&]
			(auto &out)
			{
				qpdecoder decoder{
					[&]
					(const char *p, size_t n)
					{
						while (n)
						{
							*out++=*p++;
							--n;
						}
					}, true};

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

					char ch=static_cast<char>(c);

					decoder(&ch, 1);
				}

				error_msg="qp decoding error";
			});
		break;
	case 'b':
	case 'B':
		callback(
			charset, language,
			[&]
			(auto &out)
			{
				base64decoder decoder{
					[&]
					(const char *p, size_t n)
					{
						while (n)
						{
							*out++=*p++;
							--n;
						}
					}};

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

						break;
					}

					char wcc=wc;

					decoder(&wcc, 1);
				}
				decoder.report_error();
				// Error no matter how we got here.
			});
		break;
	default:
		error(inp.b, inp.e, "unknown RFC 2047 encoding");
		return false;
	}

	if (error_msg)
	{
		error(inp.b, inp.e, error_msg);
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
inline void decode_rfc2047_atom(in_iter &inp,
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

template<typename in_iter_b, typename in_iter_e>
void ignore_decoding_error(const in_iter_b &, const in_iter_e &,
			   const char *)
{
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

template<typename in_iterb,
	 typename in_itere, typename callback_closure,
	 typename error_closure=void(const in_iterb &, const in_itere &,
				     const char *)>
void decode(in_iterb b, in_itere e,
	    callback_closure &&callback,
	    error_closure &&error=ignore_decoding_error<in_iterb, in_itere>)
{
	std::string charset;
	std::string language;
	std::string skip_buf;
	iter<in_iterb, in_itere> inp{b, e};

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

// decode(), then convert the decoded text to unicode, using the
// character set indication. The third parameter is a char32_t output
// iterator.

template<typename in_iterb,
	 typename in_itere,
	 typename unicode_iter,
	 typename error_closure=void(in_iterb, in_itere, const char *)>
auto decode_unicode(in_iterb &&b, in_itere &&e,
		    unicode_iter &&iter,
		    error_closure &&error=ignore_decoding_error<in_iterb,
		    in_itere>)
{
	std::string buffer;

	decode(std::forward<in_iterb>(b),
	       std::forward<in_itere>(e),
	       [&]
	       (const auto &charset, const auto &language, auto &&callback)
	       {
		       buffer.clear();

		       auto striter=std::back_inserter(buffer);
		       callback(striter);

		       bool errflag;

		       unicode::iconvert::tou::convert(
			       buffer.begin(),
			       buffer.end(),
			       charset, errflag, iter);

		       if (errflag)
			       error(b, e, "[unicode encoding error]");
	       },
	       std::forward<error_closure>(error));
	if constexpr(!std::is_same_v<unicode_iter, unicode_iter &>)
		return iter;
}

#if 0
{
#endif
};

#endif

#include "rfc822/rfc822_2047.h"

#endif
