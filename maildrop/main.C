/*
** Copyright 1998 - 2009 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"lexer.h"
#include	"recipe.h"
#include	"varlist.h"
#include	"funcs.h"
#include	"tempfile.h"
#include	"message.h"
#include	"messageinfo.h"
#include	"xconfig.h"
#include	"exittrap.h"
#include	"maildrop.h"
#include	"config.h"
#include	"setgroupid.h"
#include	<sys/types.h>

#if	HAVE_LOCALE_H
#include	<locale.h>
#endif

#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<sysexits.h>
#include	<string.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<pwd.h>
#include	<grp.h>
#include	"../dbobj.h"

/*
** This switch can later be moved to config.h file with appropriate
** configure option like --with-dovecotauth or something similar
*/
#if DOVECOTAUTH
#include	"dovecotauth.h"
#endif

#if AUTHLIB
#include	<courierauth.h>
#endif

#if	HAS_GETHOSTNAME
#else
extern "C" int gethostname(const char *, size_t);
#endif

void rfc2045_error(const char *p)
{
	fprintf(stderr, "%s\n", p);
	fflush(stderr);
	exit(1);
}

extern void setprocgroup();

static Message m1, m2;
extern char **environ;
static int errexit=EX_TEMPFAIL;
int quota_warn_percent = -1;
const char *quota_warn_message=0;

static const char *defaults_vars[]={"LOCKEXT","LOCKSLEEP","LOCKTIMEOUT",
					"LOCKREFRESH", "PATH", "SENDMAIL",
					"MAILDIRQUOTA"};
static const char *defaults_vals[]={LOCKEXT_DEF,LOCKSLEEP_DEF,LOCKTIMEOUT_DEF,
					LOCKREFRESH_DEF, DEFAULT_PATH,
					SENDMAIL_DEF, ""};

Maildrop maildrop;

Maildrop::Maildrop()
{
	verbose_level=0;
	isdelivery=0;
	sigfpe=0;
	includelevel=0;
	embedded_mode=0;
	msgptr= &m1;
	savemsgptr= &m2;
#if AUTHLIB_TEMPREJECT
	authlib_essential=1;
#else
	authlib_essential=0;
#endif
}

static void help()
{
	mout << "Usage: maildrop [options] [-d user] [arg] [arg] ...\n";
	mout << "       maildrop [options] [filterfile [arg] [arg] ...\n";
}

static void bad()
{
	errexit=EX_TEMPFAIL;
	throw "Bad command line arguments, -h for help.";
}

static void nouser()
{
	merr << "Invalid user specified.\n";
	exit(EX_NOUSER);
}

static void nochangeuidgid()
{
	errexit=EX_TEMPFAIL;
	throw "Cannot set my user or group id.";
}

static int trusted_user(uid_t uid)
{
static char trusted_users[]=TRUSTED_USERS;
static char buf[ sizeof(trusted_users) ];
char	*p;

	strcpy(buf, trusted_users);
	for (p=buf; (p=strtok(p, " ")) != 0; p=0)
	{
	struct	passwd *q=getpwnam(p);

		if (q && q->pw_uid == uid)
			return (1);
	}
	return (0);
}

static int trusted_group(gid_t gid)
{
static char trusted_groups[]=TRUSTED_GROUPS;
static char buf[ sizeof(trusted_groups) ];
char	*p;

	strcpy(buf, trusted_groups);
	for (p=buf; (p=strtok(p, " ")) != 0; p=0)
	{
	struct	group *q=getgrnam(p);

		if (q && (gid_t)q->gr_gid == gid)
			return (1);
	}
	return (0);
}

static int trusted_uidgid(uid_t uid, gid_t gid, gid_t gid2)
{
	if (trusted_user(uid) || trusted_group(gid) ||
		trusted_group(gid2))
		return (1);
	return (0);
}

static void sethostname(Buffer &buf)
{
char    hostname[256];

        hostname[0]=0;
        gethostname(hostname, 256);
        hostname[sizeof(hostname)-1]=0;

	buf=hostname;
}


static void copyright()
{
static const char msg[]="maildrop " VERSION " Copyright 1998-2017 Double Precision, Inc."

#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
#if HAVE_COURIER
	"Courier-specific maildrop build. This version of maildrop should only be used"
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
	"with Courier, and not any other mail server."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
#endif
#ifdef DbObj
	"GDBM/DB extensions enabled."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
#endif
#if DOVECOTAUTH
	"Dovecot Authentication extension enabled."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
#endif
#if AUTHLIB
	"Courier Authentication Library extension enabled."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
#endif
	"Maildir quota extension are now always enabled."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
	"This program is distributed under the terms of the GNU General Public"
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif
        "License. See COPYING for additional information."
#if CRLF_TERM
	"\r\n"
#else
	"\n"
#endif

		;

	mout << msg;
	mout.flush();
}

void Maildrop::reset_vars()
{
int	i;
Buffer	name, value;

	for (i=0; i<(int)(sizeof(defaults_vars)/sizeof(defaults_vars[0])); i++)
	{
		name=defaults_vars[i];
		value=defaults_vals[i];
		SetVar(name, value);
	}
	name="HOME";
	SetVar(name, maildrop.init_home);
	name="LOGNAME";
	SetVar(name, maildrop.init_logname);
	name="SHELL";
	SetVar(name, maildrop.init_shell);
	name="DEFAULT";
	SetVar(name, maildrop.init_default);

	name="UMASK";
	value="077";
	SetVar(name, value);

	if (maildrop.init_quota.Length() > 0)
	{
		name="MAILDIRQUOTA";
		SetVar(name, maildrop.init_quota);
	}
}

#if AUTHLIB
// Authlib lookup

static int callback_authlib(struct authinfo *auth,
			    void *void_arg)
{
	Maildrop &maildrop=*(Maildrop *)void_arg;

	if (auth_mkhomedir(auth))
	{
		perror(auth->homedir);
		exit(1);
	}

	if (VerboseLevel() > 1)
	{
		Buffer b;

		b.set(auth->sysgroupid);
		b.push(0);

		merr << "maildrop: authlib: groupid="
		     << b << "\n";
	}

	if (setgroupid(auth->sysgroupid) < 0)
	{
		perror("setgid");
		exit(1);
	}

	uid_t u;
	if (auth->sysusername)
	{
		struct	passwd *q=getpwnam(auth->sysusername);

		if (q == NULL)
		{
			merr << "Cannot find system user "
			     << auth->sysusername
			     << "\n";

			nochangeuidgid();
		}

		u=q->pw_uid;
	}
	else
		u=*auth->sysuserid;

	if (VerboseLevel() > 1)
	{
		Buffer b;

		b.set(u);
		b.push(0);

		merr << "maildrop: authlib: userid="
		     << b << "\n";
	}

	if (setuid(u) < 0 ||
	    getuid() != u)
		nochangeuidgid();

	if (VerboseLevel() > 1)
	{
		merr << "maildrop: authlib: logname="
		     << auth->address
		     << ", home="
		     << auth->homedir
		     << ", mail="
		     << (auth->maildir ? auth->maildir:"(default)")
		     << "\n";
	}

	maildrop.init_home=auth->homedir;
	maildrop.init_logname=auth->address;
	maildrop.init_shell="/bin/sh";
	maildrop.init_default=auth->maildir ? auth->maildir:
		GetDefaultMailbox(auth->address);

	if ( auth->quota )
		maildrop.init_quota=auth->quota;

	return 0;
}

int find_in_authlib(Maildrop *maildrop, const char* user)
{
	int rc=auth_getuserinfo("login",
				user, callback_authlib, maildrop);

	if (rc == 0)
		return 1;

	if ((rc > 0) && (maildrop->authlib_essential == 1))
	{
		errexit=EX_TEMPFAIL;
		throw "Temporary authentication failure.";
	}

	return 0;
}
#else
int find_in_authlib(Maildrop *maildrop, const char* user)
{
	return 0;
}
#endif

#if DOVECOTAUTH
static int callback_dovecotauth(struct dovecotauthinfo *auth,
			    void *void_arg)
{
	Maildrop &maildrop=*(Maildrop *)void_arg;

	if (VerboseLevel() > 1)
	{
		Buffer b;

		b.set(auth->sysgroupid);
		b.push(0);

		merr << "maildrop: dovecotauth: groupid="
		     << b << "\n";
	}

	setgroupid(auth->sysgroupid);

	uid_t u;
	if (auth->sysusername)
	{
		struct	passwd *q=getpwnam(auth->sysusername);

		if (q == NULL)
		{
			merr << "Cannot find system user "
			     << auth->sysusername
			     << "\n";

			nochangeuidgid();
		}

		u=q->pw_uid;
	}
	else
		u=*auth->sysuserid;

	if (VerboseLevel() > 1)
	{
		Buffer b;

		b.set(u);
		b.push(0);

		merr << "maildrop: dovecotauth: userid="
		     << b << "\n";
	}

	setuid(u);

	if ( getuid() != u)
		nochangeuidgid();

	if (VerboseLevel() > 1)
	{
		merr << "maildrop: dovecotauth: logname="
		     << auth->address
		     << ", home="
		     << auth->homedir
		     << ", mail="
		     << (auth->maildir ? auth->maildir:"(default)")
		     << "\n";
	}

	maildrop.init_home=auth->homedir;
	maildrop.init_logname=auth->address;
	maildrop.init_shell="/bin/sh";
	maildrop.init_default=auth->maildir ? auth->maildir:
		GetDefaultMailbox(auth->address);

	return 0;
}

int find_in_dovecotauth(const char *addr, Maildrop *maildrop, const char* user)
{
	int rc=dovecotauth_getuserinfo(addr,
				user, callback_dovecotauth, maildrop);

	if (rc == 0)
		return 1;

	if (rc > 0)
	{
		errexit=EX_TEMPFAIL;
		throw "Temporary authentication failure.";
	}

	return 0;
}
#endif

static void tempfail(const char *msg)
{
	errexit = EX_TEMPFAIL;
	throw msg;
}

static int run(int argc, char **argv)
{
int	argn;
const	char *deliverymode=0;
char *embedded_filter=0;
const	char *from=0;
int     explicit_from=0;
Buffer	recipe;
uid_t	orig_uid;
gid_t	orig_gid, orig_gid2;
Buffer	extra_headers;
struct passwd *my_pw;
int	found;
#if	HAVE_COURIER
#if	RESTRICT_TRUSTED
const	char *numuidgid=0;
#endif
#endif
#if DOVECOTAUTH
const	char *dovecotauth_addr=0;
#endif


	umask( 0007 );
	for (argn=1; argn < argc; )
	{
		if (argv[argn][0] != '-')	break;
		if (strcmp(argv[argn], "--") == 0)	{ ++argn; break; }

	char	optc=argv[argn][1];
	const char *optarg=argv[argn]+2;

		++argn;
		switch (optc)	{

#if	HAVE_COURIER
#if	RESTRICT_TRUSTED
		case 'D':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			numuidgid=optarg;
			break;
#endif
#endif
		case 'd':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			deliverymode=optarg;
			break;
		case 'V':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			maildrop.verbose_level=atoi(optarg);
			break;
		case 'v':
			copyright();
			return (0);
		case 'm':
			maildrop.embedded_mode=1;
			break;
		case 'M':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			maildrop.embedded_mode=1;
			if (!deliverymode)	deliverymode="";
			if (!*optarg)
			{
				help();
				return (EX_TEMPFAIL);
			}
			embedded_filter=(char *)malloc(strlen(optarg)+1);
			if (!embedded_filter)	outofmem();
			strcpy(embedded_filter, optarg);
			{
			char *p;

				for (p=embedded_filter; *p; p++)
					if (*p == SLASH_CHAR || *p == '.')
						*p=':';
			}
			break;
		case 'A':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			if (*optarg)
			{
				extra_headers += optarg;
				extra_headers += '\n';
			}
			break;
		case 'f':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			if (*optarg)
			{
				from=optarg;
				explicit_from=1;
			}
			break;
		case 'w':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			if (*optarg)
				quota_warn_percent=atoi(optarg);
			break;
		case 'W':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			if (*optarg)
				quota_warn_message=optarg;
			break;
		case 'a':
			maildrop.authlib_essential=1;
			break;
#if DOVECOTAUTH
		case 't':
			if (!*optarg && argn < argc)	optarg=argv[argn++];
			if (!*optarg)
			{
				mout << "You didn't specify the location of Dovecot auth socket.\n";
				return (EX_TEMPFAIL);
			}
			else
				dovecotauth_addr=optarg;
			break;
#endif
		case 'h':
			help();
			return (EX_TEMPFAIL);
		default:
			bad();
		}
	}

	my_pw=0;
	found=0;
	orig_uid=getuid();
	orig_gid=getgid();
	orig_gid2=getegid();
	if (!deliverymode && argn < argc)
		recipe=argv[argn++];
	else
	{
		if (!deliverymode)	deliverymode="";

		if (*deliverymode)
		{

#if DOVECOTAUTH
			if (dovecotauth_addr)
			{
				found = find_in_dovecotauth(dovecotauth_addr, &maildrop, deliverymode);
			}
			else
#endif
			{
				found = find_in_authlib(&maildrop, deliverymode);
			}

			if ( !found )
			{
				my_pw=getpwnam(deliverymode);
				if (!my_pw)
					nouser();
				if (
#if	RESET_GID
				    setgroupid(my_pw->pw_gid) < 0
#else
				    (geteuid() == 0 && setgroupid(getegid()) < 0)
#endif
				     ||
				    setuid(my_pw->pw_uid) < 0)
				{
					nochangeuidgid();
				}
				if (getuid() != my_pw->pw_uid)
					nochangeuidgid(); // Security violation.

				maildrop.init_home=my_pw->pw_dir;
				maildrop.init_logname=my_pw->pw_name;
				maildrop.init_shell=
					my_pw->pw_shell && *my_pw->pw_shell
						? my_pw->pw_shell:"/bin/sh";
				maildrop.init_default=
					GetDefaultMailbox(my_pw->pw_name);
				found=1;
			}
		}
		maildrop.isdelivery=1;
#if	RESTRICT_TRUSTED
		if ( getuid() != orig_uid && !trusted_uidgid(orig_uid,
			orig_gid, orig_gid2))
		{
			errexit=EX_TEMPFAIL;
			throw "You are not a trusted user.";
					// Security violation
		}
#endif
	}

#if	HAVE_COURIER
#if	RESTRICT_TRUSTED
	if (numuidgid)
	{
	uid_t	un=0;
	gid_t	gn=getgid();

		if (deliverymode && *deliverymode)
		{
			errexit=EX_TEMPFAIL;
			throw "Cannot use both -d and -D options.";
		}

		if ( !trusted_uidgid(orig_uid, orig_gid, orig_gid2))
		{
			errexit=EX_TEMPFAIL;
			throw "You are not authorized to use the -D option.";
		}

		if (!isdigit( (int)(unsigned char)*numuidgid))
		{
			errexit=EX_TEMPFAIL;
			throw "Invalid -D option.";
		}

		do
		{
			un=un * 10 + (*numuidgid++ - '0');
		} while (isdigit( (int)(unsigned char)*numuidgid));

		if ( *numuidgid )
		{
			if ( *numuidgid++ != '/' ||
				!isdigit( (int)(unsigned char)*numuidgid))
			{
				errexit=EX_TEMPFAIL;
				throw "Invalid -D option.";
			}
			gn=0;
			do
			{
				gn=gn * 10 + (*numuidgid++ - '0');
			} while (isdigit( (int)(unsigned char)*numuidgid));

			if ( *numuidgid )
			{
				errexit=EX_TEMPFAIL;
				throw "Invalid -D option.";
			}
		}
		if (setgroupid(gn) < 0 ||
		    setuid(un) < 0)
		{
			perror("setuid/setgid");
			exit(1);
		}
		deliverymode="";
		orig_uid=un;	/* See below for another Courier hook */
	}
#endif
#endif


#if	RESET_GID
	if (setgroupid(getgid()) < 0)
	{
		perror("setgid");
		exit(1);
	}
#endif

uid_t	my_u=getuid();

	if (setuid(my_u) < 0)	// Drop any setuid privileges.
	{
		perror("setuid");
		exit(1);
	}

	if (!found)
	{
#if HAVE_COURIER
		if (!deliverymode)
#endif
		{
			my_pw=getpwuid(my_u);
			if (!my_pw)
			{
				errexit=EX_TEMPFAIL;
				throw "Cannot determine my username.";
			}

			maildrop.init_home=my_pw->pw_dir;
			maildrop.init_logname=my_pw->pw_name;
			maildrop.init_shell=
				my_pw->pw_shell && *my_pw->pw_shell
					? my_pw->pw_shell:"/bin/sh";
			maildrop.init_default=
				GetDefaultMailbox(my_pw->pw_name);
		}
	}

int	i;
Buffer	name;
Buffer	value;

	for (i=0; environ[i]; i++)
	{
		name=environ[i];

		char	*p=strchr(environ[i], '=');

		value= p ? (name.Length(p - environ[i]), p+1):"";

		if (maildrop.isdelivery)
		{
			if (name == "LANG" ||
			    name == "LANGUAGE" ||
			    strncmp(name, "LC_", 3) == 0)
				;
			else
				continue;
		}

		SetVar(name, value);
	}

	i=1;
	while (argn < argc)
	{
		name="";
		name.append( (unsigned long)i);
		value=argv[argn++];
		SetVar(name, value);
		++i;
	}

#if	HAVE_COURIER
	if (deliverymode && orig_uid == getuid())
	{
	const char *p;

		if ((p=getenv("HOME")) && *p)
			maildrop.init_home=p;

		if ((p=getenv("LOGNAME")) && *p)
			maildrop.init_logname=p;

		if ((p=getenv("SHELL")) && *p)
			maildrop.init_shell=p;

		p=getenv("MAILDROPDEFAULT");

		if (!p || !*p)
		{
			p=getenv("LOCAL");

			if (p && *p)
				p=GetDefaultMailbox(p);
			else
				p="./Maildir";
		}
		maildrop.init_default=p;

		if ((p=getenv("MAILDIRQUOTA")) && *p)
			maildrop.init_quota=p;
	}
#endif

	if (deliverymode)
	{
	struct	stat	buf;
	Buffer	b;

		b=maildrop.init_home;
		b += '\0';

	const char *h=b;

		if (VerboseLevel() > 1)
			merr << "maildrop: Changing to " << h << "\n";

		if (chdir(h) < 0)
		{
			errexit=EX_TEMPFAIL;
			throw "Unable to change to home directory.";
		}
		recipe=".mailfilter";

		if ( stat(".", &buf) < 0)
			tempfail("Cannot stat() home directory.");
		if ( !S_ISDIR(buf.st_mode))
			tempfail("Home directory is not a directory.");
		if ( buf.st_mode & S_IWOTH)
			tempfail("Invalid home directory permissions - world writable.");
		if ( buf.st_uid != getuid())
			tempfail("Home directory owned by wrong user.");

		// Quietly terminate if the sticky bit is set on the homedir

		if ( buf.st_mode & S_ISVTX)
			return (EX_TEMPFAIL);

		if (embedded_filter)
		{
			i=stat(".mailfilters", &buf);

			if ( i < 0 && errno != ENOENT)
				tempfail("Unable to read $HOME/.mailfilters.");
			else if ( i >= 0)
			{
				if ( !S_ISDIR(buf.st_mode))
					tempfail("$HOME/.mailfilters should be a directory.");
				if ( buf.st_mode & (S_IRWXO|S_IRWXG))
					tempfail("Invalid permissions on $HOME/.mailfilters - remove world and group perms.");
				if ( buf.st_uid != getuid())
					tempfail("Invalid user ownership of $HOME/.mailfilters.");
			}
			recipe = embedded_filter;
		}
	}
#if	SHARED_TEMPDIR

#else
	maildrop.tempdir=maildrop.init_home;
	maildrop.tempdir += "/" TEMPDIR;
	maildrop.tempdir += '\0';
	mkdir( (const char *)maildrop.tempdir, 0700 );
#endif
	maildrop.reset_vars();

Buffer	msg;

	maildrop.global_timer.Set(GLOBAL_TIMEOUT);
	maildrop.msgptr->Init(0, extra_headers); // Read message from standard input.
	maildrop.msginfo.info( *maildrop.msgptr );
	maildrop.msgptr->setmsgsize();


	if (!from)	from="";
	if (*from)	maildrop.msginfo.fromname=from;

// If invoking user is trusted, trust the From line, else set it to invoking
// user.

	if (
#if KEEP_FROMLINE

// The original From_ line is kept, if necessary

#else

// If invoking user is trusted, trust the From line, else set it to invoking
// user.
		!trusted_uidgid(orig_uid, orig_gid, orig_gid2) ||
#endif

		maildrop.msginfo.fromname.Length() == 0)
	{
		maildrop.msginfo.fromname=maildrop.init_logname;
	}

	if (explicit_from)
		maildrop.msginfo.fromname=from;

	name="FROM";
	value=maildrop.msginfo.fromname;
	SetVar(name, value);

	if (VerboseLevel() > 1)
	{
		msg.reset();
		msg.append("Message envelope sender=");
		if (maildrop.msginfo.fromname.Length() > 0)
			msg += maildrop.msginfo.fromname;
		msg.append("\n");
		msg += '\0';
		merr.write(msg);
	}

	name="HOSTNAME";
	sethostname(value);
	SetVar(name, value);

int	fd;

	//
	//

	if (!embedded_filter && deliverymode)
	{
	Recipe r;
	Lexer in;

		fd=in.Open(ETCDIR "/maildroprc");
		if (fd < 0)
		{
			if (errno != ENOENT)
			{
				errexit=EX_TEMPFAIL;
				throw "Error opening " ETCDIR "/maildroprc.";
			}
		}
		else
		{
			if (r.ParseRecipe(in) < 0)
				return (EX_TEMPFAIL);
			r.ExecuteRecipe();
		}
	}

Recipe	r;
Lexer	in;

#ifdef	DEFAULTEXT
int	firstdefault=1;
#endif

	name="MAILFILTER";
	value=recipe;
	SetVar(name, value);

	for (;;)
	{
		if (embedded_filter)
		{
			msg=".mailfilters/";
			msg += recipe;
			if (VerboseLevel() > 1)
				merr << "maildrop: Attempting " << msg << "\n";
			msg += '\0';
			fd=in.Open((const char *)msg);
		}
		else
		{
			msg=recipe;
			if (VerboseLevel() > 1)
				merr << "maildrop: Attempting " << msg << "\n";
			msg += '\0';
			fd=in.Open((const char *)msg);
			break;
		}
#ifndef	DEFAULTEXT
		break;
#else
		if (fd >= 0)	break;
		if (errno != ENOENT)	break;

		if (firstdefault)
		{
			recipe += DEFAULTEXT;
			firstdefault=0;
			continue;
		}

		// Pop DEFAULTEXT bytes from end of recipe name

		for (fd=sizeof(DEFAULTEXT)-1; fd; --fd)
			recipe.pop();

		while (recipe.Length())
		{
			if (recipe.pop() == '-')
			{
				recipe += DEFAULTEXT;
				break;
			}
		}
		if (recipe.Length() == 0)
		{
			msg=".mailfilters/";
			msg += DEFAULTEXT+1;
			if (VerboseLevel() > 1)
				merr << "maildrop: Attempting " << msg << "\n";
			msg += '\0';
			fd=in.Open((const char *)msg);
			break;
		}
#endif
	}

	if (fd < 0)
	{
		//
		//  If we are operating in delivery mode, it's ok if
		//  .mailfilter does not exist.
		//
		if (!deliverymode || errno != ENOENT)
		{
		static char buf[80];

			sprintf(buf, "Unable to open filter file, errno=%d.",
				errno);
			errexit=EX_TEMPFAIL;
			throw buf;
		}
	}
	else
	{
	struct	stat	stat_buf;

		setprocgroup();

		if (fstat( fd, &stat_buf) != 0)
			tempfail("stat() failed.");

		if (!S_ISREG(stat_buf.st_mode))
			tempfail("mailfilter file isn't a regular file.");
		if (stat_buf.st_mode & (S_IRWXO | S_IRWXG))
			tempfail("Cannot have world/group permissions on the filter file - for your own good.");
		if (stat_buf.st_uid != getuid())
			tempfail("mailfilter file is owned by the wrong user.");

		if (r.ParseRecipe(in) < 0)
			return (EX_TEMPFAIL);

//		if (maildrop.msgptr->MessageSize() > 0)
			r.ExecuteRecipe();
	}
//
// If a message is successfully delivered, an exception is thrown.
// If we're here, we should deliver to the default mailbox.
//

	if (!maildrop.embedded_mode)
	{
		name="DEFAULT";

	const char *v=GetVarStr(name);

		if (!v)
		{
			errexit=EX_TEMPFAIL;
			throw "DEFAULT mailbox not defined.";
		}

		value=v;
		value += '\0';
		if (delivery((const char *)value) < 0)
			return (EX_TEMPFAIL);
	}

	value="EXITCODE";
	return ( GetVar(value)->Int("0") );
}

int main(int argc, char **argv)
{

#if HAVE_SETLOCALE
	setlocale(LC_ALL, "C");
#endif

	_exit(Maildrop::trap(run, argc, argv));
}

const char *GetDefaultMailbox(const char *username)
{
static Buffer buf;
int	isfile=0;

	buf="";

const	char *p=DEFAULT_DEF;

	if (*p != SLASH_CHAR)	// Relative to home directory
	{
		buf=maildrop.init_home;
		buf.push(SLASH_CHAR);
		isfile=1;
	}

	while (*p)
	{
		if (*p != '=')
		{
			buf.push(*p);
			++p;
		}

	const char *q=username;

		while (*p == '=')
		{
			buf.push (*q ? *q:'.');
			if (*q)	q++;
			p++;
		}
	}

	if (!isfile)
	{
		buf.push(SLASH_CHAR);
		buf += username;
	}
	buf += '\0';
	return (buf);
}

#if	SHARED_TEMPDIR

#else
const char *TempName()
{
Buffer	t;

	t=(const char *)maildrop.tempdir;
	t += "/tmp.";
	t += '\0';
	return (TempName((const char *)t, 0));
}
#endif
