/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	"sqwebmail.h"
#include	"cgi/cgi.h"
#include	"sqconfig.h"
#include	"maildir.h"
#include	"folder.h"
#include	"pref.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"token.h"
#include	"newmsg.h"
#include	"gpg.h"
#include	"gpglib/gpglib.h"
#include	"courierauth.h"
#include	<stdio.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<signal.h>
#include	<errno.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include        <unistd.h>
#endif
#include	<sys/types.h>
#include	<sys/stat.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#include	"maildir/maildirmisc.h"

#include	"htmllibdir.h"
#include	<courier-unicode.h>

extern "C" {
#if 0
}
#endif

extern char *alloc_filename(const char *, const char *, const char *);
extern int newdraftfd;
extern void output_scriptptrget();

extern int ishttps();

extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_content_language;

static void attachment_showname(const char *);

#if 0
{
#endif
}

extern void output_attrencoded(std::string_view);
extern void output_urlencoded(std::string_view);
extern void newmsg_hiddenheader(const char *, const char *);
extern char *newmsg_alladdrs(FILE *);
extern const char *showsize(unsigned long);
extern void newmsg_copy_content_headers(FILE *fp);
extern void newmsg_create_multipart(int, const char *, const char *);
extern void newmsg_copy_nonmime_headers(FILE *);
extern char *multipart_boundary_create();
extern int multipart_boundary_checkf(const char *, FILE *);
extern void sendmsg_done();

#define HASTEXTPLAIN(q) (rfc2045_searchcontenttype((q), "text/plain") != NULL)
/* Also in newmsg_create.c */


static off_t max_attach()
{
	off_t n=0;
	const char *p=getenv("SQWEBMAIL_MAXMSGSIZE");

	if (p)
		n=atol(p);

	if (n < MAXMSGSIZE)
		n=MAXMSGSIZE;
	return n;
}

extern "C"
void attachments_head(const char *folder, const char *pos, const char *draft)
{
char *filename;
int	cnt=0;
bool	foundtextplain=false;
const char	*noattach_lab=getarg("NOATTACH");
const char	*quotaerr=getarg("QUOTAERR");
const char	*limiterr=getarg("LIMITERR");

	CHECKFILENAME(draft);
	filename=maildir_find(INBOX "." DRAFTS, draft);
	if (!filename)	return;

	rfc822::fdstreambuf fd2{maildir_safeopen(filename, O_RDONLY, 0)};
	free(filename);

	if (fd2.error())
	{
		return;
	}

	rfc2045::entity message;

	{
		std::istreambuf_iterator<char> b{&fd2}, e;
		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	if (strcmp(cgi("error"), "quota") == 0)
	{
		printf("%s", quotaerr);
	}

	if (strcmp(cgi("error"), "limits") == 0)
	{
		printf(limiterr, (unsigned long)(max_attach() / (1024 * 1024)));
	}

	if (strcmp(cgi("error"), "makemime") == 0)
	{
		printf(getarg("MAKEMIMEERR"), MAKEMIME);
	}
	newmsg_hiddenheader("pos", pos);
	newmsg_hiddenheader("draft", draft);
	tokennew();
	printf("<table width=\"100%%\" border=\"0\">");

	for (auto &q:message.subentities)
	{
		if (message.content_type.value == "multipart/alternative")
		{
			/* No attachments here */
			break;
		}

		if (!foundtextplain && q.find_content_type("text/plain"))
		{
			foundtextplain=true;
			continue;
		}

		++cnt;
		printf("<tr><td align=\"left\"><input type=\"checkbox\" name=\"del%d\" id=\"del%d\" />&nbsp;",
		       cnt, cnt);

		std::string content_name;

		auto name_iter=q.content_type.parameters.find("name");

		if (name_iter != q.content_type.parameters.end())
		{
			content_name=name_iter->second.value_in_charset(
				sqwebmail_content_charset
			);
		}
		else
		{
			rfc2045::entity::rfc2231_header content_disposition{
				q.content_disposition
			};

			auto filename_iter=content_disposition.parameters.find(
				"filename"
			);

			if (filename_iter !=
			    content_disposition.parameters.end())
			{
				content_name=
					filename_iter->second.value_in_charset(
						sqwebmail_content_charset
					);
			}
		}

		{
			std::string s;

			rfc822::display_header(
				"subject",
				content_name,
				sqwebmail_content_charset,
				std::back_inserter(s)
			);
			content_name=std::move(s);
		}

		if (content_name.empty() &&
		    q.content_type.value == "application/pgp-keys")
		{
			content_name=getarg("KEYDESCR");
		}

		attachment_showname(content_name.c_str());
		printf("</td><td align=\"left\">&nbsp;&nbsp;<label for=\"del%d\">", cnt);
		output_attrencoded( q.content_type.value );
		printf("</label></td><td align=\"right\">%s<br /></td></tr>",
			showsize(q.endbody - q.startbody));
	}

	if (cnt == 0)
		printf("<tr><td align=\"center\">%s<br /></td></tr>\n",
			noattach_lab);
	printf("</table>\n");
}

void attachments_opts(const char *draft)
{
	char *filename;
	FILE *fp;

	CHECKFILENAME(draft);

	filename=maildir_find(INBOX "." DRAFTS, draft);
	if (!filename)
		return;
	fp=fopen(filename, "r");
	free(filename);
	if (!fp)
		return;

	printf("<label><input type=\"checkbox\" name=\"fcc\"%s />%s</label><br />",
	       pref_noarchive ? "":" checked=\"checked\"",
	       getarg("PRESERVELAB"));
	if (auth_getoptionenvint("wbnodsn") == 0)
		printf("<label><input type=\"checkbox\" name=\"dsn\" />%s</label><br />",
		       getarg("DSN"));

	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		char *all_addr;

		printf("<label><input type=\"checkbox\" "
		       "name=\"sign\" />%s</label><select name=\"signkey\">",
		       getarg("SIGNLAB"));
		gpgselectkey();
		printf("</select><br />\n");

		all_addr=newmsg_alladdrs(fp);

		printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">"
		       "<tr valign=\"middle\"><td><input type=\"checkbox\""
		       " name=\"encrypt\" id=\"encrypt\" /></td><td><label for=\"encrypt\">%s</label></td>"
		       "<td><select size=\"4\" multiple=\"multiple\" name=\"encryptkey\">",
		       getarg("ENCRYPTLAB"));
		gpgencryptkeys(all_addr);
		printf("</select></td></tr>\n");

		if (ishttps())
			printf("<tr valign=\"middle\"><td>&nbsp;</td><td>%s</td><td><input type=\"password\" name=\"passphrase\" /></td></tr>\n",
			       getarg("PASSPHRASE"));

		printf("</table><br />\n");
		if (all_addr)
			free(all_addr);
	}
	fclose(fp);
}

static void attachment_showname(const char *name)
{
	if (!name || !*name)	name="[attachment]";	/* Eh??? */
	output_attrencoded(name);
}

static void attachment_open(const char *draft,
	FILE **fp,
	int	*fd2,
	struct rfc2045 **rfcp)
{
char	*oldname=maildir_find(INBOX "." DRAFTS, draft);

	if (!oldname)	enomem();

	*fd2=maildir_safeopen(oldname, O_RDONLY, 0);

	*fp=0;
	if (*fd2 >= 0)
	{
		*fp=fdopen(*fd2, "r");
		if (*fp == NULL)
			close(*fd2);
	}

	if (*fp == NULL)	enomem();
	*rfcp=rfc2045_fromfp( *fp );
	if (!*rfcp)	enomem();
}

static int messagecopy(FILE *fp, off_t start, off_t end)
{
char	buf[512];
int	n;

	if (fseek(fp, start, SEEK_SET) == -1)	return (-1);
	while (start < end)
	{
		n=sizeof(buf);
		if (n > end - start)
			n=end - start;
		n=fread(buf, 1, n, fp);
		if (n <= 0)	enomem();
		maildir_writemsg(newdraftfd, buf, n);
		start += n;
	}
	return (0);
}

/* Return non-zero if user selected all attachments for deletion */

static int deleting_all_attachments(struct rfc2045 *p)
{
struct	rfc2045 *q;
const char *content_type;
const char *content_transfer_encoding;
const char *charset;
int	foundtextplain, cnt;
char	buf[MAXLONGSIZE+4];

	foundtextplain=0;
	cnt=0;
	for (q=p->firstpart; q; q=q->next)
	{
		rfc2045_mimeinfo(q, &content_type,
			&content_transfer_encoding, &charset);
		if (q->isdummy)	continue;

		if (!foundtextplain && HASTEXTPLAIN(q))
		{
			foundtextplain=1;
			continue;
		}

		sprintf(buf, "del%d", ++cnt);
		if (*cgi(buf) == '\0')	return (0);
	}
	return (1);
}

static int del_final_attachment(FILE *fp, struct rfc2045 *rfcp)
{
struct	rfc2045 *q;
const char *content_type;
const char *content_transfer_encoding;
const char *charset;
off_t start_pos, end_pos, start_body;
off_t dummy;

	for (q=rfcp->firstpart; q; q=q->next)
	{
		if (q->isdummy)	continue;
		rfc2045_mimeinfo(q, &content_type,
			&content_transfer_encoding, &charset);
		if (HASTEXTPLAIN(q))
			break;
	}
	if (!q)	return (-1);

	if (fseek(fp, 0L, SEEK_SET) == -1)	return (-1);
	newmsg_copy_nonmime_headers(fp);
	maildir_writemsgstr(newdraftfd, "mime-version: 1.0\n");

	rfc2045_mimepos(q, &start_pos, &end_pos, &start_body, &dummy, &dummy);
	return (messagecopy(fp, start_pos, end_pos));
}

static int del_some_attachments(FILE *fp, struct rfc2045 *rfcp)
{
struct	rfc2045 *q;
const char *content_type;
const char *content_transfer_encoding;
const char *charset;
int	foundtextplain;
int	cnt;
const char *boundary=rfc2045_boundary(rfcp);
off_t	start_pos, end_pos, start_body;
off_t	dummy;

	rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body, &dummy,
		&dummy);
	if (messagecopy(fp, 0, start_body))	return (-1);

	foundtextplain=0;
	cnt=0;
	for (q=rfcp->firstpart; q; q=q->next)
	{
		rfc2045_mimeinfo(q, &content_type,
			&content_transfer_encoding, &charset);
		if (q->isdummy)
			;
		else if (!foundtextplain && HASTEXTPLAIN(q))
			foundtextplain=1;
		else
		{
		char	buf[MAXLONGSIZE+4];

			sprintf(buf, "del%d", ++cnt);
			if (*cgi(buf))	continue;	/* This one's gone */
		}

		if (!q->isdummy)
		{
			maildir_writemsgstr(newdraftfd, "\n--");
			maildir_writemsgstr(newdraftfd, boundary);
			maildir_writemsgstr(newdraftfd, "\n");
		}
		rfc2045_mimepos(q, &start_pos, &end_pos, &start_body, &dummy,
			&dummy);
		if (messagecopy(fp, start_pos, end_pos))
			return (-1);
	}
	maildir_writemsgstr(newdraftfd, "\n--");
	maildir_writemsgstr(newdraftfd, boundary);
	maildir_writemsgstr(newdraftfd, "--\n");
	return (0);
}

extern "C" void attach_delete(const char *draft)
{
FILE	*fp;
int	fd2;
struct	rfc2045 *rfcp;
char	*draftfilename;
int	isok=1;
struct	stat	stat_buf;

	attachment_open(draft, &fp, &fd2, &rfcp);
	if (!rfcp->firstpart)
	{
		rfc2045_free(rfcp);
		fclose(fp);
		return;	/* No attachments to delete */
	}

	if (fstat(fileno(fp), &stat_buf))
	{
		fclose(fp);
		enomem();
	}

	newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, draft, &draftfilename);
	if (newdraftfd < 0)
	{
		fclose(fp);
		enomem();
	}

	if (deleting_all_attachments(rfcp))
	{
		/* Deleting all attachments */

		if (del_final_attachment(fp, rfcp))	isok=0;
	}
	else
	{
		if (del_some_attachments(fp, rfcp))	isok=0;
	}
	fclose(fp);
	rfc2045_free(rfcp);

	if ( maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, isok,
		stat_buf.st_size))
	{
		free(draftfilename);
		enomem();
	}
	free(draftfilename);
	maildir_remcache(INBOX "." DRAFTS);	/* Cache file invalid now */
}

/* ---------------------------------------------------------------------- */
/* Upload an attachment */

static int isbinary;
static int attachfd;
static const char *cgi_attachname, *cgi_attachfilename;

static int upload_start(const char *name, const char *filename, void *dummy)
{
const	char *p;

	p=strrchr(filename, '/');
	if (p)	filename=p+1;

	p=strrchr(filename, '\\');
	if (p)	filename=p+1;

	cgi_attachname=name;
	cgi_attachfilename=filename;
	isbinary=0;
	return (0);
}

static int upload_file(const char *ptr, size_t cnt, void *voidptr)
{
size_t	i;

	for (i=0; i<cnt; i++)
		if ( (ptr[i] < ' ' || ptr[i] >= 127) && ptr[i] != '\n' &&
			ptr[i] != '\r')
			isbinary=1;
	maildir_writemsg(attachfd, ptr, cnt);
	return (0);
}

static void upload_end(void *dummy)
{
}

#if 0
static void writebase64encode(const char *p, size_t n)
{
	maildir_writemsg(newdraftfd, p, n);
}
#endif

static const char *search_mime_type(const char *mimetype, const char *filename)
{
FILE	*fp;
char	*p, *q;

	if (!filename || !(filename=strrchr(filename, '.')))	return (0);
	++filename;

	if ((fp=fopen(mimetype, "r")) == NULL)	return(0);
	while ((p=maildir_readline(fp)) != NULL)
	{
		if ((q=strchr(p, '#')) != NULL)	*q='\0';
		if ((p=strtok(p, " \t")) == NULL)	continue;
		while ((q=strtok(NULL, " \t")) != NULL)
			if (strcasecmp(q, filename) == 0)
			{
				fclose(fp);
				return (p);
			}
	}
	fclose(fp);
	return (NULL);
}

const char *calc_mime_type(const char *filename)
{
static const char mimetypes[]=MIMETYPES;
const char	*p;
char *q;
const char *r;
char *s;

	p=mimetypes;
	if (!p)	enomem();
	while (*p)
	{
		if (*p == ':')
		{
			++p;
			continue;
		}
		q=strdup(p);
		if (!q)	enomem();
		if ((s=strchr(q, ':')) != NULL)	*s='\0';
		if ((r=search_mime_type(q, filename)) != 0)
		{
			free(q);
			return (r);
		}
		free(q);
		while (*p && *p != ':')
			p++;
	}
	return ("auto");
}

static int getkey(const char *keyname, int issecret)
{
	int rc;

	if (!*keyname)
		return (1);
	upload_start("", "", NULL);

	rc=gpgexportkey(keyname, issecret, &upload_file, NULL);
	upload_end(NULL);
	return (rc);
}

#if 0
static void write_disposition_param(const char *label, const char *value)
{
char	*p, *q;
const char *r;

        while (value && ((r=strchr(value, ':')) || (r=strchr(value, '/'))
                || (r=strchr(value, '\\'))))
                value=r+1;

	if (!value || !*value)	return;
	maildir_writemsgstr(newdraftfd, "; ");
	maildir_writemsgstr(newdraftfd, label);
	maildir_writemsgstr(newdraftfd, "=\"");
	p=strdup(value);
	if (!p)	enomem();
	while ((q=strchr(p, '\\')) || (q=strchr(p, '"')))
		*q='_';
	maildir_writemsgstr(newdraftfd, p);
	maildir_writemsgstr(newdraftfd, "\"");
	free(p);
}
#endif

static int cnt_filename(const char *param,
			const char *value,
			void *void_arg)
{
	*(int *)void_arg += strlen(param)+strlen(value)+5;
	return 0;
}

static int save_filename(const char *param,
			 const char *value,
			 void *void_arg)
{
	strcat(strcat(strcat(strcat((char *)void_arg, ";\n  "), param),
		      "="), value);
	return 0;
}

extern "C" int attach_upload(const char *draft,
			     const char *attpubkey,
			     const char *attprivkey)
{
	char	*attachfilename;
	char	*draftfilename;
	FILE	*draftfp;
	char	*boundary;
	FILE	*tempfp;
	struct	rfc2045 *rfcp, *q;
	const char *content_type;
	const char *content_transfer_encoding;
	const char *charset;
	off_t	start_pos, end_pos, start_body;
	int	n;
	char	buf[BUFSIZ];
	int	pipefd[2];
	struct	stat	stat_buf, attach_stat_buf;
	off_t	dummy;
	int	fd2;
	char	*filenamemime;
	char *argvec[20];
	char	*filenamebuf;
	pid_t pid1, pid2;
	int waitstat;

	/* Open the file containing the draft message */

	draftfilename=maildir_find(INBOX "." DRAFTS, draft);
	if (!draftfilename)	return (0);

	fd2=maildir_safeopen(draftfilename, O_RDONLY, 0);

	draftfp=0;
	if (fd2 >= 0)
	{
		draftfp=fdopen(fd2, "r");
		if (draftfp == NULL)
			close(fd2);
	}

	if (draftfp == 0)
		enomem();

	free(draftfilename);
	if (fstat(fileno(draftfp), &stat_buf))
	{
		fclose(draftfp);
		enomem();
	}

	/* Create a temporary file in tmp where we'll temporarily store the
	** attachment
	*/

	attachfd=maildir_createmsg(INBOX "." DRAFTS, "temp", &attachfilename);
	if (attachfd < 0)
	{
		fclose(draftfp);
		enomem();
	}

	if ((
	     attpubkey ? getkey(attpubkey, 0):
	     attprivkey ? getkey(attprivkey, 1):
	     cgi_getfiles( &upload_start, &upload_file, &upload_end, 1, NULL))
		|| maildir_writemsg_flush(attachfd))
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		free(attachfilename);
		fclose(draftfp);
		close(attachfd);
		return (0);
	}

	if (fstat(attachfd, &attach_stat_buf) ||
	    attach_stat_buf.st_size + stat_buf.st_size > max_attach())
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		maildir_deletenewmsg(attachfd, INBOX "." DRAFTS, attachfilename);
		free(attachfilename);
		fclose(draftfp);
		close(attachfd);
		return (-2);
	}


	/* Calculate new MIME content boundary */

	boundary=0;
	tempfp=0;

	n=dup(attachfd);

	if (n < 0)
	{
		fclose(draftfp);
		enomem();
	}
	tempfp=fdopen(n, "r");
	if (tempfp == 0)
	{
		fclose(draftfp);
		enomem();
	}

	do
	{
		if (boundary)	free(boundary);
		boundary=multipart_boundary_create();
	} while ( multipart_boundary_checkf(boundary, draftfp) ||
		  multipart_boundary_checkf(boundary, tempfp));

	if (tempfp)	fclose(tempfp);

	/* Parse existing draft for its MIME structure */

	rfcp=rfc2045_fromfp(draftfp);

	rfc2045_mimeinfo(rfcp, &content_type,
		&content_transfer_encoding, &charset);

	/* Create a new version of the draft message */

	newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, draft, &draftfilename);
	if (newdraftfd < 0)
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		fclose(draftfp);
		close(attachfd);
		enomem();
	}

	if (fseek(draftfp, 0L, SEEK_SET) < 0)
	{
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		fclose(draftfp);
		close(attachfd);
		enomem();
	}

	newmsg_copy_nonmime_headers(draftfp);

	/* Create a multipart message, 1st attachment is the existing
	** contents.
	*/

	newmsg_create_multipart(newdraftfd, charset, boundary);
	maildir_writemsgstr(newdraftfd, "--");
	maildir_writemsgstr(newdraftfd, boundary);
	maildir_writemsgstr(newdraftfd, "\n");

	if (rfcp == NULL || strcmp(content_type, "multipart/mixed"))
	{
		int rc;

		/*
		** The current draft does not have attachments.  Take its
		** sole contents, and write it as a text/plain attachment.
		*/

		if (fseek(draftfp, 0L, SEEK_SET) < 0)
			rc = -1;
		else
		{
			newmsg_copy_content_headers(draftfp);
			maildir_writemsgstr(newdraftfd, "\n");
			rfc2045_mimepos(rfcp, &start_pos, &end_pos,
					&start_body,
					&dummy, &dummy);
			rc=messagecopy(draftfp, start_body, end_pos);
		}

		if (rc)
		{
			maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename,
				0, 0);
			maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename,
				0, 0);
			fclose(draftfp);
			close(newdraftfd);
			close(attachfd);
			enomem();
		}

		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, boundary);
		maildir_writemsgstr(newdraftfd, "\n");
	}
	else
	{
		/* If the current draft already has MIME attachments,
		** just copy them over to the new draft message.
		*/

		for (q=rfcp->firstpart; q; q=q->next)
		{
			if (q->isdummy)	continue;
			rfc2045_mimepos(q, &start_pos, &end_pos, &start_body,
					&dummy, &dummy);
			if (messagecopy(draftfp, start_pos, end_pos))
			{
				maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
					draftfilename, 0, 0);
				maildir_closemsg(attachfd, INBOX "." DRAFTS,
					attachfilename, 0, 0);
				fclose(draftfp);
				close(newdraftfd);
				close(attachfd);
				enomem();
			}
			maildir_writemsgstr(newdraftfd, "\n--");
			maildir_writemsgstr(newdraftfd, boundary);
			maildir_writemsgstr(newdraftfd, "\n");
		}
	}

	{
		const char *cp=strrchr(cgi_attachfilename, '/');
		int len;
		static const char fnStr[]="filename";

		if (cp)
			++cp;
		else
			cp=cgi_attachfilename;

		len=1;
		rfc2231_attrCreate(fnStr, cp,
				   sqwebmail_content_charset,
				   sqwebmail_content_language,
				   &cnt_filename, &len);

		filenamemime=static_cast<char *>(malloc(len));

		if (filenamemime)
		{
			*filenamemime=0;
			rfc2231_attrCreate(fnStr, cp,
					   sqwebmail_content_charset,
					   sqwebmail_content_language,
					   save_filename, filenamemime);
		}
	}

	static char makemime_str[]="makemime";
	static char copt_str[]="-c";
	argvec[0]=makemime_str;
	argvec[1]=copt_str;

	if (attpubkey || attprivkey)
	{
		static char mimetype_str[]="application/pgp-keys";
		argvec[2]=mimetype_str;

		static char nopt_str[]="-N";
		argvec[3]=nopt_str;

		static char pgpkeys_txt[]="pgpkeys.txt";
		argvec[4]=pgpkeys_txt;

		static char aopt_str[]="-a";
		argvec[5]=aopt_str;

		static char disposition_str[]="Content-Disposition: attachment; filename=\"pgpkeys.txt\"";
		argvec[6]=disposition_str;
		n=7;
		filenamebuf=0;
	}
	else
	{
		const char *pp;

		argvec[2]=(char *)calc_mime_type(cgi_attachfilename);

		static char Nopt_str[]="-N";
		argvec[3]=Nopt_str;

		static char filename_dat[]="filename.dat";
		argvec[4]=cgi_attachfilename ?
			(char *)cgi_attachfilename:filename_dat;
		n=5;

		pp=*cgi("attach_inline") ?
			"Content-Disposition: inline":
			"Content-Disposition: attachment";

		filenamebuf=static_cast<char *>(
			malloc(strlen(pp)+strlen(filenamemime ?
						 filenamemime:"") + 15)
		);

		if (filenamebuf)
		{
			strcpy(filenamebuf, pp);
			strcat(filenamebuf, filenamemime ? filenamemime:"");

			static char aopt_str[]="-a";
			argvec[n++]=aopt_str;
			argvec[n++]=filenamebuf;
		}
	}

	static char Copt[]="-C";
	argvec[n++]=Copt;
	argvec[n++]=(char *)sqwebmail_content_charset;

	signal(SIGCHLD, SIG_DFL);

	static char noopt[]="-";
	argvec[n++]=noopt;
	argvec[n++]=0;

	if (pipe(pipefd) < 0)
	{
		if (filenamemime)
			free(filenamemime);
		if (filenamebuf)
			free(filenamebuf);
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		fclose(draftfp);
		close(newdraftfd);
		close(attachfd);
		enomem();
	}

	if (lseek(attachfd, 0L, SEEK_SET) < 0 || (pid1=fork()) < 0)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		if (filenamemime)
			free(filenamemime);
		if (filenamebuf)
			free(filenamebuf);
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		fclose(draftfp);
		close(newdraftfd);
		close(attachfd);
		enomem();
		return (0);
	}

	if (pid1 == 0)
	{
		setenv("CHARSET", sqwebmail_content_charset, 1);
		dup2(attachfd, 0);
		dup2(pipefd[1], 1);
		close(attachfd);
		close(newdraftfd);
		close(pipefd[0]);
		close(pipefd[1]);
		execv(MAKEMIME, argvec);
		fprintf(stderr,
		       "CRIT: exec %s: %s\n", MAKEMIME, strerror(errno));
		exit(1);
	}

	if (filenamemime)
		free(filenamemime);
	if (filenamebuf)
		free(filenamebuf);

	close (pipefd[1]);


	while ((n=read(pipefd[0], buf, sizeof(buf))) > 0)
	{
		maildir_writemsg(newdraftfd, buf, n);
	}
	close(pipefd[0]);

	for (;;)
	{
		pid2=wait(&waitstat);

		if (pid2 == pid1)
		{
			waitstat= WIFEXITED(waitstat) ? WEXITSTATUS(waitstat)
				: 1;
			break;
		}

		if (pid2 == -1)
		{
			waitstat=1;
			break;
		}
	}

	if (waitstat > 0 || n < 0)
	{
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		fclose(draftfp);
		close(newdraftfd);
		maildir_deletenewmsg(attachfd, INBOX "." DRAFTS, attachfilename);
		close(attachfd);
		return (-3);
	}

	maildir_writemsgstr(newdraftfd, "\n--");
	maildir_writemsgstr(newdraftfd, boundary);
	maildir_writemsgstr(newdraftfd, "--\n");

	/* Finish new draft message, let it replace the current one */

	if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 1,
		stat_buf.st_size))
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS, attachfilename, 0, 0);
		free(draftfilename);
		maildir_deletenewmsg(attachfd, INBOX "." DRAFTS, attachfilename);
		free(attachfilename);
		rfc2045_free(rfcp);
		fclose(draftfp);
		close(attachfd);
		return (-1);
	}
	free(draftfilename);

	fclose(draftfp);

	/* Remove and delete temp attachment file */

	maildir_deletenewmsg(attachfd, INBOX "." DRAFTS, attachfilename);
	free(attachfilename);
	rfc2045_free(rfcp);
	return (0);
}

void doattach(const char *folder, const char *draft)
{
int	quotaflag=0;

	CHECKFILENAME(draft);
	if (*cgi("dodelete"))
	{
		if (!tokencheck())
		{
			attach_delete(draft);
			tokensave();
		}
	}
	else if (*cgi("upload"))
	{
		if (!tokencheck())
		{
			quotaflag=attach_upload(draft, NULL, NULL);
			tokensave();
		}
	}
	else if (*cgi("uppubkey") && libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		if (!tokencheck())
		{
			quotaflag=attach_upload(draft, cgi("pubkey"), NULL);
			tokensave();
		}
	}
	else if (*cgi("upprivkey") && *cgi("really") &&
		 libmail_gpg_has_gpg(GPGDIR) == 0)
	{
		if (!tokencheck())
		{
			quotaflag=attach_upload(draft, NULL, cgi("privkey"));
			tokensave();
		}
	}
	else if (*cgi("previewmsg"))
	{
		cgi_put("draft", draft);
		newmsg_do(folder);
		return;
	}
	else if (*cgi("sendmsg"))
	{
		cgi_put("draftmessage", draft);
		newmsg_do(folder);
		return;
	}
	else if (*cgi("savedraft"))
	{
		sendmsg_done();
		return;
	}

	if (quotaflag == -2)
        {
                http_redirect_argss(
                  "&form=attachments&pos=%s&draft=%s&error=limits",
                  cgi("pos"), draft);
        }
	else if (quotaflag == -3)
	{
                http_redirect_argss(
                  "&form=attachments&pos=%s&draft=%s&error=makemime",
                  cgi("pos"), draft);
	}
        else
        {
                http_redirect_argss(
                  (quotaflag ? "&form=attachments&pos=%s&draft=%s&error=quota":
                  "&form=attachments&pos=%s&draft=%s"), cgi("pos"),
                  draft);
        }
}
