/*
** Copyright 1998 - 2015 Double Precision, Inc.  See COPYING for
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

extern int check_sqwebpass(const char *);

extern char *sqwebmail_content_charset, *sqwebmail_system_charset;
#include	<courier-unicode.h>

const char *myhostname()
{
char    buf[512];
static char *my_hostname=0;
FILE	*f;

	if (my_hostname == 0)
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

		if ((my_hostname=malloc(strlen(buf)+1)) == 0)
			enomem();
		strcpy(my_hostname, buf);
	}
	return (my_hostname);
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

	static char *authaddr=NULL;
	static char *authfullname=NULL;
	static char *authoptions=NULL;
	static char *authenticated=NULL;
	const char *n;
	char *b;
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

	b=malloc(sizeof("AUTHADDR=")+strlen(c));

	if (!b)
		enomem();
	strcat(strcpy(b, "AUTHADDR="), c);
	putenv(b);
	if (authaddr)
		free(authaddr);
	authaddr=b;


	n=a->fullname;
	if (!n) n="";
	b=malloc(sizeof("AUTHFULLNAME=")+strlen(n));

	if (!b)
		enomem();
	strcat(strcpy(b, "AUTHFULLNAME="), n);
	putenv(b);
	if (authfullname)
		free(authfullname);
	authfullname=b;

	n=a->options;
	if (!n) n="";

	b=malloc(sizeof("OPTIONS=")+strlen(n));

	if (!b)
		enomem();
	strcat(strcpy(b, "OPTIONS="), n);
	putenv(b);
	if (authoptions)
		free(authoptions);
	authoptions=b;

	n=a->address;
	if (!n) n="";

	b=malloc(sizeof("AUTHENTICATED=")+strlen(n));

	if (!b)
		enomem();
	strcat(strcpy(b, "AUTHENTICATED="), n);
	putenv(b);
	if (authenticated)
		free(authenticated);
	authenticated=b;

	return (0);
}

const char *do_login(const char *u, const char *p, const char *ip)
{
	if (auth_login("webmail", u, p, doauthlogin, (void *)u))
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

int prelogin(const char *u)
{
	return auth_getuserinfo("webmail", u, doauthlogin, (void *)u);
}

const char *login_returnaddr()
{
	static char *addrbuf=0;
	const char *p, *domain="";

	if ((p=getenv("AUTHENTICATED")) == NULL || *p == 0)
		p=getenv("AUTHADDR");
	if (!p)	p="";
	if (strchr(p, '@') == 0)
		domain=myhostname();

	if (addrbuf)	free(addrbuf);
	addrbuf=malloc(strlen(domain)+strlen(p)+2);
	if (!addrbuf)	enomem();

	strcpy(addrbuf, p);
	if (*domain)
		strcat(strcat(addrbuf, "@"), domain);
	return (addrbuf);
}

const char *login_fromhdr()
{
const char *address=login_returnaddr();
const char *fullname=getenv("AUTHFULLNAME");
int	l;
const char *p;
char	*q;

static char *hdrbuf=0;

FILE *fp;
char authcharset[128];
char *ufullname=0;
static char *uhdrbuf=0;

	if (!fullname || !*fullname)
		return (address);	/* That was easy */

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
	    && sqwebmail_system_charset && *sqwebmail_system_charset
	    && strcasecmp(sqwebmail_system_charset, "ASCII"))
		strncat(authcharset, sqwebmail_system_charset,
			sizeof(authcharset)-1);

	if (authcharset[0]
	    && sqwebmail_content_charset && *sqwebmail_content_charset
	    && (ufullname=unicode_convert_toutf8(fullname, authcharset,NULL)))
		fullname = ufullname;

	l=sizeof("\"\" <>")+strlen(address)+strlen(fullname);

	for (p=fullname; *p; p++)
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	++l;

	for (p=address; *p; p++)
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	++l;

	if (hdrbuf)	free(hdrbuf);
	hdrbuf=malloc(l);
	if (!hdrbuf)	enomem();
	q=hdrbuf;
	*q++='"';
	for (p=fullname; *p; p++)
	{
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	*q++ = '\\';
		*q++= *p;
	}
	*q++='"';
	*q++=' ';
	*q++='<';
	for (p=address; *p; p++)
	{
		if (*p == '"' || *p == '\\' || *p == '(' || *p == ')' ||
			*p == '<' || *p == '>')	*q++ = '\\';
		*q++= *p;
	}
	*q++='>';
	*q=0;

	if (ufullname)	free(ufullname);
	if (uhdrbuf)	free(uhdrbuf);
	if ((uhdrbuf=unicode_convert_fromutf8(hdrbuf,
						sqwebmail_content_charset,
						NULL)) != NULL)
		return (uhdrbuf);

	return (hdrbuf);
}
