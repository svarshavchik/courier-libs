/*
** Copyright 1998 - 2008 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<string.h>
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"courierauth.h"
#include	"courierauthsasl.h"
#include	"courierauthdebug.h"


extern int main_argc;
extern char **main_argv;

extern int login_callback(struct authinfo *ainfo, void *dummy);

extern const char *imap_externalauth();

static char *send_auth_reply(const char *q, void *dummy)
{
	struct imaptoken *tok;
	char	*p;

#if SMAP
	const char *cp=getenv("PROTOCOL");

	if (cp && strcmp(cp, "SMAP1") == 0)
		writes("> ");
	else
#endif

	{
		writes("+ ");
	}
	writes(q);
	writes("\r\n");
	writeflush();
	read_timeout(SOCKET_TIMEOUT);
	tok=nexttoken_nouc();

	switch (tok->tokentype)	{
	case IT_ATOM:
	case IT_NUMBER:
		p=my_strdup(tok->tokenbuf);
		break;
	case IT_EOL:
		p=my_strdup("");
		break;
	default:
		return (0);
	}
	if (!p)
	{
		perror("malloc");
		return (0);
	}

	if (nexttoken()->tokentype != IT_EOL)
	{
		free(p);
		fprintf(stderr, "Invalid SASL response\n");
		return (0);
	}
	read_eol();
	return (p);
}

int authenticate(const char *tag, char *methodbuf, int methodbuflen)
{
struct imaptoken *tok=nexttoken();
char	*authmethod;
char	*initreply=0;
char	*authtype, *authdata;
char	authservice[40];
char	*p ;
int	rc;

	switch (tok->tokentype)	{
	case IT_ATOM:
	case IT_QUOTED_STRING:
		break;
	default:
		return (0);
	}

	authmethod=my_strdup(tok->tokenbuf);
	if (methodbuf)
		snprintf(methodbuf, methodbuflen, "%s", authmethod);

	tok=nexttoken_nouc();
	if (tok->tokentype != IT_EOL)
	{
		switch (tok->tokentype)	{
		case IT_ATOM:
		case IT_NUMBER:
			break;
		default:
			return (0);
		}
		initreply=my_strdup(tok->tokenbuf);
		if (strcmp(initreply, "=") == 0)
			*initreply=0;
		tok=nexttoken_nouc();
	}

	if (tok->tokentype != IT_EOL)	return (0);

	read_eol();
	if ((rc = auth_sasl_ex(authmethod, initreply, imap_externalauth(),
			       &send_auth_reply, NULL,
			       &authtype, &authdata)) != 0)
	{
		free(authmethod);
		if (initreply)
			free(initreply);
		return (rc);
	}

	free(authmethod);
	if (initreply)
		free(initreply);

	strcat(strcpy(authservice, "AUTHSERVICE"),
			 getenv("TCPLOCALPORT"));
	p=getenv(authservice);

	if (!p || !*p)
		p="imap";

	rc=auth_generic(p, authtype, authdata, login_callback, (void *)tag);
	free(authtype);
	free(authdata);
	return (rc);
}
