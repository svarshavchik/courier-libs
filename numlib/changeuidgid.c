/*
** Copyright 1998 - 2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<sys/types.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<grp.h>
#include	<pwd.h>
#include	<errno.h>

#include	"numlib.h"


void libmail_changegroup(gid_t gid)
{
	if ( setgid(gid))
	{
		perror("setgid");
		exit(1);
	}

#if HAVE_SETGROUPS
	if ( getuid() == 0 && setgroups(1, &gid) )
	{
		perror("setgroups");
		exit(1);
	}
#endif
}

void libmail_changeuidgid(uid_t uid, gid_t gid)
{
	libmail_changegroup(gid);
	if ( setuid(uid))
	{
		perror("setuid");
		exit(1);
	}
}

/**
 * Obtain the uid associated to uname and, optionally, the user primary gid
 */
uid_t libmail_getuid(const char *uname, gid_t *pw_gid)
{
	int bufsize;
	char *buf;
	struct passwd pwbuf;
	struct passwd *pw;
	int s;

	/*
	** uname might be a pointer returned from a previous called to getpw(),
	** and libc has a problem getting it back.
	*/
	char	*p=malloc(strlen(uname)+1);

	if (!p)
	{
		perror("malloc");
		exit(1);
	}
	strcpy(p, uname);

#ifdef _SC_GETPW_R_SIZE_MAX
	bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsize == -1)          /* Value was indeterminate */
#endif
	{
		bufsize = 16384;        /* Should be more than enough */
	}

	do {
		buf = malloc(bufsize);
		if (buf == NULL)
		{
			perror("malloc");
			exit(1);
		}

		s = getpwnam_r(p, &pwbuf, buf, bufsize, &pw);
		if (s == ERANGE) {
			free(buf);
			bufsize += 1024;
		}
	} while (s == ERANGE && bufsize <= 65536);

	free(p);

	if (pw == 0)
	{
		errno = s;
		perror("getpwnam_r");
		exit(1);
	}

	free(buf);

	if ( pw_gid ) *pw_gid = pw->pw_gid;

	return pw->pw_uid;
}

void libmail_changeusername(const char *uname, const gid_t *forcegrp)
{
uid_t	changeuid;
gid_t	changegid;

	changeuid=libmail_getuid(uname, &changegid);

	if ( forcegrp )	changegid= *forcegrp;

	if ( setgid( changegid ))
	{
		perror("setgid");
		exit(1);
	}

#if HAVE_INITGROUPS
	if ( getuid() == 0 && initgroups(uname, changegid) )
	{
		perror("initgroups");
		exit(1);
	}
#else
#if HAVE_SETGROUPS
	if ( getuid() == 0 && setgroups(1, &changegid) )
	{
		perror("setgroups");
		exit(1);
	}
#endif
#endif

	if (setuid(changeuid))
	{
		perror("setuid");
		exit(1);
	}
}

gid_t libmail_getgid(const char *gname)
{
	gid_t g;
	struct group grp;
	struct group *result;
	char *buf;
	int bufsize;
	int s;
	char	*p=malloc(strlen(gname)+1);

	if (!p)
	{
		perror("malloc");
		exit(1);
	}
	strcpy(p, gname);

#ifdef _SC_GETGR_R_SIZE_MAX
	bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
	if (bufsize == -1)          /* Value was indeterminate */
#endif
	{
		bufsize = 16384;        /* Should be more than enough */
	}

	do {
		buf = malloc(bufsize);
		if (buf == NULL)
		{
			perror("malloc");
			exit(1);
		}

		s = getgrnam_r(p, &grp, buf, bufsize, &result);
		if (s == ERANGE) {
			free(buf);
			bufsize += 1024;
		}
	} while (s == ERANGE && bufsize <= 65536);

	free(p);

	if (result == NULL)
	{
		if (s == 0)
		{
			fprintf(stderr, "CRIT: Group %s not found\n", gname);
		}
		else
		{
			errno = s;
			perror("getgrnam_r");
		}
		exit(1);
	}

	g = grp.gr_gid;
	free(buf);

	return g;
}
