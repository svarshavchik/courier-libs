/*
** Copyright 1998 - 2006 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<stdio.h>
#include	<stdlib.h>


static void print_func(const char *c, size_t n, void *p)
{
	p=p;
	fwrite(c, n, 1, stdout);
}

static void print_separator(const char *s, void *p)
{
	p=p;
	printf("%s", s);
}

static struct rfc822t *tokenize(const char *p)
{
struct	rfc822t	*tp;
int	i;
char	buf[2];

	printf("Tokenize: %s\n", p);
	tp=rfc822t_alloc_new(p, NULL, NULL);
	if (!tp)	exit(0);
	buf[1]=0;
	for (i=0; i<tp->ntokens; i++)
	{
		buf[0]=tp->tokens[i].token;
		if (buf[0] == '\0' || buf[0] == '"' || buf[0] == '(')
		{
			printf("%s: ", buf[0] == '"' ? "Quote":
				buf[0] == '(' ? "Comment":"Atom");
			if (tp->tokens[i].len &&
			    fwrite(tp->tokens[i].ptr, tp->tokens[i].len, 1,
				   stdout) != 1)
				exit(1);

			printf("\n");
		}
		else	printf("Token: %s\n", buf[0] ? buf:"atom");
	}
	return (tp);
}

static struct rfc822a *doaddr(struct rfc822t *t)
{
struct rfc822a *a=rfc822a_alloc(t);

	if (!a)	exit(0);
	printf("----\n");
	rfc822_print(a, print_func, print_separator, NULL);
	printf("\n");
	return (a);
}

int main()
{
	struct	rfc822t *t1, *t2, *t3, *t4, *t5, *t6, *t7, *t8;
	struct	rfc822a *a1, *a2, *a3, *a4, *a5, *a6, *a7, *a8;
	char *c;

	rfc822t_free(tokenize("(Break 1"));
	rfc822t_free(tokenize("(Break 2\\"));
	rfc822t_free(tokenize("(Break 3\\))"));
	rfc822t_free(tokenize("(Break 4())"));
	rfc822t_free(tokenize("\"Quote 1"));
	rfc822t_free(tokenize("\"Quote 2\\"));
	rfc822t_free(tokenize("\"Quote 3\\\""));
	rfc822t_free(tokenize("=?Atom 1()"));
	rfc822t_free(tokenize("=?Atom 2?"));
	rfc822t_free(tokenize("=?Atom 3?="));
	rfc822t_free(tokenize("<>"));

	t1=tokenize("nobody@example.com (Nobody (is) here\\) right)");
	t2=tokenize("Distribution  list: nobody@example.com daemon@example.com");
	t3=tokenize("Mr Nobody <nobody@example.com>, Mr. Nobody <nobody@example.com>");
	t4=tokenize("nobody@example.com, <nobody@example.com>, Mr. Nobody <nobody@example.com>");

	t5=tokenize("=?UTF-8?Q?Test?= <nobody@example.com>, foo=bar <nobody@example.com>");
	t6=tokenize("\"Quoted \\\\ \\\" String\" <nobody@example.com>,"
		    "\"Trailing slash \\\\\" <nobody@example.com>");
	t7=tokenize("undisclosed-recipients: ;");
	t8=tokenize("mailing-list: nobody@example.com, nobody@example.com;");

	a1=doaddr(t1);
	a2=doaddr(t2);
	a3=doaddr(t3);
	a4=doaddr(t4);
	a5=doaddr(t5);
	a6=doaddr(t6);
	a7=doaddr(t7);
	a8=doaddr(t8);

	c=rfc822_getaddrs_wrap(a4, 70);
	printf("[%s]\n", c);
	free(c);
	c=rfc822_getaddrs_wrap(a4, 160);
	printf("[%s]\n", c);
	free(c);
	c=rfc822_getaddrs_wrap(a4, 10);
	printf("[%s]\n", c);
	free(c);
	rfc822a_free(a8);
	rfc822a_free(a7);
	rfc822a_free(a6);
	rfc822a_free(a5);
	rfc822a_free(a4);
	rfc822a_free(a3);
	rfc822a_free(a2);
	rfc822a_free(a1);
	rfc822t_free(t8);
	rfc822t_free(t7);
	rfc822t_free(t6);
	rfc822t_free(t5);
	rfc822t_free(t4);
	rfc822t_free(t3);
	rfc822t_free(t2);
	rfc822t_free(t1);

#define FIVEUTF8 "\xe2\x85\xa4"

#define FIVETIMES4 FIVEUTF8 FIVEUTF8 FIVEUTF8 FIVEUTF8

#define FIVETIMES16 FIVETIMES4 FIVETIMES4 FIVETIMES4 FIVETIMES4

#define FIVEMAX FIVETIMES16 FIVETIMES4 FIVETIMES4

	{
		char *p=rfc2047_encode_str(FIVEMAX, "utf-8",
					   rfc2047_qp_allow_any);

		if (p)
		{
			printf("%s\n", p);
			free(p);
		}
	}

	{
		char *p=rfc2047_encode_str(FIVEMAX FIVEUTF8, "utf-8",
					   rfc2047_qp_allow_any);

		if (p)
		{
			printf("%s\n", p);
			free(p);
		}
	}

	{
		char *p=rfc2047_encode_str(FIVEMAX "\xcc\x80", "utf-8",
					   rfc2047_qp_allow_any);

		if (p)
		{
			printf("%s\n", p);
			free(p);
		}
	}

	return (0);
}
