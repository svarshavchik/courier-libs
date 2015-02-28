/*
** Copyright 2004-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<courierauth.h>
#include	"config.h"
#include	"sqwebmail.h"
#include	"maildir.h"
#include	"cgi/cgi.h"
#include	"pref.h"
#include	"sqconfig.h"
#include	"auth.h"
#include	"acl.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirrequota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirwatch.h"
#include	"htmllibdir.h"

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#if	HAVE_DIRENT_H
#include	<dirent.h>
#define	NAMLEN(dirent)	strlen(dirent->d_name)
#else
#define	dirent	direct
#define	NAMLEN(dirent)	((dirent)->d_namlen)
#if	HAVE_SYS_NDIR_H
#include	<sys/ndir.h>
#endif
#if	HAVE_SYS_DIR_H
#include	<sys/dir.h>
#endif
#if	HAVE_NDIR_H
#include	<ndir.h>
#endif
#endif

#include	<sys/types.h>
#include	<sys/stat.h>
#if	HAVE_UTIME_H
#include	<utime.h>
#endif

#include	<courier-unicode.h>

#include	"strftime.h"


/* ACL support stuff */

extern const char *sqwebmail_folder;
extern void output_urlencoded(const char *p);
extern void output_attrencoded(const char *p);
extern void output_scriptptrget();
extern void output_scriptptr();
extern void output_scriptptrpostinfo();

extern dev_t sqwebmail_homedir_dev;
extern ino_t sqwebmail_homedir_ino;

extern const char *sqwebmail_content_charset;
int verify_shared_index_file=0;

int maildir_info_suppress(const char *maildir)
{
	struct stat stat_buf;

	if (stat(maildir, &stat_buf) < 0 ||

	    (stat_buf.st_dev == sqwebmail_homedir_dev &&
	     stat_buf.st_ino == sqwebmail_homedir_ino))
		return 1;
	return 0;
}

const char *maildir_shared_index_file()
{
	static char *filenamep=NULL;

	if (filenamep == NULL)
	{
		const char *p=getenv("SQWEBMAIL_SHAREDINDEXFILE");

		if (!p || !*p)
			p=SHAREDINDEXFILE;

		if (p && *p)
		{
			const char *q=auth_getoptionenv("sharedgroup");

			if (!q) q="";

			filenamep=malloc(strlen(p)+strlen(q)+1);

			if (!filenamep)
				enomem();

			strcat(strcpy(filenamep, p), q);
		}
	}

	if (filenamep && verify_shared_index_file)
	{
		struct stat stat_buf;

		if (stat(filenamep, &stat_buf))
		{
			fprintf(stderr, "ERR: ");
			perror(filenamep);
		}
	}

	return filenamep;
}

int acl_read(maildir_aclt_list *l, const char *folder,
	     char **owner)
{
	struct maildir_info minfo;
	int rc;

	if (maildir_info_imap_find(&minfo, folder,
				   login_returnaddr())<0)
	{
		return -1;
	}

	rc=acl_read2(l, &minfo, owner);
	maildir_info_destroy(&minfo);
	return rc;
}

int acl_read2(maildir_aclt_list *l,
	      struct maildir_info *minfo,
	      char **owner)
{
	int rc;
	char *p;

	if (minfo->mailbox_type == MAILBOXTYPE_OLDSHARED)
	{
		/* Legacy shared., punt. */

		maildir_aclt_list_init(l);
		if (maildir_aclt_list_add(l, "anyone",
					  ACL_LOOKUP ACL_READ
					  ACL_SEEN ACL_WRITE
					  ACL_INSERT
					  ACL_DELETEMSGS ACL_EXPUNGE, NULL) < 0
		    || (*owner=strdup("vendor=courier.internal")) == NULL)
		{
			maildir_aclt_list_destroy(l);
			return -1;
		}
		return 0;
	}

	if (minfo->homedir == NULL || minfo->maildir == NULL)
		return -1;

	p=maildir_name2dir(".", minfo->maildir);

	if (!p)
		return -1;

	rc=maildir_acl_read(l, minfo->homedir,
			    strncmp(p, "./", 2) == 0 ? p+2:p);
	free(p);
	if (owner && rc == 0)
	{
		*owner=minfo->owner;
		minfo->owner=NULL;
	}
	return rc;
}

void acl_computeRightsOnFolder(const char *folder, char *rights)
{
	maildir_aclt_list l;
	char *owner;

	if (acl_read(&l, folder, &owner) < 0)
	{
		*rights=0;
		return;
	}
	acl_computeRights(&l, rights, owner);
	if (owner)
		free(owner);
	maildir_aclt_list_destroy(&l);
}

void acl_computeRights(maildir_aclt_list *l, char *rights,
		       const char *owner)
{
	char *p, *q;

	maildir_aclt a;

	if (maildir_acl_computerights(&a, l, login_returnaddr(), owner) < 0)
	{
		*rights=0;
		return;
	}

	for (p=q=rights; *p; p++)
	{
		if (strchr(maildir_aclt_ascstr(&a), *p))
			*q++ = *p;
	}
	*q=0;
	maildir_aclt_destroy(&a);
}

static void showrights(const char *buf)
{
	size_t i;
	char buf2[40];

	for (i=0; buf[i]; i++)
	{
		const char *p;

		if (i)
			printf(", ");

		sprintf(buf2, "ACL_%c", buf[i]);

		p=getarg(buf2);
		if (p && *p)
			printf("%s", p);
		else
		{
			buf2[0]=buf[i];
			buf2[1]=0;

			printf(getarg("ACL_unknown"), buf2);
		}
	}
}

static void doupdate();

void listrights()
{
	maildir_aclt_list l;
	char buf[40];
	char *owner;

	if (*cgi("do.update") || *cgi("delentity"))
	{
		struct maildir_info minfo;

		if (maildir_info_imap_find(&minfo, sqwebmail_folder,
					   login_returnaddr()) == 0)
		{
			if (minfo.homedir)
			{
				struct maildirwatch *w;
				char *lock;
				int tryanyway;

				w=maildirwatch_alloc(minfo.homedir);

				if (!w)
				{
					maildir_info_destroy(&minfo);
					enomem();
					return;
				}

				lock=maildir_lock(minfo.homedir, w,
						  &tryanyway);

				maildir_info_destroy(&minfo);

				if (lock == NULL)
				{
					if (!tryanyway)
					{
						printf("%s",
						       getarg("ACL_noaccess"));
						return;
					}
				}
				doupdate();
				if (lock)
				{
					unlink(lock);
					free(lock);
				}
				maildirwatch_free(w);
			}
		}
	}

	if (acl_read(&l, sqwebmail_folder, &owner) < 0)
	{
		printf("%s", getarg("ACL_cantread"));
		return;
	}
	buf[0]=0;
	strncat(buf, getarg("ACL_all"), sizeof(buf)-2);
	acl_computeRights(&l, buf, owner);
	maildir_aclt_list_destroy(&l);
	if (owner)
		free(owner);

	if (!maildir_acl_canlistrights(buf))
	{
		printf("%s", getarg("ACL_cantread"));
		return;
	}

	showrights(buf);
}

static void doupdate()
{
	maildir_aclt_list l;
	char *owner;
	char buf[2];
	char *p;
	struct maildir_info minfo;

	if (maildir_info_imap_find(&minfo, sqwebmail_folder,
				   login_returnaddr()) < 0)
		return;

	if (acl_read2(&l, &minfo, &owner) < 0)
	{
		maildir_info_destroy(&minfo);
		return;
	}

	strcpy(buf, ACL_ADMINISTER);
	acl_computeRights(&l, buf, owner);
	if (!*buf)
	{
		if (owner)
			free(owner);
		maildir_aclt_list_destroy(&l);
		maildir_info_destroy(&minfo);
		return;
	}

	if (*cgi("delentity"))
	{
		if (maildir_aclt_list_del(&l, cgi("delentity")))
			printf("%s", getarg("ACL_failed"));
	}

	if (*cgi("do.update"))
	{
		char *entity=NULL;
		const char *p;
		char new_acl[40];

		p=cgi("entitytype");

		if (strcmp(p, "anonymous") == 0 ||
		    strcmp(p, "owner") == 0)
			entity=strdup(p);
		else if (strcmp(p, "user") == 0)
		{
			p=cgi("entity");

			if (*p)
			{
				entity=malloc(sizeof("user=")+strlen(p));
				if (entity)
					strcat(strcpy(entity, "user="), p);
			}
		}
		else if (strcmp(p, "group") == 0)
		{
			p=cgi("entity");

			if (*p)
			{
				entity=malloc(sizeof("group=")+strlen(p));
				if (entity)
					strcat(strcpy(entity, "group="), p);
			}
		}
		else
		{
			entity=strdup(cgi("entity"));
		}

		if (*cgi("negate") == '-' && entity)
		{
			char *p=malloc(strlen(entity)+2);

			if (p)
				strcat(strcpy(p, "-"), entity);
			free(entity);
			entity=p;
		}

		if (entity)
		{
			char *val=
				unicode_convert_toutf8(entity,
							 sqwebmail_content_charset,
							 NULL);


			if (val)
			{
				free(entity);
				entity=val;
			}
		}
		p=getarg("ACL_all");

		new_acl[0]=0;

		while (*p && strlen(new_acl) < sizeof(new_acl)-2)
		{
			char b[40];

			sprintf(b, "acl_%c", *p);

			if (*cgi(b))
			{
				b[0]=*p;
				b[1]=0;
				strcat(new_acl, b);
			}
			++p;
		}

		if (!entity || !*entity ||
		    maildir_aclt_list_add(&l, entity, new_acl, NULL) < 0)
			printf("%s", getarg("ACL_failed"));

		if (entity)
			free(entity);
	}

	p=maildir_name2dir(".", minfo.maildir);

	if (p)
	{
		const char *err_ident;

		if (maildir_acl_write(&l, minfo.homedir,
				      strncmp(p, "./", 2) == 0 ? p+2:p,
				      owner, &err_ident))
			printf("%s", getarg("ACL_failed"));
		free(p);
	}

	if (owner)
		free(owner);
	maildir_aclt_list_destroy(&l);
	maildir_info_destroy(&minfo);
}

static void p_ident_name(const char *identifier)
{
	char *val=unicode_convert_fromutf8(identifier,
					     sqwebmail_content_charset,
					     NULL);

	if (val)
	{
		output_attrencoded(val);
		free(val);
		return;
	}

	output_attrencoded(identifier);
}

static int getacl_cb(const char *identifier, const maildir_aclt *acl,
		     void *dummy)
{
	printf("<tr><td>");
	p_ident_name(identifier);
	printf("</td><td>");
	showrights(maildir_aclt_ascstr(acl));



	printf("<span class=\"folder-acl-list-action\">&nbsp;(<a href=\"");
	output_scriptptrget();
	printf("&amp;form=acl&amp;editentity=");
	output_urlencoded(identifier);
	printf("&amp;editaccess=");
	output_urlencoded(maildir_aclt_ascstr(acl));
	printf("\">%s</a>)&nbsp;(<a href=\"", getarg("EDIT"));
	output_scriptptrget();
	printf("&amp;form=acl&amp;delentity=");
	output_urlencoded(identifier);
	printf("\">%s</a>)</td></tr>\n", getarg("DELETE"));
	return 0;
}

void getacl()
{
	maildir_aclt_list l;
	char buf[2];
	char *owner;
	const char *a;
	const char *editentity=cgi("editentity");
	const char *editaccess=cgi("editaccess");

	const char *entitytype="";
	const char *entityval="";
	int negate=0;

	if (acl_read(&l, sqwebmail_folder, &owner) < 0)
	{
		printf("%s", getarg("ACL_noaccess"));
		return;
	}
	strcpy(buf, ACL_ADMINISTER);
	acl_computeRights(&l, buf, owner);
	if (owner)
		free(owner);

	if (buf[0] == 0)
	{
		maildir_aclt_list_destroy(&l);
		return;
	}

	printf("<form method=\"post\" name=\"form1\" action=\"");
	output_scriptptr();
	printf("\">");
	output_scriptptrpostinfo();
	printf("<input type=\"hidden\" name=\"update\" value=\"1\" />\n"
	       "<input type=\"hidden\" name=\"form\" value=\"acl\" />\n");
	printf("<table class=\"folder-acl-list\"><tbody>"
	       "<tr><th align=\"left\">%s</th><th align=\"left\">%s</th></tr>\n",
	       getarg("ENTITY"),
	       getarg("ACCESSRIGHTS"));

	maildir_aclt_list_enum(&l, getacl_cb, NULL);

	if (*editentity == '-')
	{
		++editentity;
		negate=1;
	}

	if (*editentity)
	{
		if (strncmp(editentity, "user=", 5) == 0)
		{
			entitytype="user";
			entityval=editentity+5;
		}
		else if (strncmp(editentity, "group=", 6) == 0)
		{
			entitytype="group";
			entityval=editentity+6;
		}
		else if (strcmp(editentity, "owner") == 0 ||
			 strcmp(editentity, "anonymous") == 0)
		{
			entitytype=editentity;
		}
		else
		{
			entitytype="other";
			entityval=editentity;
		}
	}

	printf("<tr><td colspan=\"2\"><hr width=\"90%%\" />");
	printf("<table><tbody>\n");
	printf("<tr><th colspan=\"2\" align=\"left\">%s</th></tr>\n",
	       getarg("UPDATEHDR"));
	printf("<tr align=\"top\"><td>"
	       "<select name=\"negate\" id=\"negate\">\n"
	       "<option value=\"\" > </option>\n"
	       "<option value=\"-\" %s>-</option>\n"
	       "</select>\n"
	       "<select name=\"entitytype\" id=\"entitytype\" "
	       "onchange=\"javascript:updent()\" >\n"
	       "<option value=\"user\" %s >%s</option>\n"
	       "<option value=\"group\" %s >%s</option>\n"
	       "<option value=\"owner\" %s >%s</option>\n"
	       "<option value=\"anonymous\" %s >%s</option>\n"
	       "<option value=\"administrators\" %s >%s</option>\n"
	       "<option value=\"other\" %s >%s</option>\n"
	       "</select><input type=\"text\" name=\"entity\" "
	       " id=\"entity\" value=\"",
	       negate ? "selected=\"selected\"":"",
	       strcmp(entitytype, "user") == 0 ? "selected=\"selected\"":"",
	       getarg("USER"),

	       strcmp(entitytype, "group") == 0 ? "selected=\"selected\"":"",
	       getarg("GROUP"),

	       strcmp(entitytype, "owner") == 0 ? "selected=\"selected\"":"",
	       getarg("OWNER"),

	       strcmp(entitytype, "anonymous") == 0 ? "selected=\"selected\"":"",
	       getarg("ANONYMOUS"),

	       strcmp(entitytype, "administrators") == 0 ? "selected=\"selected\"":"",
	       getarg("ADMINISTRATORS"),

	       strcmp(entitytype, "other") == 0 ? "selected=\"selected\"":"",
	       getarg("OTHER"));

	p_ident_name(entityval);

	printf("\"/></td><td><table><tbody>");

	a=getarg("ACL_all");

	while (*a)
	{
		char buf2[40];

		sprintf(buf2, "ACL_%c", *a);

		printf("<tr><td><input type=\"checkbox\" name=\"acl_%c\" "
		       "id=\"acl_%c\" %s />"
		       "</td><td>%s</td></tr>\n",
		       *a, *a,
		       strchr(editaccess, *a) ? "checked=\"checked\"":"",
		       getarg(buf2));
		++a;
	}

	printf("</tbody></table></td></tr>\n"
	       "<tr><td>&nbsp;</td>"
	       "<td><input type=\"submit\" name=\"do.update\" value=\"%s\" />"
	       "</td>"
	       "</table></tbody></td></tr>\n",
	       getarg("UPDATE"));

	printf("</tbody></table></form>\n");
}

