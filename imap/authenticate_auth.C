/*
** Copyright 1998 - 2008 S. Varshavchik.
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
#include	<vector>

extern int main_argc;
extern char **main_argv;

extern "C" int login_callback(struct authinfo *ainfo, void *dummy);

extern "C" const char *imap_externalauth();

static char *send_auth_reply(const char *q, void *dummy)
{
	imaptoken tok;

	std::vector<char> charbuf;

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

	charbuf.clear();

	switch (tok->tokentype)	{
	case IT_ATOM:
	case IT_NUMBER:
		charbuf.reserve(tok->tokenbuf.size()+1);
		charbuf.insert(charbuf.end(),
			       tok->tokenbuf.begin(),
			       tok->tokenbuf.end());
		charbuf.push_back(0);
		break;
	case IT_EOL:
		charbuf.push_back(0);
		break;
	default:
		return (0);
	}

	if (nexttoken()->tokentype != IT_EOL)
	{
		fprintf(stderr, "Invalid SASL response\n");
		return (0);
	}
	read_eol();
	return strdup(charbuf.data()); // TODO: fix when authlib is C++.
}

int authenticate(const char *tag, char *methodbuf, int methodbuflen)
{
	imaptoken tok=nexttoken();
	std::vector<char> initreply;

	char	*authtype, *authdata;
	char	authservice[40];
	const char	*p ;
	int	rc;

	switch (tok->tokentype)	{
	case IT_ATOM:
	case IT_QUOTED_STRING:
		break;
	default:
		return (0);
	}

	std::string authmethod=tok->tokenbuf;

	if (methodbuf)
		snprintf(methodbuf, methodbuflen, "%s", authmethod.c_str());

	tok=nexttoken_nouc();
	if (tok->tokentype != IT_EOL)
	{
		switch (tok->tokentype)	{
		case IT_ATOM:
		case IT_NUMBER:
		case IT_QUOTED_STRING:
			break;
		default:
			return (0);
		}
		initreply.reserve(tok->tokenbuf.size()+1);
		initreply.insert(initreply.end(),
				 tok->tokenbuf.begin(),
				 tok->tokenbuf.end());
		initreply.push_back(0);

		if (strcmp(initreply.data(), "=") == 0)
			initreply[0]=0;

		tok=nexttoken_nouc();
	}

	if (tok->tokentype != IT_EOL)	return (0);

	read_eol();
	if ((rc = auth_sasl_ex(authmethod.c_str(),
			       initreply.data(), imap_externalauth(),
			       &send_auth_reply, NULL,
			       &authtype, &authdata)) != 0)
	{
		return (rc);
	}

	strcat(strcpy(authservice, "AUTHSERVICE"),
			 getenv("TCPLOCALPORT"));
	p=getenv(authservice);

	if (!p || !*p)
		p="imap";

	rc=auth_generic_meta(NULL, p, authtype, authdata,
			     login_callback, (void *)tag);
	free(authtype);
	free(authdata);
	return (rc);
}
