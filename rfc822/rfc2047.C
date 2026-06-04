/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"rfc822.h"
#include	<stdio.h>
#include	<ctype.h>
#include	<string.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<courier-unicode.h>

#include	"rfc822hdr.h"
#include	"rfc2047.h"
#include <idn2.h>


#define	RFC2047_ENCODE_FOLDLENGTH	76

const char rfc2047_xdigit[]="0123456789ABCDEFabcdef";

const unsigned char rfc2047_decode64tab[]={
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0, 99,  0,  0,
	 0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
	 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};



static const char base64tab[]=
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int encodebase64(const char *ptr, size_t len, const char *charset,
			int (*qp_allow)(char),
			int (*func)(const char *, size_t, void *),
			int *inappropriate,
			void *arg)
{
	unsigned char ibuf[3];
	char obuf[4];
	int	rc;

	if ((rc=(*func)("=?", 2, arg)) ||
	    (rc=(*func)(charset, strlen(charset), arg))||
	    (rc=(*func)("?B?", 3, arg)))
		return rc;

	while (len)
	{
		size_t n=len > 3 ? 3:len;

		ibuf[0]= ptr[0];
		if (n>1)
			ibuf[1]=ptr[1];
		else
			ibuf[1]=0;
		if (n>2)
			ibuf[2]=ptr[2];
		else
			ibuf[2]=0;
		ptr += n;
		len -= n;

		obuf[0] = base64tab[ ibuf[0]        >>2 ];
		obuf[1] = base64tab[(ibuf[0] & 0x03)<<4|ibuf[1]>>4];
		obuf[2] = base64tab[(ibuf[1] & 0x0F)<<2|ibuf[2]>>6];
		obuf[3] = base64tab[ ibuf[2] & 0x3F ];
		if (n < 2)
			obuf[2] = '=';
		if (n < 3)
			obuf[3] = '=';

		if ((rc=(*func)(obuf, 4, arg)))
			return rc;
	}

	if ((rc=(*func)("?=", 2, arg)))
		return rc;
	return 0;
}

#define ISSPACE(i) ((i)=='\t' || (i)=='\r' || (i)=='\n' || (i)==' ')
#define DOENCODEWORD(c) \
	((c) < 0x20 || (c) > 0x7F || (c) == '"' || \
	 (c) == '_' || (c) == '=' || (c) == '?' || !(*qp_allow)((char)c))

/*
** Encode a character stream using quoted-printable encoding.
*/
static int encodeqp(const char *ptr, size_t len,
		    const char *charset,
		    int (*qp_allow)(char),
		    int (*func)(const char *, size_t, void *),
		    int *inappropriate,
		    void *arg)
{
	size_t i;
	int rc;
	char buf[3];

	if ((rc=(*func)("=?", 2, arg)) ||
	    (rc=(*func)(charset, strlen(charset), arg))||
	    (rc=(*func)("?Q?", 3, arg)))
		return rc;

	for (i=0; i<len; ++i)
	{
		size_t j;

		for (j=i; j<len; ++j)
		{
			if (ptr[j] == ' ' || DOENCODEWORD(ptr[j]))
				break;
		}

		if (j > i)
		{
			rc=(*func)(ptr+i, j-i, arg);

			if (rc)
				return rc;
			if (j >= len)
				break;
		}
		i=j;

		if (ptr[i] == ' ')
			rc=(*func)("_", 1, arg);
		else
		{
			if (i == 0)
			{
				/* The preceding char was ? so we can't follow
				** it with a = */
				*inappropriate=1;
				return 0;
			}

			buf[0]='=';
			buf[1]=rfc2047_xdigit[ ( ptr[i] >> 4) & 0x0F ];
			buf[2]=rfc2047_xdigit[ ptr[i] & 0x0F ];

			rc=(*func)(buf, 3, arg);
		}

		if (rc)
			return rc;
	}

	return (*func)("?=", 2, arg);
}

/*
** Calculate whether the next word should be RFC2047-encoded.
**
** Returns 0 if not, 1 if any character in the next word is flagged by
** DOENCODEWORD().
*/

static int encode_word(const char32_t *uc,
		       size_t ucsize,
		       int (*qp_allow)(char),

		       /*
		       ** Points to the starting offset of word in uc.
		       ** At exit, points to the end of the word in uc.
		       */
		       size_t *word_ptr)
{
	size_t i;
	int encode=0;

	for (i=*word_ptr; i<ucsize; ++i)
	{
		if (ISSPACE(uc[i]))
			break;

		if (DOENCODEWORD(uc[i]))
			encode=1;
	}

	*word_ptr=i;
	return encode;
}

/*
** Calculate whether the next sequence of words should be RFC2047-encoded.
**
** Whatever encode_word() returns for the first word, look at the next word
** and keep going as long as encode_word() keeps returning the same value.
*/

static int encode_words(const char32_t *uc,
			size_t ucsize,
			int (*qp_allow)(char),

			/*
			** Points to the starting offset of words in uc.
			** At exit, points to the end of the words in uc.
			*/

			size_t *word_ptr)
{
	size_t i= *word_ptr, j, k;

	int flag=encode_word(uc, ucsize, qp_allow, &i);

	if (!flag)
	{
		*word_ptr=i;
		return flag;
	}

	j=i;

	while (j < ucsize)
	{
		if (ISSPACE(uc[j]))
		{
			++j;
			continue;
		}

		k=j;

		if (!encode_word(uc, ucsize, qp_allow, &k))
			break;
		i=j=k;
	}

	*word_ptr=i;
	return flag;
}

/*
** Encode a sequence of words.
*/
static int do_encode_words_method(const char32_t *uc,
				  size_t ucsize,
				  const char *charset,
				  int (*qp_allow)(char),
				  size_t offset,
				  int (*encoder)(const char *ptr, size_t len,
						 const char *charset,
						 int (*qp_allow)(char),
						 int (*func)(const char *,
							     size_t, void *),
						 int *inappropriate,
						 void *arg),
				  int (*func)(const char *, size_t, void *),
				  int *inappropriate,
				  void *arg)
{
	char    *p;
	size_t  psize;
	int rc;
	int first=1;

	while (ucsize)
	{
		size_t j;
		size_t i;

		if (!first)
		{
			rc=(*func)(" ", 1, arg);

			if (rc)
				return rc;
		}
		first=0;

		j=(RFC2047_ENCODE_FOLDLENGTH-offset)/2;

		if (j >= ucsize)
			j=ucsize;
		else
		{
			/*
			** Do not split rfc2047-encoded works across a
			** grapheme break.
			*/

			for (i=j; i > 0; --i)
				if (unicode_grapheme_break(uc[i-1], uc[i]))
				{
					j=i;
					break;
				}
		}

		if ((rc=unicode_convert_fromu_tobuf(uc, j, charset,
						      &p, &psize,
						      NULL)) != 0)
			return rc;


		if (psize && p[psize-1] == 0)
			--psize;

		rc=(*encoder)(p, psize, charset, qp_allow,
			      func, inappropriate, arg);
		free(p);
		if (rc)
			return rc;
		offset=0;
		ucsize -= j;
		uc += j;
	}
	return 0;
}

static int cnt_conv(const char *dummy, size_t n, void *arg)
{
	*(size_t *)arg += n;
	return 0;
}

/*
** Encode, or not encode, words.
*/

static int do_encode_words(const char32_t *uc,
			   size_t ucsize,
			   const char *charset,
			   int flag,
			   int (*qp_allow)(char),
			   size_t offset,
			   int (*func)(const char *, size_t, void *),
			   void *arg)
{
	char    *p;
	size_t  psize;
	int rc;
	size_t b64len, qlen;
	int inappropriate_q;
	int inappropriate_b64;
	/*
	** Convert from unicode
	*/

	if ((rc=unicode_convert_fromu_tobuf(uc, ucsize, charset,
					      &p, &psize,
					      NULL)) != 0)
		return rc;

	if (psize && p[psize-1] == 0)
		--psize;

	if (!flag) /* If not converting, then the job is done */
	{
		rc=(*func)(p, psize, arg);
		free(p);
		return rc;
	}
	free(p);

	/*
	** Try first quoted-printable, then base64, then pick whichever
	** one gives the shortest results.
	*/
	qlen=0;
	b64len=0;
	inappropriate_q=0;
	inappropriate_b64=0;

	rc=do_encode_words_method(uc, ucsize, charset, qp_allow, offset,
				  &encodeqp, cnt_conv,
				  &inappropriate_q,
				  &qlen);
	if (rc)
		return rc;

	rc=do_encode_words_method(uc, ucsize, charset, qp_allow, offset,
				  &encodebase64, cnt_conv,
				  &inappropriate_b64, &b64len);
	if (rc)
		return rc;

	return do_encode_words_method(uc, ucsize, charset, qp_allow, offset,
				      (qlen < b64len
				       && inappropriate_q == 0)
				      ? encodeqp:encodebase64,
				      func,
				      &inappropriate_q, // Doesn't matter
				      arg);
}

/*
** RFC2047-encoding pass.
*/
int rfc2047_encode_callback(const char32_t *uc,
			    size_t ucsize,
			    const char *charset,
			    int (*qp_allow)(char),
			    int (*func)(const char *, size_t, void *),
			    void *arg)
{
	int	rc;
	size_t	i;
	int	flag;

	size_t	offset=27; /* FIXME: initial offset for line length */

	while (ucsize)
	{
		/* Pass along all the whitespace */

		if (ISSPACE(*uc))
		{
			char c= *uc++;
			--ucsize;

			if ((rc=(*func)(&c, 1, arg)) != 0)
				return rc;
			continue;
		}

		i=0;

		/* Check if the next word needs to be encoded, or not. */

		flag=encode_words(uc, ucsize, qp_allow, &i);

		/*
		** Then proceed to encode, or not encode, the following words.
		*/

		if ((rc=do_encode_words(uc, i, charset, flag,
					qp_allow, offset,
					func, arg)) != 0)
			return rc;

		offset=0;
		uc += i;
		ucsize -= i;
	}

	return 0;
}


int rfc2047_qp_allow_any(char c)
{
	return 1;
}

int rfc2047_qp_allow_comment(char c)
{
	if (c == '(' || c == ')' || c == '"')
		return 0;
	return 1;
}

int rfc2047_qp_allow_word(char c)
{
	return strchr(base64tab, c) != NULL ||
	       strchr("*-=_", c) != NULL;
}
