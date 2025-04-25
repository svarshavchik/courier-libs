#ifndef	rfc2047_h
#define	rfc2047_h

#include	<stdlib.h>
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
#if 0
{
#endif
};

#endif

#endif
