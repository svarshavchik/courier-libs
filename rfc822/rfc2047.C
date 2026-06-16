/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <courier-unicode.h>

#include "rfc2047.h"
#include <idn2.h>

#ifndef RFC2047_ENCODE_FOLDLENGTH
#define	RFC2047_ENCODE_FOLDLENGTH	76
#endif

const char rfc2047::xdigit[]="0123456789ABCDEFabcdef";

static const char base64tab[]=
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Use RFC 2047 to encode a string using the base64 algorithm.
static void encodebase64(std::string_view str,
			std::string_view charset,
			bool (*qp_allow)(char),
			rfc2047::encoder &encoder,
			bool &inappropriate)
{
	unsigned char ibuf[3];
	char obuf[4];

	encoder.encoded("=?");
	encoder.encoded(charset);
	encoder.encoded("?B?");

	while (str.size())
	{
		size_t n=str.size() > 3 ? 3:str.size();

		ibuf[0]= str[0];
		if (n>1)
			ibuf[1]=str[1];
		else
			ibuf[1]=0;
		if (n>2)
			ibuf[2]=str[2];
		else
			ibuf[2]=0;
		str.remove_prefix(n);

		obuf[0] = base64tab[ ibuf[0]        >>2 ];
		obuf[1] = base64tab[(ibuf[0] & 0x03)<<4|ibuf[1]>>4];
		obuf[2] = base64tab[(ibuf[1] & 0x0F)<<2|ibuf[2]>>6];
		obuf[3] = base64tab[ ibuf[2] & 0x3F ];
		if (n < 2)
			obuf[2] = '=';
		if (n < 3)
			obuf[3] = '=';

		encoder.encoded({obuf, 4});
	}

	encoder.encoded("?=");
}

#define ISSPACE(i) ((i)=='\t' || (i)=='\r' || (i)=='\n' || (i)==' ')
#define DOENCODEWORD(c) \
	((c) < 0x20 || (c) > 0x7F || (c) == '"' || \
	 (c) == '_' || (c) == '=' || (c) == '?' || !(*qp_allow)((char)c))

// Use RFC 2047 to encode a string using the quoted-printable algorithm.

static void encodeqp(std::string_view str,
		    std::string_view charset,
		    bool (*qp_allow)(char),
		    rfc2047::encoder &encoder,
		    bool &inappropriate)
{
	size_t i;
	char buf[3];

	encoder.encoded("=?");
	encoder.encoded(charset);
	encoder.encoded("?Q?");

	for (i=0; i<str.size(); ++i)
	{
		size_t j;

		for (j=i; j<str.size(); ++j)
		{
			if (str[j] == ' ' || DOENCODEWORD(str[j]))
				break;
		}

		if (j > i)
		{
			encoder.encoded(str.substr(i, j-i));

			if (j >= str.size())
				break;
		}
		i=j;

		if (str[i] == ' ')
			encoder.encoded("_");
		else
		{
			if (i == 0)
			{
				// The preceding char was ? so we can't follow
				// it with a = 
				inappropriate=true;
				return;
			}

			buf[0]='=';
			buf[1]=rfc2047::xdigit[ ( str[i] >> 4) & 0x0F ];
			buf[2]=rfc2047::xdigit[ str[i] & 0x0F ];

			encoder.encoded({buf, 3});
		}
	}

	encoder.encoded("?=");
}

// Use RFC 2047 to calculate whether the next word should be RFC2047-encoded.
//
// Returns false if not, true if any character in the next word is flagged by
// DOENCODEWORD().
//
// word_ptr: On entry: points to the starting offset of word in str.
//           On exit: points to the end of the word in str.

static bool encode_word(std::u32string_view str,
		       bool (*qp_allow)(char),
		       size_t &word_ptr)
{
	size_t i;
	bool encode=false;

	for (i=word_ptr; i<str.size(); ++i)
	{
		if (ISSPACE(str[i]))
			break;

		if (DOENCODEWORD(str[i]))
			encode=true;
	}

	word_ptr=i;
	return encode;
}

// Use RFC 2047 to calculate whether the next sequence of words should be RFC2047-encoded.
//
// Whatever encode_word() returns for the first word, look at the next word
// and keep going as long as encode_word() keeps returning the same value.

static bool encode_words(std::u32string_view str,
			bool (*qp_allow)(char),
			size_t &word_ptr)
{
	size_t i= word_ptr, j, k;

	bool flag=encode_word(str, qp_allow, i);

	if (!flag)
	{
		word_ptr=i;
		return flag;
	}

	j=i;

	while (j < str.size())
	{
		if (ISSPACE(str[j]))
		{
			++j;
			continue;
		}

		k=j;

		if (!encode_word(str, qp_allow, k))
			break;
		i=j=k;
	}

	word_ptr=i;
	return flag;
}

namespace {

	// Convert all or a part of the unicode string that's passed into
	// rfc2047::encode() into the requested character set.

	struct fromu_impl : public unicode::iconvert::fromu {

		// The buffer where the converted string is stored.

		std::string buffer;

		// Any errors encountered during conversion.

		bool errflag=false;

		// Callback from unicode::iconvert::fromu - appends the
		// converted chunk to the buffer.

		int converted(const char *p, size_t n) override
		{
			buffer.insert(buffer.end(), p, p+n);
			return 0;
		}

		// Convert a unicode string to the given character set.

		std::string convert(
			std::u32string_view str,
			const std::string &charset,
			bool &errflag
		)
		{
			buffer.clear();

			if (!begin(charset))
			{
				errflag=true;
				begin(unicode::iso_8859_1);
			}
			operator()(str.data(), str.size());

			bool end_errflag=false;
			end(end_errflag);
			errflag=errflag || end_errflag;

			if (!buffer.empty() && buffer.back() == 0)
				buffer.pop_back();

			return buffer;
		}
	};
}

// Encode a sequence of words.

static void do_encode_words_method(
	std::u32string_view uc,
	const std::string &charset,
	bool (*qp_allow)(char),

	// Reduce the maximum length of the encoded words by the
	// offset amount.  Used when encoding a header. The first line of the
	// header needs to have some room for the header name.

	size_t offset,

	// The encoding function to use, encodeqp or encodebase64.

	void (*encoding_func)(
		std::string_view str,
		std::string_view charset,
		bool (*qp_allow)(char),
		rfc2047::encoder &encoder,
		bool &inappropriate
	),

	// Conversion buffer.

	fromu_impl &fromu,
	rfc2047::encoder &encoder,

	// This passed to the encoding function.  It's true if the
	// encoding function wants to say that the encoding is
	// inappropriate.

	bool &inappropriate,
	bool &errflag
)
{
	bool first=true;

	while (uc.size())
	{
		size_t j;
		size_t i;

		if (!first)
			encoder.encoded(" ");
		first=false;

		// We are estimating here, we'll ass-ume that each unicode
		// character will be encoded into two octets. This is close
		// enough for determining if we'll exceed the
		// RFC2047_ENCODE_FOLDLENGTH.

		j=(RFC2047_ENCODE_FOLDLENGTH-offset)/2;

		if (j >= uc.size())
			j=uc.size();
		else
		{
			// Do not split rfc2047-encoded works across a
			// grapheme break.

			for (i=j; i > 0; --i)
				if (unicode_grapheme_break(uc[i-1], uc[i]))
				{
					j=i;
					break;
				}
		}

		fromu.convert({uc.data(), j}, charset, errflag);

		(*encoding_func)(fromu.buffer, charset, qp_allow,
			      encoder, inappropriate);
		offset=0;
		uc.remove_prefix(j);
	}
}

// Encode, or not encode, words.

static bool do_encode_words(std::u32string_view uc,
			   const std::string &charset,
			   bool flag,
			   bool (*qp_allow)(char),
			   size_t offset,
			   rfc2047::encoder &encoder,
			   fromu_impl &fromu)
{
	bool inappropriate_q;
	bool inappropriate_b64;

	// Convert from unicode

	bool errflag=false;

	if (!flag) /* If not converting, then the job is done */
	{
		fromu.convert(uc, charset, errflag);

		encoder.encoded(fromu.buffer);
		return errflag;
	}

	// Try first quoted-printable, then base64, then pick whichever
	// one gives the shortest results.

	rfc2047::encode_estimate qlen, b64len;
	inappropriate_q=false;
	inappropriate_b64=false;

	do_encode_words_method(
		uc, charset, qp_allow, offset,
		&encodeqp, fromu, qlen,
		inappropriate_q, errflag
	);

	do_encode_words_method(
		uc, charset, qp_allow, offset,
		&encodebase64, fromu, b64len,
		inappropriate_b64, errflag
	);

	do_encode_words_method(
		uc, charset, qp_allow, offset,
		(qlen.length < b64len.length && !inappropriate_q)
			? encodeqp:encodebase64,
			fromu, encoder,
			inappropriate_q, // Doesn't matter
			errflag
	);

	return errflag;
}

/*
** RFC2047-encoding pass.
*/

bool rfc2047::encoder::operator()(
	std::u32string_view str,
	const std::string &charset,
	bool (*qp_allow)(char)
)
{
	size_t	i;
	bool errflag=false;

	size_t	offset=27; // FIXME: initial offset for line length

	fromu_impl fromu;

	while (!str.empty())
	{
		// Pass along all the whitespace

		if (ISSPACE(str.front()))
		{
			char c= str.front();
			str.remove_prefix(1);

			encoded({&c, 1});
			continue;
		}

		i=0;

		// Check if the next word needs to be encoded, or not.

		bool flag=encode_words(str, qp_allow, i);

		// Then proceed to encode, or not encode, the following words.

		errflag |= do_encode_words(
			str.substr(0, i), charset, flag,
			qp_allow, offset,
			*this, fromu
		);

		offset=0;
		str.remove_prefix(i);
	}

	return errflag;
}


bool rfc2047::qp_allow_any(char c)
{
	return true;
}

bool rfc2047::qp_allow_comment(char c)
{
	if (c == '(' || c == ')' || c == '"')
		return false;
	return true;
}

bool rfc2047::qp_allow_word(char c)
{
	return strchr(base64tab, c) != NULL ||
	       strchr("*-=_", c) != NULL;
}
