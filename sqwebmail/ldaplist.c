#include "config.h"
/*
** Copyright 2000-2006 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/
#include	"sqwebmail.h"
#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<ctype.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<string.h>
#include	"cgi/cgi.h"
#include	"ldapaddressbook/ldapaddressbook.h"
#include	"maildir/maildircreate.h"
#include	"numlib/numlib.h"
#include	"htmllibdir.h"
#include	"addressbook.h"
#include	"pref.h"
#include	"rfc2045/base64.h"

#define	LOCALABOOK	"sqwebmail-ldapaddressbook"

extern void output_scriptptrget();
extern void output_attrencoded(const char *);
extern void output_attrencoded_oknl(const char *p);
extern void output_urlencoded(const char *);
extern void output_attrencoded_fp(const char *, FILE *);
extern void output_attrencoded_oknl_fp(const char *, FILE *);

extern const char *sqwebmail_content_charset;

#include	<courier-unicode.h>

void	ldaplist()
{
struct ldapabook *abooks[2];
int	i;
struct ldapabook *p;
const char	*delabook=getarg("DELABOOK");
const char	*sysbook=getarg("SYSBOOK");

	if (!delabook)	delabook="";
	if (!sysbook)	sysbook="";

	if (*cgi("addabook"))
	{
		struct ldapabook newbook;

		memset(&newbook, 0, sizeof(newbook));

		newbook.name=(char *)cgi("name");
		newbook.host=(char *)cgi("host");
		newbook.port=(char *)cgi("port");
		newbook.suffix=(char *)cgi("suffix");

		if (*newbook.name && *newbook.host &&
			ldapabook_add(LOCALABOOK, &newbook) < 0)
		{
			printf("<pre>\n");
			perror("ldapabook_add");
			printf("</pre>\n");
		}
	}

	if (*cgi("delabook"))
	{
		struct maildir_tmpcreate_info createInfo;
		int fd;
		maildir_tmpcreate_init(&createInfo);

		createInfo.uniq="abook";

		if ((fd=maildir_tmpcreate_fd(&createInfo)) >= 0)
		{
			close(fd);
			unlink(createInfo.tmpname);
			ldapabook_del(LOCALABOOK, createInfo.tmpname,
				      cgi("ABOOK"));
			maildir_tmpcreate_free(&createInfo);
		}
	}

	abooks[0]=ldapabook_read(LDAPADDRESSBOOK);
	abooks[1]=ldapabook_read(LOCALABOOK);

	printf("<table border=\"0\" cellpadding=\"8\" width=\"100%%\">\n");
	for (i=0; i<2; i++)
	{
		for (p=abooks[i]; p; p=p->next)
		{
			printf("<tr valign=\"top\"><td align=\"right\">");
			printf("<input type=\"radio\" name=\"ABOOK\"");

			if (pref_ldap && strcmp(pref_ldap, p->name) == 0)
				printf(" checked=\"checked\"");

			printf(" value=\"");
			output_attrencoded(p->name);
			printf("\" /></td><td><font size=\"+1\""
			       " class=\"ldaplist-name\">%s</font><br />"
			       "&nbsp;&nbsp;&nbsp;<span class=\"tt\"><font size=\"-2\""
			       " class=\"ldaplist-ldapurl\">ldap://", p->name);
			if (*p->binddn || *p->bindpw)
			{
				printf("%s", p->binddn);
				if (*p->bindpw)
					printf(":%s", p->bindpw);
				printf("@");
			}
			printf("%s", p->host);
			if (atoi(p->port) != 389)
				printf(":%s", p->port);
			if (*p->suffix)
			{
			char	*q;

				printf("/");
				q=cgiurlencode_noeq(p->suffix);
				if (q)
				{
					printf("%s", q);
					free(q);
				}
			}
			printf("</font></span>%s</td></tr>",
				i ? "":sysbook);
		}
	}

	if (abooks[1])
	{
		printf("<tr><td></td><td>");
		printf("<input type=\"submit\" name=\"delabook\" value=\"%s\" />",
				delabook);
		printf("</td></tr>\n");
	}
	printf("</table>\n");
	ldapabook_free(abooks[0]);
	ldapabook_free(abooks[1]);
}

static char *getfilter()
{
	const char *p;

	if (!*cgi("ldapsearch"))
		return NULL;

	p=cgi("value");

	if (!*p)
		return NULL;

	return unicode_convert_toutf8(p, sqwebmail_content_charset, NULL);
}

struct search_info {
	FILE *fpw;
	char errmsgbuf[1024];
	unsigned counter;
};

int	ldapsearch()
{
	char *p;

	if (*cgi("ABOOK") == 0) return (-1);

	if ((p=getfilter()) == NULL)
		return -1;
	free(p);
	return 0;
}

static void save_errmsg(const char *errmsg,
			void *voidarg)
{
	struct search_info *si=(struct search_info *)voidarg;

	si->errmsgbuf[0]=0;
	strncat(si->errmsgbuf, errmsg, sizeof(si->errmsgbuf)-1);
}


static int parsesearch(const char *cn, const char *mail,
		       void *voidarg)
{
	struct search_info *si=(struct search_info *)voidarg;

	char	numbuf[NUMBUFSIZE];
	char	numbuf2[NUMBUFSIZE+10];

	char *cn_native;

	cn_native=unicode_convert_fromutf8(cn, sqwebmail_content_charset,
					     NULL);

	if (cn_native)
		cn=cn_native;
 
	fprintf(si->fpw, "<tr valign=\"top\"><td><input type=\"checkbox\" "
				"name=\"%s\" value=\"&lt;",
		strcat(strcpy(numbuf2, "ADDY"),
		       libmail_str_size_t(si->counter++, numbuf)));

	output_attrencoded_fp(mail, si->fpw);
	fprintf(si->fpw, "&gt;");

	output_attrencoded_fp(cn, si->fpw);

	fprintf(si->fpw, "&quot;\" /></td><td><font size=\"+1\" class=\"ldapsearch-name\">\"");

	output_attrencoded_fp(cn, si->fpw);
	fprintf(si->fpw,
		"\"</font><font size=\"+1\" class=\"ldapsearch-addr\">&lt;");
	output_attrencoded_fp(mail, si->fpw);
	fprintf(si->fpw, "&gt;</font>");
	fprintf(si->fpw, "</td></tr>\n");

	if (cn_native)
		free(cn_native);
	return 0;
}

void	doldapsearch()
{
char	*f;
struct ldapabook *abooks[2];
const struct ldapabook *ptr;

	abooks[0]=ldapabook_read(LDAPADDRESSBOOK);
	abooks[1]=ldapabook_read(LOCALABOOK);

	ptr=ldapabook_find(abooks[0], cgi("ABOOK"));
	if (!ptr)
		ptr=ldapabook_find(abooks[1], cgi("ABOOK"));

	if (ptr && (f=getfilter()) != 0)
	{
		char	*tmpname=0;
		struct search_info si;
		struct maildir_tmpcreate_info createInfo;

		pref_setldap(ptr->name);
		printf("<pre>");
		fflush(stdout);


		si.fpw=NULL;
		si.errmsgbuf[0]=0;
		si.counter=0;

		maildir_tmpcreate_init(&createInfo);
		createInfo.uniq="ldap";
		createInfo.doordie=1;

		si.fpw=maildir_tmpcreate_fp(&createInfo);

		tmpname=createInfo.tmpname;
		createInfo.tmpname=NULL;
		maildir_tmpcreate_free(&createInfo);

		if (ldapabook_search(ptr, LDAPSEARCH, f, parsesearch,
				     save_errmsg, &si) == 0)
		{
			int	c;

			printf("</pre>");

			if (si.counter == 0)
				printf("%s", getarg("NOTFOUND"));

			printf("<table border=\"0\" cellpadding=\"4\">\n");
		
			fflush(si.fpw);
			rewind(si.fpw);
			while ((c=getc(si.fpw)) != EOF)
				putchar(c);

			printf("<tr><td colspan=\"2\"><hr width=\"90%%\" />"
			       "<input type=\"hidden\" name=\"ADDYCNT\" value=\"%u\" />\n"
			       "</td></tr>\n", si.counter);
			printf("<tr><td colspan=\"2\"><table>");

			printf("<tr><td align=\"right\">%s</td><td>"
			       "<select name=\"nick1\"><option value=\"\"></option>\n", getarg("ADD"));
			ab_listselect_fp(stdout);
			printf("</select></td></tr>\n");
			printf("<tr><td align=\"right\">%s</td><td>"
			       "<input type=\"text\" name=\"nick2\" /></td></tr>\n", getarg("CREATE"));
			printf("<tr><td></td><td>"
			       "<input type=\"submit\" name=\"import\" value=\"%s\" /></td></tr>",
			       getarg("SUBMIT"));
			printf("</table></td></tr></table>\n");
		}
		else if (si.errmsgbuf[0])
		{
			output_attrencoded_oknl(si.errmsgbuf);
			printf("</pre>");

		}
		fclose(si.fpw);

		if (tmpname)
		{
			unlink(tmpname);
			free(tmpname);
		}
	}
}
