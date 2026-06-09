/*
** Copyright 1998 - 2025 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/
#include	"rfc822.h"
#include	<stdio.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	<string.h>

void rfc822_tokenize(const char *p,
		     size_t plen,
		     void (*parsed_func)(char token,
					 const char *ptr, size_t len,
					 void *voidp),
		     void *voidp_parsed_func,
		     void (*err_func)(const char *, size_t, void *),
		     void *voidp_err_func)
{
	const char *addr=p;
	size_t	i=0;
	int	inbracket=0;

	char	tokp_token;
	const char *tokp_ptr;
	size_t  tokp_len;

	while (plen)
	{
		if (isspace((int)(unsigned char)*p))
		{
			++p; --plen;
			i++;
			continue;
		}

#define SPECIALS "<>@,;:.[]()%!\"\\?=/"

		switch (*p)	{
		int	level;

		case '(':

			tokp_token='(';
			tokp_ptr=p;
			tokp_len=0;

			level=0;
			for (;;)
			{
				if (plen == 0)
				{
					(*err_func)(addr, i, voidp_err_func);
					tokp_token='"';
					(*parsed_func)(tokp_token,
						       tokp_ptr, tokp_len,
						       voidp_parsed_func);
					return;
				}
				if (*p == '(')
					++level;
				if (*p == ')' && --level == 0)
				{
					++p; --plen;
					i++;
					tokp_len++;
					break;
				}
				if (*p == '\\' && plen > 1)
				{
					++p; --plen;
					i++;
					tokp_len++;
				}

				i++;
				tokp_len++;
				++p; --plen;
			}
			(*parsed_func)(tokp_token, tokp_ptr, tokp_len,
				       voidp_parsed_func);
			continue;

		case '"':
			++p; --plen;
			i++;

			tokp_token='"';
			tokp_ptr=p;
			tokp_len=0;

			while (*p != '"')
			{
				if (plen == 0)
				{
					(*err_func)(addr, i, voidp_err_func);
					(*parsed_func)(tokp_token,
						       tokp_ptr, tokp_len,
						       voidp_parsed_func);
					return;
				}
				if (*p == '\\' && plen > 1)
				{
					tokp_len++;
					++p; --plen;
					i++;
				}
				tokp_len++;
				++p; --plen;
				i++;
			}
			(*parsed_func)(tokp_token, tokp_ptr, tokp_len,
				       voidp_parsed_func);
			++p; --plen;
			i++;
			continue;
		case '\\':
		case ')':
			(*err_func)(addr, i, voidp_err_func);
			++p; --plen;
			++i;
			continue;

		case '=':

			if (plen > 1 && p[1] == '?')
			{
				size_t j;

				/* exception: =? ... ?= */

				for (j=2; j < plen; j++)
				{
					if (p[j] == '?' && j+1 < plen &&
					    p[j+1] == '=')
						break;

					if (p[j] == '?' || p[j] == '=')
						continue;

					if (strchr(RFC822_SPECIALS, p[j]) ||
					    isspace(p[j]))
						break;
				}

				if (j+1 < plen && p[j] == '?' && p[j+1] == '=')
				{
					j += 2;

					tokp_token=0;
					tokp_ptr=p;
					tokp_len=j;
					(*parsed_func)(tokp_token,
						       tokp_ptr, tokp_len,
						       voidp_parsed_func);

					p += j; plen -= j;
					i += j;
					continue;
				}
			}
			/* FALLTHROUGH */

		case '<':
		case '>':
		case '@':
		case ',':
		case ';':
		case ':':
		case '.':
		case '[':
		case ']':
		case '%':
		case '!':
		case '?':
		case '/':

			if ( (*p == '<' && inbracket) ||
				(*p == '>' && !inbracket))
			{
				(*err_func)(addr, i, voidp_err_func);
				++p; --plen;
				++i;
				continue;
			}

			if (*p == '<')
				inbracket=1;

			if (*p == '>')
				inbracket=0;

			tokp_token= *p;
			tokp_ptr=p;
			tokp_len=1;
			(*parsed_func)(tokp_token, tokp_ptr, tokp_len,
				       voidp_parsed_func);

			if (*p == '<' && plen > 1 && p[1] == '>')
					/* Fake a null address */
			{
				tokp_token=0;
				tokp_ptr=p+1;
				tokp_len=0;
				(*parsed_func)(tokp_token, tokp_ptr, tokp_len,
					       voidp_parsed_func);
			}
			++p; --plen;
			++i;
			continue;
		default:

			tokp_token=0;
			tokp_ptr=p;
			tokp_len=0;

			size_t j=i;

			while (plen &&
			       !isspace((int)(unsigned char)*p) &&
			       strchr(SPECIALS, *p) == 0)
			{
				++tokp_len;
				++p; --plen;
				++i;
			}
			if (i == j)	/* Idiot check */
			{
				(*err_func)(addr, i, voidp_err_func);

				tokp_token='"';
				tokp_ptr=p;
				tokp_len=1;
				(*parsed_func)(tokp_token,
					       tokp_ptr, tokp_len,
					       voidp_parsed_func);
				++p; --plen;
				++i;
				continue;
			}
			(*parsed_func)(tokp_token,
				       tokp_ptr, tokp_len,
				       voidp_parsed_func);
		}
	}
}

/*
** Parse rfc822 tokens into discrete addresses.
**
** - ntokens: number of tokens
**
** - get_nth_token: retrieve nth token, the parameter goes from 0 to ntokens-1
**
** - consume_n_token: mark the first n token as being processed, subsequent
**   calls to get_nth_token are 0-based from the remaining unprocessed
**   tokens.
**
** - make_quoted_token: take the first n tokens, and replace them with a single
**   quote token, '"'.
**
** - define_addr_name, define_addr_tokens - specify the next address's
**   recipient name, and the address itself
**
** define_addr_name and define_addr_tokens get called to define the next
** address's recipient name, if there is one, and the address itself.
**
** define_addr_name is always called before define_addr_tokens, except if
** name_from_comment is set to true, the second parameter to define_addr_tokens.
** Both define_addr_name and define_attr_tokens' first parameter is the
** number of tokens that comprise the recipient name, or the recipient address.
** They are interleaved with consume_n_token calls that end up discarding
** all tokens that do not comprise the name or the address portion.
**
** The second parameter to define_addr_name is convert_quotes, if set any
** '(' tokens in the name portion should be replaced with '"', this is to
** adjust invalid formatting.
**
** define_addr_tokens' second parameter, name_from_comment, is set when it is
** called to set the address's tokens, but any '(' from the number of tokens
** specified by the first parameter should be removed and set to the address's
** name (and define_addr_name is not called, beforehand, in this case). The
** current implementation only grabs the last '(' token (there should only
** be one).
*/

void rfc822_parseaddr(size_t ntokens,
		      char (*get_nth_token)(size_t, void *),
		      void (*consume_n_tokens)(size_t, void *),
		      void (*make_quoted_token)(size_t, void *),
		      void (*define_addr_name)(size_t, int, void *),
		      void (*define_addr_tokens)(size_t, int, void *),
		      void *voidp)
{
	int	flag;
	char c;

	while (ntokens)
	{
		size_t	i;

		/* atoms (token=0) or quoted strings, followed by a : token
		is a list name. */

		for (i=0; i<ntokens; i++)
		{
			c=get_nth_token(i, voidp);

			if (c && c != '"')
				break;
		}

		if (i < ntokens && c == ':')
		{
			++i;

			define_addr_name(i, 0, voidp);
			define_addr_tokens(0, 0, voidp);
			ntokens -= i;
			consume_n_tokens(i, voidp);
			continue;  /* Group=phrase ":" */
		}

		/* Spurious commas are skipped, ;s are recorded */

		c=get_nth_token(0, voidp);

		if (c == ',' || c == ';')
		{
			if (c == ';')
			{
				define_addr_name(1, 0, voidp);
				define_addr_tokens(0, 0, voidp);
			}
			--ntokens;
			consume_n_tokens(1, voidp);
			continue;
		}

		/* If we can find a '<' before the next comma or semicolon,
		we have new style RFC path address */

		for (i=0; i<ntokens; i++)
		{
			c=get_nth_token(i, voidp);

			if (c == ';' || c == ',' || c == '<')
				break;
		}

		if (i < ntokens && c == '<')
		{
			size_t	j;

			/* Ok -- what to do with the stuff before '>'???
			If it consists exclusively of atoms, leave them alone.
			Else, make them all a quoted string. */

			for (j=0; j<i; j++)
			{
				c=get_nth_token(j, voidp);

				if (! (c == 0 || c == '('))
					break;
			}

			if (j == i)
			{
				define_addr_name(i, 1, voidp);
			}
			else	/* Intentionally corrupt the original toks */
			{
				make_quoted_token(i, voidp);
				define_addr_name(1, 1, voidp);
			}

			/* Now that's done and over with, see what can
			be done with the <...> part. */

			++i;
			ntokens -= i;
			consume_n_tokens(i, voidp);
			for (i=0; i<ntokens && get_nth_token(i, voidp) != '>'; i++)
				;

			define_addr_tokens(i, 0, voidp);
			ntokens -= i;
			consume_n_tokens(i, voidp);
			if (ntokens)	/* Skip the '>' token */
			{
				--ntokens;
				consume_n_tokens(1, voidp);
			}
			continue;
		}

		/* Ok - old style address.  Assume the worst */

		/* Try to figure out where the address ends.  It ends upon:
		a comma, semicolon, or two consecutive atoms. */

		flag=0;
		for (i=0; i<ntokens; i++)
		{
			c=get_nth_token(i, voidp);

			if (c == ',' || c == ';')
				break;

			if (c == '(')	continue;
					/* Ignore comments */
			if (c == 0 || c == '"')
				/* Atom */
			{
				if (flag)	break;
				flag=1;
			}
			else	flag=0;
		}
		if (i == 0)	/* Must be spurious comma, or something */
		{
			--ntokens;
			consume_n_tokens(1, voidp);
			continue;
		}

		define_addr_tokens(i, 1, voidp);
		ntokens -= i;
		consume_n_tokens(i, voidp);
	}
}
void rfc822print_token(int token_token,
		       const char *token_ptr,
		       size_t token_len,
		       void (*print_func)(const char *, size_t, void *),
		       void *ptr)
{
	char c;

	if (token_token == 0 || token_token == '(')
	{
		(*print_func)(token_ptr, token_len, ptr);
		return;
	}

	if (token_token != '"')
	{
		c= (char)token_token;
		(*print_func)(&c, 1, ptr);
		return;
	}

	c='"';

	(*print_func)(&c, 1, ptr);

	while (token_len)
	{
		size_t i;

		for (i=0; i<token_len; ++i)
		{
			if (token_ptr[i] == '"')
				break;

			if (token_ptr[i] == '\\')
			{
				if (i+1 == token_len)
					break;
				++i;
			}
		}

		if (i)
		{
			(*print_func)(token_ptr, i, ptr);
			token_ptr += i;
			token_len -= i;
			continue;
		}

		c='\\';
		(*print_func)(&c, 1, ptr);
		(*print_func)(token_ptr, 1, ptr);
		++token_ptr;
		--token_len;
	}
	c='"';

	(*print_func)(&c, 1, ptr);
}
