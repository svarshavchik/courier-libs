/*
** Copyright 2001-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#if	HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <sys/types.h>
#if	HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include "mimegpgheader.h"
#include "mimegpgstack.h"
#include "mimegpgfork.h"
#include "tempname.h"
#include "gpglib.h"
#include "rfc822/encode.h"
#include "rfc2045/rfc2045.h"

static int my_rewind(FILE *fp)
{
	if (fflush(fp) || ferror(fp) || fseek(fp, 0L, SEEK_SET))
		return (-1);
	clearerr(fp);
	return (0);
}

int libmail_gpg_inputfunc_readfp(char *buf, size_t cnt, void *vp)
{
	FILE *fp=(FILE *)vp;
	size_t i;
	int c;

	if (cnt == 0)
		return -1;

	--cnt;

	for (i=0; i<cnt; i++)
	{
		if ((c=getc(fp)) == EOF)
		{
			if (i == 0)
				return -1;
			break;
		}
		buf[i]=c;

		if (c == '\n')
		{
			++i;
			break;
		}
	}
	buf[i]=0;
	return 0;
}

void libmail_gpg_noexec(int fd)
{
#ifdef FD_CLOEXEC
	fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
}

/*
** Check if the line just read is a MIME boundary line.  Search the
** current MIME stack for a matching MIME boundary delimiter.
*/

static struct mimestack *is_boundary(struct mimestack *s, const char *line,
				     int *isclosing)
{
	struct mimestack *b;

	if (line[0] != '-' || line[1] != '-' ||
	    (b=libmail_mimestack_search(s, line+2)) == 0)
		return (NULL);


	*isclosing=strncmp(line+2+strlen(b->boundary), "--", 2) == 0;
	return (b);
}

static const char *get_boundary(struct mimestack *,
				const char *,
				FILE *);

/*
** Skip until EOF or a MIME boundary delimiter other than a closing MIME
** boundary delimiter.  After returning from bind_boundary we expect to
** see MIME headers.  Copy any intermediate lines to fpout.
*/

static void find_boundary(struct mimestack **stack, int *iseof,
			  int (*input_func)(char *, size_t, void *vp),
			  void *input_func_arg,
			  void (*output_func)(const char *,
					      size_t,
					      void *),
			  void *output_func_arg,
			  int doappend)
{
	char buf[BUFSIZ];

	for (;;)
	{
		int is_closing;
		struct mimestack *b;

		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			return;
		}

		if (!(b=is_boundary(*stack, buf, &is_closing)))
		{
			if (output_func)
				(*output_func)(buf, strlen(buf),
					       output_func_arg);

			while (strchr(buf, '\n') == 0)
			{
				if ( (*input_func)(buf, sizeof(buf),
						   input_func_arg))
				{
					*iseof=1;
					return;
				}
				if (output_func)
					(*output_func)(buf, strlen(buf),
						       output_func_arg);
			}
			continue;
		}

		if (output_func)
		{
			(*output_func)("--", 2, output_func_arg);
			(*output_func)(b->boundary, strlen(b->boundary),
				       output_func_arg);

			if (is_closing)
				(*output_func)("--", 2, output_func_arg);

			(*output_func)("\n", 1, output_func_arg);
		}

		if (is_closing)
		{
			libmail_mimestack_pop_to(stack, b);
			continue;
		}
		break;
	}
}


/*
** Read a set of headers.
*/

static struct header *read_headers(struct mimestack **stack, int *iseof,
				   int (*input_func)(char *, size_t, void *vp),
				   void *input_func_arg,
				   void (*output_func)(const char *,
						       size_t,
						       void *),
				   void *output_func_arg,
				   int doappend,
				   int *errflag)
{
	char buf[BUFSIZ];
	struct read_header_context rhc;
	struct header *h;

	*errflag=0;
	libmail_readheader_init(&rhc);

	while (!*iseof)
	{
		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}

		if (READ_START_OF_LINE(rhc))
		{
			struct mimestack *b;
			int is_closing;

			if (strcmp(buf, "\n") == 0
			    || strcmp(buf, "\r\n") == 0)
				break;

			b=is_boundary(*stack, buf, &is_closing);

			if (b)
			{
				/*
				** Corrupted MIME message.  We should NOT
				** see a MIME boundary in the middle of the
				** headers!
				**
				** Ignore this damage.
				*/

				struct header *p;

				h=libmail_readheader_finish(&rhc);

				for (p=h; p; p=p->next)
					(*output_func)(p->header,
						       strlen(p->header),
						       output_func_arg);

				(*output_func)("--", 2, output_func_arg);
				(*output_func)(b->boundary,
					       strlen(b->boundary),
					       output_func_arg);

				if (is_closing)
					(*output_func)("--", 2,
						       output_func_arg);

				(*output_func)("\n", 1, output_func_arg);

				if (is_closing)
				{
					libmail_mimestack_pop_to(stack, b);
					find_boundary(stack, iseof,
						      input_func,
						      input_func_arg,
						      output_func,
						      output_func_arg,
						      doappend);
				}
				libmail_header_free(h);

				libmail_readheader_init(&rhc);
				continue; /* From the top */
			}
		}
		if (libmail_readheader(&rhc, buf) < 0)
		{
			libmail_header_free(libmail_readheader_finish(&rhc));
			*errflag= -1;
			return NULL;
		}
	}

	return (libmail_readheader_finish(&rhc));
}

/*
** Here we do actual signing/encoding
*/

static int encode_header(const char *h)
{
	if (strncasecmp(h, "content-", 8) == 0)
		return (1);
	return (0);
}

struct gpg_fork_output_info {
	void (*output_func)(const char *, size_t, void *);
	void *output_func_arg;
	struct gpgmime_forkinfo *gpgptr;
};

static int gpg_fork_output(const char *p, size_t n, void *dummy)
{
	struct gpg_fork_output_info *info=(struct gpg_fork_output_info *)dummy;

	(*info->output_func)(p, n, info->output_func_arg);
	return 0;
}


static int dogpgencrypt(const char *gpghome,
			const char *passphrase_fd,
			struct mimestack **stack,
			struct header *h, int *iseof,
			int (*input_func)(char *, size_t, void *vp),
			void *input_func_arg,
			void (*output_func)(const char *,
					    size_t,
					    void *),
			void *output_func_arg,
			int argc,
			char **argv,
			int dosign,
			void (*errhandler)(const char *, void *),
			void *errhandler_arg)
{
	struct header *hp;
	char buf[BUFSIZ];
	struct gpgmime_forkinfo gpg;
	int clos_flag=0;
	struct mimestack *b=0;
	int rc;
	const char *boundary;
	int need_crlf;
	struct gpg_fork_output_info gfoi;

	boundary=get_boundary(*stack, "", NULL);

	gfoi.output_func=output_func;
	gfoi.output_func_arg=output_func_arg;

	if (libmail_gpgmime_forksignencrypt(gpghome, passphrase_fd,
					    (dosign ? GPG_SE_SIGN:0)
					    | GPG_SE_ENCRYPT,
					    argc, argv,
					    &gpg_fork_output, &gfoi,
					    &gpg))
	{
		return -1;
	}

	for (hp=h; hp; hp=hp->next)
	{
		if (encode_header(hp->header))
			continue;

		(*output_func)(hp->header, strlen(hp->header),
			       output_func_arg);
	}

#define C(s) (*output_func)( s, sizeof(s)-1, output_func_arg)
#define S(s) (*output_func)( s, strlen(s), output_func_arg)


	C("Content-Type: multipart/encrypted;\n"
	  "    boundary=\"");

	S(boundary);

	C("\";\n"
	  "    protocol=\"application/pgp-encrypted\"\n"
	  "\n"
	  "This is a MIME GnuPG-encrypted message.  If you see this text, it means\n"
	  "that your E-mail or Usenet software does not support MIME encrypted messages.\n"
	  "The Internet standard for MIME PGP messages, RFC 2015, was published in 1996.\n"
	  "To open this message correctly you will need to install E-mail or Usenet\n"
	  "software that supports modern Internet standards.\n"
	  "\n--");

	S(boundary);

	C("\n"
	  "Content-Type: application/pgp-encrypted\n"
	  "Content-Transfer-Encoding: 7bit\n"
	  "\n"
	  "Version: 1\n"
	  "\n--");

	S(boundary);

	C("\n"
	  "Content-Type: application/octet-stream\n"
	  "Content-Transfer-Encoding: 7bit\n\n");

#undef C
#undef S

	/* For Eudora compatiblity */
        libmail_gpgmime_write(&gpg, "Mime-Version: 1.0\r\n", 19);

	for (hp=h; hp; hp=hp->next)
	{
		const char *p;

		if (!encode_header(hp->header))
			continue;

		for (p=hp->header; *p; p++)
		{
			if (*p == '\r')
				continue;

			if (*p == '\n')
				libmail_gpgmime_write(&gpg, "\r\n", 2);
			else
				libmail_gpgmime_write(&gpg, p, 1);
		}
	}

	/*
	** Chew the content until the next MIME boundary.
	*/
	need_crlf=1;

	while (!*iseof)
	{
		const char *p;

		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}

		if (need_crlf)
		{
			if ((b=is_boundary(*stack, buf, &clos_flag)) != NULL)
				break;

			libmail_gpgmime_write(&gpg, "\r\n", 2);
		}

		need_crlf=0;
		for (;;)
		{
			for (p=buf; *p; p++)
			{
				if (*p == '\r')
					continue;
				if (*p == '\n')
				{
					need_crlf=1;
					break;
				}

				libmail_gpgmime_write(&gpg, p, 1);
			}
			if (*p == '\n')
				break;

			if ( (*input_func)(buf, sizeof(buf), input_func_arg))
			{
				*iseof=1;
				break;
			}
		}
	}

	/*
	** This needs some 'splainin.  Note that we spit out a newline at
	** the BEGINNING of each line, above.  This generates the blank
	** header->body separator line.  Now, if we're NOT doing multiline
	** content, we need to follow the last line of the content with a
	** newline.  If we're already doing multiline content, that extra
	** newline (if it exists) is already there.
	*/

	if (!*stack)
	{
		libmail_gpgmime_write(&gpg, "\r\n", 2);
	}

	rc=libmail_gpgmime_finish(&gpg);

	if (rc)
	{
		(*errhandler)(libmail_gpgmime_getoutput(&gpg), errhandler_arg);
		return (-1);
	}

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(boundary, strlen(boundary), output_func_arg);
	(*output_func)("--\n", 3, output_func_arg);

	if (*iseof)
		return 0;

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(b->boundary, strlen(b->boundary), output_func_arg);
	if (clos_flag)
		(*output_func)("--", 2, output_func_arg);
	(*output_func)("\n", 1, output_func_arg);

	if (clos_flag)
	{
		libmail_mimestack_pop_to(stack, b);
		find_boundary(stack, iseof, input_func,
			      input_func_arg, output_func,
			      output_func_arg, 1);
	}

	return 0;
}

static int dogpgsign(const char *gpghome, const char *passphrase_fd,
		     struct mimestack **stack, struct header *h, int *iseof,
		     int (*input_func)(char *, size_t, void *vp),
		     void *input_func_arg,
		     void (*output_func)(const char *,
					 size_t,
					 void *),
		     void *output_func_arg,
		     int argc,
		     char **argv,
		     void (*errhandler)(const char *, void *),
		     void *errhandler_arg)
{
	struct header *hp;
	char buf[8192];
	struct gpgmime_forkinfo gpg;
	int clos_flag=0;
	struct mimestack *b=0;
	int rc=0;
	char signed_content_name[TEMPNAMEBUFSIZE];
	int signed_content;
	FILE *signed_content_fp;
	const char *boundary;
	int need_crlf;
	struct gpg_fork_output_info gfoi;

	for (hp=h; hp; hp=hp->next)
	{
		if (encode_header(hp->header))
			continue;
		(*output_func)(hp->header, strlen(hp->header),
			       output_func_arg);
	}

	signed_content=libmail_tempfile(signed_content_name);
	if (signed_content < 0 ||
	    (signed_content_fp=fdopen(signed_content, "w+")) == NULL)
	{
		if (signed_content >= 0)
		{
			close(signed_content);
			unlink(signed_content_name);
		}
		return -1;

	}
	libmail_gpg_noexec(fileno(signed_content_fp));
	unlink(signed_content_name);	/* UNIX semantics */

	for (hp=h; hp; hp=hp->next)
	{
		const char *p;

		if (!encode_header(hp->header))
			continue;

		for (p=hp->header; *p; p++)
		{
			if (*p == '\r')
				continue;

			if (*p == '\n')
				putc('\r', signed_content_fp);
			putc(*p, signed_content_fp);
		}
	}

	/*
	** Chew the content until the next MIME boundary.
	*/
	need_crlf=1;
	while (!*iseof)
	{
		const char *p;

		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}

		if (need_crlf)
		{
			if ((b=is_boundary(*stack, buf, &clos_flag)) != NULL)
				break;

			fprintf(signed_content_fp, "\r\n");
		}

		need_crlf=0;
		for (;;)
		{
			for (p=buf; *p; p++)
			{
				if (*p == '\r')
					continue;
				if (*p == '\n')
				{
					need_crlf=1;
					break;
				}

				putc(*p, signed_content_fp);
			}
			if (*p == '\n')
				break;

			if ( (*input_func)(buf, sizeof(buf), input_func_arg))
			{
				*iseof=1;
				break;
			}
		}
	}

	/*
	** This needs some 'splainin.  Note that we spit out a newline at
	** the BEGINNING of each line, above.  This generates the blank
	** header->body separator line.  Now, if we're NOT doing multiline
	** content, we need to follow the last line of the content with a
	** newline.  If we're already doing multiline content, that extra
	** newline (if it exists) is already there.
	*/

	if (!*stack)
	{
		fprintf(signed_content_fp, "\r\n");
	}

	if (fflush(signed_content_fp) < 0 || ferror(signed_content_fp))
	{
		fclose(signed_content_fp);
		return (-1);
	}

	boundary=get_boundary(*stack, "", signed_content_fp);

	if (my_rewind(signed_content_fp) < 0)
	{
		fclose(signed_content_fp);
		return (-1);
	}

#define C(s) (*output_func)( s, sizeof(s)-1, output_func_arg)
#define S(s) (*output_func)( s, strlen(s), output_func_arg)

	C("Content-Type: multipart/signed;\n"
	  "    boundary=\"");
	S(boundary);
	C("\";\n"
	  "    micalg=pgp-sha1;"
	  " protocol=\"application/pgp-signature\"\n"
	  "\n"
	  "This is a MIME GnuPG-signed message.  If you see this text, it means that\n"
	  "your E-mail or Usenet software does not support MIME signed messages.\n"
	  "The Internet standard for MIME PGP messages, RFC 2015, was published in 1996.\n"
	  "To open this message correctly you will need to install E-mail or Usenet\n"
	  "software that supports modern Internet standards.\n"
	  "\n--");
	S(boundary);
	C("\n");


	gfoi.output_func=output_func;
	gfoi.output_func_arg=output_func_arg;
	gfoi.gpgptr= &gpg;

	if (libmail_gpgmime_forksignencrypt(gpghome, passphrase_fd,
					    GPG_SE_SIGN,
					    argc, argv,
					    &gpg_fork_output, &gfoi,
					    &gpg))
	{
		fclose(signed_content_fp);
		return (-1);
	}

	while (fgets(buf, sizeof(buf), signed_content_fp) != NULL)
	{
		char *p;
		size_t j, k;

		libmail_gpgmime_write(&gpg, buf, strlen(buf));

		p=buf;
		for (j=k=0; p[j]; j++)
			if (p[j] != '\r')
				p[k++]=p[j];

		if (k)
			(*output_func)(p, k, output_func_arg);
	}

	C("\n--");
	S(boundary);

	C("\n"
	  "Content-Type: application/pgp-signature\n"
	  "Content-Transfer-Encoding: 7bit\n\n");

#undef C
#undef S

	if (libmail_gpgmime_finish(&gpg))
		rc= -1; /* TODO */

	if (rc)
	{
		(*errhandler)(libmail_gpgmime_getoutput(&gpg),
			      errhandler_arg);
		fclose(signed_content_fp);
		return -1;
	}

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(boundary, strlen(boundary), output_func_arg);
	(*output_func)("--\n", 3, output_func_arg);

	fclose(signed_content_fp);
	if (*iseof)
		return 0;

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(b->boundary, strlen(b->boundary), output_func_arg);
	if (clos_flag)
		(*output_func)("--", 2, output_func_arg);
	(*output_func)("\n", 1, output_func_arg);

	if (clos_flag)
	{
		libmail_mimestack_pop_to(stack, b);
		find_boundary(stack, iseof, input_func, input_func_arg,
			      output_func, output_func_arg, 1);
	}
	return 0;
}

static int isgpg(struct mime_header *);
static int checksign(const char *gpghome,
		     const char *passphrase_fd,
		     struct mimestack **, int *, struct header *,
		     int (*input_func)(char *, size_t, void *vp),
		     void *input_func_arg,
		     void (*)(const char *, size_t, void *),
		     void *,
		     int, char **,
		     int *);
static int decrypt(const char *gpghome,
		   const char *passphrase_fd,
		   struct mimestack **, int *, struct header *,
		   int (*input_func)(char *, size_t, void *vp),
		   void *input_func_arg,
		   void (*)(const char *, size_t, void *),
		   void *,
		   int, char **,
		   int *);

static void print_noncontent_headers(struct header *h,
				     void (*output_func)(const char *,
							 size_t,
							 void *),
				     void *output_func_arg)
{
	struct header *p;

	for (p=h; p; p=p->next)
	{
		if (strncasecmp(p->header, "content-", 8) == 0)
			continue;
		(*output_func)(p->header, strlen(p->header), output_func_arg);
	}
}

static int dosignencode2(int dosign, int doencode, int dodecode,
			 const char *gpghome,
			 const char *passphrase_fd,
			 int (*input_func)(char *, size_t, void *vp),
			 void *input_func_arg,
			 void (*output_func)(const char *,
					     size_t,
					     void *),
			 void *output_func_arg,
			 void (*errhandler_func)(const char *, void *),
			 void *errhandler_arg,
			 int argc, char **argv,
			 int *status)
{
	struct mimestack *boundary_stack=0;
	int iseof=0;

	*status=0;

	while (!iseof)
	{
		int errflag;

		static const char ct_s[]="content-type:";
		struct header *h=read_headers(&boundary_stack, &iseof,
					      input_func, input_func_arg,
					      output_func,
					      output_func_arg,
					      dodecode ? 0:1,
					      &errflag),
			*hct;

		if (errflag)
			return 1;

		if (iseof && !h)
			continue;	/* Artifact */

		hct=libmail_header_find(h, ct_s);

		/*
		** If this is a multipart MIME section, we can keep on
		** truckin'.
		**
		*/

		if (hct)
		{
			struct mime_header *mh=
				libmail_mimeheader_parse(hct->header+
							 (sizeof(ct_s)-1));
			const char *bv;

			if (!mh)
			{
				libmail_header_free(h);
				return (-1);
			}

			if (strcasecmp(mh->header_name, "multipart/x-mimegpg")
			    == 0)
			{
				/* Punt */

				char *buf=malloc(strlen(hct->header)+100);
				const char *p;

				if (!buf)
				{
					libmail_mimeheader_free(mh);
					libmail_header_free(h);
					return (-1);
				}
				strcpy(buf, "Content-Type: multipart/mixed");
				p=strchr(hct->header, ';');
				strcat(buf, p ? p:"");
				free(hct->header);
				hct->header=buf;

				libmail_mimeheader_free(mh);
				mh=libmail_mimeheader_parse(hct->header+
							    sizeof(ct_s)-1);
				if (!mh)
				{
					libmail_header_free(h);
					return (-1);
				}
			}

			if (strncasecmp(mh->header_name, "multipart/", 10)==0
			    && (bv=libmail_mimeheader_getattr(mh, "boundary")) != 0

			    && (doencode & LIBMAIL_GPG_ENCAPSULATE) == 0

			    && !dosign
			    )
			{
				struct header *p;

				if (libmail_mimestack_push(&boundary_stack,
							   bv) < 0)
				{
					libmail_header_free(h);
					return (-1);
				}

				if (dodecode)
				{
					int rc;

					if (strcasecmp(mh->header_name,
						       "multipart/signed")==0
					    && (dodecode & LIBMAIL_GPG_CHECKSIGN)
					    && isgpg(mh))
					{
						int errflag;

						print_noncontent_headers(h,
									 output_func,
									 output_func_arg
									 );
						libmail_mimeheader_free(mh);
						rc=checksign(gpghome,
							     passphrase_fd,
							     &boundary_stack,
							     &iseof,
							     h,
							     input_func,
							     input_func_arg,
							     output_func,
							     output_func_arg,
							     argc, argv,
							     &errflag);
						libmail_header_free(h);

						if (errflag)
							*status |=
								LIBMAIL_ERR_VERIFYSIG;
						if (rc)
							return -1;

						continue;
					}

					if (strcasecmp(mh->header_name,
						       "multipart/encrypted")
					    ==0
					    && (dodecode & LIBMAIL_GPG_UNENCRYPT)
					    && isgpg(mh))
					{
						int errflag;

						print_noncontent_headers(h,
									 output_func,
									 output_func_arg
									 );
						libmail_mimeheader_free(mh);
						rc=decrypt(gpghome,
							   passphrase_fd,
							   &boundary_stack,
							   &iseof,
							   h,
							   input_func,
							   input_func_arg,
							   output_func,
							   output_func_arg,
							   argc, argv,
							   &errflag);
						libmail_header_free(h);

						if (errflag)
							*status |=
								LIBMAIL_ERR_DECRYPT;
						if (rc)
							return -1;
						continue;
					}
				}

				for (p=h; p; p=p->next)
				{
					(*output_func)(p->header,
						       strlen(p->header),
						       output_func_arg);
				}

				(*output_func)("\n", 1, output_func_arg);
				libmail_header_free(h);
				libmail_mimeheader_free(mh);

				find_boundary(&boundary_stack, &iseof,
					      input_func,
					      input_func_arg,
					      output_func,
					      output_func_arg, dodecode ? 0:1);
				continue;
			}
			libmail_mimeheader_free(mh);
		}

		if (dodecode)
		{
			struct header *p;
			int is_message_rfc822=0;

			for (p=h; p; p=p->next)
			{
				(*output_func)(p->header,
					       strlen(p->header),
					       output_func_arg);
			}
			(*output_func)("\n", 1, output_func_arg);

			/*
			** If this is a message/rfc822 attachment, we can
			** resume reading the next set of headers.
			*/

			hct=libmail_header_find(h, ct_s);
			if (hct)
			{
				struct mime_header *mh=
					libmail_mimeheader_parse(hct->header+
								 (sizeof(ct_s)
								  -1));
				if (!mh)
				{
					libmail_header_free(h);
					return (-1);
				}

				if (strcasecmp(mh->header_name,
					       "message/rfc822") == 0)
					is_message_rfc822=1;
				libmail_mimeheader_free(mh);
			}
			libmail_header_free(h);

			if (!is_message_rfc822)
				find_boundary(&boundary_stack, &iseof,
					      input_func,
					      input_func_arg,
					      output_func,
					      output_func_arg, 0);
			continue;
		}

		if (doencode ?
		    dogpgencrypt(gpghome,
				 passphrase_fd,
				 &boundary_stack, h, &iseof,
				 input_func,
				 input_func_arg,
				 output_func,
				 output_func_arg,
				 argc, argv, dosign,
				 errhandler_func, errhandler_arg)
		    :
		    dogpgsign(gpghome,
			      passphrase_fd,
			      &boundary_stack, h, &iseof,
			      input_func,
			      input_func_arg,
			      output_func,
			      output_func_arg,
			      argc, argv,
			      errhandler_func, errhandler_arg))
		{
			libmail_header_free(h);
			return 1;
		}

		libmail_header_free(h);
	}

	return (0);
}


static int isgpg(struct mime_header *mh)
{
	const char *attr;

	attr=libmail_mimeheader_getattr(mh, "protocol");

	if (!attr)
		return (0);

	if (strcasecmp(attr, "application/pgp-encrypted") == 0)
		return (1);

	if (strcasecmp(attr, "application/pgp-signature") == 0)
	{
		return (1);
	}
	return (0);
}

static int nybble(char c)
{
	static const char x[]="0123456789ABCDEFabcdef";

	const char *p=strchr(x, c);
	int n;

	if (!p) p=x;

	n= p-x;
	if (n >= 16)
		n -= 6;
	return (n);
}

/*
** Check signature
*/

static int dochecksign(const char *, const char *,
		       struct mimestack *,
		       FILE *,
		       void (*output_func)(const char *,
					   size_t,
					   void *),
		       void *output_func_arg,
		       const char *,
		       const char *,
		       int, char **, int *);

static int checksign(const char *gpghome,
		     const char *passphrase_fd,
		     struct mimestack **stack, int *iseof,
		     struct header *h,
		     int (*input_func)(char *, size_t, void *vp),
		     void *input_func_arg,
		     void (*output_func)(const char *,
					 size_t,
					 void *),
		     void *output_func_arg,
		     int argc, char **argv, int *errptr)
{
	char buf[BUFSIZ];
	struct header *h2;

	char signed_content[TEMPNAMEBUFSIZE];
	char signature[TEMPNAMEBUFSIZE];
	int signed_file, signature_file;
	FILE *signed_file_fp, *signature_file_fp;
	int clos_flag;
	int need_nl, check_boundary;
	struct mimestack *b=0;
	struct mime_header *mh;
	int qpdecode=0;
	int errflag;

	*errptr=0;

	signed_file=libmail_tempfile(signed_content);

	if (signed_file < 0 || (signed_file_fp=fdopen(signed_file, "w+")) == 0)
	{
		if (signed_file > 0)
		{
			close(signed_file);
			unlink(signed_content);
		}
		return -1;
	}
	libmail_gpg_noexec(fileno(signed_file_fp));

	find_boundary(stack, iseof, input_func,
		      input_func_arg, NULL, NULL, 0);
	if (*iseof)
		return 0;

	need_nl=0;
	check_boundary=1;

	while (!*iseof)
	{
		const char *p;

		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			continue;
		}

		if (check_boundary
		    && (b=is_boundary(*stack, buf, &clos_flag)) != 0)
			break;
		if (need_nl)
			fprintf(signed_file_fp, "\r\n");

		for (p=buf; *p && *p != '\n'; p++)
			putc(*p, signed_file_fp);
		need_nl=check_boundary= *p != 0;
	}

	if (my_rewind(signed_file_fp) < 0)
	{
		fclose(signed_file_fp);
		unlink(signed_content);
		return -1;
	}

	if (clos_flag)
	{
		fclose(signed_file_fp);
		unlink(signed_content);
		if (b)
			libmail_mimestack_pop_to(stack, b);
		find_boundary(stack, iseof, input_func, input_func_arg,
			      output_func, output_func_arg, 1);
		return 0;
	}

	h=read_headers(stack, iseof, input_func, input_func_arg,
		       output_func, output_func_arg, 0, &errflag);

	if (errflag)
	{
		fclose(signed_file_fp);
		unlink(signed_content);

		return (-1);
	}

	if (!h || !(h2=libmail_header_find(h, "content-type:")))
	{
		if (h)
			libmail_header_free(h);
		fclose(signed_file_fp);
		unlink(signed_content);
		if (!*iseof)
			find_boundary(stack, iseof, input_func, input_func_arg,
				      output_func, output_func_arg, 1);
		return 0;
	}

	mh=libmail_mimeheader_parse(h2->header+sizeof("content-type:")-1);

	if (!mh)
	{
		libmail_header_free(h);
		fclose(signed_file_fp);
		unlink(signed_content);
		return (-1);
	}

	if (strcasecmp(mh->header_name, "application/pgp-signature"))
	{
		libmail_mimeheader_free(mh);
		libmail_header_free(h);
		fclose(signed_file_fp);
		unlink(signed_content);
		if (!*iseof)
			find_boundary(stack, iseof, input_func, input_func_arg,
				      output_func, output_func_arg, 1);
		return (0);
	}
	libmail_mimeheader_free(mh);

	/*
	** In rare instances, the signature is qp-encoded.
	*/

	if ((h2=libmail_header_find(h, "content-transfer-encoding:")) != NULL)
	{
		mh=libmail_mimeheader_parse
			(h2->header+sizeof("content-transfer-encoding:")-1);

		if (!mh)
		{
			libmail_header_free(h);
			fclose(signed_file_fp);
			unlink(signed_content);
			return -1;
		}

		if (strcasecmp(mh->header_name,
			       "quoted-printable") == 0)
			qpdecode=1;
		libmail_mimeheader_free(mh);
	}
	libmail_header_free(h);

	signature_file=libmail_tempfile(signature);

	if (signature_file < 0
	    || (signature_file_fp=fdopen(signature_file, "w+")) == 0)
	{
		if (signature_file > 0)
		{
			close(signature_file);
			unlink(signature);
		}
		unlink(signed_content);
		return (-1);
	}

	while (!*iseof)
	{
		const char *p;

		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			continue;
		}

		if ((b=is_boundary(*stack, buf, &clos_flag)) != 0)
			break;

		for (p=buf; *p; p++)
		{
			int n;

			if (!qpdecode)
			{
				putc(*p, signature_file_fp);
				continue;
			}

			if (*p == '=' && p[1] == '\n')
				break;

			if (*p == '=' && p[1] && p[2])
			{
				n=nybble(p[1]) * 16 + nybble(p[2]);
				if ( (char)n )
				{
					putc((char)n, signature_file_fp);
					p += 2;
				}
				p += 2;
				continue;
			}
			putc(*p, signature_file_fp);

			/* If some spits out qp-lines > BUFSIZ, they deserve
			** this crap.
			*/
		}
	}

	fflush(signature_file_fp);
	if (ferror(signature_file_fp))
	{
		unlink(signature);
		fclose(signed_file_fp);
		unlink(signed_content);
		return -1;
	}
	if (fclose(signature_file_fp))
	{
		unlink(signature);
		unlink(signed_content);
		return -1;
	}

	errflag=dochecksign(gpghome,
			    passphrase_fd,
			    *stack, signed_file_fp,
			    output_func, output_func_arg, signed_content,
			    signature,
			    argc, argv, errptr);

	fclose(signed_file_fp);
	unlink(signature);
	unlink(signed_content);

	if (errflag)
		return -1;

	while (!clos_flag)
	{
		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}
		if (!(b=is_boundary(*stack, buf, &clos_flag)))
			clos_flag=0;
	}
	if (b)
		libmail_mimestack_pop_to(stack, b);

	return 0;
}

static const char *newboundary()
{
	static char buffer[256];
	static unsigned counter=0;
	time_t t;
	char hostnamebuf[256];

	time(&t);
	hostnamebuf[sizeof(hostnamebuf)-1]=0;
	if (gethostname(hostnamebuf, sizeof(hostnamebuf)-1) < 0)
		hostnamebuf[0]=0;

	sprintf(buffer, "=_%-1.30s-%u-%u-%04u",
		hostnamebuf, (unsigned)getpid(),
		(unsigned)t, ++counter);
	return (buffer);
}

static int good_boundary(const char *boundary,
			 struct mimestack *m, const char *errmsg, FILE *fp)
{
	int dummy;
	int l=strlen(boundary);
	const char *p;
	char buf[BUFSIZ];

	if (is_boundary(m, boundary, &dummy))
		return (0);

	for (p=errmsg; *p; )
	{
		if (*p == '-' && p[1] == '-' && strncasecmp(p+2, boundary, l)
		    == 0)
			return (0);

		while (*p)
			if (*p++ == '\n')
				break;
	}

	if (fp)
	{
		if (my_rewind(fp) < 0)
			return 0;

		while (fgets(buf, sizeof(buf), fp))
		{
			if (buf[0] == '-' && buf[1] == '-' &&
			    strncasecmp(buf+2, boundary, l) == 0)
				return (0);
		}
	}
	return (1);
}

static const char *get_boundary(struct mimestack *m,
				const char *errmsg,
				FILE *fp)
{
	const char *p;

	do
	{
		p=newboundary();
	} while (!good_boundary(p, m, errmsg, fp));
	return (p);
}

static const char *encoding_str(const char *p)
{
	while (*p)
	{
		if (*p <= 0 || *p >= 0x7F)
			return ("8bit");
		++p;
	}
	return ("7bit");
}


static int copyfp(FILE *t,
		  void (*output_func)(const char *,
				      size_t,
				      void *),
		  void *output_func_arg,
		  int stripcr)
{
	char buf[BUFSIZ];
	int rc=0;

	while ((rc=fread(buf, 1, sizeof(buf), t)) > 0)
	{
		if (stripcr)
		{
			int i, j;

			for (i=j=0; i<rc; ++i)
			{
				if (buf[i] != '\r')
					buf[j++]=buf[i];
			}
			rc=j;
		}
		(*output_func)(buf, rc, output_func_arg);
	}

	return rc;
}

static void open_result_multipart(void (*)(const char *, size_t, void *),
				  void *,
				  int, const char *, const char *,
				  const char *);

static int dochecksign(const char *gpghome,
		       const char *passphrase_fd,
		       struct mimestack *stack,
		       FILE *content_fp,
		       void (*output_func)(const char *,
					   size_t,
					   void *),
		       void *output_func_arg,
		       const char *content_filename,
		       const char *signature_filename,
		       int argc,
		       char **argv, int *errptr)
{
	struct gpgmime_forkinfo gpg;
	int i;
	const char *new_boundary;
	const char *output;

	if (libmail_gpgmime_forkchecksign(gpghome, passphrase_fd,
				  content_filename,
				  signature_filename,
				  argc, argv,
				  &gpg))
	{
		return -1;
	}

	*errptr=i=libmail_gpgmime_finish(&gpg);

	output=libmail_gpgmime_getoutput(&gpg);

	new_boundary=get_boundary(stack, output, content_fp);

	open_result_multipart(output_func, output_func_arg,
			      i, new_boundary, output,
			      libmail_gpgmime_getcharset(&gpg));

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(new_boundary, strlen(new_boundary), output_func_arg);
	(*output_func)("\n", 1, output_func_arg);

	if (my_rewind(content_fp) < 0)
	{
		return -1;
	}

	if (copyfp(content_fp, output_func, output_func_arg, 1))
		return -1;

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(new_boundary, strlen(new_boundary), output_func_arg);
	(*output_func)("--\n", 3, output_func_arg);
	return 0;
}

static void open_result_multipart(void (*output_func)(const char *,
						      size_t,
						      void *),
				  void *output_func_arg,
				  int rc,
				  const char *new_boundary,
				  const char *err_str,
				  const char *err_charset)
{
#define C(s) (*output_func)( s, sizeof(s)-1, output_func_arg)
#define S(s) (*output_func)( s, strlen(s), output_func_arg)

	char n[10];
	const char *p;

	sprintf(n, "%d", rc);

	C("Content-Type: multipart/x-mimegpg; xpgpstatus=");

	S(n);

	C("; boundary=\"");

	S(new_boundary);

	C("\"\n"
	  "\nThis is a MIME GnuPG-processed message.  If you see this text, it means\n"
	  "that your E-mail or Usenetsoftware does not support MIME-formatted messages.\n\n"
	  "--");

	S(new_boundary);
	C("\nContent-Type: text/x-gpg-output; charset=");
	S(err_charset);
	C("\nContent-Transfer-Encoding: ");

	p=encoding_str(err_str);
	S(p);
	C("\n\n");
	S(err_str);
#undef C
#undef S
}

static void close_mime(struct mimestack **stack, int *iseof,
		       int (*input_func)(char *, size_t, void *vp),
		       void *input_func_arg,
		       void (*output_func)(const char *,
					   size_t,
					   void *),
		       void *output_func_arg)
{
	char buf[BUFSIZ];
	int is_closing;
	struct mimestack *b;

	for (;;)
	{
		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}

		(*output_func)(buf, strlen(buf), output_func_arg);
		if (!(b=is_boundary(*stack, buf, &is_closing)))
			continue;
		if (!is_closing)
			continue;

		libmail_mimestack_pop_to(stack, b);
		break;
	}
}

static int dodecrypt(const char *, const char *,
		     struct mimestack **, int *,
		     int (*input_func)(char *, size_t, void *vp),
		     void *input_func_arg,
		     FILE *, int, char **, const char *,
		     void (*)(const char *,
			      size_t,
			      void *),
		     void *,
		     int *);

static void write_temp_fp(const char *p, size_t n, void *vp)
{
	if (fwrite(p, n, 1, (FILE *)vp) != 1)
		; /* ignored */
}

static int decrypt(const char *gpghome,
		   const char *passphrase_fd,
		   struct mimestack **stack, int *iseof,
		   struct header *h,
		   int (*input_func)(char *, size_t, void *vp),
		   void *input_func_arg,
		   void (*output_func)(const char *,
				       size_t,
				       void *),
		   void *output_func_arg,
		   int argc, char **argv,
		   int *errptr)
{
	struct header *p, *q;
	char temp_file[TEMPNAMEBUFSIZE];
	int temp_fd;
	FILE *temp_fp;
	struct mime_header *mh;
	int flag;
	int errflag;

	*errptr=0;
	temp_fd=libmail_tempfile(temp_file);
	if (temp_fd < 0 || (temp_fp=fdopen(temp_fd, "w+")) == 0)
	{
		if (temp_fd >= 0)
			close(temp_fd);
		return -1;
	}

	for (p=h; p; p=p->next)
	{
		fprintf(temp_fp, "%s", p->header);
	}
	putc('\n', temp_fp);

	find_boundary(stack, iseof, input_func, input_func_arg,
		      write_temp_fp, temp_fp, 0);
	if (*iseof)
	{
		fclose(temp_fp);
		unlink(temp_file);
		return (0);
	}

	p=read_headers(stack, iseof, input_func, input_func_arg, write_temp_fp,
		       temp_fp, 0, &errflag);

	if (*iseof || errflag)
	{
		libmail_header_free(p);
		fclose(temp_fp);
		unlink(temp_file);

		if (errflag)
			return -1;
		return 0;
	}

	q=libmail_header_find(p, "content-type:");

	flag=0;

	if (q)
	{
		mh=libmail_mimeheader_parse(q->header+13);
		if (!mh)
		{
			libmail_header_free(p);
			fclose(temp_fp);
			unlink(temp_file);
			return -1;
		}

		if (strcasecmp(mh->header_name, "application/pgp-encrypted")
		    == 0)
			flag=1;
		libmail_mimeheader_free(mh);
	}

	for (q=p; q; q=q->next)
	{
		fprintf(temp_fp, "%s", q->header);
	}
	libmail_header_free(p);
	putc('\n', temp_fp);

	p=read_headers(stack, iseof, input_func, input_func_arg,
		       write_temp_fp, temp_fp, 0,
		       &errflag);

	if (*iseof || errflag)
	{
		libmail_header_free(p);
		fclose(temp_fp);
		unlink(temp_file);

		if (errflag)
			return -1;

		return 0;
	}

	q=libmail_header_find(p, "version:");

	if (flag)
	{
		if (!q || atoi(p->header + 8) != 1)
			flag=0;
	}
	for (q=p; q; q=q->next)
	{
		fprintf(temp_fp, "%s", q->header);
	}
	libmail_header_free(p);
	putc('\n', temp_fp);

	find_boundary(stack, iseof, input_func, input_func_arg,
		      write_temp_fp, temp_fp, 0);

	if (*iseof)
	{
		fclose(temp_fp);
		unlink(temp_file);
		return 0;
	}

	p=read_headers(stack, iseof, input_func, input_func_arg, write_temp_fp,
		       temp_fp, 0, &errflag);

	if (*iseof || errflag)
	{
		libmail_header_free(p);
		fclose(temp_fp);
		unlink(temp_file);

		if (errflag)
			return -1;

		return 0;
	}

	q=libmail_header_find(p, "content-type:");

	if (q && flag)
	{
		flag=0;
		mh=libmail_mimeheader_parse(q->header+13);
		if (!mh)
		{
			libmail_header_free(p);
			fclose(temp_fp);
			unlink(temp_file);
			return -1;
		}

		if (strcasecmp(mh->header_name, "application/octet-stream")
		    == 0)
			flag=1;
		libmail_mimeheader_free(mh);

		q=libmail_header_find(p, "content-transfer-encoding:");
		if (q && flag)
		{
			flag=0;
			mh=libmail_mimeheader_parse(strchr(q->header, ':')+1);
			if (!mh)
			{
				libmail_header_free(p);
				fclose(temp_fp);
				unlink(temp_file);
				return -1;
			}

			if (strcasecmp(mh->header_name, "7bit") == 0 ||
			    strcasecmp(mh->header_name, "8bit") == 0)
				flag=1;
			libmail_mimeheader_free(mh);
		}
	}

	for (q=p; q; q=q->next)
	{
		fprintf(temp_fp, "%s", q->header);
	}
	libmail_header_free(p);
	putc('\n', temp_fp);

	if (fflush(temp_fp) || ferror(temp_fp) || my_rewind(temp_fp) < 0)
	{
		fclose(temp_fp);
		unlink(temp_file);
		return -1;
	}

	if (!flag)
	{
		int c=copyfp(temp_fp, output_func, output_func_arg, 0);

		fclose(temp_fp);
		unlink(temp_file);
		close_mime(stack, iseof, input_func, input_func_arg,
			   output_func, output_func_arg);
		return (c);
	}

	fclose(temp_fp);
	if ((temp_fp=fopen(temp_file, "w+")) == NULL)
	{
		unlink(temp_file);
		return (-1);
	}
	libmail_gpg_noexec(fileno(temp_fp));
	errflag=dodecrypt(gpghome, passphrase_fd,
			  stack, iseof, input_func, input_func_arg,
			  temp_fp, argc, argv, temp_file,
			  output_func, output_func_arg, errptr);
	fclose(temp_fp);
	unlink(temp_file);
	return errflag;
}

static int dumpdecrypt(const char *c, size_t n, void *vp)
{
	FILE *fp=(FILE *)vp;

	if (n == 0)
		return 0;

	if (fwrite(c, n, 1, fp) != 1)
		return -1;
	return (0);
}

static int dodecrypt(const char *gpghome,
		     const char *passphrase_fd,
		     struct mimestack **stack, int *iseof,
		     int (*input_func)(char *, size_t, void *vp),
		     void *input_func_arg,
		     FILE *fpout, int argc, char **argv,
		     const char *temp_file,
		     void (*output_func)(const char *,
			      size_t,
			      void *),
		     void *output_func_arg,
		     int *errptr)
{
	struct gpgmime_forkinfo gpg;
	char buf[BUFSIZ];
	int is_closing;
	struct mimestack *b=NULL;
	int dowrite=1;
	int rc;
	const char *new_boundary;
	const char *output;

	if (libmail_gpgmime_forkdecrypt(gpghome,
				passphrase_fd,
				argc, argv, &dumpdecrypt, fpout, &gpg))
		return -1;

	for (;;)
	{
		if ( (*input_func)(buf, sizeof(buf), input_func_arg))
		{
			*iseof=1;
			break;
		}

		if (dowrite)
			libmail_gpgmime_write(&gpg, buf, strlen(buf));

		if (!(b=is_boundary(*stack, buf, &is_closing)))
			continue;
		dowrite=0;
		if (!is_closing)
			continue;
		break;
	}

	rc=libmail_gpgmime_finish(&gpg);
	if (fflush(fpout) || ferror(fpout) || my_rewind(fpout) < 0)
	{
		fclose(fpout);
		unlink(temp_file);
		return -1;
	}

	if (*iseof)
		return 0;

	output=libmail_gpgmime_getoutput(&gpg),

	new_boundary=get_boundary(*stack, output, rc ? NULL:fpout);

	open_result_multipart(output_func,
			      output_func_arg, rc, new_boundary,
			      output,
			      libmail_gpgmime_getcharset(&gpg));

	*errptr=rc;

#if 0

	/*
	** gnupg returns non-zero exit even if succesfully unencrypted, when
	** just the signature is bad.
	*/
	if (rc == 0)
#endif
	{
		if (fseek(fpout, 0L, SEEK_SET) < 0)
		{
			fclose(fpout);
			unlink(temp_file);
			return -1;
		}

		(*output_func)("\n--", 3, output_func_arg);
		(*output_func)(new_boundary, strlen(new_boundary),
			       output_func_arg);
		(*output_func)("\n", 1, output_func_arg);

		if (copyfp(fpout, output_func, output_func_arg, 1))
		    return -1;
	}

	(*output_func)("\n--", 3, output_func_arg);
	(*output_func)(new_boundary, strlen(new_boundary),
		       output_func_arg);
	(*output_func)("--\n", 3, output_func_arg);

	libmail_mimestack_pop_to(stack, b);
	return 0;
}

struct libmail_gpg_errhandler {

	struct libmail_gpg_info *options;
	int err_flag;
};

static void libmail_gpg_errfunc(const char *errmsg, void *vp)
{
	struct libmail_gpg_errhandler *eh=(struct libmail_gpg_errhandler *)vp;

	if (!eh->err_flag)
	{
		eh->err_flag=1;

		(*eh->options->errhandler_func)(errmsg,
						eh->options->errhandler_arg);
	}
}
 
static int input_func_from_fp(char *buf, size_t cnt, void *vp)
{
	if (fgets(buf, cnt, (FILE *)vp) == NULL)
		return (-1);
	return (0);
}

/*
** When signing, but not encoding, signed text must be 7bit, as per RFC.
**
** Use rfc2045's rewriter to do this.
*/

static int dosignencode(int dosign, int doencode, int dodecode,
			const char *gpghome,
			const char *passphrase_fd,
			int (*input_func)(char *, size_t, void *vp),
			void *input_func_arg,
			void (*output_func)(const char *,
					    size_t,
					    void *),
			void *output_func_arg,
			void (*errhandler_func)(const char *, void *),
			void *errhandler_arg,
			int argc, char **argv,
			int *status)
{
	char temp_decode_name[TEMPNAMEBUFSIZE];
	int fdin;
	int fdout;
	FILE *fdin_fp;
	char buffer[8192];
	struct rfc2045src *src;
	struct rfc2045 *rfcp;
	int rc;

	if (!dosign || doencode)
		return dosignencode2(dosign, doencode, dodecode,
				     gpghome,
				     passphrase_fd,
				     input_func,
				     input_func_arg,
				     output_func,
				     output_func_arg,
				     errhandler_func,
				     errhandler_arg,
				     argc, argv, status);

	/* Save the message into a temp file, first */

	fdin=libmail_tempfile(temp_decode_name);

	if (fdin < 0 ||
	    (fdin_fp=fdopen(fdin, "w+")) == NULL)
	{
		if (fdin >= 0)
			close(fdin);

		(*errhandler_func)("Cannot create temporary file",
				   errhandler_arg);
		return (-1);
	}

	unlink(temp_decode_name);

	if (!(rfcp=rfc2045_alloc_ac()))
	{
		(*errhandler_func)(strerror(errno), errhandler_arg);
		fclose(fdin_fp);
		return (-1);
	}

	while ( (*input_func)(buffer, sizeof(buffer), input_func_arg) == 0)
	{
		size_t l=strlen(buffer);

		if (fwrite(buffer, l, 1, fdin_fp) != 1)
		{
			(*errhandler_func)(strerror(errno), errhandler_arg);
			fclose(fdin_fp);
			rfc2045_free(rfcp);
			return (-1);
		}

		/* Parse the message at the same time it's being saved */

		rfc2045_parse(rfcp, buffer, l);
	}

	if (fseek(fdin_fp, 0L, SEEK_SET) < 0)
	{
		(*errhandler_func)(strerror(errno), errhandler_arg);
		fclose(fdin_fp);
		rfc2045_free(rfcp);
		return (-1);
	}

	if (!rfc2045_ac_check(rfcp, RFC2045_RW_7BIT))
	{
		rfc2045_free(rfcp);

		/* No need to rewrite, just do this */

		rc=dosignencode2(dosign, doencode, dodecode,
				     gpghome,
				     passphrase_fd,
				     input_func_from_fp,
				     fdin_fp,
				     output_func,
				     output_func_arg,
				     errhandler_func,
				     errhandler_arg,
				     argc, argv, status);

		fclose(fdin_fp);

		return rc;
	}

	/* Rewrite the message into another temp file */

	fdout=libmail_tempfile(temp_decode_name);

	src=rfc2045src_init_fd(fileno(fdin_fp));

	if (fdout < 0 || src == NULL ||
	    rfc2045_rewrite(rfcp, src, fdout, "mimegpg") < 0 ||
	    lseek(fdout, 0L, SEEK_SET) < 0)
	{
		if (fdout >= 0)
			close(fdout);
		if (src)
			rfc2045src_deinit(src);

		(*errhandler_func)(strerror(errno), errhandler_arg);
		rfc2045_free(rfcp);
		fclose(fdin_fp);
		return (-1);
	}
	fclose(fdin_fp);
	rfc2045_free(rfcp);
	rfc2045src_deinit(src);

	/* Now, read the converted message, from the temp file */

	if ((fdin_fp=fdopen(fdout, "w+")) == NULL)
	{
		close(fdout);

		(*errhandler_func)("Cannot create temporary file",
				   errhandler_arg);
		return (-1);
	}

	rc=dosignencode2(dosign, doencode, dodecode,
			 gpghome,
			 passphrase_fd,
			 input_func_from_fp,
			 fdin_fp,
			 output_func,
			 output_func_arg,
			 errhandler_func,
			 errhandler_arg,
			 argc, argv, status);
	fclose(fdin_fp);
	return rc;
}

int libmail_gpg_signencode(int dosign,
			   int doencode,
			   /*
			   ** One of LIBMAIL_GPG_INDIVIDUAL or
			   ** LIBMAIL_GPG_ENCAPSULATE
			   */
			   struct libmail_gpg_info *options)
{
	int rc;
	struct libmail_gpg_errhandler eh;

	eh.options=options;
	eh.err_flag=0;

	if (doencode != LIBMAIL_GPG_INDIVIDUAL &&
	    doencode != LIBMAIL_GPG_ENCAPSULATE)
		doencode=0;

	if (!dosign && !doencode)
	{
		(*options->errhandler_func)("Invalid arguments to"
					    " libmail_gpg_signencode",
					    options->errhandler_arg);
		return -1;
	}

	rc=dosignencode(dosign, doencode, 0,
			options->gnupghome,
			options->passphrase_fd,
			options->input_func,
			options->input_func_arg,
			options->output_func,
			options->output_func_arg,
			&libmail_gpg_errfunc,
			&eh,
			options->argc,
			options->argv,
			&options->errstatus);

	if (rc && !eh.err_flag)
		(*options->errhandler_func)(strerror(errno),
					    options->errhandler_arg);
	return rc;
}

int libmail_gpg_decode(int mode,
		       /*
		       ** LIBMAIL_GPG_UNENCRYPT OR LIBMAIL_GPG_CHECKSIGN
		       */
		       struct libmail_gpg_info *options)
{
	int rc;
	struct libmail_gpg_errhandler eh;

	eh.options=options;
	eh.err_flag=0;

	if ((mode & (LIBMAIL_GPG_UNENCRYPT|LIBMAIL_GPG_CHECKSIGN)) == 0)
	{
		(*options->errhandler_func)("Invalid arguments to"
					    " libmail_gpg_decode",
					    options->errhandler_arg);
		return -1;
	}

	rc=dosignencode(0, 0, mode,
			options->gnupghome,
			options->passphrase_fd,
			options->input_func,
			options->input_func_arg,
			options->output_func,
			options->output_func_arg,
			&libmail_gpg_errfunc,
			&eh,
			options->argc,
			options->argv,
			&options->errstatus);

	if (rc && !eh.err_flag)
		(*options->errhandler_func)(strerror(errno),
					    options->errhandler_arg);
	return rc;
}
