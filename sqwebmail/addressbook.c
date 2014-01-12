#include "config.h"
/*
** Copyright 2000-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/

#include	"sqwebmail.h"
#include	"addressbook.h"
#include	"maildir.h"
#include	"cgi/cgi.h"
#include	"rfc822/rfc822.h"
#include	"maildir/maildirmisc.h"
#include	"numlib/numlib.h"
#include	<stdio.h>
#include	<string.h>
#include	<ctype.h>

#define	ADDRESSBOOK	"sqwebmail-addressbook"

#define TOUTF8(what)	\
	unicode_convert_toutf8((what), sqwebmail_content_charset, NULL)

extern const char *sqwebmail_content_charset;

extern void output_attrencoded(const char *);
extern void output_attrencoded_fp(const char *, FILE *);
extern void output_urlencoded(const char *);
extern void print_safe(const char *);
extern void call_print_safe_to_stdout(const char *p, size_t cnt);

static char *q_escape(const char *name)
{
	char *names;
	const char *cp;
	size_t namelen;
	char *p;

	namelen=1;

	for (cp=name; *cp; ++cp, ++namelen)
		if (*cp == '"' || *cp == '\\')
			++namelen;

	names=malloc(namelen);
	if (!names)
	{
		enomem();
	}

	for (p=names, cp=name; *cp; ++cp)
	{
		if (iscntrl((int)(unsigned char)*cp))	continue;

		if (*cp == '"' || *cp == '\\')
			*p++='\\';
		*p++= *cp;
	}
	*p=0;

	return names;
}

/*
** When adding a new name/address pair into the address book delete
** bad characters from both.
*/

static void fix_nameaddr(const char *name, const char *addr,
	char **nameret, char **addrret)
{
	char *names, *addresss;
	char	*p, *q;

	names=q_escape(name);

	if ((addresss=strdup(addr)) == 0)
	{
		free(names);
		enomem();
	}

	for (p=q=addresss; *p; p++)
	{
		if (isspace((int)(unsigned char)*p))	continue;
		if (iscntrl((int)(unsigned char)*p))	continue;
		if (*p == '<' || *p == '>' || *p == '(' || *p == ')' ||
			*p == '\\')
			continue;
		*q++=*p;
	}
	*q=0;

	*nameret=names;
	*addrret=addresss;
}

static void ab_add_int(const char *name, const char *address, const char *nick)
{
	char	*nicks, *names, *addresss, *p, *q;
	FILE	*fp;
	char	*header, *value;

	int	new_fd;
	char	*new_name;
	FILE	*new_fp;
	int	written;

	/* Delete bad characters from nickname, name, address */

	if ((nicks=strdup(nick)) == 0)	enomem();

	for (p=q=nicks; *p; p++)
	{
		if (isspace((int)(unsigned char)*p))	continue;
		if (iscntrl((int)(unsigned char)*p))	continue;
		if (strchr(":;,<>@\\", *p))	continue;
		*q++=*p;
	}
	*q=0;

	if (*nicks == 0)
	{
		free(nicks);
		return;
	}

	/* Remove quotes from name */

	fix_nameaddr(name, address, &names, &addresss);

	if (*addresss == 0)
	{
		free(addresss);
		free(nicks);
		free(names);
		return;
	}

	fp=fopen(ADDRESSBOOK, "r");

	new_fd=maildir_createmsg(INBOX, "addressbook", &new_name);
	p=malloc(sizeof("tmp/")+strlen(new_name));
	if (!p)
	{
		close(new_fd);
		free(new_name);
		enomem();
	}
	strcat(strcpy(p, "tmp/"), new_name);
	free(new_name);
	new_name=p;

	if (new_fd < 0 || (new_fp=fdopen(new_fd, "w")) == 0)
	{
		if (new_fd >= 0)	close(new_fd);
		free(addresss);
		free(nicks);
		free(names);
		enomem();
		return;
	}

	written=0;
	while (fp && (header=maildir_readheader_nolc(fp, &value)) != 0)
	{
		if (strcmp(header, nicks) == 0)
		{
			fprintf(new_fp, "%s: %s,\n    ",
				nicks, value);
			written=1;
			break;
		}
		fprintf(new_fp, "%s: %s\n", header, value);
	}
	if (!written)
		fprintf(new_fp, "%s: ", nicks);
	if (*names)
		fprintf(new_fp, "\"%s\" <%s>\n",
			names, addresss);
	else
		fprintf(new_fp, "<%s>\n", addresss);
	free(names);
	free(addresss);
	free(nicks);

	while (fp && (header=maildir_readheader_nolc(fp, &value)) != 0)
		fprintf(new_fp, "%s: %s\n", header, value);

	if (fp) fclose(fp);

	if (fflush(new_fp) || ferror(new_fp))
	{
		fclose(new_fp);
		close(new_fd);
		unlink(new_name);
		free(new_name);
		error("Unable to write out new address book -- write error, or out of disk space.");
		return;
	}
	fclose(new_fp);

	rename(new_name, ADDRESSBOOK);
	free(new_name);
}

void ab_add(const char *name, const char *address, const char *nick)
{
	char *nick_utf8, *name_utf8, *addr_utf8;

	if (*nick == 0 || *address == 0)
		return;

	nick_utf8=TOUTF8(nick);
	name_utf8=TOUTF8(name ? name:"");
	addr_utf8=TOUTF8(address);

	if (nick_utf8 && name_utf8 && addr_utf8)
		ab_add_int(name_utf8, addr_utf8, nick_utf8);

	if (nick_utf8)
		free(nick_utf8);
	if (name_utf8)
		free(name_utf8);
	if (addr_utf8)
		free(addr_utf8);
}

/* note: we're always passing utf-8 to dodel() */

static void dodel(const char *nick, struct rfc822a *a, int n,
	const char *replace_name, const char *replace_addr)
{
char	*p;
FILE	*fp, *new_fp;
char	*new_name;
int	new_fd;
char	*header, *value;

char	*namebuf=0, *addrbuf=0;
struct	rfc822token namet, addresst;

	if (replace_name && replace_addr && n < a->naddrs)
	{
		fix_nameaddr(replace_name, replace_addr, &namebuf, &addrbuf);
		namet.token='"';
		namet.ptr=namebuf;
		namet.len=strlen(namebuf);
		namet.next=0;
		a->addrs[n].name= &namet;

		addresst.token=0;
		addresst.ptr=addrbuf;
		addresst.len=strlen(addrbuf);
		addresst.next=0;
		a->addrs[n].tokens= &addresst;
	}
	else
	{
		while (++n < a->naddrs)
			a->addrs[n-1]=a->addrs[n];
		--a->naddrs;		/* It's that simple... */
	}

	fp=fopen(ADDRESSBOOK, "r");

	new_fd=maildir_createmsg(INBOX, "addressbook", &new_name);
	if (new_fd < 0 || (new_fp=fdopen(new_fd, "w")) == 0)
	{
		if (new_fd >= 0)	close(new_fd);
		fclose(fp);
		enomem();
		return;
	}
	p=malloc(sizeof("tmp/")+strlen(new_name));
	if (!p)
	{
		unlink(new_name);
		fclose(fp);
		fclose(new_fp);
		free(new_name);
		enomem();
	}
	strcat(strcpy(p, "tmp/"), new_name);
	free(new_name);
	new_name=p;

	while (fp && (header=maildir_readheader_nolc(fp, &value)) != 0)
	{
		if (strcmp(header, nick) == 0)
		{
		char	*s, *t;

			if (a->naddrs == 0)
				continue;
			s=rfc822_getaddrs_wrap(a, 70);

			if (!s)
			{
				fclose(new_fp);
				close(new_fd);
				fclose(fp);
				unlink(new_name);
				enomem();
			}
			fprintf(new_fp, "%s: ", header);
				for (t=s; *t; t++)
				{
					putc(*t, new_fp);
					if (*t == '\n')
						fprintf(new_fp, "    ");
				}
			fprintf(new_fp, "\n");
			free(s);
			continue;
		}
		fprintf(new_fp, "%s: %s\n", header, value);
	}
	if (fp) fclose(fp);

	if (namebuf)	free(namebuf);
	if (addrbuf)	free(addrbuf);

	if (fflush(new_fp) || ferror(new_fp))
	{
		fclose(new_fp);
		unlink(new_name);
		free(new_name);
		error("Unable to write out new address book -- write error, or out of disk space.");
		return;
	}

	fclose(new_fp);
	rename(new_name, ADDRESSBOOK);
	free(new_name);
}

static void dodelall(const char *nick)
{
FILE	*fp, *new_fp;
int	new_fd;
char	*new_name, *p;
char	*header, *value;

	fp=fopen(ADDRESSBOOK, "r");

	new_fd=maildir_createmsg(INBOX, "addressbook", &new_name);
	if (new_fd < 0 || (new_fp=fdopen(new_fd, "w")) == 0)
	{
		if (new_fd >= 0)	close(new_fd);
		fclose(fp);
		enomem();
		return;
	}
	p=malloc(sizeof("tmp/")+strlen(new_name));
	if (!p)
	{
		unlink(new_name);
		fclose(fp);
		fclose(new_fp);
		free(new_name);
		enomem();
	}
	strcat(strcpy(p, "tmp/"), new_name);
	free(new_name);
	new_name=p;

	while (fp && (header=maildir_readheader_nolc(fp, &value)) != 0)
	{
		if (strcmp(header, nick) == 0)	continue;
		fprintf(new_fp, "%s: %s\n", header, value);
	}
	if (fp) fclose(fp);

	if (fflush(new_fp) || ferror(new_fp))
	{
		fclose(new_fp);
		unlink(new_name);
		free(new_name);
		error("Unable to write out new address book -- write error, or out of disk space.");
		return;
	}

	fclose(new_fp);
	rename(new_name, ADDRESSBOOK);
	free(new_name);
}

void ab_listselect()
{
	ab_listselect_fp(stdout);
}

struct abooklist {
	struct abooklist *next;
	char *name;
	} ;

static void abl_free(struct abooklist *a)
{
struct abooklist *b;

	while (a)
	{
		b=a->next;
		free(a->name);
		free(a);
		a=b;
	}
}

static int sortabook(const void *a, const void *b)
{
	return ( strcmp( (*(struct abooklist * const *)a)->name,
			(*(struct abooklist * const *)b)->name));
}

void ab_listselect_fp(FILE *w)
{
	FILE	*fp;
	char *header, *value;
	struct	abooklist *a=0, *b, **aa;
	size_t	acnt=0, i;

	if ((fp=fopen(ADDRESSBOOK, "r")) != 0)
	{
		while ((header=maildir_readheader_nolc(fp, &value)) != NULL)
		{
			if ((b=malloc(sizeof(struct abooklist))) == 0 ||
				(b->name=strdup(header)) == 0)
			{
				if (b)	free(b);
				abl_free(a);
				enomem();
			}
			b->next=a;
			a=b;
			acnt++;
		}
		fclose(fp);

		if ((aa=malloc(sizeof(struct abooklist *)*(acnt+1))) == 0)
		{
			abl_free(a);
			enomem();
		}

		for (acnt=0, b=a; b; b=b->next)
			aa[acnt++]=b;
		qsort(aa, acnt, sizeof(*aa), sortabook);

		for (i=0; i<acnt; i++)
		{
			char *p=unicode_convert_fromutf8(aa[i]->name,
							   sqwebmail_content_charset,
							   NULL);

			fprintf(w, "<option value=\"");
			output_attrencoded_fp(p ? p:aa[i]->name, w);
			fprintf(w, "\">");

			output_attrencoded_fp(p ? p:aa[i]->name, w);
			if (p)
				free(p);
			fprintf(w, "</option>\n");
		}
		free(aa);
		abl_free(a);
	}
}

/*
** Extract all name/address entries from the address book, for external
** processing (mostly calendaring).
*/

int ab_get_nameaddr( int (*callback_func)(const char *, const char *,
					  void *),
		     void *callback_arg)
{
	FILE	*fp;
	char *header, *value;
	int rc=0;

	if ((fp=fopen(ADDRESSBOOK, "r")) != 0)
	{
		while ((header=maildir_readheader_nolc(fp, &value)) != NULL)
		{
			struct rfc822t *t;
			struct rfc822a *a;

			if (!value)
				continue;

			t=rfc822t_alloc_new(value, NULL, NULL);
			a=t ? rfc822a_alloc(t):0;

			if (a)
			{
				int i;

				for (i=0; i<a->naddrs; i++)
				{
					char *addr;
					char *name;

					if (a->addrs[i].tokens == NULL)
						continue;

					addr=rfc822_display_addr_tobuf(a, i,
								       NULL);
					if (!addr)
						continue;

					name=a->addrs[i].name ?
						rfc822_display_name_tobuf(a, i,
									  NULL):
						NULL;

					rc=(*callback_func)(addr, name,
							    callback_arg);
					if (name)
						free(name);
					free(addr);
					if (rc)
						break;
				}
			}

			if (a) rfc822a_free(a);
			if (t) rfc822t_free(t);
			if (rc)
				break;
		}
		fclose(fp);
	}
	return (rc);
}

struct ab_addrselect_s {
	struct ab_addrselect_s *next;
	char *name;
	char *addr;
} ;

static int ab_addrselect_cb(const char *a, const char *n, void *vp)
{
	struct ab_addrselect_s **p=(struct ab_addrselect_s **)vp, *q;

	if (!n || !a)
		return (0);

	while ( (*p) && strcasecmp((*p)->name, n) < 0)
		p= &(*p)->next;

	if ((q=malloc(sizeof(struct ab_addrselect_s))) == NULL)
		return (-1);

	if ((q->name=strdup(n)) == NULL)
	{
		free(q);
		return(-1);
	}

	if ((q->addr=strdup(a)) == NULL)
	{
		free(q->name);
		free(q);
		return(-1);
	}

	q->next= *p;
	*p=q;
	return (0);
}

static void ab_show_utf8(const char *p)
{
	int err;
	char *p_s=unicode_convert_fromutf8(p, sqwebmail_content_charset,
					     &err);

	if (!p_s)
		return;

	if (err)
	{
		free(p_s);
		return;
	}

	p=p_s;

	while (*p)
	{
		size_t i;

		for (i=0; p[i]; ++i)
		{
			if (*p == '"' || *p == '\\')
				break;
		}

		if (i)
			call_print_safe_to_stdout(p, i);

		p += i;

		if (*p)
		{
			putchar('\\');
			putchar(*p);
			++p;
		}
	}
	free(p_s);
	return;
}


void ab_nameaddr_show(const char *name, const char *addr)
{
	if (name)
	{
		printf("\"");
		ab_show_utf8(name);
		printf("\"&nbsp;");
	}
	printf("&lt;");

	if (addr)
		ab_show_utf8(addr);

	printf("&gt;");
}

void ab_addrselect()
{
	struct ab_addrselect_s *list=NULL, *p;

	printf("<select name=\"addressbookname\"><option value=\"\"></option>\n");

	if (ab_get_nameaddr(ab_addrselect_cb, &list) == 0)
	{
		for (p=list; p; p=p->next)
		{
			printf("<option value=\"");
			output_attrencoded(p->addr);
			printf("\">");
			ab_nameaddr_show(p->name, p->addr);
			printf("</option>\n");
		}
	}
	printf("</select>\n");

	while ((p=list) != NULL)
	{
		list=p->next;
		free(p->name);
		free(p->addr);
		free(p);
	}
}

const char *ab_find(const char *nick)
{
FILE	*fp;
char *header, *value;

	if ((fp=fopen(ADDRESSBOOK, "r")) != 0)
	{
		while ((header=maildir_readheader_nolc(fp, &value)) != NULL)
		{
			if (strcmp(header, nick) == 0)
			{
				fclose(fp);
				return (value);
			}
		}
		fclose(fp);
	}
	return (0);
}
	
void addressbook()
{
FILE	*fp;
char    *header, *value;
const char	*nick_prompt=getarg("PROMPT");
const char	*nick_submit=getarg("SUBMIT");
const char	*nick_title1=getarg("TITLE1");
const char	*nick_title2=getarg("TITLE2");
const char	*nick_delete=getarg("DELETE");
const char	*nick_name=getarg("NAME");
const char	*nick_address=getarg("ADDRESS");
const char	*nick_add=getarg("ADD");
const char	*nick_edit=getarg("EDIT");
const char	*nick_select1=getarg("SELECT1");
const char	*nick_select2=getarg("SELECT2");
const char *nick1;
int	do_edit;
char	*s, *q, *r;

char	*edit_name=0;
char	*edit_addr=0;
int	replace_index=0;

#if 0
	fp=fopen("/tmp/pid", "w");
	fprintf(fp, "%d", getpid());
	fclose(fp);
	sleep(10);
#endif

	nick1=cgi("nick");
	do_edit=0;
	if (*cgi("nick.edit"))
		do_edit=1;
	else if (*cgi("nick.edit2"))
	{
		do_edit=1;
		nick1=cgi("nick2");
	}
	else if (*cgi("editnick"))
	{
		do_edit=1;
		nick1=cgi("editnick");
	}

	if (*cgi("ADDYCNT"))	/* Import from LDAP */
	{
	unsigned counter=atoi(cgi("ADDYCNT"));
	char	numbuf[NUMBUFSIZE];
	char	numbuf2[NUMBUFSIZE+10];
	unsigned	i;

		if (counter < 1 || counter > 1000)
			counter=1000;
		nick1=cgi("nick2");
		if (!*nick1)
			nick1=cgi("nick1");

		if (*nick1)
		{
			do_edit=1;
			for (i=0; i<counter; i++)
			{
			const char *addy=cgi(strcat(strcpy(numbuf2, "ADDY"),
                                        libmail_str_size_t(i, numbuf)));
			char	*addycpy;
			char	*name;

				if (*addy == 0)	continue;

				addycpy=strdup(addy);
				if (!addycpy)	enomem();

				name=strchr(addycpy, '>');
				if (!name)
				{
					free(addycpy);
					continue;
				}
				*name++=0;
				while (*name == ' ')	++name;
				addy=addycpy;
				if (*addy == '<')	++addy;
				ab_add(name, addy, nick1);
			}
		}
	}

	if (*cgi("nick.delete"))
	{
		char *p=TOUTF8(cgi("nick"));

		do_edit=0;

		if (p)
		{
			dodelall(p);
			free(p);
		}
	}
	else if (*cgi("add"))
	{
	const char *newname=cgi("newname");
	const char *newaddr=cgi("newaddress");
	const char *editnick=cgi("editnick");
	const char *replacenum=cgi("replacenick");

		if (*replacenum)
		{
			if ((fp=fopen(ADDRESSBOOK, "r")) != 0)
			{
				char *editnick_utf8=TOUTF8(editnick);

				while ((header=maildir_readheader_nolc(fp,
					&value)) != NULL)
					if (editnick_utf8 &&
					    strcmp(header, editnick_utf8) == 0)
						break;

				if (header && editnick_utf8)
				{
				struct rfc822t *t;
				struct rfc822a *a;

					t=rfc822t_alloc_new(value, NULL, NULL);
					a=t ? rfc822a_alloc(t):0;

					if (a)
					{
						char *newname_utf8=
							TOUTF8(newname);

						char *newaddr_utf8=
							TOUTF8(newaddr);

						dodel(editnick_utf8, a,
						      atoi(replacenum),
						      newname_utf8 && newaddr_utf8 ?
						      newname_utf8:NULL,
						      newname_utf8 && newaddr_utf8 ?
						      newaddr_utf8:NULL);
						rfc822a_free(a);
						if (newname_utf8)
							free(newname_utf8);
						if (newaddr_utf8)
							free(newaddr_utf8);
					}
					if (t)	rfc822t_free(t);
				}
				if (editnick_utf8)
					free(editnick_utf8);

				fclose(fp);
			}
		}
		else
			ab_add(newname, newaddr, editnick);
		do_edit=1;
		nick1=editnick;
	}

	printf("%s", nick_prompt);
	printf("%s\n", nick_select1);

	ab_listselect();

	printf("%s\n", nick_select2);
	printf("%s", nick_submit);

	s=strdup(nick1);
	if (!s)	enomem();
	for (q=r=s; *q; q++)
	{
		if (isspace((int)(unsigned char)*q) ||
			strchr(",;:()\"%@<>'!", *q))
			continue;
		*r++=*q;
	}
	*r=0;

	if (do_edit && *s)
	{
		printf("<input type=\"hidden\" name=\"editnick\" value=\"");
		output_attrencoded(s);
		printf("\" />\n");

		printf("<table border=\"0\" class=\"nickedit-box\">\n");
		printf("<tr><td colspan=\"3\">\n");

		printf("%s%s%s", nick_title1, s, nick_title2);
		printf("</td></tr>\n");

		if ((fp=fopen(ADDRESSBOOK, "r")) != 0)
		{
			char *s_utf8=TOUTF8(s);

			while ((header=maildir_readheader_nolc(fp, &value))
				!= NULL)
				if (s_utf8 && strcmp(header, s_utf8) == 0)
					break;

			if (s_utf8)
				free(s_utf8);

			if (header)
			{
			struct rfc822t *t;
			struct rfc822a *a;
			char	*save_value=strdup(value);

				if (!save_value)
				{
					fclose(fp);
					free(s);
					enomem();
				}
				strcpy(save_value, value);
					/* Need copy 'cause dodel also
					** calls maildir_readheader */


				t=rfc822t_alloc_new(save_value, NULL, NULL);
				a=t ? rfc822a_alloc(t):0;

				if (a)
				{
				int	i;

					for (i=0; i<a->naddrs; i++)
					{
					char buf[100];

						sprintf(buf, "del%d", i);
						if (*cgi(buf))
						{
							dodel(s, a, i, 0, 0);
							break;
						}
						sprintf(buf, "startedit%d", i);
						if (*cgi(buf))
						{
							if (edit_name)
								free(edit_name);
							edit_name=
								rfc822_display_name_tobuf(a, i, NULL);
							if (edit_addr)
								free(edit_addr);
							edit_addr=
								rfc822_display_addr_tobuf(a, i, NULL);
							replace_index=i;
							break;
						}
					}

					for (i=0; i<a->naddrs; i++)
					{
						char *s;

						if (a->addrs[i].tokens == 0)
							continue;
						printf("<tr><td align=\"right\""
						       " class=\"nickname\">");

						if (a->addrs[i].name)
							/* getname defaults it
							** here.
							*/
						{
							char *n=rfc822_display_name_tobuf(a, i, NULL);

							if (n)
							{
								printf("\"");
								ab_show_utf8(n);
								printf("\"");
								free(n);
							}

						}

						printf("</td><td align=\"left\""
						       " class=\"nickaddr\">"
						       "&lt;");
						s=rfc822_display_addr_tobuf(a, i, NULL);

						if (s)
						{
							ab_show_utf8(s);
							free(s);
						}
						printf("&gt;</td><td><input type=\"submit\" name=\"startedit%d\" value=\"%s\" />&nbsp;<input type=\"submit\" name=\"del%d\" value=\"%s\" /></td></tr>\n",
							i, nick_edit,
							i, nick_delete);
					}
					rfc822a_free(a);
				}
				if (t)	rfc822t_free(t);
				free(save_value);
			}
			fclose(fp);
		}
		printf("<tr><td colspan=\"3\"><hr width=\"90%%\" /></td></tr>\n");
		printf("<tr><td align=\"right\">%s</td><td colspan=\"2\"><input type=\"text\" name=\"newname\" class=\"nicknewname\"", nick_name);

		if (edit_name)
		{
			int err;
			char *edit_name_native=
				unicode_convert_fromutf8(edit_name,
							   sqwebmail_content_charset,
							   &err);

			if (edit_name_native)
			{
				if (err == 0)
				{
					printf(" value=\"");
					output_attrencoded(edit_name_native);
					printf("\"");
				}
				free(edit_name_native);
			}
		}
		printf(" /></td></tr>\n");

		printf("<tr><td align=\"right\">%s</td><td><input type=\"text\" name=\"newaddress\" class=\"nicknewaddr\"", nick_address);
		if (edit_addr)
		{
			int err;
			char *edit_addr_native=
				unicode_convert_fromutf8(edit_addr,
							   sqwebmail_content_charset,
							   &err);

			if (edit_addr_native)
			{
				if (err == 0)
				{
					printf(" value=\"");
					output_attrencoded(edit_addr_native);
					printf("\"");
				}
				free(edit_addr_native);
			}
		}

		printf(" /></td><td>");

		if (edit_name || edit_addr)
			printf("<input type=\"hidden\" name=\"replacenick\" value=\"%d\" />",
				replace_index);

		printf("<input type=\"submit\" name=\"add\" value=\"%s\" /></td></tr>\n",
			edit_name || edit_addr ? nick_edit:nick_add);

		printf("</table>\n");
	}
	free(s);

	if (edit_name)
		free(edit_name);
	if (edit_addr)
		free(edit_addr);
}
