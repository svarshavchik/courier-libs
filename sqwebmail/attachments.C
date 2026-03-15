/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
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
extern std::string newmsg_alladdrs(rfc822::fdstreambuf &sb);
extern const char *showsize(unsigned long);
extern void newmsg_copy_content_headers(const rfc2045::entity &message,
					rfc822::fdstreambuf &fd);
extern void newmsg_create_multipart(int, const char *, const char *);
extern void newmsg_copy_nonmime_headers(const rfc2045::entity &message,
					rfc822::fdstreambuf &fd);
extern char *multipart_boundary_create();
extern bool multipart_boundary_checkf(std::string, std::streambuf &);
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

static std::tuple<std::string, rfc822::fdstreambuf, rfc2045::entity
		  > open_draft_message(
			  const char *draft
		  )
{
	CHECKFILENAME(draft);
	auto oldname=maildir_find(INBOX "." DRAFTS, draft);

	int fd= -1;

	if (!oldname.empty())
		fd=maildir_safeopen(oldname.c_str(), O_RDONLY, 0);

	std::tuple<std::string, rfc822::fdstreambuf, rfc2045::entity> ret{
		std::move(oldname),
		fd,
		std::tuple<>{}
	};

	auto &[ignore, fp, entity] = ret;

	if (fp.error())
		return ret;

	std::istreambuf_iterator<char> b{&fp}, e;
	rfc2045::entity::line_iter<false>::iter parser{b, e};

	entity.parse(parser);

	return ret;
}

extern "C"
void attachments_head(const char *folder, const char *pos, const char *draft)
{
int	cnt=0;
bool	foundtextplain=false;
const char	*noattach_lab=getarg("NOATTACH");
const char	*quotaerr=getarg("QUOTAERR");
const char	*limiterr=getarg("LIMITERR");

	auto &&[name, fd2, message] = open_draft_message(draft);

	if (fd2.error())	return;

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
	CHECKFILENAME(draft);

	auto filename=maildir_find(INBOX "." DRAFTS, draft);
	if (filename.empty())
		return;

	rfc822::fdstreambuf fp{
		maildir_safeopen(filename.c_str(), O_RDONLY, 0)
	};
	if (fp.error())
		return;

	printf("<label><input type=\"checkbox\" name=\"fcc\"%s />%s</label><br />",
	       pref_noarchive ? "":" checked=\"checked\"",
	       getarg("PRESERVELAB"));
	if (auth_getoptionenvint("wbnodsn") == 0)
		printf("<label><input type=\"checkbox\" name=\"dsn\" />%s</label><br />",
		       getarg("DSN"));

	if (libmail_gpg_has_gpg(GPGDIR) == 0)
	{

		printf("<label><input type=\"checkbox\" "
		       "name=\"sign\" />%s</label><select name=\"signkey\">",
		       getarg("SIGNLAB"));
		gpgselectkey();
		printf("</select><br />\n");

		auto all_addr=newmsg_alladdrs(fp);

		printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">"
		       "<tr valign=\"middle\"><td><input type=\"checkbox\""
		       " name=\"encrypt\" id=\"encrypt\" /></td><td><label for=\"encrypt\">%s</label></td>"
		       "<td><select size=\"4\" multiple=\"multiple\" name=\"encryptkey\">",
		       getarg("ENCRYPTLAB"));
		gpgencryptkeys(all_addr.c_str());
		printf("</select></td></tr>\n");

		if (ishttps())
			printf("<tr valign=\"middle\"><td>&nbsp;</td><td>%s</td><td><input type=\"password\" name=\"passphrase\" /></td></tr>\n",
			       getarg("PASSPHRASE"));

		printf("</table><br />\n");
	}
}

static void attachment_showname(const char *name)
{
	if (!name || !*name)	name="[attachment]";	/* Eh??? */
	output_attrencoded(name);
}

static bool messagecopy(rfc822::fdstreambuf &fp, off_t start, off_t end)
{
	char	buf[BUFSIZ];

	fp.pubseekpos(start);

	if (fp.error())
		return false;

	while (start < end)
	{
		size_t n=sizeof(buf);

		if (n > static_cast<size_t>(end - start))
			n=end - start;

		n=fp.sgetn(buf, n);

		if (n <= 0)	enomem();
		maildir_writemsg(newdraftfd, buf, n);
		start += n;
	}
	return true;
}

/* Return non-zero if user selected all attachments for deletion */

static bool deleting_all_attachments(const rfc2045::entity &p)
{
	bool	foundtextplain=false;
	int	cnt=0;
	char	buf[MAXLONGSIZE+4];

	for (auto &q:p.subentities)
	{
		if (!foundtextplain && q.find_content_type("text/plain"))
		{
			foundtextplain=true;
			continue;
		}

		sprintf(buf, "del%d", ++cnt);
		if (*cgi(buf) == '\0')	return false;
	}
	return true;
}

static bool del_final_attachment(rfc822::fdstreambuf &fp,
				 const rfc2045::entity &msg)
{
	for (auto &subentity:msg.subentities)
	{
		if (subentity.find_content_type("text/plain"))
		{
			newmsg_copy_nonmime_headers(msg, fp);
			maildir_writemsgstr(newdraftfd, "mime-version: 1.0\n");
			return messagecopy(fp, subentity.startpos,
					   subentity.endbody);
		}
	}
	return false;
}

#include <fstream>

static bool del_some_attachments(rfc822::fdstreambuf &fp,
				 const rfc2045::entity &msg)
{
	std::string boundary;

	auto boundary_iter=msg.content_type.parameters.find("boundary");

	if (boundary_iter != msg.content_type.parameters.end())
		boundary=boundary_iter->second.value; // Better be here

	if (!messagecopy(fp, 0, msg.startbody)) return false;

	{
		std::istream i{&fp};
		std::string s;

		// Copy the MIME preamble

		while (std::getline(i, s))
		{
			if (std::string_view{s}.substr(0, 2) == "--")
				break;
			s += "\n";
			maildir_writemsg(newdraftfd, s.data(), s.size());
		}
	}

	const char *nl="";
	bool foundtextplain=false;
	int cnt=0;
	for (auto &q:msg.subentities)
	{
		if (!foundtextplain && q.find_content_type("text/plain"))
			foundtextplain=true;
		else
		{
		char	buf[MAXLONGSIZE+4];

			sprintf(buf, "del%d", ++cnt);
			if (*cgi(buf))	continue;	/* This one's gone */
		}

		maildir_writemsgstr(newdraftfd, nl);
		nl="\n"; // Initial one from the preamble was copied

		maildir_writemsgstr(newdraftfd, "--");
		maildir_writemsg(newdraftfd, boundary.data(), boundary.size());
		maildir_writemsgstr(newdraftfd, "\n");

		if (!messagecopy(fp, q.startpos, q.endbody))
			return false;
	}
	maildir_writemsgstr(newdraftfd, nl);
	maildir_writemsgstr(newdraftfd, "--");
	maildir_writemsg(newdraftfd, boundary.data(), boundary.size());
	maildir_writemsgstr(newdraftfd, "--\n");
	return (true);
}

extern "C" void attach_delete(const char *draft)
{
	auto &&[oldname, fp, message] = open_draft_message(draft);

	if (message.subentities.empty())
	{
		return;	/* No attachments to delete */
	}

	struct stat stat_buf;

	if (fstat(fp.fileno(), &stat_buf))
	{
		enomem();
	}

	std::string draftfilename;
	bool isok=true;

	newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, draft,
					    draftfilename);
	if (newdraftfd < 0)
	{
		enomem();
	}

	if (deleting_all_attachments(message))
	{
		/* Deleting all attachments */

		if (!del_final_attachment(fp, message))	isok=false;
	}
	else
	{
		if (!del_some_attachments(fp, message))	isok=false;
	}

	if ( maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
			      draftfilename.c_str(),
			      isok ? 1:0,
		stat_buf.st_size))
	{
		enomem();
	}
	maildir_remcache(INBOX "." DRAFTS);	/* Cache file invalid now */
}

/* ---------------------------------------------------------------------- */
/* Upload an attachment */

static int isbinary;
static rfc822::fdstreambuf attachfd;
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
	maildir_writemsg(attachfd.fileno(), ptr, cnt);
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

static std::string search_mime_type(const char *mimetype, const char *filename)
{
	std::ifstream fp{mimetype};

	if (!filename || !(filename=strrchr(filename, '.')))	return {};
	++filename;

	std::string extension{filename};

	rfc2045::entity::tolowercase(extension);
	if (!fp)	return {};

	std::string p;

	while (std::getline(fp, p))
	{
		rfc2045::entity::tolowercase(p);
		size_t q=p.find('#');
		if (q != std::string::npos)	p.erase(q);

		auto n=p.find_first_of(" \t");
		if (n == std::string::npos)	continue;

		auto mime_type=std::string_view{p}.substr(0, n);
		auto s=std::string_view{p}.substr(n);

		while (!s.empty() &&
			!(s=s.substr(s.find_first_not_of(" \t"))).empty())
		{
			auto n=s.find_first_of(" \t");
			if (n == std::string_view::npos)
				n=s.size();

			if (s.substr(0, n) == extension)
				return std::string{
					mime_type.begin(),
					mime_type.end()
				};

			s=s.substr(n);
		}
	}
	return {};
}

std::string calc_mime_type(const char *filename)
{
	static const char mimetypes[]=MIMETYPES;

	const char *p=mimetypes;
	while (*p)
	{
		if (*p == ':')
		{
			++p;
			continue;
		}
		const char *q=strchr(p, ':');

		std::string mimetype;

		if (q)
		{
			mimetype=std::string{p, q};
			p=q+1;
		}
		else {
			mimetype=p;
			p="";
		}
		auto r=search_mime_type(mimetype.c_str(), filename);

		if (!r.empty())
		{
			return r;
		}
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

extern "C" int attach_upload(const char *draft,
			     const char *attpubkey,
			     const char *attprivkey)
{
	std::string attachfilename;
	int	n;
	char	buf[BUFSIZ];
	int	pipefd[2];
	struct	stat	stat_buf, attach_stat_buf;
	std::string filenamemime;
	char *argvec[20];
	std::string filenamebuf;
	pid_t pid1, pid2;
	int waitstat;

	/* Open the file containing the draft message */

	auto &&[draftfilename, draftfp, draftmessage] =
		open_draft_message(draft);

	if (draftfp.error())
		enomem();

	if (fstat(draftfp.fileno(), &stat_buf))
	{
		enomem();
	}

	/* Create a temporary file in tmp where we'll temporarily store the
	** attachment
	*/

	attachfd = rfc822::fdstreambuf{
		maildir_createmsg(INBOX "." DRAFTS, "temp",
				  attachfilename)
	};

	if (attachfd.error())
	{
		enomem();
	}

	if ((attpubkey ? getkey(attpubkey, 0):
	     attprivkey ? getkey(attprivkey, 1):
	     cgi_getfiles( &upload_start, &upload_file, &upload_end, 1, NULL))
	    || maildir_writemsg_flush(attachfd.fileno()))
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		return (0);
	}

	if (fstat(attachfd.fileno(), &attach_stat_buf) ||
	    attach_stat_buf.st_size + stat_buf.st_size > max_attach())
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		maildir_deletenewmsg(-1, INBOX "." DRAFTS,
				     attachfilename.c_str());
		attachfd={};
		return (-2);
	}


	/* Calculate new MIME content boundary */

	unsigned counter=0;
	std::string boundary;

	do
	{
		boundary=rfc2045::entity::new_boundary(counter);
		draftfp.pubseekpos(0);
		attachfd.pubseekpos(0);
	} while ( !multipart_boundary_checkf(boundary, draftfp) ||
		  !multipart_boundary_checkf(boundary, attachfd) );

	/* Create a new version of the draft message */

	newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, draft, draftfilename);
	if (newdraftfd < 0)
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		enomem();
	}

	draftfp.pubseekpos(0);
	if (draftfp.error())
	{
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
				 draftfilename.c_str(), 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		enomem();
	}

	newmsg_copy_nonmime_headers(draftmessage, draftfp);

	/* Create a multipart message, 1st attachment is the existing
	** contents.
	*/

	newmsg_create_multipart(newdraftfd,
				draftmessage.content_type.value.c_str(),
				boundary.c_str());
	maildir_writemsgstr(newdraftfd, "--");
	maildir_writemsgstr(newdraftfd, boundary.c_str());
	maildir_writemsgstr(newdraftfd, "\n");

	if (draftmessage.content_type.value != "multipart/mixed")
	{
		/*
		** The current draft does not have attachments.  Take its
		** sole contents, and write it as a text/plain attachment.
		*/

		newmsg_copy_content_headers(draftmessage, draftfp);
		maildir_writemsgstr(newdraftfd, "\n");
		if (!messagecopy(draftfp,
				 draftmessage.startbody,
				 draftmessage.endbody))
		{
			maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
					 draftfilename.c_str(),
					 0, 0);
			maildir_closemsg(attachfd, INBOX "." DRAFTS,
					 attachfilename.c_str(),
					 0, 0);
			close(newdraftfd);
			enomem();
		}

		maildir_writemsgstr(newdraftfd, "\n--");
		maildir_writemsgstr(newdraftfd, boundary.c_str());
		maildir_writemsgstr(newdraftfd, "\n");
	}
	else
	{
		/* If the current draft already has MIME attachments,
		** just copy them over to the new draft message.
		*/

		for (auto &subentity:draftmessage.subentities)
		{
			if (!messagecopy(draftfp, subentity.startpos,
					 subentity.endbody))
			{
				maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
						 draftfilename.c_str(), 0, 0);
				maildir_closemsg(attachfd, INBOX "." DRAFTS,
						 attachfilename.c_str(), 0, 0);
				close(newdraftfd);
				enomem();
			}
			maildir_writemsgstr(newdraftfd, "\n--");
			maildir_writemsgstr(newdraftfd, boundary.c_str());
			maildir_writemsgstr(newdraftfd, "\n");
		}
	}

	{
		const char *cp=strrchr(cgi_attachfilename, '/');

		if (cp)
			++cp;
		else
			cp=cgi_attachfilename;

		rfc2231_attr_encode(
			"filename",
			cp,
			sqwebmail_content_charset,
			sqwebmail_content_language,
			[&]
			(const char *param, const char *value)
			{
				filenamemime += ";\n  ";
				filenamemime += param;
				filenamemime += "=";
				filenamemime += value;
			}
		);
	}

	static char makemime_str[]="makemime";
	static char copt_str[]="-c";
	argvec[0]=makemime_str;
	argvec[1]=copt_str;

	std::string mimetype;
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
	}
	else
	{
		std::string_view pp;

		mimetype=calc_mime_type(cgi_attachfilename);
		argvec[2]=mimetype.data();

		static char Nopt_str[]="-N";
		argvec[3]=Nopt_str;

		static char filename_dat[]="filename.dat";
		argvec[4]=cgi_attachfilename ?
			(char *)cgi_attachfilename:filename_dat;
		n=5;

		pp=*cgi("attach_inline") ?
			"Content-Disposition: inline":
			"Content-Disposition: attachment";

		filenamebuf.reserve(pp.size()+filenamemime.size());

		filenamebuf =pp;
		filenamebuf += filenamemime;

		static char aopt_str[]="-a";
		argvec[n++]=aopt_str;
		argvec[n++]=filenamebuf.data();
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
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
				 draftfilename.c_str(), 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		close(newdraftfd);
		enomem();
	}

	attachfd.pubseekpos(0);
	if (attachfd.error() || (pid1=fork()) < 0)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
				 draftfilename.c_str(), 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		close(newdraftfd);
		enomem();
		return (0);
	}

	if (pid1 == 0)
	{
		setenv("CHARSET", sqwebmail_content_charset, 1);
		dup2(attachfd.fileno(), 0);
		dup2(pipefd[1], 1);
		attachfd={};
		close(newdraftfd);
		close(pipefd[0]);
		close(pipefd[1]);
		execv(MAKEMIME, argvec);
		fprintf(stderr,
		       "CRIT: exec %s: %s\n", MAKEMIME, strerror(errno));
		exit(1);
	}

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
		maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
				 draftfilename.c_str(), 0, 0);
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		close(newdraftfd);
		maildir_deletenewmsg(attachfd, INBOX "." DRAFTS,
				     attachfilename.c_str());
		return (-3);
	}

	maildir_writemsgstr(newdraftfd, "\n--");
	maildir_writemsgstr(newdraftfd, boundary.c_str());
	maildir_writemsgstr(newdraftfd, "--\n");

	/* Finish new draft message, let it replace the current one */

	if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS,
			     draftfilename.c_str(), 1,
			     stat_buf.st_size))
	{
		maildir_closemsg(attachfd, INBOX "." DRAFTS,
				 attachfilename.c_str(), 0, 0);
		maildir_deletenewmsg(attachfd, INBOX "." DRAFTS,
				     attachfilename.c_str());
		return (-1);
	}

	/* Remove and delete temp attachment file */

	maildir_deletenewmsg(attachfd, INBOX "." DRAFTS,
			     attachfilename.c_str());
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
