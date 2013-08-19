/*
** Copyright 2001-2003 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#include "config.h"
#include "gpglib.h"
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>

extern void libmail_gpg_noexec(int fd);

void rfc2045_error(const char *p)
{
	fprintf(stderr, "%s\n", p);
	exit(1);
}

static void usage()
{
	fprintf(stderr, "Usage: mimegpg [-s] [-e] [-c] [-d] -- [gpg options]\n");
}

static void my_output(const char *p, size_t n,
		      void *dummy)
{
	FILE *fp=(FILE *)dummy;

	if (fwrite(p, n, 1, fp) != 1)
	{
		perror("write");
		exit(1);
	}
}

static int my_inputfunc(char *buf, size_t buf_size, void *vp)
{
	FILE *fp=(FILE *)vp;
	size_t n;

	if (buf_size <= 0)
		return -1;

	--buf_size;

	for (n=0; n<buf_size; n++)
	{
		int c=fgetc(fp);

		if (c == EOF)
		{
			buf[n]=0;

			return n ? 0:-1;
		}

		buf[n]=c;

		if (c == '\n')
		{
			n++;
			break;
		}
	}
	buf[n]=0;
	return 0;
}


static void my_errhandler(const char *msg, void *dummy)
{
	fprintf(stderr, "ERROR: %s\n", msg);
}

int main(int argc, char **argv)
{
	int argn;
	int dosign=0;
	int doencode=0;
	int dodecode=0;
	int rc;
	const char *passphrase_fd=0;
	struct libmail_gpg_info gpg_info;

	setlocale(LC_ALL, "C");

	for (argn=1; argn < argc; argn++)
	{
		if (strcmp(argv[argn], "--") == 0)
		{
			++argn;
			break;
		}

		if (strcmp(argv[argn], "-s") == 0)
		{
			dosign=1;
			continue;
		}

		if (strcmp(argv[argn], "-e") == 0)
		{
			doencode=LIBMAIL_GPG_INDIVIDUAL;
			continue;
		}

		if (strcmp(argv[argn], "-E") == 0)
		{
			doencode=LIBMAIL_GPG_ENCAPSULATE;
			continue;
		}

		if (strcmp(argv[argn], "-d") == 0)
		{
			dodecode |= LIBMAIL_GPG_UNENCRYPT;
			continue;
		}

		if (strcmp(argv[argn], "-c") == 0)
		{
			dodecode |= LIBMAIL_GPG_CHECKSIGN;
			continue;
		}

		if (strcmp(argv[argn], "-p") == 0)
		{
			++argn;
			if (argn < argc)
			{
				passphrase_fd=argv[argn];
				continue;
			}
			--argn;
		}

		fprintf(stderr, "Unknown option: %s\n", argv[argn]);
		exit(1);
	}

	if (!dosign && !doencode && !dodecode)
	{
		usage();
		return (1);
	}

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);

	/* Make things sane */

	if (dosign || doencode)
		dodecode=0;

#if 0
	if (dosign && !doencode)
		reformime(argv[0]);
#endif

	memset(&gpg_info, 0, sizeof(gpg_info));

	gpg_info.input_func=my_inputfunc;
	gpg_info.input_func_arg=stdin;
	gpg_info.output_func=my_output;
	gpg_info.output_func_arg=stdout;
	gpg_info.errhandler_func=my_errhandler;
	gpg_info.errhandler_arg=NULL;
	gpg_info.passphrase_fd=passphrase_fd;
	gpg_info.argc=argc - argn;
	gpg_info.argv=argv + argn;

	libmail_gpg_noexec(fileno(stdin));
	libmail_gpg_noexec(fileno(stdout));

	rc=dodecode ? libmail_gpg_decode(dodecode, &gpg_info)
		: libmail_gpg_signencode(dosign, doencode, &gpg_info);


	if (rc == 0 && (fflush(stdout) || ferror(stdout)))
	{
		perror("write");
		rc=1;
	}

	exit(rc);
	return (0);
}
