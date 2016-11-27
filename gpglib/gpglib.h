#ifndef	gpglib_h
#define	gpglib_h
/*
** Copyright 2001-2016 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#ifdef  __cplusplus
extern "C" {
#endif

#include	"config.h"
#include	<sys/types.h>
#include	<stdlib.h>


#define LIBMAIL_GPG_INDIVIDUAL	1
#define LIBMAIL_GPG_ENCAPSULATE	2

#define LIBMAIL_GPG_CHECKSIGN	1
#define LIBMAIL_GPG_UNENCRYPT	2

struct libmail_gpg_info {

	const char *gnupghome; /* May be NULL, sets GNUPGHOME */

	const char *passphrase_fd; /* NULL, or string giving */

	/*
	** input_func gets called repeatedly to obtain the message to
	** encrypt/sign/decrypt/check.  input_func() receives the same
	** arguments as fgets(), with its third argument being input_func_arg.
	** input_func should read up to cnt-1 bytes, or a newline, whichever
	** comes first, and save read data in buf, appending a single null
	** byte.  input_func should return 0, or -1 on EOF condition.
	*/
	int (*input_func)(char *buf, size_t cnt, void *vp);
	void *input_func_arg;

	/*
	** Output_func gets repeatedly invoked with the contents of the
	** encrypted/signed/decrypted/verified message.
	*/

	void (*output_func)(const char *output, size_t nbytes,
			    void *output_arg);
	void *output_func_arg; /* Passthru arg to output_func */

	/*
	** In the event of an error, the error handler will be invoked with
	** the error message text.  The error handler will be invoked
	** just before libmail_gpg_*() exits.  Note that the memory used
	** by the error message text will be destroyed by the time
	** libmail_gpg_*() exits, so the application needs to make a copy of
	** it, if it intends to use it later.
	*/

	void (*errhandler_func)(const char *errmsg, void *errmsg_arg);
	void *errhandler_arg; /* Passthru arg to errhandler_func */

	/* Additional, arbitrary, arguments to GnuPG */

	int argc;
	char **argv;

	/* On exit, the following bits may be set: */

	int errstatus;

#define LIBMAIL_ERR_VERIFYSIG 1
#define LIBMAIL_ERR_DECRYPT 2

};

int libmail_gpg_signencode(int dosign,
			   int doencode,
			   /*
			   ** One of LIBMAIL_GPG_INDIVIDUAL or
			   ** LIBMAIL_GPG_ENCAPSULATE
			   */
			   struct libmail_gpg_info *options);

int libmail_gpg_decode(int mode,
		       /*
		       ** LIBMAIL_GPG_UNENCRYPT OR LIBMAIL_GPG_CHECKSIGN
		       */
		       struct libmail_gpg_info *options);


	/* A convenient input_func, where vp is FILE * */

int libmail_gpg_inputfunc_readfp(char *buf, size_t cnt, void *vp);

	/* Other functions: */

int libmail_gpg_cleanup();
int libmail_gpg_has_gpg(const char *gpgdir);

int libmail_gpg_genkey(const char *gpgdir,
		       const char *charset,
		       const char *name,
		       const char *addr,
		       const char *comment,
		       int skeylen,
		       int ekeylen,
		       unsigned expire,
		       char expire_unit,
		       const char *passphrase,

		       int (*dump_func)(const char *, size_t, void *),
		       int (*timeout_func)(void *),
		       void *voidarg);

struct gpg_list_info {
	const char *charset;
	const char *disabled_msg;
	const char *revoked_msg;
	const char *expired_msg;
	const char *group_msg;
	void *voidarg;
} ;

int libmail_gpg_listkeys(const char *gpgdir,
		 int secret,
		 int (*callback_func)(const char *, const char *,
				      const char *, int,
				      struct gpg_list_info *),
		 int (*err_func)(const char *, size_t, void *),
		 struct gpg_list_info *);

int libmail_gpg_listgroups(const char *gpgdir,
			   int (*callback_func)(const char *, const char *,
						const char *,
						int,
						struct gpg_list_info *),
			   struct gpg_list_info *voidarg);

int libmail_gpg_exportkey(const char *gpgdir,
		  int secret,
		  const char *fingerprint,
		  int (*out_func)(const char *, size_t, void *),
		  int (*err_func)(const char *, size_t, void *),
		  void *voidarg);

int libmail_gpg_deletekey(const char *gpgdir, int secret, const char *fingerprint,
		  int (*dump_func)(const char *, size_t, void *),
		  void *voidarg);

int libmail_gpg_signkey(const char *gpgdir, const char *signthis, const char *signwith,
		int passphrase_fd,
		int (*dump_func)(const char *, size_t, void *),
		void *voidarg);

int libmail_gpg_makepassphrasepipe(const char *passphrase,
				   size_t passphrase_size);
	/*
	** Create a pipe and fork, the child process writes the passphrase
	** to the pipe and exits.
	**
	** Returns the read end of the pipe.
	*/

int libmail_gpg_checksign(const char *gpgdir,
		  const char *content,	/* Filename, for now */
		  const char *signature, /* Filename, for now */
		  int (*dump_func)(const char *, size_t, void *),
		  void *voidarg);

	/* IMPORT A KEY */

int libmail_gpg_import_start(const char *gpgdir, int issecret);

int libmail_gpg_import_do(const char *p, size_t n,	/* Part of the key */
		  int (*dump_func)(const char *, size_t, void *),
		  /* gpg output callback */

		  void *voidarg);

int libmail_gpg_import_finish(int (*dump_func)(const char *, size_t, void *),
		      void *voidarg);



	     /* INTERNAL: */

pid_t libmail_gpg_fork(int *, int *, int *, const char *, char **);

#define GPGARGV_PASSPHRASE_FD(argv,i,fd,buf) \
	((argv)[(i)++]="--passphrase-fd", \
	 (argv)[(i)++]=libmail_str_size_t((fd),(buf)))

int libmail_gpg_write(const char *, size_t,
	      int (*)(const char *, size_t, void *),
	      int (*)(const char *, size_t, void *),
	      int (*)(void *),
	      unsigned,
	      void *);

int libmail_gpg_read(int (*)(const char *, size_t, void *),
	     int (*)(const char *, size_t, void *),
	     int (*)(void *),
	     unsigned,
	     void *);

char *libmail_gpg_options(const char *gpgdir);
	/* Filename of the options file.  If gpgdir is NULL try
	** the environment variables. */


struct rfc2045 *libmail_gpgmime_is_multipart_signed(const struct rfc2045 *);
	/*
	** Return ptr to signed content if ptr is a multipart/signed.
	*/

struct rfc2045 *libmail_gpgmime_is_multipart_encrypted(const struct rfc2045 *);
	/*
	** Return ptr to encrypted content if ptr is a multipart/encrypted.
	*/

int libmail_gpgmime_has_mimegpg(const struct rfc2045 *);
	/*
	** Return non-zero if MIME content has any signed or encrypted
	** content.
	*/

int libmail_gpgmime_is_decoded(const struct rfc2045 *, int *);
	/*
	** Return non-zero if this is a multipart/mixed section generated
	** by mimegpg, and return the GnuPG return code.
	*/

struct rfc2045 *libmail_gpgmime_decoded_content(const struct rfc2045 *);
	/*
	** If is_decoded, then return the ptr to the decoded content.
	** (note - if decryption failed, NULL is returned).
	*/

struct rfc2045 *libmail_gpgmime_signed_content(const struct rfc2045 *);
	/*
	** If is_multipart_signed, return ptr to the signed content.
	*/

#ifdef  __cplusplus
}
#endif
#endif
