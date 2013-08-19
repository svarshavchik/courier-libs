#ifndef	maildirquota_h
#define	maildirquota_h

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<sys/types.h>
#include	<stdio.h>

#ifdef  __cplusplus
extern "C" {
#endif


int maildir_checkquota(const char *,	/* Pointer to directory */
	int *,	/* Initialized to -1, or opened descriptor for maildirsize */
	const char *,	/* The quota */
	long,		/* Extra bytes planning to add/remove from maildir */
	int);		/* Extra messages planning to add/remove from maildir */

int maildir_addquota(const char *,	/* Pointer to the maildir */
	int,	/* Must be the int pointed to by 2nd arg to checkquota */
	const char *,	/* The quota */
	long,	/* +/- bytes */
	int);	/* +/- files */

extern int maildir_parsequota(const char *, unsigned long *);
	/* Attempt to parse file size encoded in filename.  Returns 0 if
	** parsed, non-zero if we didn't parse. */

#ifdef  __cplusplus
}
#endif

#endif
