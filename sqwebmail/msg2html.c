#include "config.h"
/*
** Copyright 2007-2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/

#include "msg2html.h"
#include "buf.h"
#include <courier-unicode.h>
#include "numlib/numlib.h"
#include "gpglib/gpglib.h"
#include "cgi/cgi.h"
#include "rfc822/rfc2047.h"
#include "rfc2045/rfc3676parser.h"
#include "md5/md5.h"

#include "filter.h"
#include "html.h"

#include <ctype.h>

static void (*get_known_handler(struct rfc2045 *mime,
				struct msg2html_info *info))
	(FILE *, struct rfc2045 *, struct rfc2045id *, struct msg2html_info *);

static void (*get_handler(struct rfc2045 *mime,
			  struct msg2html_info *info))
	(FILE *, struct rfc2045 *,
	 struct rfc2045id *,
	 struct msg2html_info *);

static void addbuf(int c, char **buf, size_t *bufsize, size_t *buflen)
{
	if (*buflen == *bufsize)
	{
		char	*newbuf= *buf ?
			realloc(*buf, *bufsize+512):malloc(*bufsize+512);

		if (!newbuf)
			return;
		*buf=newbuf;
		*bufsize += 512;
	}
	(*buf)[(*buflen)++]=c;
}

static char *get_next_header(FILE *fp, char **value,
			     int preserve_nl,
			     off_t *mimepos, const off_t *endpos)
{
	int	c;
	int	eatspaces=0;

	size_t bufsize=256;
	char *buf=malloc(bufsize);
	size_t buflen=0;

	if (!buf)
		return NULL;

	if (mimepos && *mimepos >= *endpos)	return (NULL);

	while (mimepos == 0 || *mimepos < *endpos)
	{
		if ((c=getc(fp)) != '\n' && c >= 0)
		{
			if (c != ' ' && c != '\t' && c != '\r')
				eatspaces=0;

			if (!eatspaces)
				addbuf(c, &buf, &bufsize, &buflen);
			if (mimepos)	++ *mimepos;
			continue;
		}
		if ( c == '\n' && mimepos)	++ *mimepos;
		if (buflen == 0)
		{
			free(buf);
			return (0);
		}
		if (c < 0)	break;
		c=getc(fp);
		if (c >= 0)	ungetc(c, fp);
		if (c < 0 || c == '\n' || !isspace(c))	break;
		addbuf(preserve_nl ? '\n':' ', &buf, &bufsize, &buflen);
		if (!preserve_nl)
			eatspaces=1;
	}
	addbuf(0, &buf, &bufsize, &buflen);
	buf[buflen-1]=0;  /* Make sure, in outofmem situations */

	for ( *value=buf; **value; (*value)++)
	{
		if (**value == ':')
		{
			**value='\0';
			++*value;
			break;
		}
		**value=tolower(**value);
	}
	while (**value && isspace((int)(unsigned char)**value))	++*value;
	return(buf);
}

struct msg2html_info *msg2html_alloc(const char *charset)
{
	struct msg2html_info *p=malloc(sizeof(struct msg2html_info));

	if (!p)
		return NULL;
	memset(p, 0, sizeof(*p));

	p->output_character_set=charset;
	return p;
}

void msg2html_add_smiley(struct msg2html_info *i,
			 const char *txt, const char *imgurl)
{
	char buf[2];
	struct msg2html_smiley_list *l;

	buf[0]=*txt;
	buf[1]=0;

	if (strlen(i->smiley_index) < sizeof(i->smiley_index)-1)
		strcat(i->smiley_index, buf);


	if ((l=malloc(sizeof(struct msg2html_smiley_list))) != NULL)
	{
		if ((l->code=strdup(txt)) != NULL)
		{
			if ((l->url=strdup(imgurl)) != NULL)
			{
				l->next=i->smileys;
				i->smileys=l;
				return;
			}
			free(l->code);
		}
		free(l);
	}
}

void msg2html_free(struct msg2html_info *p)
{
	struct msg2html_smiley_list *sl;

	while ((sl=p->smileys) != NULL)
	{
		p->smileys=sl->next;
		free(sl->code);
		free(sl->url);
		free(sl);
	}
	free(p);
}

static void html_escape(const char *p, size_t n)
{
	char	buf[10];
	const	char *q=p;

	while (n)
	{
		--n;
		if (*p == '<')	strcpy(buf, "&lt;");
		else if (*p == '>') strcpy(buf, "&gt;");
		else if (*p == '&') strcpy(buf, "&amp;");
		else if (*p == ' ') strcpy(buf, "&nbsp;");
		else if (*p == '\n') strcpy(buf, "<br />");
		else if ((unsigned char)(*p) < ' ')
			sprintf(buf, "&#%d;", (int)(unsigned char)*p);
		else
		{
			p++;
			continue;
		}

		fwrite(q, p-q, 1, stdout);
		printf("%s", buf);
		p++;
		q=p;
	}
	fwrite(q, p-q, 1, stdout);
}

/*
** Consider header name: all lowercase, except the very first character,
** and the first character after every "-"
*/

static void header_uc(char *h)
{
	while (*h)
	{
		*h=toupper( (int)(unsigned char) *h);
		++h;
		while (*h)
		{
			*h=tolower((int)(unsigned char) *h);
			if (*h++ == '-')	break;
		}
	}
}

static void show_email_header(const char *h)
{
	html_escape(h, strlen(h));
}

static void print_header_uc(struct msg2html_info *info, char *h)
{
	header_uc(h);

	printf("<tr valign=\"baseline\"><th align=\"right\" class=\"message-rfc822-header-name\">");

	if (info->email_header)
		(*info->email_header)(h, show_email_header);
	else
		show_email_header(h);
	printf(":<span class=\"tt\">&nbsp;</span></th>");

}

struct showaddrinfo {
	struct msg2html_info *info;
	struct rfc822a *a;
	int curindex;
	int isfirstchar;
} ;

static void showaddressheader_printc(char c, void *p)
{
	struct showaddrinfo *sai= (struct showaddrinfo *)p;

	if (sai->isfirstchar)
	{
		char *name=0;
		char *addr=0;

		if (sai->curindex < sai->a->naddrs &&
		    sai->a->addrs[sai->curindex].tokens)
		{
			name=rfc822_display_name_tobuf(sai->a,
						       sai->curindex,
						       sai->info->
						       output_character_set);
			addr=rfc822_display_addr_tobuf(sai->a,
						       sai->curindex,
						       sai->info->
						       output_character_set);
		}

		if (sai->info->email_address_start)
			(*sai->info->email_address_start)(name, addr);

		if (addr)
			free(addr);
		if (name)
			free(name);

		sai->isfirstchar=0;
	}

	html_escape(&c, 1);
}

static void showaddressheader_printsep(const char *sep, void *p)
{
	struct showaddrinfo *sai= (struct showaddrinfo *)p;

	if (sai && !sai->isfirstchar)
		printf("</span>");

	if (sai->info->email_address_end)
		(*sai->info->email_address_end)();

	if (sai)
	{
		sai->curindex++;
		sai->isfirstchar=1;
	}

	printf("%s<span class=\"message-rfc822-header-contents\">", sep);
}

static void showaddressheader_printsep_plain(const char *sep, void *p)
{
	printf("%s", sep);
}

static void showmsgrfc822_addressheader(struct msg2html_info *info,
					const char *p)
{
	struct	rfc822t *rfcp;
	struct  rfc822a *rfca;

	struct showaddrinfo sai;

	rfcp=rfc822t_alloc_new(p, NULL, NULL);
	if (!rfcp)
		return;

	rfca=rfc822a_alloc(rfcp);
	if (!rfca)
	{
		rfc822t_free(rfcp);
		return;
	}

	sai.info=info;
	sai.a=rfca;
	sai.curindex=0;
	sai.isfirstchar=1;

	rfc2047_print_unicodeaddr(rfca, info->output_character_set,
				  showaddressheader_printc,
				  showaddressheader_printsep, &sai);
	if (!sai.isfirstchar)
		showaddressheader_printsep("", &sai);
	/* This closes the final </a> */


	rfc822a_free(rfca);
	rfc822t_free(rfcp);
}

static void showrfc2369_printheader(char c, void *p)
{
	p=p;
	putchar(c);
}

struct showmsgrfc2369_buflist {
	struct showmsgrfc2369_buflist *next;
	char *p;
};

static void showmsgrfc2369_header(struct msg2html_info *info, const char *p)
{
	struct	rfc822t *rfcp;
	struct  rfc822a *rfca;
	int	i;
	struct showmsgrfc2369_buflist *buflist=NULL;

	rfcp=rfc822t_alloc_new(p, NULL, NULL);
	if (!rfcp)
		return;

	rfca=rfc822a_alloc(rfcp);
	if (!rfca)
	{
		rfc822t_free(rfcp);
		return;
	}

	for (i=0; i<rfca->naddrs; i++)
	{
		char	*p=rfc822_getaddr(rfca, i);
		char	*q=info->get_textlink ?
			(*info->get_textlink)(p, info->arg):NULL;
		struct showmsgrfc2369_buflist *next;

		if (q)
		{
			next=malloc(sizeof(struct showmsgrfc2369_buflist));

			if (!next)
			{
				free(q);
				q=NULL;
			}
			else
			{
				next->next=buflist;
				buflist=next;
				next->p=q;
			}
		}

		if (q && rfca->addrs[i].tokens)
		{
			rfca->addrs[i].tokens->token=0;
			if (*q)
				free(p);
			else
			{
			struct	buf b;

				buf_init(&b);
				free(q);
				for (q=p; *q; q++)
				{
				char	c[2];

					switch (*q)	{
					case '<':
						buf_cat(&b, "&lt;");
						break;
					case '>':
						buf_cat(&b, "&gt;");
						break;
					case '&':
						buf_cat(&b, "&amp;");
						break;
					case ' ':
						buf_cat(&b, "&nbsp;");
						break;
					default:
						c[1]=0;
						c[0]=*q;
						buf_cat(&b, c);
						break;
					}
				}
				free(p);
				q=strdup(b.ptr ? b.ptr:"");
				buf_free(&b);
				next->p=q;
			}
			rfca->addrs[i].tokens->ptr=q;
			rfca->addrs[i].tokens->len=q ? strlen(q):0;
			rfca->addrs[i].tokens->next=0;
		}
		else
			free(p);
	}

	rfc822_print(rfca, showrfc2369_printheader,
				showaddressheader_printsep_plain, NULL);

	while (buflist)
	{
		struct showmsgrfc2369_buflist *next=buflist;

		buflist=next->next;

		free(next->p);
		free(next);
	}

	rfc822a_free(rfca);
	rfc822t_free(rfcp);
}

static int isaddressheader(const char *header)
{
	return (strcmp(header, "to") == 0 ||
		strcmp(header, "cc") == 0 ||
		strcmp(header, "from") == 0 ||
		strcmp(header, "sender") == 0 ||
		strcmp(header, "resent-to") == 0 ||
		strcmp(header, "resent-cc") == 0 ||
		strcmp(header, "reply-to") == 0);
}


static void showmsgrfc822_headerp(const char *p, size_t l, void *dummy)
{
	if (fwrite(p, l, 1, stdout) != 1)
	    ; /* ignore */
}

static int showmsgrfc822_header(const char *output_chset,
				const char *p, const char *chset)
{
	struct filter_info info;

	char32_t *uc;
	size_t ucsize;

	int conv_err;

	if (unicode_convert_tou_tobuf(p, strlen(p), chset,
					&uc, &ucsize,
					&conv_err))
	{
		conv_err=1;
		uc=NULL;
	}

	filter_start(&info, output_chset, showmsgrfc822_headerp, NULL);

	if (uc)
	{
		filter(&info, uc, ucsize);
		free(uc);
	}
	filter_end(&info);

	if (info.conversion_error)
		conv_err=1;

	return conv_err;
}

static void showmsgrfc822_body(FILE *fp, struct rfc2045 *rfc,
			       struct rfc2045id *idptr, int flag,
			       struct msg2html_info *info)
{
char	*header, *value;
char	*save_subject=0;
char	*save_date=0;
off_t	start_pos, end_pos, start_body;
struct	rfc2045id *p, newpart;
off_t	dummy;
off_t	pos;

	rfc2045_mimepos(rfc, &start_pos, &end_pos, &start_body, &dummy, &dummy);
	if (fseek(fp, start_pos, SEEK_SET) < 0)
		return;

	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" class=\"message-rfc822-header\">\n");

	pos=start_pos;
	while ((header=get_next_header(fp, &value, 1,
		&pos, &start_body)) != 0)
	{
		if (strcmp(header, "list-help") == 0 ||
			strcmp(header, "list-subscribe") == 0 ||
			strcmp(header, "list-unsubscribe") == 0 ||
			strcmp(header, "list-owner") == 0 ||
			strcmp(header, "list-archive") == 0 ||
			strcmp(header, "list-post") == 0)
		{
			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			showmsgrfc2369_header(info, value);
			printf("</span></td></tr>\n");
			free(header);
			continue;
		}

		if (info->fullheaders)
		{
			int	isaddress=isaddressheader(header);

			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			if (isaddress)
				showmsgrfc822_addressheader(info, value);
			else
				showmsgrfc822_header(info->output_character_set,
						     value,
						     "utf-8");
			printf("</span></td></tr>\n");
			free(header);
			continue;
		}
		if (strcmp(header, "subject") == 0)
		{
			if (save_subject)	free(save_subject);

			save_subject=
				rfc822_display_hdrvalue_tobuf(header, value,
							      info->output_character_set,
							      NULL,
							      NULL);

			if (!save_subject)
				save_subject=strdup(value);

			free(header);
			continue;
		}
		if (strcmp(header, "date") == 0)
		{
			if (save_date)	free(save_date);
			save_date=strdup(value);
			free(header);
			continue;
		}
		if (isaddressheader(header))
		{
			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			showmsgrfc822_addressheader(info, value);
			printf("</span></td></tr>\n");
		}
		free(header);
	}

	if (save_date)
	{
		time_t	t;
		struct tm *tmp=0;
		char	date_buf[256];

		if (rfc822_parsedate_chk(save_date, &t) == 0)
			tmp=localtime(&t);

		if (tmp)
		{
			char date_header[10];
			const char *date_fmt="%d %b %Y, %I:%M:%S %p";

			if (info->email_header_date_fmt)
				date_fmt=(*info->email_header_date_fmt)
					(date_fmt);

			strcpy(date_header, "Date");
			print_header_uc(info, date_header);

			strftime(date_buf, sizeof(date_buf)-1, date_fmt, tmp);
			date_buf[sizeof(date_buf)-1]=0;
			printf("<td><span class=\"message-rfc822-header-contents\">");

			showmsgrfc822_header(info->output_character_set,
					     date_buf,
					     unicode_default_chset());
			printf("</span></td></tr>\n");
		}
		free(save_date);
	}

	if (save_subject)
	{
		char subj_header[20];

		strcpy(subj_header, "Subject");
		print_header_uc(info, subj_header);

		printf("<td><span class=\"message-rfc822-header-contents\">");
		showmsgrfc822_header(info->output_character_set, save_subject,
				     info->output_character_set);
		printf("</span></td></tr>\n");
		free(save_subject);
	}

	if (flag && info->message_rfc822_action)
		(*info->message_rfc822_action)(idptr);

	printf("</table>\n<hr width=\"100%%\" />\n");

	if (!flag && info->gpgdir && libmail_gpg_has_gpg(info->gpgdir) == 0
	    && libmail_gpgmime_has_mimegpg(rfc)
	    && info->gpg_message_action)
		(*info->gpg_message_action)();

	if (!idptr)
	{
		idptr= &newpart;
		p=0;
	}
	else
	{
		for (p=idptr; p->next; p=p->next)
			;
		p->next=&newpart;
	}
	newpart.idnum=1;
	newpart.next=0;
	(*get_handler(rfc, info))(fp, rfc, idptr, info);
	if (p)
		p->next=0;
}

void msg2html(FILE *fp, struct rfc2045 *rfc,
	      struct msg2html_info *info)
{
	if (!info->mimegpgfilename)
		info->mimegpgfilename="";

	showmsgrfc822_body(fp, rfc, NULL, 0, info);
}

static void showmsgrfc822(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
			  struct msg2html_info *info)
{
	if (rfc->firstpart)
		showmsgrfc822_body(fp, rfc->firstpart, id, 1, info);
}

static void showunknown(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
			struct msg2html_info *info)
{
const char	*content_type, *cn;
const char	*dummy;
off_t start_pos, end_pos, start_body;
off_t dummy2;
char	*content_name;

	id=id;
	rfc2045_mimeinfo(rfc, &content_type, &dummy, &dummy);

	/* Punt for image/ MIMEs */

	if (strncmp(content_type, "image/", 6) == 0 &&
		(rfc->content_disposition == 0
		 || strcmp(rfc->content_disposition, "attachment")))
	{
		if (info->inline_image_action)
			(*info->inline_image_action)(id, content_type,
						     info->arg);
		return;
	}

	if (rfc2231_udecodeType(rfc, "name",
				info->output_character_set, &content_name)
	    < 0 &&
	    rfc2231_udecodeDisposition(rfc, "filename",
				       info->output_character_set,
				       &content_name) < 0)
	{
		if (content_name)
			free(content_name);
		content_name=NULL;
	}

	if (!content_name &&
	    ((cn=rfc2045_getattr(rfc->content_type_attr, "name")) ||
	     (cn=rfc2045_getattr(rfc->content_disposition_attr,
				 "filename"))) &&
	    strstr(cn, "=?") && strstr(cn, "?="))
	    /* RFC2047 header encoding (not compliant to RFC2047) */
	{
		content_name =
			rfc822_display_hdrvalue_tobuf("subject",
						      cn,
						      info->
						      output_character_set,
						      NULL, NULL);
	}

	rfc2045_mimepos(rfc, &start_pos, &end_pos, &start_body,
			&dummy2, &dummy2);

	if (info->unknown_attachment_action)
		(*info->unknown_attachment_action)(id, content_type,
						   content_name,
						   end_pos-start_body,
						   info->arg);


	if (content_name)
		free(content_name);
}

void showmultipartdecoded_start(int status, const char **styleptr)
{
	const char *style= status ? "message-gpg-bad":"message-gpg-good";

	printf("<table border=\"0\" cellpadding=\"2\" class=\"%s\"><tr><td>"
	       "<table border=\"0\" class=\"message-gpg\"><tr><td>", style);
	*styleptr=status ? "message-gpg-bad-text":"message-gpg-good-text";

}

void showmultipartdecoded_end()
{
	printf("</td></tr></table></td></tr></table>\n");
}

static void showmultipart(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
			  struct msg2html_info *info)
{
const char	*content_type, *dummy;
struct	rfc2045 *q;
struct	rfc2045id	nextpart, nextnextpart;
struct	rfc2045id	*p;
int gpg_status;

	for (p=id; p->next; p=p->next)
		;
	p->next=&nextpart;
	nextpart.idnum=0;
	nextpart.next=0;

	rfc2045_mimeinfo(rfc, &content_type, &dummy, &dummy);

	if (info->is_gpg_enabled &&
	    libmail_gpgmime_is_decoded(rfc, &gpg_status))
	{
		const char *style;
		showmultipartdecoded_start(gpg_status, &style);
		for (q=rfc->firstpart; q; q=q->next, ++nextpart.idnum)
		{
			if (q->isdummy)	continue;


			if (nextpart.idnum == 1)
			{
				printf("<blockquote class=\"%s\">",
				       style);
			}

			(*get_handler(q, info))(fp, q, id, info);
			if (nextpart.idnum == 1)
			{
				printf("</blockquote>");
			}
			else
				if (q->next)
					printf("<hr width=\"100%%\" />\n");
		}
		showmultipartdecoded_end();
	}
	else if (strcmp(content_type, "multipart/alternative") == 0)
	{
		struct	rfc2045 *q, *r=0, *s;
	int	idnum=0;
	int	dummy;

		for (q=rfc->firstpart; q; q=q->next, ++idnum)
		{
			int found=0;
			if (q->isdummy)	continue;

			/*
			** We pick this multipart/related section if:
			**
			** 1) This is the first section, or
			** 2) We know how to display this section, or
			** 3) It's a multipart/signed section and we know
			**    how to display the signed content.
			** 4) It's a decoded section, and we know how to
			**    display the decoded section.
			*/

			if (!r)
				found=1;
			else if ((s=libmail_gpgmime_is_multipart_signed(q))
				 != 0)
			{
				if (get_known_handler(s, info))
					found=1;
			}
			else if ( *info->mimegpgfilename
				  && libmail_gpgmime_is_decoded(q, &dummy))
			{
				if ((s=libmail_gpgmime_decoded_content(q)) != 0
				    && get_known_handler(s, info))
					found=1;
			}
			else if (get_known_handler(q, info))
			{
				found=1;
			}

			if (found)
			{
				r=q;
				nextpart.idnum=idnum;
			}
		}

		if (r)
			(*get_handler(r, info))(fp, r, id, info);
	}
	else if (strcmp(content_type, "multipart/related") == 0)
	{
	char *sid=rfc2045_related_start(rfc);

		/*
		** We can't just walts in, search for the Content-ID:,
		** and skeddaddle, that's because we need to keep track of
		** our MIME section.  So we pretend that we're multipart/mixed,
		** see below, and abort at the first opportunity.
		*/

		for (q=rfc->firstpart; q; q=q->next, ++nextpart.idnum)
		{
		const char *cid;

			if (q->isdummy)	continue;

			cid=rfc2045_content_id(q);

			if (sid && *sid && strcmp(sid, cid))
			{
				struct rfc2045 *qq;

				qq=libmail_gpgmime_is_multipart_signed(q);

				if (!qq) continue;

				/* Don't give up just yet */

				cid=rfc2045_content_id(qq);

				if (sid && *sid && strcmp(sid, cid))
				{
					/* Not yet, check for MIME/GPG stuff */



					/* Ok, we can give up now */
					continue;
				}
				nextnextpart.idnum=1;
				nextnextpart.next=0;
				nextpart.next= &nextnextpart;
			}
			(*get_handler(q, info))(fp, q, id, info);

			break;
			/* In all cases, we stop after dumping something */
		}
		if (sid)	free(sid);
	}
	else
	{
		for (q=rfc->firstpart; q; q=q->next, ++nextpart.idnum)
		{
			if (q->isdummy)	continue;
			(*get_handler(q, info))(fp, q, id, info);
			if (q->next)
				printf("<hr width=\"100%%\" />\n");
		}
	}
	p->next=0;
}

static int text_to_stdout(const char *p, size_t n, void *dummy)
{
	while (n)
	{
		--n;
		putchar(*p++);
	}
	return 0;
}

static void convert_unicode(const char32_t *uc,
			    size_t n, void *dummy)
{
	unicode_convert_uc(*(unicode_convert_handle_t *)dummy, uc, n);
}

static int htmlfilter_stub(const char *ptr, size_t cnt, void *voidptr)
{
	htmlfilter((struct htmlfilter_info *)voidptr,
		   (const char32_t *)ptr, cnt/sizeof(char32_t));
	return (0);
}


/* Recursive search for a Content-ID: header that we want */

static struct rfc2045 *find_cid(struct rfc2045 *p, const char *cidurl)
{
const char *cid=rfc2045_content_id(p);

	if (cid && strcmp(cid, cidurl) == 0)
		return (p);

	for (p=p->firstpart; p; p=p->next)
	{
	struct rfc2045 *q;

		if (p->isdummy)	continue;

		q=find_cid(p, cidurl);
		if (q)	return (q);
	}
	return (0);
}

/*
** Given an rfc2045 ptr, return the mime reference that will resolve to
** this MIME part.
*/

static char *rfc2mimeid(struct rfc2045 *p)
{
char	buf[MAXLONGSIZE+1];
char	*q=0;
unsigned n=p->pindex+1;	/* mime counts start at one */
char	*r;

	if (p->parent)
	{
		q=rfc2mimeid(p->parent);
		if (p->parent->firstpart->isdummy)
			--n;	/* ... except let's ignore the dummy part */
	}
	else	n=1;

	sprintf(buf, "%u", n);
	r=malloc( (q ? strlen(q)+1:0)+strlen(buf)+1);
	if (!r)
	{
		if (q)
			free(q);
		return NULL;
	}
	*r=0;
	if (q)
	{
		strcat(strcat(r, q), ".");
		free(q);
	}
	strcat(r, buf);
	return (r);
}

/*
** Convert cid: url to a http:// reference that will access the indicated
** MIME section.
*/

struct convert_cid_info {
	struct rfc2045 *rfc;
	struct msg2html_info *info;
};

static void add_decoded_link(struct rfc2045 *, const char *, int);

static char *convertcid(const char *cidurl, void *voidp)
{
	struct convert_cid_info *cid_info=
		(struct convert_cid_info *)voidp;

	struct	rfc2045 *rfc=cid_info->rfc;
	struct	rfc2045 *savep;

	char	*mimeid;
	char	*p;
	char *mimegpgfilename=cgiurlencode(cid_info->info->mimegpgfilename);
	int dummy;

	if (!mimegpgfilename)
		return NULL;

	if (rfc->parent)	rfc=rfc->parent;
	if (rfc->parent)
	{
		if (libmail_gpgmime_is_multipart_signed(rfc) ||
		    (*mimegpgfilename
		     && libmail_gpgmime_is_decoded(rfc, &dummy)))
			rfc=rfc->parent;
	}

	savep=rfc;
	rfc=find_cid(rfc, cidurl);

	if (!rfc)
		/* Sometimes broken MS software needs to go one step higher */
	{
		while ((savep=savep->parent) != NULL)
		{
			rfc=find_cid(savep, cidurl);
			if (rfc)
				break;
		}
	}

	if (!rfc)	/* Not found, punt */
	{
		free(mimegpgfilename);
		return strdup("");
	}

	mimeid=rfc2mimeid(rfc);

	if (!mimeid)
		p=NULL;
	else if (!cid_info->info->get_url_to_mime_part)
		p=strdup("");
	else
		p=(*cid_info->info->get_url_to_mime_part)(mimeid,
							  cid_info->info);
	free(mimeid);

	if (*mimegpgfilename && rfc->parent &&
	    libmail_gpgmime_is_decoded(rfc->parent, &dummy))
		add_decoded_link(rfc->parent, mimeid, dummy);

	return p;
}

/*
** When we output a multipart/related link to some content that has been
** signed/encoded, we save the decoding status, for later.
**
** Note -- we collapse multiple links to the same content.
*/

static struct decoded_list {
	struct decoded_list *next;
	struct rfc2045 *ptr;
	char *mimeid;
	int status;
} *decoded_first=0, *decoded_last=0;

static void add_decoded_link(struct rfc2045 *ptr, const char *mimeid,
			     int status)
{
	struct decoded_list *p;

	for (p=decoded_first; p; p=p->next)
	{

		if (strcmp(p->mimeid, mimeid) == 0)
			return;	/* Dupe */
	}

	p=(struct decoded_list *)malloc(sizeof(*p));

	if (!p)
		return;

	p->mimeid=strdup(mimeid);

	if (!p->mimeid)
	{
		free(p);
		return;
	}
	p->next=0;

	if (decoded_last)
		decoded_last->next=p;
	else
		decoded_first=p;

	decoded_last=p;

	p->ptr=ptr;
	p->status=status;
}

static void showtexthtml(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
			 struct msg2html_info *info)
{
	char	*content_base;
	const char *mime_charset, *dummy_s;

	struct htmlfilter_info *hf_info;
	unicode_convert_handle_t h;

	id=id;


	content_base=rfc2045_content_base(rfc);

	if (!content_base)
		return;

	rfc2045_mimeinfo(rfc, &dummy_s, &dummy_s, &mime_charset);

	h=unicode_convert_init(unicode_u_ucs4_native,
				 info->output_character_set,
				 text_to_stdout, NULL);

	if (!h)
		hf_info=NULL;
	else
		hf_info=htmlfilter_alloc(&convert_unicode, &h);

	if (hf_info)
	{
		struct rfc2045src *src;
		struct convert_cid_info cid_info;

		cid_info.rfc=rfc;
		cid_info.info=info;

#if 0
	{
		FILE *fp=fopen("/tmp/pid", "w");

		if (fp)
		{
			fprintf(fp, "%d", (int)getpid());
			fclose(fp);
			sleep(10);
		}
	}
#endif

		htmlfilter_set_http_prefix(hf_info, info->wash_http_prefix);
		htmlfilter_set_convertcid(hf_info, &convertcid, &cid_info);

		htmlfilter_set_contentbase(hf_info, content_base);

		htmlfilter_set_mailto_prefix(hf_info, info->wash_mailto_prefix);

		if (info->html_content_follows)
			(*info->html_content_follows)();

		printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\"><tr><td>\n");

		src=rfc2045src_init_fd(fileno(fp));

		if (src)
		{
			int conv_err;

			rfc2045_decodetextmimesection(src, rfc,
						      unicode_u_ucs4_native,
						      &conv_err,
						      &htmlfilter_stub,
						      hf_info);
			rfc2045src_deinit(src);

			if (conv_err && info->charset_warning)
				(*info->charset_warning)(mime_charset,
							 info->arg);
		}

		htmlfilter_free(hf_info);
		unicode_convert_deinit(h, NULL);
		printf("</td></tr>");
	}

	free(content_base);

	while (decoded_first)
	{
		struct decoded_list *p=decoded_first;
		const char *style;

		struct rfc2045 *q;

		printf("<tr><td>");

		showmultipartdecoded_start(p->status, &style);

		for (q=p->ptr->firstpart; q; q=q->next)
		{
			if (q->isdummy)
				continue;

			printf("<div class=\"%s\">", style);
			(*get_handler(q, info))(fp, q, NULL, info);
			printf("</div>\n");
			break;
		}
		showmultipartdecoded_end();
		decoded_first=p->next;
		free(p->mimeid);
		free(p);
		printf("</td></tr>\n");
	}
	printf("</table>\n");

}

static void showdsn(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
		    struct msg2html_info *info)
{
off_t	start_pos, end_pos, start_body;
off_t	dummy;

	id=id;
	rfc2045_mimepos(rfc, &start_pos, &end_pos, &start_body, &dummy, &dummy);
	if (fseek(fp, start_body, SEEK_SET) < 0)
	{
		printf("Seek error.");
		return;
	}
	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n");
	while (start_body < end_pos)
	{
	int	c=getc(fp);
	char	*header, *value;

		if (c == EOF)	break;
		if (c == '\n')
		{
			printf("<tr><td colspan=\"2\"><hr /></td></tr>\n");
			++start_body;
			continue;
		}
		ungetc(c, fp);

		if ((header=get_next_header(fp, &value, 1,
			&start_body, &end_pos)) == 0)
			break;

		print_header_uc(info, header);
		printf("<td><span class=\"message-rfc822-header-contents\">");
		/* showmsgrfc822_addressheader(value); */
		printf("%s", value);
		printf("</span></td></tr>\n");
		free(header);
	}
	printf("</table>\n");
}

static const char validurlchars[]=
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	":/.~%+?&#=@;-_,";

const char *skip_text_url(const char *r, const char *end)
{
	const char *q=r;

	for (; r < end && strchr(validurlchars, *r); r++)
	{
		if (*r == '&' && (end-r < 5 || strncmp(r, "&amp;", 5)))
			break;
	}
	if (r > q && (r[-1] == ',' || r[-1] == '.' || r[-1] == ';'))	--r;
	return (r);
}

/*
** The plain text HTML formatter renders text/plain content as HTML.
**
** The following structure maintains state of the HTML formatter.
*/

struct msg2html_textplain_info {

	/*
	** RFC 3676 parser of the raw text/plain content.
	*/
	rfc3676_parser_t parser;

	/*
	** This layer also needs to know whether the raw format is flowed
	** format.
	*/
	int flowed;

	int conv_err; /* A transcoding error has occured */

	/*
	** Optionally replace smiley sequences with image URLs
	*/

	const char *smiley_index; /* First character in all smiley seqs */
	struct msg2html_smiley_list *smileys; /* All smiley seqs */

	/*
	** Flag - convert text/plain content to HTML using wiki-style
	** formatting codes. Implies flowed format, as well.
	*/
	int wikifmt;

	/*
	** Whether a paragraph is now open. Only used with flowed format.
	*/

	int paragraph_open;

	/*
	** Whether the <LI> tag is open, when doing wiki-style formatting.
	*/
	int li_open;

	/*
	** Quotation level of flowed format line.
	*/
	size_t cur_quote_level;

	/*
	** Whether this line's quotation level is different than the previous
	** line's.
	*/
	size_t quote_level_has_changed;

	/*
	** Whether process_text() is getting invoked at the start of the
	** line.
	*/
	int start_of_line;

	/* wikifmt settings */

	int ttline; /* Line begun with a space, <tt> is now open */

	int text_decor_state;	/* Future text should have these decorations */
	int text_decor_state_cur; /* Current decorations in place */

	int text_decor_apostrophe_cnt; /* Apostrophe accumulator */

	char32_t text_decor_uline_prev;
	/* Previous character, used when scanning for underline enable */

	/*
	** Although we're getting the parsed text incrementally, with no
	** guarantees how many characters in each chunk, we want to accumulate
	** some context at the beginning of the line in order to be able to
	** handle wikifmt codes.
	*/

	char32_t lookahead_buf[64];
	size_t lookahead_saved;

	/*
	** Current list level
	*/
	char current_list_level[16];

	/*
	** Close paragraph, </p> or </hX>
	*/

	char paragraph_close[8];

	/*
	** Handler that searches for http/https/mailto URLs in plain text and
	** highlights them.
	*/
	size_t (*text_url_handler)(struct msg2html_textplain_info *,
				   const char32_t *,
				   size_t);

	/*
	** Output filter for unescaped text. Replaces HTML codes.
	*/
	struct filter_info info;

	/*
	** A URL being accumulated.
	*/
	char urlbuf[8192];
	size_t urlindex;

	/*
	** Caller-provided function to take a URL and return an HTML
	** sequence to display the URL.
	**
	** The characters in the provided URLs come from validurlchars, and
	** are "safe".
	**
	** The caller returns a malloced buffer, or NULL.
	*/
	char *(*get_textlink)(const char *url, void *arg);
	void *get_textlink_arg;
};

/*
** Convenience function. Accumulated latin chars that should be generated
** without escaping them.
*/
static void text_emit_passthru(struct msg2html_textplain_info *info,
			       const char *str)
{
	while (*str)
	{
		char32_t ch=(unsigned char)*str++;

		filter_passthru(&info->info, &ch, 1);
	}
}

/* If there's an open paragraph at this time, it needs to be closed */

static void text_close_paragraph(struct msg2html_textplain_info *info)
{
	if (info->paragraph_open)
	{
		char32_t uc='\n';

		info->paragraph_open=0;
		text_emit_passthru(info, info->paragraph_close);
		filter(&info->info, &uc, 1);
	}
}

/* Need to make sure an <LI> tag is open at this time */

static void text_open_li(struct msg2html_textplain_info *info)
{
	if (!info->li_open)
	{
		text_emit_passthru(info, "<li>");
		info->li_open=1;
	}
}

/* Need to make sure that an <LI> tag is now closed */

static void text_close_li(struct msg2html_textplain_info *info)
{
	text_close_paragraph(info);

	if (info->li_open)
	{
		char32_t uc='\n';

		info->li_open=0;
		text_emit_passthru(info, "</li>");
		filter(&info->info, &uc, 1);
	}
}

/* Opening tag for a list */
static const char *text_list_open_tag(char ch)
{
	return ch == '#' ? "<ol>":"<ul>";
}

/* Closing tag for a list */
static const char *text_list_close_tag(char ch)
{
	return ch == '#' ? "</ol>":"</ul>";
}

/*
** A list level is specified. Open or close list tags, in order to achieve
** that. Take into account the existing level, and issue the appropriate
** HTML to result in the given list level being open.
*/
static int text_set_list_level(struct msg2html_textplain_info *info,
			       const char *new_level,
			       size_t nl)
{
	size_t pl=strlen(info->current_list_level);
	int list_level_changed=0;

	if (nl > sizeof(info->current_list_level)-1)
		nl=sizeof(info->current_list_level)-1;

	/*
	** If there's a nesting mismatch, keep closing until we find a matching
	** level prefix.
	*/

	while (pl &&
	       (pl > nl || memcmp(info->current_list_level, new_level, pl)))
	{
		text_close_li(info);
		text_emit_passthru(info,
				   text_list_close_tag(info->current_list_level
						       [--pl]));

		list_level_changed=1;
		if (pl > 0)
			info->li_open=1;
		/*
		** Nested lists always begin with <LI> being open, so restore
		** the LI open state.
		*/
	}

	while (pl < nl)
	{
		text_close_paragraph(info);

		/* an <LI> must be open before opening a nested list */

		if (pl > 0)
			text_open_li(info);
		text_emit_passthru(info, text_list_open_tag(new_level[pl]));
		++pl;
		list_level_changed=1;
		info->li_open=0; /* No LI is currently in place */
	}

	memcpy(info->current_list_level, new_level, nl);
	info->current_list_level[nl]=0;

	return list_level_changed;
}

/*
** The next flowed format line has the indicated quote level. Issue
** appropriate HTML to close or open BLOCKQUOTE tags, as required.
*/
static void text_set_quote_level(struct msg2html_textplain_info *info,
				 size_t new_quote_level)
{
	info->quote_level_has_changed=0;

	/*
	** When formatting flowed text, need to stop any open lists before
	** entering quoted content.
	*/

	if (info->flowed && info->cur_quote_level != new_quote_level)
	{
		text_set_list_level(info, "", 0);

		text_close_paragraph(info);
		info->quote_level_has_changed=1;
	}

	while (info->cur_quote_level < new_quote_level)
	{
		char str[160];

		sprintf(str, info->wikifmt ?
			"\n<blockquote type=\"cite\">":
			"\n<blockquote type=\"cite\" class=\"cite%d\">"
			"<div class=\"quotedtext\">",
			(int)(info->cur_quote_level % 3));

		text_emit_passthru(info, str);
		++info->cur_quote_level;
	}

	while (info->cur_quote_level > new_quote_level)
	{
		text_emit_passthru(info,
				   info->wikifmt ?
				   "</blockquote>\n":
				   "</div></blockquote>\n");
		--info->cur_quote_level;
	}
}

static void text_process_decor_begin(struct msg2html_textplain_info *ptr);
static void text_process_decor_end(struct msg2html_textplain_info *ptr);

static size_t text_contents_notalpha(struct msg2html_textplain_info *ptr,
				     const char32_t *txt,
				     size_t txt_size);

static void text_line_contents_with_lookahead(const char32_t *txt,
					      size_t txt_size,
					      struct msg2html_textplain_info
					      *info);
static void process_text(const char32_t *txt,
			 size_t txt_size,
			 struct msg2html_textplain_info *info);

/*****************************************************************************/

/*
** RFC 3676 text processing layer. The raw text/plain encoding gets parsed
** using the rfc3676 parsing API, which invokes the following callbacks that
** define a logical line.
**
** Text at the beginning of the line gets accumulated into a lookahead buffer
** until there's sufficient amount of text to parse any wiki-formatting codes
** that are present at the beginning of the line.
**
** The contents of the logical line are passed to process_text().
** start_of_line gets set when process_text() is invoked for the first time,
** at the beginning of the logical line.
*/

static int do_text_line_contents(const char32_t *txt,
				 size_t txt_size,
				 void *arg);

/*
** Start of a new logical line.
*/
static int text_line_begin(size_t quote_level,
			   void *arg)
{
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;

	/*
	** Process the logical line's quoting level.
	*/
	text_set_quote_level(info, quote_level);

	/*
	** Initialize the lookahead mid-layer.
	*/
	info->lookahead_saved=0;
	info->start_of_line=1;

	/* Initialize the decoration layer */

	info->ttline=0;
	text_process_decor_begin(info);

	/*
	** Initialize URL collection layer.
	*/
	info->text_url_handler=text_contents_notalpha;
	return 0;
}

/*
** Process the contents of a logical line.
*/

static int text_line_contents(const char32_t *txt,
			      size_t txt_size,
			      void *arg)
{
#if 1
	return do_text_line_contents(txt, txt_size, arg);
#else
	/* For debugging purposes */

	while (txt_size)
	{
		do_text_line_contents(txt, 1, arg);
		++txt;
		--txt_size;
	}
#endif
}

static int do_text_line_contents(const char32_t *txt,
				 size_t txt_size,
				 void *arg)
{
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;
	char32_t lookahead_cpy_buf[sizeof(info->lookahead_buf)
				       /sizeof(info->lookahead_buf[0])];
	size_t n;

	/*
	** Prepend any saved lookahead data to the new unicode stream.
	*/

	while (txt_size)
	{
		if (info->lookahead_saved == 0)
		{
			/*
			** Nothing saved from the last go-around, we can
			** pass this off to the lookahead mid-layer.
			*/

			text_line_contents_with_lookahead(txt, txt_size, info);
			break;
		}

		/*
		** Use as much as can be taken from the new unicode chunk.
		**
		** text_line_contents_with_lookahead makes sure that
		** lookahead_saved is not larger than half the buffer size.
		*/
		n=sizeof(lookahead_cpy_buf)/sizeof(lookahead_cpy_buf[0])
			- info->lookahead_saved;

		if (n > txt_size)
			n=txt_size;

		memcpy(lookahead_cpy_buf,
		       info->lookahead_buf,
		       info->lookahead_saved*sizeof(lookahead_cpy_buf[0]));

		memcpy(lookahead_cpy_buf+info->lookahead_saved,
		       txt, n*sizeof(lookahead_cpy_buf[0]));

		text_line_contents_with_lookahead(lookahead_cpy_buf,
						  info->lookahead_saved + n,
						  info);

		txt += n;
		txt_size -= n;
	}

	return 0;
}

/*
** Lookahead accumulator mid-layer. Accumulates line content into
** lookahead_buf. Next time, the accumulated line content gets resubmitted
** to this function, prepended to any new content.
*/

static void text_line_contents_with_lookahead(const char32_t *txt,
					      size_t txt_size,
					      struct msg2html_textplain_info
					      *info)
{
	size_t i;

	info->lookahead_saved=0;


	/*
	** At the beginning of the line, if using wiki markups, make sure
	** there's enough stuff buffered for the main logic to do its work.
	*/

	if (info->flowed && info->start_of_line && info->wikifmt)
	{
		for (i=0; i<txt_size; ++i)
		{
			switch ((unsigned char)txt[i]) {
			case '#':
			case '*':
			case '=':
				continue;
			default:
				break;
			}
			break;
		}

		if (i == txt_size && i <
		    sizeof(info->lookahead_buf)
		    /sizeof(info->lookahead_buf[0])/2)
		{
			info->lookahead_saved=i;
			memcpy(info->lookahead_buf, txt,
			       i*sizeof(info->lookahead_buf[0]));
			return;
		}
	}

	/*
	** In the rest of the line, look for smileys.
	*/

	while (txt_size)
	{
		struct msg2html_smiley_list *l;
		int flag=0;

		/* Look for the first char that might be a smiley */

		for (i=0; i<txt_size; ++i)
		{
			if ((unsigned char)txt[i] == txt[i] &&
			    info->smiley_index &&
			    strchr(info->smiley_index, txt[i]))
				break;
		}

		if (i)
		{
			process_text(txt, i, info);
			txt += i;
			txt_size -= i;
			continue;
		}

		/*
		** Ok, now figure out if this is a smiley.
		*/

		for (l=info->smileys; l; l=l->next)
		{
			size_t j;

			if (strlen(l->code) > txt_size)
			{
				flag=1;
				continue; /* Not enough context */
			}

			for (j=0; l->code[j]; j++)
			{
				if ( (unsigned char)txt[j] != txt[j])
					break;

				if ((unsigned char)txt[j] !=
				    (unsigned char)l->code[j])
					break;
			}

			if (l->code[j] == 0)
			{
				process_text(txt, 0, info);
				/* May be needed to start a paragraph */

				text_emit_passthru(info, l->url);

				txt += j;
				txt_size -= j;
				break;
			}
		}

		if (l) /* A smiley was found */
			continue;

		if (flag) /* Insufficient context */
		{
			i=txt_size;

			if (i > sizeof(info->lookahead_buf)
			    /sizeof(info->lookahead_buf[0])/2)
			{
				/*
				** Internal breakage, lookahead buffer
				** not big enough for the smiley.
				*/

				process_text(txt, txt_size, info);
				break;
			}
			info->lookahead_saved=i;
			memcpy(info->lookahead_buf, txt,
			       i*sizeof(info->lookahead_buf[0]));
			break;
		}

		/* Did not find a smiley, consume one character */

		process_text(txt, 1, info);
		++txt;
		--txt_size;
	}
}

/*
** Don't want to generate long HTML without line breaks. It's OK to
** have a linebreak here.
*/
static int text_line_flowed_notify(void *arg)
{
	char32_t nl='\n';
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;
	filter(&info->info, &nl, 1);
	return 0;
}

/*
** End of the line. Wrap up all the layers.
*/
static int text_line_end(void *arg)
{
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;

	/*
	** Wrap up the lookahead mid-layer.
	*/
	if (info->lookahead_saved)
		process_text(info->lookahead_buf,
			     info->lookahead_saved, info);

	/*
	** Wrap up the URL collection layer.
	*/
	(*info->text_url_handler)(info, NULL, 0);

	/*
	** Wrap up the text decoration layer
	*/
	text_process_decor_end(info);

	if (info->flowed)
	{
		char32_t uc='\n';

		if (info->start_of_line)
		{
			/*
			** This was an empty line.
			*/

			if (info->paragraph_open)
			{
				/*
				** A paragraph was open, so this empty line
				** marks the end of the paragraph.
				*/
				text_close_paragraph(info);
				filter(&info->info, &uc, 1);
			}
			else if (!info->quote_level_has_changed)
			{
				/*
				** In all other cases, an empty line generates
				** another <br />. However, if the quoting level
				** has changed, let it slide, because the
				** forthcoming <p> tag is going to advance
				** vertical white space.
				*/
				text_emit_passthru(info, "<br/>");
				filter(&info->info, &uc, 1);
			}
		}
		else
		{
			/*
			** Close the open <tt> tag.
			*/
			if (info->ttline)
			{
				text_emit_passthru(info, "</tt>");
			}

			filter(&info->info, &uc, 1);
		}
		return 0;
	}


	{
		char32_t uc='\n';

		filter(&info->info, &uc, 1);
	}
	return 0;
}

static void process_text_wiki(char *paragraph_open,
			      const char32_t **txt_ret,
			      size_t *txt_size_ret,
			      struct msg2html_textplain_info *info);

static void process_text(const char32_t *txt,
			 size_t txt_size,
			 struct msg2html_textplain_info *info)
{
	if (info->flowed && info->start_of_line)
	{
		char32_t uc='\n';

		filter(&info->info, &uc, 1);

		/* Starting a logical line */

		if (!info->paragraph_open)
		{
			char paragraph_open[8];

			/*
			** A paragraph is not open, so open it.
			*/

			strcpy(paragraph_open, "<p>");
			strcpy(info->paragraph_close, "</p>");

			if (info->wikifmt && info->cur_quote_level == 0)
				process_text_wiki(paragraph_open,
						  &txt, &txt_size, info);

			text_emit_passthru(info, paragraph_open);
			info->paragraph_open=1;
		}
		else
		{
			/*
			** Start of a logical line, but not a start of
			** a paragraph results in an extra <br/>, in the
			** middle of the existing paragraph.
			*/

			text_emit_passthru(info, "<br/>");
		}
		if (txt_size && *txt == ' ')
			info->ttline=1;
		info->start_of_line=0;

		if (info->ttline)
			text_emit_passthru(info, "<tt class='tt'>");
	}

	/*
	** Pass the rest of the text to the URL collection layer.
	*/
	while (txt_size)
	{
		size_t n= (*info->text_url_handler)(info, txt, txt_size);

		txt += n;
		txt_size -= n;
	}
}


/*
** Do additional wiki-style HTML formatting.
** The lookahead mid-layer made sure
** we have enough context here.
*/

static void process_text_wiki(char *paragraph_open,
			      const char32_t **txt,
			      size_t *txt_size,
			      struct msg2html_textplain_info *info)
{
	size_t i;

	/*
	** "=" at the beginning of the line marks up a heading.
	*/

	for (i=0; i<*txt_size; i++)
		if ((*txt)[i] != '=')
			break;

	if (i > 0)
	{
		int n=i < 8 ? i:8;

		/* Use <hX> instead of a boring paragraph */

		sprintf(paragraph_open, "<h%d>", n);
		sprintf(info->paragraph_close, "</h%d>", n);

		*txt += i;
		*txt_size -= i;

		if (*txt_size && **txt == ' ')
		{
			++*txt;
			--*txt_size;
		}

		text_set_list_level(info, "", 0);
	}
	else
	{
		/*
		** Otherwise, #* characters at the beginning of the line
		** mark up a list.
		*/

		for (i=0; i<*txt_size; i++)
			if ((*txt)[i] != '#' &&
			    (*txt)[i] != '*')
				break;

		if (i > 0)
		{
			char new_list_level[sizeof(info->current_list_level)];
			size_t j;
			int rc;

			for (j=0; j<i; j++)
			{
				if (j >= sizeof(info->current_list_level)-1)
					break;

				new_list_level[j]=(*txt)[j];
			}

			new_list_level[j]=0;

			rc=text_set_list_level(info, new_list_level, j);

			*txt += i;
			*txt_size -= i;

			/*
			** The same list nesting level prefix followed by +
			** continues the existing list entry. Otherwise,
			** a new list entry is started. This is done by
			** closing the existing list entry, first.
			*/

			if (*txt_size && **txt == '+' && !rc)
			{
				++*txt;
				--*txt_size;
			}
			else
			{
				text_close_li(info);
			}

			/* Prepend <li> to <p> in the paragraph open marker */

			paragraph_open[0]=0;

			if (!info->li_open)
			{
				strcat(paragraph_open, "<li>");
				info->li_open=1;
			}

			strcat(paragraph_open, "<p>");

			if (*txt_size && **txt == ' ')
			{
				++*txt;
				--*txt_size;
			}
		}
		else /* Make sure that all lists are now closed */
		{
			text_set_list_level(info, "", 0);

			/*
			** A space at the beginning of the line generates
			** a <tt>
			*/

			if (*txt_size && **txt == ' ')
				info->ttline=1;
		}
	}
}

/**************************************************************************/

/*
** The URL collection layer.
**
** A potential URL gets accumulated in a buffer, until it's known whether
** it's a URL or not.
**
** If it's not a URL, the text is passed to the text decoration layer.
**
** A NULL pointer passed to the URL handling layer is an end-of-line
** indication, and anything that's left in the accumulated buffer is
** passed through to the text decoration layer.
*/

static void text_process_decor(struct msg2html_textplain_info *info,
			       const char32_t *uc,
			       size_t cnt);

static void text_process_decor_uline(struct msg2html_textplain_info *info,
				     const char32_t *uc,
				     size_t cnt);

static void text_process_plain(struct msg2html_textplain_info *info,
			       const char32_t *uc,
			       size_t cnt);

static void emit_char_buffer(struct msg2html_textplain_info *info,
			     const char *uc,
			     size_t cnt,
			     void (*func)(struct msg2html_textplain_info *info,
					  const char32_t *uc,
					  size_t cnt));


static size_t text_contents_checkurl(struct msg2html_textplain_info *info,
				     const char32_t *txt,
				     size_t txt_size);
/*
** Initial state of the URL collection layer -- processing non-alphabetic
** content. The non-alphabetic content is passed through to the text
** decoration layer.
*/

static size_t text_contents_notalpha(struct msg2html_textplain_info *info,
				     const char32_t *txt,
				     size_t txt_size)
{
	size_t i;

	if (!txt)
		return 0;

	for (i=0; i<txt_size; i++)
	{
		if (txt[i] >= 'a' && txt[i] <= 'z')
		{
			/*
			** Seen a first alphabetic character, so begin
			** collecting a URL candidate.
			*/
			info->urlindex=0;
			info->text_url_handler=text_contents_checkurl;
			break;
		}
	}

	if (i)
		text_process_decor(info, txt, i);

	return i;
}

static size_t text_contents_nourl(struct msg2html_textplain_info *info,
				  const char32_t *txt,
				  size_t txt_size);

static size_t text_contents_collecturl(struct msg2html_textplain_info *info,
				       const char32_t *txt,
				       size_t txt_size);
/*
** Collecting what may be a URL method name.
*/
static size_t text_contents_checkurl(struct msg2html_textplain_info *info,
				     const char32_t *txt,
				     size_t txt_size)
{
	size_t i;

	if (txt == NULL)
	{
		/* End of line, flush the buffer */
		if (info->urlindex)
		{
			emit_char_buffer(info, info->urlbuf,
					 info->urlindex,
					 text_process_decor);
			info->urlindex=0;
		}
		return 0;
	}

	/*
	** Accumulate this content, until notified otherwise.
	*/

	for (i=0; i<txt_size; i++)
	{
		if (i+info->urlindex > 32)
		{
			/*
			** Too long, can't be a method name.
			*/

			emit_char_buffer(info, info->urlbuf,
					 info->urlindex,
					 text_process_decor);

			info->text_url_handler=text_contents_nourl;
			return text_contents_nourl(info, txt, txt_size);
		}

		if (txt[i] == ':') /* Bingo? */
		{
			info->urlbuf[info->urlindex+i]=0;

			if (strcmp(info->urlbuf, "http") == 0 ||
			    strcmp(info->urlbuf, "https") == 0 ||
			    strcmp(info->urlbuf, "mailto") == 0)
			{
				/* Bingo! */
				info->urlbuf[info->urlindex+i]=':';
				++i;

				info->urlindex += i;

				info->text_url_handler=
					text_contents_collecturl;
				return i;
			}
		}

		if (txt[i] < 'a' || txt[i] > 'z')
		{
			/* Hit another non-alphabetic character, reset */

			emit_char_buffer(info, info->urlbuf,
					 info->urlindex+i,
					 text_process_decor);

			info->text_url_handler=text_contents_notalpha;
			return i;
		}

		info->urlbuf[info->urlindex+i]=txt[i];
	}

	info->urlindex += i;
	return i;
}

/*
** Word too long to be a URL, so ignore it.
*/

static size_t text_contents_nourl(struct msg2html_textplain_info *info,
				  const char32_t *txt,
				  size_t txt_size)
{
	size_t i;

	if (!txt)
		return 0;

	for (i=0; i<txt_size; i++)
	{
		if (txt[i] < 'a' || txt[i] > 'z')
		{
			info->text_url_handler=text_contents_notalpha;
			break;
		}
	}

	if (i)
		text_process_decor(info, txt, i);
	return i;
}

/*
** Call the msg2html user to obtain how the URL should be marked up.
*/
static void doemiturl(struct msg2html_textplain_info *info)
{
	char *link=info->get_textlink ?
		(*info->get_textlink)(info->urlbuf, info->get_textlink_arg):0;

	if (link)
	{
		text_emit_passthru(info, link);
		free(link);
		return;
	}

	/* Caller doesn't want the URL to be marked up */

	emit_char_buffer(info, info->urlbuf, strlen(info->urlbuf),
			 text_process_decor);
}

static void text_process_decor_apostrophe(struct msg2html_textplain_info *info);
static void set_text_decor(struct msg2html_textplain_info *info, int new_decor);


/*
** Collected a URL. Given that this is text/plain content, if a URL ends
** with a period, comma, semicolon, or a colon, it shouldn't be a part of the
** URL, of course.
*/

static void emiturl(struct msg2html_textplain_info *info)
{
	size_t url_size=info->urlindex;
	char save_char;

	text_process_decor_apostrophe(info);
	set_text_decor(info, info->text_decor_state);

	info->text_url_handler=text_contents_notalpha;

	while (url_size > 0)
	{
		if (strchr(",.;:", info->urlbuf[url_size-1]) == NULL)
			break;
		--url_size;
	}

	info->urlbuf[info->urlindex]=0;

	save_char=info->urlbuf[url_size];
	info->urlbuf[url_size]=0;
	doemiturl(info);
	info->urlbuf[url_size]=save_char;

	emit_char_buffer(info, info->urlbuf+url_size,
			 strlen(info->urlbuf+url_size),
			 text_process_decor);
}

/*
** Ok, we have a URL, so collect it, then mark it up.
*/
static size_t text_contents_collecturl(struct msg2html_textplain_info *info,
				       const char32_t *txt,
				       size_t txt_size)
{
	size_t i;

	if (txt == NULL)
	{
		emiturl(info);
		return 0;
	}

	for (i=0; i<txt_size; i++)
	{
		if (txt[i] < ' ' || txt[i] >= 127 ||
		    strchr(validurlchars, txt[i]) == NULL)
		{
			emiturl(info);
			break;
		}

		if (info->urlindex < sizeof(info->urlbuf)-1)
			info->urlbuf[info->urlindex++]=txt[i];
	}

	return i;
}

/*
** Convenience function for upconverting chars to unicode chars.
*/

static void emit_char_buffer(struct msg2html_textplain_info *info,
			     const char *uc,
			     size_t cnt,
			     void (*func)(struct msg2html_textplain_info *info,
					  const char32_t *uc,
					  size_t cnt))
{
	char32_t buf[64];

	while (cnt)
	{
		size_t n=sizeof(buf)/sizeof(buf[0]);
		size_t i;

		if (n > cnt)
			n=cnt;

		for (i=0; i<n; i++)
			buf[i]=(unsigned char)uc[i];

		(*func)(info, buf, i);

		uc += n;
		cnt -= n;
	}
}

/****************************************************************************/

/*
** The text decoration layer.
**
** The text decoration layer marks up bold, italic, and underline sequences
** in a logical line.
*/

#define TEXT_DECOR_B	1
#define TEXT_DECOR_I	2
#define TEXT_DECOR_U	4

/*
** Emit markup tags to select the request decoration.
*/
static void set_text_decor(struct msg2html_textplain_info *info, int new_decor)
{
	if (info->text_decor_state_cur == new_decor)
		return; /* Already the right state */

	/*
	** The easiest way to do it is to first turn off old decoration state
	** then turn on the new one.
	*/

	if (info->text_decor_state_cur & TEXT_DECOR_U)
		text_emit_passthru(info, "</u>");

	if (info->text_decor_state_cur & TEXT_DECOR_I)
		text_emit_passthru(info, "</i>");

	if (info->text_decor_state_cur & TEXT_DECOR_B)
		text_emit_passthru(info, "</b>");

	if (new_decor & TEXT_DECOR_B)
		text_emit_passthru(info, "<b>");

	if (new_decor & TEXT_DECOR_I)
		text_emit_passthru(info, "<i>");

	if (new_decor & TEXT_DECOR_U)
		text_emit_passthru(info, "<u>");

	info->text_decor_state_cur=new_decor;
}

/*
** Initialize the decoration layer.
*/

static void text_process_decor_begin(struct msg2html_textplain_info *info)
{
	info->text_decor_state=0;
	info->text_decor_state_cur=0;
	info->text_decor_apostrophe_cnt=0;

	info->text_decor_uline_prev=' ';
}

/*
** Process accumulated apostrophes.
*/

static void text_process_decor_apostrophe(struct msg2html_textplain_info *info)
{
	char32_t apos='\'';
	int n=info->text_decor_apostrophe_cnt;

	info->text_decor_apostrophe_cnt=0;

	while (n > 0)
	{
		if (n == 3)
		{
			info->text_decor_state ^= TEXT_DECOR_B;
			n -= 3;
			continue;
		}

		if (n == 2)
		{
			info->text_decor_state ^= TEXT_DECOR_I;
			n -= 2;
			continue;
		}

		text_process_decor_uline(info, &apos, 1);
		--n;
	}
}

/*
** Deinitialize the text decoration layer.
*/
static void text_process_decor_end(struct msg2html_textplain_info *info)
{
	text_process_decor_apostrophe(info);
	set_text_decor(info, 0);
}

/*
** Process text decorations.
*/
static void text_process_decor(struct msg2html_textplain_info *info,
			       const char32_t *uc,
			       size_t cnt)
{
	size_t i;

	if (!info->wikifmt)
	{
		/* They are only processed when wiki formatting is requested */
		text_process_plain(info, uc, cnt);
		return;
	}

	/*
	** Look for apostrophes.
	*/

	while (cnt)
	{
		if (*uc == '\'')
		{
			++info->text_decor_apostrophe_cnt;
			++uc;
			--cnt;
			continue;
		}

		/*
		** Not an apostrophe right now. Process accumulated apostrophes
		** then look for the next one.
		*/
		text_process_decor_apostrophe(info);

		for (i=0; i<cnt && uc[i] != '\''; ++i)
			;

		text_process_decor_uline(info, uc, i);

		uc += i;
		cnt -= i;
	}
}

/*
** Text decoration sub-layer for the underline markup.
*/

static void text_process_decor_uline(struct msg2html_textplain_info *info,
				     const char32_t *uc,
				     size_t cnt)
{
	size_t i;
	char32_t space=' ';

	while (cnt)
	{
		/*
		** When underlining is not turned on, look for a space followed
		** by a _.
		*/

		if (!(info->text_decor_state & TEXT_DECOR_U))
		{
			if (info->text_decor_uline_prev == ' ' && *uc == '_')
			{
				info->text_decor_state |= TEXT_DECOR_U;
				++uc;
				--cnt;

				/* Found it */
				continue;
			}

			/* Look for it */

			for (i=0; i<cnt; i++)
			{
				if (info->text_decor_uline_prev == ' ' &&
				    uc[i] == '_')
					break;

				info->text_decor_uline_prev=uc[i];
			}

			if (i)
				text_process_plain(info, uc, i);

			uc += i;
			cnt -= i;
			continue;
		}

		/*
		** Underlining is on, so look for an underscore that was
		** followed by a space, tab, comma, semicolon, colon, or period.
		*/

		if (info->text_decor_uline_prev == '_')
			switch (*uc) {
			case ' ':
			case '\t':
			case ',':
			case ';':
			case ':':
			case '.':
				info->text_decor_state &= ~TEXT_DECOR_U;
				/* Found it */
				continue;
			}

		/*
		** If _ was suppressed, but, obviously, it's not followed by
		** a space, emit the space in place of that _.
		*/

		if (info->text_decor_uline_prev == '_')
			text_process_plain(info, &space, 1);

		/*
		** If the current character is _, suppress it.
		*/
		if (*uc == '_')
		{
			info->text_decor_uline_prev='_';
			++uc;
			--cnt;
			continue;
		}

		/* Otherwise look for the next _ character */

		for (i=0; i<cnt; ++i)
		{
			if (uc[i] == '_')
				break;
			info->text_decor_uline_prev=uc[i];
		}

		if (i)
			text_process_plain(info, uc, i);

		uc += i;
		cnt -= i;
	}
}

/***************************************************************************/

/*
** End of the road. Only unmarked up, plain text left.
*/

static void text_process_plain(struct msg2html_textplain_info *info,
			       const char32_t *uc,
			       size_t cnt)
{
	/* Set any requested text decorations that should be active now. */
	set_text_decor(info, info->text_decor_state);

	if (!info->ttline)
	{
		filter(&info->info, uc, cnt);
		return;
	}

	/*
	** Within a <tt>, replace spaces by non-breakable spaces.
	*/

	while (cnt)
	{
		size_t i;

		if (*uc == ' ')
		{
			text_emit_passthru(info, "&nbsp;");
			++uc;
			--cnt;
			continue;
		}

		for (i=0; i<cnt; ++i)
		{
			if (uc[i] == ' ')
				break;
		}

		filter(&info->info, uc, i);
		uc += i;
		cnt -= i;
	}
}

struct msg2html_textplain_info *
msg2html_textplain_start(const char *message_charset,
			 const char *output_character_set,
			 int isflowed,
			 int isdelsp,
			 int isdraft,
			 char *(*get_textlink)(const char *url, void *arg),
			 void *get_textlink_arg,

			 const char *smiley_index,
			 struct msg2html_smiley_list *smileys,
			 int wikifmt,

			 void (*output_func)(const char *p,
					     size_t n, void *arg),
			 void *arg)
{
	struct msg2html_textplain_info *tinfo=
		malloc(sizeof(struct msg2html_textplain_info));

	memset(tinfo, 0, sizeof(*tinfo));

	tinfo->flowed=isflowed;
	tinfo->get_textlink=get_textlink;
	tinfo->get_textlink_arg=get_textlink_arg;
	tinfo->smiley_index=smiley_index;
	tinfo->smileys=smileys;
	tinfo->wikifmt=wikifmt;

	tinfo->text_url_handler=text_contents_notalpha;
	filter_start(&tinfo->info,
		     output_character_set,
		     output_func, arg);

	tinfo->conv_err=0;
	{
		struct rfc3676_parser_info pinfo;

		memset(&pinfo, 0, sizeof(pinfo));

		pinfo.charset=message_charset;
		pinfo.isflowed=isflowed;
		pinfo.isdelsp=isdelsp;
		pinfo.line_begin=text_line_begin;
		pinfo.line_contents=text_line_contents;
		pinfo.line_flowed_notify=text_line_flowed_notify;
		pinfo.line_end=text_line_end;
		pinfo.arg=tinfo;

		tinfo->parser=rfc3676parser_init(&pinfo);
	}

	if (tinfo->parser == NULL)
		tinfo->conv_err=1;

	if (!wikifmt)
	{
		text_emit_passthru(tinfo,
				   isflowed ?
				   "<div class=\"message-text-plain\">":
				   "<pre class=\"message-text-plain\">");
	}

	return tinfo;
}

void msg2html_textplain(struct msg2html_textplain_info *info,
			const char *ptr,
			size_t cnt)
{
	if (info->parser)
		rfc3676parser(info->parser, ptr, cnt);
}

static int msg2html_textplain_trampoline(const char *ptr, size_t cnt, void *arg)
{
	struct msg2html_textplain_info *info=
		(struct msg2html_textplain_info *)arg;

	msg2html_textplain(info, ptr, cnt);
	return 0;
}

int msg2html_textplain_end(struct msg2html_textplain_info *tinfo)
{
	int errptr;

	if (tinfo->parser)
	{
		rfc3676parser_deinit(tinfo->parser, &errptr);

		if (errptr)
			tinfo->conv_err=1;
	}

	text_set_quote_level(tinfo, 0);
	text_set_list_level(tinfo, "", 0);
	text_close_paragraph(tinfo);

	if (!tinfo->wikifmt)
	{
		text_emit_passthru(tinfo, tinfo->flowed ? "</div><br />\n":
				   "</pre><br />\n");
	}

	filter_end(&tinfo->info);

	if (tinfo->info.conversion_error)
		tinfo->conv_err=1;

	errptr=tinfo->conv_err;

	free(tinfo);
	return errptr;
}

static void output_html_func(const char *p, size_t n, void *dummy)
{
        if (fwrite(p, 1, n, stdout) != n)
                ; /* ignore */
}

static void showtextplain(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
			  struct msg2html_info *info)
{
	int rc;

	const char *mime_charset, *dummy;

	int isflowed;
	int isdelsp;

	struct msg2html_textplain_info *tinfo;
	struct rfc2045src *src;

	rfc2045_mimeinfo(rfc, &dummy, &dummy, &mime_charset);

	isflowed=rfc2045_isflowed(rfc);
	isdelsp=0;

	if (isflowed)
		isdelsp=rfc2045_isdelsp(rfc);

	if (info->noflowedtext)
		isflowed=isdelsp=0;

	tinfo=msg2html_textplain_start(mime_charset,
				       info->output_character_set,
				       isflowed, isdelsp,
				       info->is_preview_mode,
				       info->get_textlink,
				       info->arg,
				       info->smiley_index,
				       info->smileys,
				       0,
				       output_html_func, NULL);

	if (tinfo)
	{
		src=rfc2045src_init_fd(fileno(fp));

		if (src)
		{
			rc=rfc2045_decodemimesection(src, rfc,
						     &msg2html_textplain_trampoline,
						     tinfo);
			rfc2045src_deinit(src);
		}
	}

	rc=msg2html_textplain_end(tinfo);

	fseek(fp, 0L, SEEK_END);
	fseek(fp, 0L, SEEK_SET);	/* Resync stdio with uio */

	if (rc && info->charset_warning)
		(*info->charset_warning)(mime_charset, info->arg);

}



static void showkey(FILE *fp, struct rfc2045 *rfc, struct rfc2045id *id,
		    struct msg2html_info *info)
{
	if (info->application_pgp_keys_action)
		(*info->application_pgp_keys_action)(id);
}

static void (*get_known_handler(struct rfc2045 *mime,
				struct msg2html_info *info))
	(FILE *, struct rfc2045 *, struct rfc2045id *,
	 struct msg2html_info *)
{
const char	*content_type, *dummy;

	rfc2045_mimeinfo(mime, &content_type, &dummy, &dummy);
	if (strncmp(content_type, "multipart/", 10) == 0)
		return ( &showmultipart );

	if (strcmp(content_type, "application/pgp-keys") == 0
	    && info->gpgdir && libmail_gpg_has_gpg(info->gpgdir) == 0)
		return ( &showkey );

	if (mime->content_disposition
	    && strcmp(mime->content_disposition, "attachment") == 0)
		return (0);

	if (strcmp(content_type, "text/plain") == 0 ||
	    strcmp(content_type, "text/rfc822-headers") == 0 ||
	    strcmp(content_type, "text/x-gpg-output") == 0)
		return ( &showtextplain );
	if (strcmp(content_type, "message/delivery-status") == 0)
		return ( &showdsn);
	if (info->showhtml && strcmp(content_type, "text/html") == 0)
		return ( &showtexthtml );
	if (strcmp(content_type, "message/rfc822") == 0)
		return ( &showmsgrfc822);

	return (0);
}

static void (*get_handler(struct rfc2045 *mime,
			  struct msg2html_info *info))
	(FILE *, struct rfc2045 *,
	 struct rfc2045id *,
	 struct msg2html_info *)
{
	void (*func)(FILE *, struct rfc2045 *, struct rfc2045id *,
		     struct msg2html_info *);

	if ((func=get_known_handler(mime, info)) == 0)
		func= &showunknown;

	return (func);
}

static int download_func(const char *, size_t, void *);

static void disposition_attachment(FILE *fp, const char *p, int attachment)
{
	fprintf(fp, "Content-Disposition: %s; filename=\"",
		attachment ? "attachment":"inline");
	while (*p)
	{
		if (*p == '"' || *p == '\\')
			putc('\\', fp);
		if (!((unsigned char)(*p) < (unsigned char)' '))
			putc(*p, fp);
		p++;
	}
	fprintf(fp, "\"\n");
}


void msg2html_download(FILE *fp, const char *mimeid, int dodownload,
		       const char *system_charset)
{
	struct	rfc2045 *rfc, *part;
	char	buf[BUFSIZ];
	int	n,cnt;
	const char	*content_type, *dummy, *charset;
	off_t	start_pos, end_pos, start_body;
	char	*content_name;
	off_t	ldummy;

	rfc=rfc2045_alloc();

	while ((n=fread(buf, 1, sizeof(buf), fp)) > 0)
		rfc2045_parse(rfc, buf, n);
	rfc2045_parse_partial(rfc);

	part=*mimeid ? rfc2045_find(rfc, mimeid):rfc;
	if (!part)
	{
		rfc2045_free(rfc);
		return;
	}

	rfc2045_mimeinfo(part, &content_type, &dummy, &charset);

	if (rfc2231_udecodeType(part, "name", system_charset,
				&content_name) < 0)
		content_name=NULL;

	if (dodownload)
	{
		char *disposition_filename;
		const char *p;

		if (rfc2231_udecodeDisposition(part, "filename",
					       (strncmp(content_type, "text/",
							5) == 0 ?
						charset:system_charset),
					       &disposition_filename) < 0)
		{
			if (content_name)
				free(content_name);
			disposition_filename=NULL;
		}


		p=disposition_filename;

		if (!p || !*p) p=content_name;
		if (!p || !*p) p="message.dat";
		disposition_attachment(stdout, p, 1);
		content_type="application/octet-stream";
		if (disposition_filename)
			free(disposition_filename);
	} else {
		if (content_name && *content_name)
			disposition_attachment(stdout, content_name, 0);
	}

	printf(
		content_name && *content_name ?
		"Content-Type: %s; charset=%s; name=\"%s\"\n\n":
		"Content-Type: %s; charset=%s\n\n",
		content_type,
		charset,
		content_name ? content_name:"");
	if (content_name)
		free(content_name);

	rfc2045_mimepos(part, &start_pos, &end_pos, &start_body,
		&ldummy, &ldummy);

	if (*mimeid == 0)	/* Download entire message */
	{
		if (fseek(fp, start_pos, SEEK_SET) < 0)
		{
			rfc2045_free(rfc);
			return;
		}

		while (start_pos < end_pos)
		{
			cnt=sizeof(buf);
			if (cnt > end_pos-start_pos)
				cnt=end_pos-start_pos;
			cnt=fread(buf, 1, cnt, fp);
			if (cnt <= 0)	break;
			start_pos += cnt;
			download_func(buf, cnt, NULL);
		}
	}
	else
	{
		if (fseek(fp, start_body, SEEK_SET) < 0)
		{
			rfc2045_free(rfc);
			return;
		}

		rfc2045_cdecode_start(part, &download_func, 0);

		while (start_body < end_pos)
		{
			cnt=sizeof(buf);
			if (cnt > end_pos-start_body)
				cnt=end_pos-start_body;
			cnt=fread(buf, 1, cnt, fp);
			if (cnt <= 0)	break;
			start_body += cnt;
			rfc2045_cdecode(part, buf, cnt);
		}
		rfc2045_cdecode_end(part);
	}
	rfc2045_free(rfc);
}

static int download_func(const char *p, size_t cnt, void *voidptr)
{
	if (fwrite(p, 1, cnt, stdout) != cnt)
		return (-1);
	return (0);
}

void msg2html_showmimeid(struct rfc2045id *idptr, const char *p)
{
	if (!p)
		p="&amp;mimeid=";

	while (idptr)
	{
		printf("%s%d", p, idptr->idnum);
		idptr=idptr->next;
		p=".";
	}
}
