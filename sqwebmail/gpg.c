/*
** Copyright 2001-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"config.h"
#include	"gpg.h"
#include	"pref.h"
#include	"cgi/cgi.h"
#include	"gpglib/gpglib.h"
#include	<courier-unicode.h>
#include	"numlib/numlib.h"
#include	"rfc822/rfc822.h"
#include	"htmllibdir.h"
#include	<stdio.h>
#include	<string.h>
#include	<errno.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif

#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

extern void output_scriptptrget();
extern void print_attrencodedlen(const char *, size_t, int, FILE *);
extern void print_safe(const char *);
extern const char *sqwebmail_content_charset;


static char gpgerrbuf[1024];
static size_t gpgerrcnt=0;

static void gpginiterr()
{
	gpgerrcnt=0;
}

static int gpg_error(const char *p, size_t l, void *dummy)
{
	while (l && gpgerrcnt < sizeof(gpgerrbuf)-1)
	{
		gpgerrbuf[gpgerrcnt++]= *p++;
		--l;
	}
	return (0);
}

static void gpg_error_save(const char *errmsg, void *dummy)
{
	gpg_error(errmsg, strlen(errmsg), dummy);
}

int gpgbadarg(const char *p)
{
	for ( ; *p; p++)
	{
		int c=(unsigned char)*p;

		if (c < ' ' || strchr("\",'`;*?()<>", c))
			return (1);
	}
	return (0);
}

static void dump_error()
{
	if (gpgerrcnt >= 0)
	{
		printf("<span style=\"color: #e00000\"><pre class=\"gpgerroutput\">");
		print_attrencodedlen (gpgerrbuf, gpgerrcnt, 1, stdout);
		printf("</pre></span>\n");
	}
}

struct listinfo {
	int issecret;
	const char *default_key;
} ;

static int show_key(const char *fingerprint, const char *shortname,
                    const char *key, int invalid,
		    struct gpg_list_info *gli)
{
	struct listinfo *li=(struct listinfo *)gli->voidarg;

	printf("<tr valign=\"middle\" class=\"%s\"><td>"
	       "<input type=\"radio\" name=\"%s\" value=\"",
	       li->issecret ? "gpgseckey":"gpgpubkey",
	       li->issecret ? "seckeyname":"pubkeyname");

	print_attrencodedlen(fingerprint, strlen(fingerprint), 0, stdout);
	printf("\"%s /></td><td><span class=\"tt\">",
	       li->default_key && strcmp(li->default_key, fingerprint) == 0
	       ? " checked=\"checked\"":"");
	print_safe(key);
	printf("</span></td></tr>\n");
	return (0);
}

static void listpubsec(int flag,
		       int (*callback_func)(const char *,
					    const char *,
					    const char *,
					    int,
					    struct gpg_list_info *),
		       const char *default_key
		       )
{
	int rc;
	struct gpg_list_info gli;
	struct listinfo li;

	li.issecret=flag;

	li.default_key=default_key;

	memset(&gli, 0, sizeof(gli));
	gli.charset=sqwebmail_content_charset;

	gli.disabled_msg=getarg("DISABLED");
	gli.revoked_msg=getarg("REVOKED");
	gli.expired_msg=getarg("EXPIRED");
	gli.voidarg= &li;

	gpginiterr();

	rc=libmail_gpg_listkeys(GPGDIR, flag, callback_func, gpg_error, &gli);

	if (rc)
	{
		dump_error();
	}
}

void gpglistpub()
{
	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"2\" cellpadding=\"0\" class=\"gpgpubkeys\">");
	listpubsec(0, show_key, NULL);
	printf("</table>");
}

void gpglistsec()
{
	printf("<table width=\"100%%\" border=\"0\" cellspacing=\"2\" cellpadding=\"0\" class=\"gpgseckeys\">");
	listpubsec(1, show_key, NULL);
	printf("</table>");
}

static int select_key(const char *fingerprint, const char *shortname,
		      const char *key,
		      struct gpg_list_info *gli,
		      int is_select)
{
	printf("<option value=\"");
	print_attrencodedlen(fingerprint, strlen(fingerprint), 0, stdout);
	printf("\"%s>", is_select ? " selected='selected'":"");

	print_safe(shortname);
	printf("</option>");
	return (0);
}

static int select_key_default(const char *fingerprint, const char *shortname,
			      const char *key,
			      int invalid,
			      struct gpg_list_info *gli)
{
	struct listinfo *li=(struct listinfo *)gli->voidarg;

	return (select_key(fingerprint, shortname, key, gli,
			   li->default_key && strcmp(li->default_key,
						     fingerprint)
			   == 0));
}

void gpgselectkey()
{
	char *default_key=pref_getdefaultgpgkey();

	listpubsec(1, select_key_default, default_key);

	if (default_key)
		free(default_key);
}

void gpgselectpubkey()
{
	listpubsec(0, select_key_default, NULL);
}

void gpgselectprivkey()
{
	listpubsec(1, select_key_default, NULL);
}

/*
** Check if this encryption key address is included in the list of recipients
** of the message.
*/

static int knownkey(const char *shortname, const char *known_keys)
{
	struct rfc822t *t=rfc822t_alloc_new(shortname, NULL, NULL);
	struct rfc822a *a;
	int i;

	if (!t)
		return (0);

	a=rfc822a_alloc(t);

	if (!a)
	{
		rfc822t_free(t);
		return (0);
	}

	for (i=0; i<a->naddrs; i++)
	{
		char *p=rfc822_getaddr(a, i);
		int plen;
		const char *q;

		if (!p)
			continue;

		if (!*p)
		{
			free(p);
			continue;
		}

		plen=strlen(p);

		for (q=known_keys; *q; )
		{
			if (strncasecmp(q, p, plen) == 0 && q[plen] == '\n')
			{
				free(p);
				rfc822a_free(a);
				rfc822t_free(t);
				return (1);
			}

			while (*q)
				if (*q++ == '\n')
					break;
		}
		free(p);
	}
	rfc822a_free(a);
	rfc822t_free(t);
	return (0);
}

static int encrypt_key_default(const char *fingerprint, const char *shortname,
			       const char *key,
			       int invalid,
			       struct gpg_list_info *gli)
{
	struct listinfo *li=(struct listinfo *)gli->voidarg;

	if (invalid)
		return (0);

	return (select_key(fingerprint, shortname, key, gli,
			   knownkey(shortname, li->default_key)));
}

void gpgencryptkeys(const char *select_keys)
{
	listpubsec(0, encrypt_key_default, select_keys);
}


/*
** Create a new key
*/

static int dump_func(const char *p, size_t l, void *vp)
{
	int *ip=(int *)vp;

	while (l)
	{
		if (*ip >= 80)
		{
			printf("\n");
			*ip=0;
		}

		++*ip;

		switch (*p) {
		case '<':
			printf("&lt;");
			break;
		case '>':
			printf("&gt;");
			break;
		case '\n':
			*ip=0;
			/* FALLTHROUGH */
		default:
			putchar(*p);
			break;
		}

		++p;
		--l;
	}
	fflush(stdout);
	return (0);
}

static int timeout_func(void *vp)
{
	return (dump_func("*", 1, vp));
}

void gpgcreate()
{
	int linelen;

	const char *newname=cgi("newname");
	const char *newaddress=cgi("newaddress");
	const char *newcomment=cgi("newcomment");
	unsigned skl=atoi(cgi("skeylength"));
	unsigned ekl=atoi(cgi("ekeylength"));
	unsigned newexpire=atoi(cgi("newexpire"));
	char newexpirewhen=*cgi("newexpirewhen");
	const char *passphrase, *p;

	if (*newname == 0 || *newaddress == 0 || strchr(newaddress, '@') == 0
	    || gpgbadarg(newname) || gpgbadarg(newaddress)
	    || gpgbadarg(newcomment)
	    || ekl < 512 || ekl > 2048 || skl < 512 || skl > 1024)
	{
		printf("%s\n", getarg("BADARGS"));
		return;
	}
	passphrase=cgi("passphrase");
	if (strcmp(passphrase, cgi("passphrase2")))
	{
		printf("%s\n", getarg("PASSPHRASEFAIL"));
		return;
	}

	for (p=passphrase; *p; p++)
	{
		if ((int)(unsigned char)*p < ' ')
		{
			printf("%s\n", getarg("PASSPHRASEFAIL"));
			return;
		}
	}

	printf("<pre class=\"gpgcreate\">");

	linelen=0;

	libmail_gpg_genkey(GPGDIR, sqwebmail_content_charset,
			   newname, newaddress, newcomment,
			   skl, ekl,
			   newexpire, newexpirewhen,
			   passphrase,
			   &dump_func,
			   &timeout_func,
			   &linelen);
	printf("</pre>");
}

static void delkey(const char *keyname, int flag)
{
	int rc;

	if (gpgbadarg(keyname))
		return;

	gpginiterr();

	rc=libmail_gpg_deletekey(GPGDIR, flag, keyname, gpg_error, NULL);

	if (rc)
	{
		printf("<div class=\"indent\">%s\n", getarg("DELETEFAIL"));
		dump_error();
		printf("</div>\n");
	}
}

static FILE *passphrasefp()
{
	FILE *fp=NULL;
	const char *passphrase;

	passphrase=cgi("passphrase");
	if (*passphrase)
	{
		fp=tmpfile();
		if (fp)
		{
			fprintf(fp, "%s", passphrase);
			if (fflush(fp) || ferror(fp)
			    || lseek(fileno(fp), 0L, SEEK_SET) < 0
			    || fcntl(fileno(fp), F_SETFD, 0) < 0)
			{
				fclose(fp);
				fp=NULL;
			}
		}
	}
	return (fp);
}

static void signkey(const char *signthis, const char *signwith)
{
	int rc;
	FILE *fp=NULL;

	if (gpgbadarg(signthis) || gpgbadarg(signwith))
		return;

	gpginiterr();


	fp=passphrasefp();

	rc=libmail_gpg_signkey(GPGDIR, signthis, signwith,
			       fp ? fileno(fp):-1, gpg_error, NULL);

	if (fp)
		fclose(fp);

	if (rc)
	{
		printf("<div class=\"indent\">%s\n", getarg("SIGNFAIL"));
		dump_error();
		printf("</div>\n");
	}
}

static void setdefault(const char *def)
{
	if (gpgbadarg(def))
		return;

	pref_setdefaultgpgkey(def);
}

void gpgdo()
{
	if (*cgi("delpub"))
		delkey(cgi("pubkeyname"), 0);
	else if (*cgi("delsec") && *cgi("really"))
		delkey(cgi("seckeyname"), 1);
	else if (*cgi("sign"))
		signkey(cgi("pubkeyname"), cgi("seckeyname"));
	else if (*cgi("setdefault"))
		setdefault(cgi("seckeyname"));
}

static char gpgerrbuf[1024];

static int read_fd(char *buf, size_t cnt, void *vp)
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

static void write_fd(const char *p, size_t n, void *dummy)
{
	if (n == 0)
		return;

	if (fwrite(p, n, 1, (FILE *)dummy) != 1)
		exit(1);
}

int gpgdomsg(int in_fd, int out_fd, const char *signkey,
	     const char *encryptkeys)
{
	char *k=strdup(encryptkeys ? encryptkeys:"");
	int n;
	int i;
	char *p;
	char **argvec;
	FILE *passfd=NULL;
	char passfd_buf[NUMBUFSIZE];
	struct libmail_gpg_info gi;

	int in_dup, out_dup;
	FILE *in_fp, *out_fp;

	gpginiterr();

	if (!k)
	{
		enomem();
		return 1;
	}

	if ((in_dup=dup(in_fd)) < 0 || (in_fp=fdopen(in_dup, "r")) == NULL)
	{
		if (in_dup >= 0)
			close(in_dup);
		free(k);
		enomem();
		return 1;
	}

	if ((out_dup=dup(out_fd)) < 0 || (out_fp=fdopen(out_dup, "w")) == NULL)
	{
		if (out_dup >= 0)
			close(out_dup);
		fclose(in_fp);
		close(in_dup);
		free(k);
		enomem();
		return 1;
	}

	passfd=passphrasefp();

	n=0;
	for (p=k; (p=strtok(p, " ")) != NULL; p=NULL)
		++n;

	argvec=malloc((n * 2 + 22)*sizeof(char *));
	if (!argvec)
	{
		fclose(out_fp);
		close(out_dup);
		fclose(in_fp);
		close(in_dup);
		free(k);
		enomem();
		return 1;
	}

	memset(&gi, 0, sizeof(gi));

	gi.gnupghome=GPGDIR;
	if (passfd)
	{
		gi.passphrase_fd=libmail_str_size_t(fileno(passfd),
						    passfd_buf);
	}

	gi.input_func= read_fd;
	gi.input_func_arg= in_fp;
	gi.output_func= write_fd;
	gi.output_func_arg= out_fp;
	gi.errhandler_func= gpg_error_save;
	gi.errhandler_arg= NULL;


	i=0;
	argvec[i++] = "--no-tty";
	if (signkey)
	{
		argvec[i++]="--default-key";
		argvec[i++]=(char *)signkey;
	}

	argvec[i++]="--always-trust";

	for (p=strcpy(k, encryptkeys ? encryptkeys:"");
	     (p=strtok(p, " ")) != NULL; p=NULL)
	{
		argvec[i++]="-r";
		argvec[i++]=p;
	}
	argvec[i]=0;
	gi.argc=i;
	gi.argv=argvec;

	i=libmail_gpg_signencode(signkey ? 1:0,
				 n > 0 ? LIBMAIL_GPG_ENCAPSULATE:0,
				 &gi);

	free(argvec);
	fclose(out_fp);
	close(out_dup);
	fclose(in_fp);
	close(in_dup);
	free(k);
	if (passfd)
		fclose(passfd);

	return i;
}

void sent_gpgerrtxt()
{
	const char *p;

	for (p=gpgerrbuf; *p; p++)
	{
		switch (*p) {
		case '<':
			printf("&lt;");
			break;
		case '>':
			printf("&gt;");
			break;
		default:
			putchar((int)(unsigned char)*p);
			break;
		}
	}
}

void sent_gpgerrresume()
{
	output_scriptptrget();
	printf("&form=newmsg&pos=%s&draft=%s", cgi("pos"),
	       cgi("draftmessage"));
}

int gpgdecode(int in_fd, int out_fd)
{
	char passfd_buf[NUMBUFSIZE];
	FILE *fp=passphrasefp();
	int in_dup, out_dup;
	FILE *in_fp, *out_fp;
	struct libmail_gpg_info gi;
	char *argvec[2];
	int i;

	gpginiterr();

	if ((in_dup=dup(in_fd)) < 0 || (in_fp=fdopen(in_dup, "r")) == NULL)
	{
		if (in_dup >= 0)
			close(in_dup);
		fclose(fp);
		enomem();
		return 1;
	}

	if ((out_dup=dup(out_fd)) < 0 || (out_fp=fdopen(out_dup, "w")) == NULL)
	{
		if (out_dup >= 0)
			close(out_dup);
		fclose(in_fp);
		close(in_dup);
		fclose(fp);
		enomem();
		return 1;
	}

	memset(&gi, 0, sizeof(gi));

	gi.gnupghome=GPGDIR;
	if (fp)
	{
		gi.passphrase_fd=libmail_str_size_t(fileno(fp), passfd_buf);
	}

	gi.input_func= read_fd;
	gi.input_func_arg= in_fp;
	gi.output_func= write_fd;
	gi.output_func_arg= out_fp;
	gi.errhandler_func= gpg_error_save;
	gi.errhandler_arg= NULL;

	argvec[0] = "--no-tty";
	argvec[1]=NULL;
	gi.argc=1;
	gi.argv=argvec;

	i=libmail_gpg_decode(LIBMAIL_GPG_UNENCRYPT|LIBMAIL_GPG_CHECKSIGN,
			     &gi);
	fclose(out_fp);
	close(out_dup);
	fclose(in_fp);
	close(in_dup);
	if (fp)
		fclose(fp);

	if (i)
	{
		printf("<div class=\"indent\"><pre style=\"color: red;\">");
		sent_gpgerrtxt();
		printf("</pre></div>\n");
	}
	return (i);
}

int gpgexportkey(const char *fingerprint, int issecret,
		 int (*func)(const char *, size_t, void *),
		 void *arg)
{
	gpginiterr();

	return (libmail_gpg_exportkey(GPGDIR, issecret, fingerprint,
				      func,
				      gpg_error,
				      arg));
}
