/*
** Copyright 1998 - 2025 Double Precision, Inc.
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
				int j;

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

			while (plen &&
			       !isspace((int)(unsigned char)*p) &&
			       strchr(SPECIALS, *p) == 0)
			{
				++tokp_len;
				++p; --plen;
				++i;
			}
			if (i == 0)	/* Idiot check */
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
	int	i;

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
			int	j;

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

void rfc822tok_print(const struct rfc822token *token,
		     void (*print_func)(const char *, size_t, void *),
		     void *ptr)
{
int	prev_isatom=0;
int	isatom;

	while (token)
	{
		isatom=rfc822_is_atom(token->token);
		if (prev_isatom && isatom)
			(*print_func)(" ", 1, ptr);
		rfc822print_token(token->token, token->ptr, token->len,
				  print_func, ptr);
		prev_isatom=isatom;
		token=token->next;
	}
}

static void rfc822_prname_int(const struct rfc822addr *addrs,
			      void (*print_func)(const char *, size_t, void *),
			      void *ptr)

{
	struct rfc822token *i;
	int	prev_isatom=0;
	int	isatom=0;

	for (i=addrs->name; i; i=i->next, prev_isatom=isatom)
	{
		isatom=rfc822_is_atom(i->token);
		if (isatom && prev_isatom)
			(*print_func)(" ", 1, ptr);

		if (i->token == '"')
		{
			size_t p=0, n;

			while (p < i->len)
			{
				for (n=p; n<i->len; n++)
				{
					if (i->ptr[n] == '\\' && n+1 < i->len)
						break;
				}

				if (n > p)
				{
					(*print_func)(i->ptr+p, n-p, ptr);
				}

				p=n;

				if (p < i->len)
				{
					++p;
					(*print_func)(i->ptr+p, 1, ptr);
					++p;
				}
			}
			continue;
		}

		if (i->token != '(')
		{
			rfc822print_token(i->token, i->ptr, i->len,
					  print_func, ptr);
			continue;
		}

		if (i->len > 2)
			(*print_func)(i->ptr+1, i->len-2, ptr);
	}
}

static void rfc822_print_common_nameaddr_cntlen(const char *, size_t l, void *p)
{
	*(size_t *)p += l;
}

static void rfc822_print_common_nameaddr_saveaddr(const char *c, size_t l,
						  void *p)
{
	char **cp=(char **)p;

	memcpy(*cp, c, l);
	*cp += l;
}

static int rfc822_print_common_nameaddr(const struct rfc822addr *addrs,
					char *(*decode_func)(const char *,
							     const char *, int),
					const char *chset,
					void (*print_func)(const char *,
							   size_t, void *),
					void *ptr)
{
	size_t n=1;
	char *addrbuf, *namebuf;
	char *p, *q;
	int print_braces=0;

	if (addrs->tokens)
		rfc822tok_print(addrs->tokens,
				rfc822_print_common_nameaddr_cntlen, &n);


	p=addrbuf=malloc(n);

	if (!addrbuf)
		return -1;

	if (addrs->tokens)
		rfc822tok_print(addrs->tokens,
				rfc822_print_common_nameaddr_saveaddr, &p);

	*p=0;

	n=1;

	rfc822_prname_int(addrs,
			  rfc822_print_common_nameaddr_cntlen, &n);

	p=namebuf=malloc(n);

	if (!p)
	{
		free(addrbuf);
		return -1;
	}

	rfc822_prname_int(addrs,
			  rfc822_print_common_nameaddr_saveaddr, &p);

	*p=0;

	p=(*decode_func)(namebuf, chset, 0);

	free(namebuf);
	if (!p)
	{
		free(addrbuf);
		return -1;
	}

	if (*p)
	{
		print_braces=1;
		print_func(p, strlen(p), ptr);
	}
	free(p);

	p=(*decode_func)(addrbuf, chset, 1);
	free(addrbuf);

	if (!p)
		return -1;

	if (print_braces)
		(*print_func)(" ", 1, ptr);

	for (q=p; *q; ++q)
		if (*q != '.' && *q != '@' && strchr(RFC822_SPECIALS, *q))
		{
			print_braces=1;
			break;
		}

	if (print_braces)
		(*print_func)("<", 1, ptr);

	(*print_func)(p, strlen(p), ptr);

	if (print_braces)
		(*print_func)(">", 1, ptr);

	free(p);
	return (0);
}

int rfc822_print(const struct rfc822a *rfcp,
		 void (*print_func)(const char *, size_t, void *),
		 void (*print_separator)(const char *s, void *), void *ptr)
{
	return rfc822_print_common(rfcp, 0, 0, print_func, print_separator, ptr);
}

int rfc822_print_common(const struct rfc822a *rfcp,
			char *(*decode_func)(const char *, const char *, int),
			const char *chset,
			void (*print_func)(const char *, size_t, void *),
			void (*print_separator)(const char *, void *),
			void *ptr)
{
const struct rfc822addr *addrs=rfcp->addrs;
int naddrs=rfcp->naddrs;

	while (naddrs)
	{
		if (addrs->tokens == 0)
		{
			rfc822tok_print(addrs->name, print_func, ptr);
			++addrs;
			--naddrs;
			if (addrs[-1].name && naddrs)
			{
			struct	rfc822token *t;

				for (t=addrs[-1].name; t && t->next; t=t->next)
					;

				if (t && (t->token == ':' || t->token == ';'))
					(*print_separator)(" ", ptr);
			}
			continue;
		}
		else if (addrs->name && addrs->name->token == '(')
		{	/* old style */

			if (!decode_func)
			{
				rfc822tok_print(addrs->tokens, print_func, ptr);
				(*print_func)(" ", 1, ptr);
				rfc822tok_print(addrs->name, print_func, ptr);
			}
			else
			{
				if (rfc822_print_common_nameaddr(addrs,
								 decode_func,
								 chset,
								 print_func,
								 ptr) < 0)
					return -1;
			}
		}
		else
		{
			if (!decode_func)
			{
				int	print_braces=0;

				if (addrs->name)
				{
					rfc822tok_print(addrs->name,
							print_func, ptr);
					(*print_func)(" ", 1, ptr);
					print_braces=1;
				}
#if 1
				else
				{
					struct rfc822token *p;

					for (p=addrs->tokens; p && p->next; p=p->next)
						if (rfc822_is_atom(p->token) &&
						    rfc822_is_atom(p->next->token))
							print_braces=1;
				}
#endif

				if (print_braces)
					(*print_func)("<", 1, ptr);

				rfc822tok_print(addrs->tokens, print_func, ptr);

				if (print_braces)
					(*print_func)(">", 1, ptr);
			}
			else
			{
				if (rfc822_print_common_nameaddr(addrs,
								 decode_func,
								 chset,
								 print_func,
								 ptr) < 0)
					return -1;
			}
		}
		++addrs;
		--naddrs;
		if (naddrs)
			if (addrs->tokens || (addrs->name &&
				rfc822_is_atom(addrs->name->token)))
				(*print_separator)(", ", ptr);
	}
	return 0;
}

void rfc822t_free(struct rfc822t *p)
{
	if (p->tokens)	free(p->tokens);
	free(p);
}

void rfc822a_free(struct rfc822a *p)
{
	if (p->addrs)	free(p->addrs);
	free(p);
}

void rfc822_deladdr(struct rfc822a *rfcp, int index)
{
int	i;

	if (index < 0 || index >= rfcp->naddrs)	return;

	for (i=index+1; i<rfcp->naddrs; i++)
		rfcp->addrs[i-1]=rfcp->addrs[i];
	if (--rfcp->naddrs == 0)
	{
		free(rfcp->addrs);
		rfcp->addrs=0;
	}
}

static void ignore_errors(const char *, size_t, void *)
{
}

static void count_token(char, const char *, size_t, void *voidp)
{
	struct rfc822t *p=(struct rfc822t *)voidp;

	++p->ntokens;
}

static void save_token(char token, const char *ptr, size_t len, void *voidp)
{
	struct rfc822token **tptr=(struct rfc822token **)voidp;

	(*tptr)->token=token;
	(*tptr)->ptr=ptr;
	(*tptr)->len=len;
	++*tptr;
}

struct rfc822t *rfc822t_alloc_new(const char *addr,
	void (*err_func)(const char *, size_t, void *), void *voidp)
{
	struct rfc822t *p=(struct rfc822t *)malloc(sizeof(struct rfc822t));
	struct rfc822token *q;
	size_t l=strlen(addr);

	if (!p)	return (NULL);
	memset(p, 0, sizeof(*p));

	if (!err_func)
		err_func=ignore_errors;

	rfc822_tokenize(addr, l, count_token, p, err_func, voidp);
	p->tokens=p->ntokens ? (struct rfc822token *)
			calloc(p->ntokens, sizeof(struct rfc822token)):0;
	if (p->ntokens && !p->tokens)
	{
		rfc822t_free(p);
		return (NULL);
	}
	q=p->tokens;
	rfc822_tokenize(addr, l, save_token, &q, ignore_errors, NULL);
	return (p);
}

struct rfc822a_info {
	struct rfc822t *t;
	size_t pos;

	size_t naddrs;
	struct rfc822addr *addrs;
};

static char get_nth_token(size_t n, void *voidp)
{
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;

	return info->t->tokens[info->pos+n].token;
}

static void consume_n_tokens(size_t n, void *voidp)
{
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;

	info->pos += n;
}

static void make_quoted_token(size_t n, void *voidp)
{
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;

	info->t->tokens[info->pos].len=
		info->t->tokens[info->pos+n-1].ptr +
		info->t->tokens[info->pos+n-1].len
		- info->t->tokens[info->pos].ptr;

	/* We know that all the ptrs point
	   to parts of the same string. */
	info->t->tokens[info->pos].token='"';
	/* Quoted string. */
}

static void make_quoted_token_ignore(size_t n, void *voidp)
{
}

static void define_addr_name_ignore(size_t n, int convert_quotes, void *voidp)
{
}

static void define_addr_tokens_ignore(size_t n, int name_from_comment,
				      void *voidp)
{
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;
	++info->naddrs;
}

static void define_addr_name(size_t n, int convert_quotes, void *voidp)
{
	size_t j;
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;

	info->addrs->name=n ? info->t->tokens+info->pos:NULL;

	for (j=1; j<n; j++)
		info->addrs->name[j-1].next=info->addrs->name+j;
	if (n)
		info->addrs->name[n-1].next=0;

	if (convert_quotes)
	{
		/* Any comments in the name part are changed to quotes */

		struct rfc822token *t;

		for (t=info->addrs->name; t; t=t->next)
			if (t->token == '(')
				t->token='"';
	}
}

static void define_addr_tokens(size_t n, int name_from_comment, void *voidp)
{
	size_t k;
	struct rfc822a_info *info=(struct rfc822a_info *)voidp;

	if (name_from_comment)
	{
		size_t j, k;
		struct	rfc822token	save_token;

		define_addr_name(0, 0, voidp);

		/*
		** Ok, now get rid of embedded comments in the address.
		** Consider the last comment to be the real name
		*/

		memset(&save_token, 0, sizeof(save_token));

		for (j=k=0; j<n; j++)
		{
			if (info->t->tokens[info->pos+j].token == '(')
			{
				save_token=info->t->tokens[info->pos+j];
				continue;
			}
			info->t->tokens[info->pos+k]=
				info->t->tokens[info->pos+j];
			k++;
		}

		if (save_token.ptr)
		{
			info->t->tokens[info->pos+n-1]=save_token;
			info->addrs->name=info->t->tokens+info->pos+n-1;
			info->addrs->name->next=0;
		}

		n=k;
	}

	info->addrs->tokens=n ? info->t->tokens+info->pos:NULL;

	for (k=1; k<n; k++)
		info->addrs->tokens[k-1].next=info->addrs->tokens+k;
	if (n)
		info->addrs->tokens[k-1].next=0;
	++info->addrs;
}

struct rfc822a *rfc822a_alloc(struct rfc822t *t)
{
	struct rfc822a *p=(struct rfc822a *)malloc(sizeof(struct rfc822a));

	struct rfc822a_info info;

	if (!p)	return (NULL);
	memset(p, 0, sizeof(*p));

	/* First pass, count how many times define_addr_tokens get called. */

	info.t=t;
	info.pos=0;
	info.naddrs=0;

	rfc822_parseaddr(t->ntokens,
			 get_nth_token, consume_n_tokens,
			 make_quoted_token_ignore,
			 define_addr_name_ignore, define_addr_tokens_ignore,
			 &info);

	/* Second pass, actually populate rfc822a */

	p->naddrs=info.naddrs;
	p->addrs=p->naddrs ? (struct rfc822addr *)
		calloc(info.naddrs, sizeof(struct rfc822addr)):0;
	if (p->naddrs && !p->addrs)
	{
		rfc822a_free(p);
		return (NULL);
	}

	info.pos=0;
	info.addrs=p->addrs;

	rfc822_parseaddr(t->ntokens,
			 get_nth_token, consume_n_tokens,
			 make_quoted_token,
			 define_addr_name, define_addr_tokens,
			 &info);
	return (p);
}
