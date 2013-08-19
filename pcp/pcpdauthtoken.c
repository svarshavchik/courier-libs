/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/

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
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	<errno.h>

#include	"pcpdauthtoken.h"
#include	"calendardir.h"
#include	"random128/random128.h"
#include	"numlib/numlib.h"
#include	"libhmac/hmac.h"

static char prev_token[128/8];
static char cur_token[128/8];
static time_t expire_time;

static const char xdigit[]="0123456789ABCDEF";

static int nyb(char c)
{
	const char *p=strchr(xdigit, c);

	if (!p)
		return (-1);
	return (p-xdigit);
}

static int goodtoken(const char *p, char *q)
{
	int i;
	for (i=0; i<128/8; i++)
	{
		int a, b;

		if ((a=nyb(*p++)) < 0 || (b=nyb(*p++)) < 0)
			return (-1);

		*q++= a * 16 + b;
	}
	return (0);
}

static void newtoken(char *p)
{
	const char *q=random128();
	int i;

	for (i=0; i<128/8; i++)
	{
		int a=nyb(*q++);
		int b=nyb(*q++);

		*p++ = a * 16 + b;
	}
}

static void savetokens()
{
	FILE *fp;

	int i;

	umask(077);
	fp=fopen(RANDTOKENFILE ".tmp", "w");
	umask(022);

	if (fp)
	{
		for (i=0; i<128/8; i++)
			fprintf(fp, "%02X", (int)(unsigned char)prev_token[i]);
		fprintf(fp, "\n");
		for (i=0; i<128/8; i++)
			fprintf(fp, "%02X", (int)(unsigned char)cur_token[i]);
		fprintf(fp, "\n");
		if (fflush(fp) == 0 && ferror(fp) == 0)
		{
			if (fclose(fp) == 0)
			{
				rename(RANDTOKENFILE ".tmp", RANDTOKENFILE);
				return;
			}
		}
		else
			fclose(fp);
	}

	fprintf(stderr, "CRIT: cannot save authentication tokens\n");
}

void authtoken_init()
{
	char buf1[512], buf2[512];
	char t1[128/8], t2[128/8];
	struct stat stat_buf;

	FILE *fp=fopen(RANDTOKENFILE, "r");

	time(&expire_time);

	if (fp)
	{
		if (fgets(buf1, sizeof(buf1), fp) &&
		    fgets(buf2, sizeof(buf2), fp))
		{
			char *p;

			if ((p=strchr(buf1, '\n')) != NULL)
				*p=0;
			if ((p=strchr(buf2, '\n')) != NULL)
				*p=0;

			/* Determine if the saved tokens are kosher */

			if (strlen(buf1) == 32 &&
			    strlen(buf2) == 32 &&
			    goodtoken(buf1, t1) == 0 &&
			    goodtoken(buf2, t2) == 0 &&
			    fstat(fileno(fp), &stat_buf) == 0 &&

			    /* Haven't expired (+tolerate clock adjustments) */
			    stat_buf.st_mtime < expire_time + 60 &&
			    stat_buf.st_mtime >= expire_time - TIMEOUT*2)
			{
				expire_time=stat_buf.st_mtime + TIMEOUT*2;
				memcpy(prev_token, t1, sizeof(t1));
				memcpy(cur_token, t2, sizeof(t2));
				fclose(fp);
				fprintf(stderr, "NOTICE: restored saved authentication tokens\n");
				return;
			}
		}
		fclose(fp);
	}
	newtoken(prev_token);
	newtoken(cur_token);
	expire_time += TIMEOUT*2;
	savetokens();
}

/* Check if authentication tokens have expired */

time_t authtoken_check()
{
	time_t now;

	time(&now);

	if (now < expire_time && now >= expire_time - TIMEOUT*4)
		return (expire_time - now);

	memcpy(prev_token, cur_token, sizeof(cur_token));
	newtoken(cur_token);
	savetokens();
	now += TIMEOUT*2;
	expire_time=now;
	return (TIMEOUT*2);
}

/*
** Create a new authentication token.
*/

static char *mktoken(const char *, const char *, time_t);

char *authtoken_create(const char *userid, time_t now)
{
	return (mktoken(userid, cur_token, now));
}

static char *mktoken(const char *hash, const char *t, time_t now)
{
	char now_s[NUMBUFSIZE];
	char *p;
	unsigned char *q;
	int i;
	char *r;

	libmail_strh_time_t(now, now_s);

	p=malloc(strlen(hash)+strlen(now_s)+3+hmac_sha1.hh_L*2);

	if (!p)
		return (NULL);

	strcat(strcpy(p, hash), now_s);

	q=malloc(hmac_sha1.hh_L*3);

	if (!q)
	{
		free(p);
		return (NULL);
	}

	hmac_hashkey(&hmac_sha1, t, sizeof(cur_token),
		     q, q + hmac_sha1.hh_L);
	hmac_hashtext(&hmac_sha1, p, strlen(p),
		      q, q + hmac_sha1.hh_L, q + hmac_sha1.hh_L*2);

	strcpy(p, now_s);
	r=p + strlen(p);
	*r++='-';

	for (i=0; i<hmac_sha1.hh_L; i++)
	{
		int c=(unsigned char)q[hmac_sha1.hh_L*2+i];

		*r++ = xdigit[c / 16];
		*r++ = xdigit[c % 16];
	}
	*r=0;
	free(q);
	return (p);
}

/*
** Check if this token is valid.
**
** Here's what we do: extract time_t from the first part of the token,
** then run the usrid and time_t against both the current seed, and the
** previous seed.
*/

int authtoken_verify(const char *userid, const char *token, time_t *when)
{
	time_t tval=0;
	const char *p=token;
	char *q;
	time_t now;

	while (*p)
	{
		int n=nyb(*p);

		if (n < 0)
			break;

		tval *= 16;
		tval += n;
		++p;
	}

	if (*p != '-' || p != token + sizeof(tval)*2)
		return (-1);

	time(&now);
	if (tval < now - TIMEOUT*4 || tval > now+60)
		return (-1);
	*when=tval;

	q=mktoken(userid, cur_token, tval);

	if (q)
	{
		if (strcmp(q, token) == 0)
		{
			free(q);
			return (0);
		}
		free(q);
		q=mktoken(userid, prev_token, tval);

		if (q)
		{
			if (strcmp(q, token) == 0)
			{
				free(q);
				return (0);
			}
			free(q);
			return (-1);
		}
	}

	fprintf(stderr, "CRIT: authtoken_create() failed.\n");
	return (-1);
}
