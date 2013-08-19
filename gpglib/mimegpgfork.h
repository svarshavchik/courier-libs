#ifndef mimegpgfork_h
#define mimegpgfork_h

/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct gpgmime_forkinfo {
	int togpg_fd;
	int fromgpg_fd;
	int fromgpg_errfd;

	char gpg_writebuf[BUFSIZ];
	char gpg_errbuf[1024];

	unsigned gpg_writecnt;
	unsigned gpg_errcnt;

	int gpg_errflag;
	pid_t gpg_pid;

	struct sigaction old_pipe_sig;

	int (*gpg_readhandler)(const char *, size_t, void *);
	void *gpg_voidarg;
} ;

int libmail_gpgmime_forksignencrypt(const char *,	/* gpgdir */
				    const char *,	/* passphrase fd */
				    int,	/* Flags: */

#define GPG_SE_SIGN	1
#define	GPG_SE_ENCRYPT	2

				    int, char **,	/* argc/argv */

				    int (*)(const char *, size_t, void *),
				    /* Encrypted output */
				    void *, /* 3rd arg to encrypted output */

				    struct gpgmime_forkinfo *
				    /* Allocated struct */
				    );

int libmail_gpgmime_forkchecksign(const char *,	/* gpgdir */
				  const char *,	/* passphrase fd */
				  const char *,	/* content filename */
				  const char *,	/* signature filename */
				  int, char **,	/* argc/argv */
				  struct gpgmime_forkinfo *);
	/* Allocated struct */

int libmail_gpgmime_forkdecrypt(const char *,	/* gpgdir */
				const char *,	/* passphrase fd */
				int, char **,	/* argc/argv */
				int (*)(const char *, size_t, void *),
				/* Output callback function */
				void *,	/* 3rd arg to callback function */

				struct gpgmime_forkinfo *);
	/* Allocated struct */

void libmail_gpgmime_write(struct gpgmime_forkinfo *, const char *, size_t);
int libmail_gpgmime_finish(struct gpgmime_forkinfo *);

const char *libmail_gpgmime_getoutput(struct gpgmime_forkinfo *);
const char *libmail_gpgmime_getcharset(struct gpgmime_forkinfo *);

#ifdef  __cplusplus
} ;
#endif

#endif
