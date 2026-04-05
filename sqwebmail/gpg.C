/*
** Copyright 2001-2026 S. Varshavchik.  See COPYING for
** distribution information.
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
#include	<algorithm>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif

extern void output_scriptptrget();
extern void print_attrencodedlen(const char *, size_t, int, FILE *);
extern void print_safe(const char *);


static std::string gpgerrbuf;

static void gpginiterr()
{
	gpgerrbuf.clear();
}

static int gpg_error(const char *p, size_t l, void *dummy)
{
	l=std::min((size_t)1024-gpgerrbuf.size(), l);
	if (l)
	{
		gpgerrbuf.append(p, l);
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
	if (gpgerrbuf.size())
	{
		printf("<span style=\"color: #e00000\"><pre class=\"gpgerroutput\">");
		print_attrencodedlen (gpgerrbuf.data(), gpgerrbuf.size(), 1, stdout);
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
	gli.charset=sqwebmail_content_charset.c_str();

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
	auto default_key=pref_getdefaultgpgkey();

	listpubsec(1, select_key_default, default_key.size() ?
		   default_key.c_str():nullptr);
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
	rfc822::tokens t{shortname};
	rfc822::addresses a{t};

	if (!known_keys)
	{
		return 0;
	}

	for (auto &address:a)
	{
		if (address.address.empty())
			return 0;

		std::string s;

		address.address.display_address(
			sqwebmail_content_charset,
			std::back_inserter(s)
		);

		std::string_view sknown_keys{known_keys};

		while (!sknown_keys.empty())
		{
			auto p=sknown_keys.find('\n');

			if (p == std::string_view::npos)
				p=sknown_keys.size();

			std::string_view skey=sknown_keys.substr(0, p);

			if (s == skey)
			{
				return 1;
			}

			if (p < sknown_keys.size())
				++p;

			sknown_keys.remove_prefix(p);
		}
	}
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

	libmail_gpg_genkey(GPGDIR, sqwebmail_content_charset.c_str(),
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

	std::vector<std::string> argvec;
	argvec.reserve(n * 2 + 22);

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

	char notty_opt[] = "--no-tty";
	char defaultkeyopt[] = "--default-key";

	argvec.push_back(notty_opt);
	if (signkey)
	{
		argvec.push_back(defaultkeyopt);
		argvec.push_back(signkey);
	}

	argvec.push_back("--always-trust");

	for (p=strcpy(k, encryptkeys ? encryptkeys:"");
	     (p=strtok(p, " ")) != NULL; p=NULL)
	{
		argvec.push_back("-r");
		argvec.push_back(p);
	}

	std::vector<char *> argv;
	argv.reserve(argvec.size());
	for (auto &s:argvec)
		argv.push_back(s.data());
	gi.argc=argv.size();
	gi.argv=argv.data();
	i=libmail_gpg_signencode(signkey ? 1:0,
				 n > 0 ? LIBMAIL_GPG_ENCAPSULATE:0,
				 &gi);
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
	for (char c:gpgerrbuf)
	{
		switch (c) {
		case '<':
			printf("&lt;");
			break;
		case '>':
			printf("&gt;");
			break;
		default:
			putchar((int)(unsigned char)c);
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

	char notty[]="--no-tty";
	argvec[0] = notty;
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
