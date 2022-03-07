/*
** Copyright 1998 - 2018 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<ctype.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif


extern const char *externalauth();

int have_starttls()
{
	const char *p;

        if ((p=getenv("POP3_STARTTLS")) == 0)   return (0);
        if (*p != 'y' && *p != 'Y')             return (0);

        p=getenv("COURIERTLS");
        if (!p || !*p)  return (0);
        if (access(p, X_OK))    return (0);
        return (1);
}


int tls_required()
{
	const char *p=getenv("POP3_TLS_REQUIRED");
	if (!p || !atoi(p))	return(0);

	p=getenv("POP3_TLS");
	if (p && atoi(p))       return (0);

	return (1);
}

const char *pop3_externalauth()
{
	const char *external=NULL;
	const char *p;

	if ((p=getenv("POP3_TLS")) != 0 && atoi(p))
		external=externalauth();

	return external;
}

void pop3dcapa()
{
	const char *p;
	const char *external=pop3_externalauth();

	printf("+OK Here's what I can do:\r\n");

	if ((p=getenv("POP3_TLS")) != 0 && atoi(p) &&
	    (p=getenv("POP3AUTH_TLS")) != 0 && *p)
		;
	else
		p=getenv("POP3AUTH");

	if ((p && *p) || external)
	{
		if (!p)
			p="";

		if (!external)
			external="";

		printf("SASL %s%s%s\r\n", p, *p && *external ? " ":"",
		       *external ? "EXTERNAL":"");
	}

	if (have_starttls())
		printf("STLS\r\n");

	printf("TOP\r\nUSER\r\nLOGIN-DELAY 10\r\n"
	       "PIPELINING\r\nUIDL\r\n"
	       "UTF8 USER\r\n"
	       "LANG\r\n"
	       "IMPLEMENTATION Courier Mail Server\r\n.\r\n");
	fflush(stdout);
}

void pop3dlang(const char *lang)
{
	if (!lang)
	{
		printf("+OK Language listing follows:\r\n"
		       "en English\r\n"
		       "i-default Default language\r\n"
		       ".\r\n");
		fflush(stdout);
		return;
	}

	if (strcmp(lang, "*") == 0)
		lang="en";

	if (strcmp(lang, "en") == 0 ||
	    strcmp(lang, "i-default") == 0)
	{
		printf("+OK %s King's English\r\n", lang);
		fflush(stdout);
		return;
	}
	printf("-ERR Language not available.\r\n");
	fflush(stdout);
}
