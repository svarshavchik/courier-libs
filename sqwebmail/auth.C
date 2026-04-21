/*
** Copyright 1998 - 2015 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/

#include	"sqwebmail.h"
#include	"config.h"

#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<courierauth.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<errno.h>

#include	"maildir/loginexec.h"
#include	"courierauthdebug.h"
#include	"auth.h"
#include	"htmllibdir.h"

#include	<courier-unicode.h>

const char *myhostname()
{
char    buf[512];
static std::string my_hostname;
FILE	*f;

	if (my_hostname.empty())
	{
		buf[0]=0;
		if ((f=fopen(HOSTNAMEFILE, "r")) != 0)
		{
		char *p;

			if (fgets(buf, sizeof(buf), f) == NULL)
				buf[0]=0;

			fclose(f);

			if ((p=strchr(buf, '\n')) != 0)
				*p=0;
		}

		if (buf[0] == 0 && gethostname(buf, sizeof(buf)-1))
			strcpy(buf, "localhost");

		my_hostname = buf;
	}
	return (my_hostname.c_str());
}

static int login_maildir(const char *maildir)
{
	if (!maildir || !*maildir)
		maildir=getenv("MAILDIRPATH");
	if (!maildir || !*maildir)
		maildir="Maildir";
	if (chdir(maildir))	return (-1);
	maildir_loginexec();
	return (0);
}

static int doauthlogin(struct authinfo *a, void *vp)
{
	const char *p=auth_getoption(a->options ? a->options:"",
				     "disablewebmail");
	const char *c=(const char *)vp;

	const char *n;
	int rc;


	if (p && atoi(p))
		return -1;

	if ((rc = auth_callback_default_autocreate(a)) != 0)
	{
		if (rc > 0)
			perror("ERR: authentication error");
		return -1;
	}

	if (login_maildir(a->maildir))
	{
		error("Unable to open the maildir for this account -- the maildir doesn't exist or has incorrect ownership or permissions.");
		return (-1);
	}

	setenv("AUTHADDR", c, 1);

	n=a->fullname;
	if (!n) n="";

	setenv("AUTHFULLNAME", n, 1);

	n=a->options;
	if (!n) n="";

	setenv("OPTIONS", n, 1);

	n=a->address;
	if (!n) n="";

	setenv("AUTHENTICATED", n, 1);

	return (0);
}

const char *do_login(const char *u, const char *p, const char *ip)
{
	char ipbuf[strlen(ip)+sizeof("TCPREMOTEIP=")];
	char *envvars[2]={ipbuf, 0};
	struct auth_meta meta;

	strcat(strcpy(ipbuf, "TCPREMOTEIP="), ip);

	memset(&meta, 0, sizeof(meta));
	meta.envvars=envvars;

	if (auth_login_meta(&meta, "webmail", u, p, doauthlogin, (void *)u))
	{
		courier_safe_printf("INFO: LOGIN FAILED, user=%s, ip=[%s]",
				  u?u:"", ip);
		return NULL;
	}

	fprintf(stderr, "INFO: LOGIN, user=%s, ip=[%s]\n", u, ip);
	return u;
}

int nochangepass()
{
	if (auth_getoptionenvint("wbnochangepass")) return 1;
	if (access(SQWEBPASSWD, X_OK)) return 1;
	return 0;
}

/*
** login_changepwd tries to call the password change function for the
** authentication module.
**
** Returns -1 if authentication module does not have a password change
** function.
**
** Returns 0 if the authentication module has a password change function.
**
** *rc is set to 0 if password was changed, non-zero otherwise
*/

int login_changepwd(const char *u, const char *oldpwd, const char *newpwd,
		    int *rc)
{
	if (nochangepass())
		return -1;

	*rc= -1;

	if (changepw("webmail", u, oldpwd, newpwd) == 0)
	{
		*rc=0;
	}
	return 0;
}

const char *login_returnaddr()
{
	static std::string addrbuf;
	const char *p, *domain="";

	if (!addrbuf.empty())
		return addrbuf.c_str();

	if ((p=getenv("AUTHENTICATED")) == NULL || *p == 0)
		p=getenv("AUTHADDR");
	if (!p)	p="";
	if (strchr(p, '@') == 0)
		domain=myhostname();

	addrbuf.clear();
	addrbuf.reserve(strlen(domain)+strlen(p)+2);

	addrbuf = p;
	if (*domain)
	{
		addrbuf += "@";
		addrbuf += domain;
	}
	return (addrbuf.c_str());
}

std::string login_fromhdr()
{
	std::string address=login_returnaddr();
	std::string fullname=getenv("AUTHFULLNAME");
	int	l;
	const char *p;

	std::string hdrbuf;

	FILE *fp;
	char authcharset[128];
	std::string uhdrbuf;

	if (fullname.empty())
		return address;

	authcharset[0] = 0;
	if ((fp=fopen(AUTHCHARSET, "r")))
	{
		char *p;
		if (fgets(authcharset, sizeof(authcharset), fp) == NULL)
			authcharset[0]=0;
		fclose(fp);

		if ((p=strchr(authcharset, '\n')))
			*p = '\0';
	}

	if (authcharset[0] == 0
	    && !sqwebmail_system_charset.empty()
	    && strcasecmp(sqwebmail_system_charset.c_str(), "ASCII"))
		strncat(authcharset, sqwebmail_system_charset.c_str(),
			sizeof(authcharset)-1);

	if (authcharset[0]
	    && !sqwebmail_content_charset.empty())
	    {
		fullname=unicode::iconvert::convert(
			fullname,
			authcharset,
			unicode::utf_8
		);
	    }

	l=sizeof("\"\" <>")+address.size()+fullname.size();

	for (p=fullname.c_str(); *p; p++)
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	++l;

	for (p=address.c_str(); *p; p++)
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	++l;

	hdrbuf.reserve(l);
	hdrbuf += '"';
	for (p=fullname.c_str(); *p; p++)
	{
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	hdrbuf += '\\';
		hdrbuf += *p;
	}
	hdrbuf += '"';
	hdrbuf += ' ';
	hdrbuf += '<';
	for (p=address.c_str(); *p; p++)
	{
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	hdrbuf += '\\';
		hdrbuf += *p;
	}
	hdrbuf += '>';

	bool errflag;

	uhdrbuf = unicode::iconvert::convert(
		hdrbuf,
		unicode::utf_8,
		sqwebmail_content_charset,
		errflag
	);

	if (errflag)
		uhdrbuf=hdrbuf;

	return (uhdrbuf);
}
