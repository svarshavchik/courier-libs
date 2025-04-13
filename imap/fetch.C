/*
** Copyright 1998 - 2018 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#include	<sys/stat.h>

#include	"imapd.h"
#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"imapscanclient.h"
#include	"fetchinfo.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"
#include	"maildir/config.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildiraclt.h"

#include	<algorithm>

#if SMAP
extern int smapflag;
#endif

static const char unavailable[]=
	"\
From: System Administrator <root@localhost>\n\
Subject: message unavailable\n\n\
This message is no longer available on the server.\n";

unsigned long header_count=0, body_count=0;	/* Total transferred */

extern int current_mailbox_ro;
extern imapscaninfo current_maildir_info;
extern char *rfc2045id(struct rfc2045 *);

extern void snapshot_needed();

extern void msgenvelope(void (*)(const char *, size_t),
			FILE *, struct rfc2045 *);
extern void msgbodystructure( void (*)(const char *, size_t), int,
			      FILE *, struct rfc2045 *);

extern int is_trash(const char *);
extern void get_message_flags(struct imapscanmessageinfo *,
	char *, struct imapflags *);
extern void append_flags(std::string &buf, struct imapflags &flags);

static int fetchitem(FILE **, int *, const fetchinfo *,
		     imapscaninfo *,  unsigned long,
		     struct rfc2045 **, int *);

static void bodystructure(FILE *, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	struct rfc2045 *);

static void body(FILE *, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	struct rfc2045 *);

static void fetchmsgbody(FILE *, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	struct rfc2045 *);

static void dofetchmsgbody(FILE *, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	struct rfc2045 *);

static void envelope(FILE *, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	struct rfc2045 *);

void doflags(FILE *, const fetchinfo *,
	     imapscaninfo *, unsigned long, struct rfc2045 *);

static void internaldate(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

static void uid(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

static void all(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

static void fast(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

static void full(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

static void rfc822size(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long, struct rfc2045 *);

#if 0
static void do_envelope(FILE *, const fetchinfo *,
	struct imapscanmessageinfo *, struct rfc2045 *);
#endif

static void dofetchheadersbuf(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long,
	struct rfc2045 *,
	int (*)(const fetchinfo *fi, const char *));
static void dofetchheadersfile(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long,
	struct rfc2045 *,
	int (*)(const fetchinfo *fi, const char *));

static void print_bodysection_partial(const fetchinfo *,
				      void (*)(const std::string &));
static void print_bodysection_output(const std::string &);

static int dofetchheaderfields(const fetchinfo *, const char *);
static int dofetchheadernotfields(const fetchinfo *, const char *);
static int dofetchheadermime(const fetchinfo *, const char *);

static void dorfc822(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long,
	struct rfc2045 *);

static void rfc822header(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long,
	struct rfc2045 *);

static void rfc822text(FILE *, const fetchinfo *,
	imapscaninfo *, unsigned long,
	struct rfc2045 *);

struct rfc2045 *fetch_alloc_rfc2045(unsigned long, FILE *);
FILE *open_cached_fp(unsigned long);

void fetchflags(unsigned long);

static void fetcherrorprt(const std::string &p)
{
	fprintf(stderr, "%s", p.c_str());
}

static void fetcherror(const char *errmsg,
		const fetchinfo *fi,
		imapscaninfo *info, unsigned long j)
{
	imapscanmessageinfo &mi=info->msgs.at(j);

	fprintf(stderr, "IMAP FETCH ERROR: %s, uid=%u, filename=%s: %s",
		errmsg, (unsigned)getuid(), mi.filename.c_str(),
		fi->name.c_str());
	if (fi->hasbodysection)
		print_bodysection_partial(fi, &fetcherrorprt);
	fprintf(stderr, "\n");
}

std::string get_reflagged_filename(std::string fn, struct imapflags &newflags)
{
	size_t n=fn.rfind(MDIRSEP[0]);

	if (n != fn.npos)
		fn.resize(n);

	fn += MDIRSEP "2,";
	append_flags(fn, newflags);
	return fn;
}

int reflag_filename(struct imapscanmessageinfo *mi, struct imapflags *flags,
	int fd)
{
	int	rc=0;
	struct	imapflags old_flags;
	struct	stat	stat_buf;

	get_message_flags(mi, 0, &old_flags);

	auto p=get_reflagged_filename(mi->filename, *flags);
	std::string q=current_maildir_info.current_mailbox;
	q += "/cur/";
	q += mi->filename;

	std::string r=current_maildir_info.current_mailbox;

	r += "/cur/";
	r += p;

	if (q != r)
	{
		if (maildirquota_countfolder(
			    current_maildir_info.current_mailbox.c_str()
		    ) && old_flags.deleted != flags->deleted
		    && fstat(fd, &stat_buf) == 0)
		{
			struct maildirsize quotainfo;
			int64_t	nbytes;
			unsigned long unbytes;
			int	nmsgs=1;

			if (maildir::parsequota(mi->filename, unbytes))
				nbytes=unbytes;
			else
				nbytes=stat_buf.st_size;
			if ( flags->deleted )
			{
				nbytes= -nbytes;
				nmsgs= -nmsgs;
			}

			if ( maildir_quota_delundel_start(
				     current_maildir_info.current_mailbox
				     .c_str(),
				     &quotainfo,
				     nbytes, nmsgs))
				rc= -1;
			else
				maildir_quota_delundel_end(&quotainfo,
							   nbytes, nmsgs);
		}

		if (rc == 0)
			rename(q.c_str(), r.c_str());

#if SMAP
		snapshot_needed();
#endif
	}
	mi->filename=p;

#if 0
	if (is_sharedsubdir(current_mailbox))
		maildir_shared_updateflags(current_mailbox, p);
#endif

	return (rc);
}

int do_fetch(unsigned long n, int byuid, const std::list<fetchinfo> &filist)
{
	FILE	*fp;
	struct	rfc2045 *rfc2045p;
	int	seen;
	int	open_err;
	int	unicode_err=0;
	int	report_unicode_err=0;

	fp=NULL;
	open_err=0;

	writes("* ");
	writen(n);
	writes(" FETCH (");

	if (byuid)
	{

		if (std::find_if(
			    filist.begin(), filist.end(),
			    [&]
			    (auto &fip)
			    {
				    return fip.name == "UID";
			    })
		    == filist.end())
		{
			writes("UID ");
			writen(current_maildir_info.msgs[n-1].uid);
			writes(" ");
		}
	}
	seen=0;
	rfc2045p=0;

	const char *sep="";

	for (auto &fi:filist)
	{
		writes(sep);
		int rc=fetchitem(&fp, &open_err,
				 &fi, &current_maildir_info, n-1,
				 &rfc2045p, &unicode_err);

		if (rc > 0)
			seen=1;

		sep=" ";
	}
	writes(")\r\n");

	if (open_err)
	{
		writes("* NO [ALERT] Cannot open message ");
		writen(n);
		writes("\r\n");
		return (0);
	}

#if SMAP
	if (!smapflag)
#endif
		if (!current_maildir_info.has_acl(ACL_SEEN[0]))
			seen=0; /* No permissions */

	if (seen && !current_mailbox_ro)
	{
	struct	imapflags	flags;

		get_message_flags(&current_maildir_info.msgs.at(n-1),
				0, &flags);
		if (!flags.seen)
		{
			flags.seen=true;
			reflag_filename(&current_maildir_info.msgs[n-1],&flags,
				fileno(fp));
			current_maildir_info.msgs[n-1].changedflags=1;

			report_unicode_err=unicode_err;
		}
	}

	if (report_unicode_err)
	{
		writes("* OK [ALERT] Message ");
		writen(n);
		writes(" appears to be a Unicode message and your"
		       " E-mail reader did not enable Unicode support."
		       " Please use an E-mail reader that supports"
		       " IMAP with UTF-8 (see"
		       " https://datatracker.ietf.org/doc/html/rfc6855)\r\n");
	}

	if (current_maildir_info.msgs[n-1].changedflags)
		fetchflags(n-1);
	return (0);
}

static int fetchitem(FILE **fp, int *open_err, const fetchinfo *fi,
		     imapscaninfo *i, unsigned long msgnum,
		     struct rfc2045 **mimep,
		     int *unicode_err)
{
	void (*fetchfunc)(FILE *, const fetchinfo *,
			  imapscaninfo *, unsigned long,
			  struct rfc2045 *);
	int	parsemime=0;
	int	rc=0;
	int	do_open=1;
	int	mimecorrectness=0;

	if (fi->name == "ALL")
	{
		parsemime=1;
		fetchfunc= &all;
	}
	else if (fi->name == "BODYSTRUCTURE")
	{
		parsemime=1;
		fetchfunc= &bodystructure;
	}
	else if (fi->name == "BODY")
	{
		parsemime=1;
		fetchfunc= &body;
		if (fi->hasbodysection)
		{
			fetchfunc= &fetchmsgbody;
			mimecorrectness=1;
			rc=1;
		}
	}
	else if (fi->name == "BODY.PEEK")
	{
		parsemime=1;
		mimecorrectness=1;
		fetchfunc= &body;
		if (fi->hasbodysection)
		{
			fetchfunc= &fetchmsgbody;
			mimecorrectness=1;
		}
	}
	else if (fi->name == "ENVELOPE")
	{
		parsemime=1;
		fetchfunc= &envelope;
	}
	else if (fi->name == "FAST")
	{
		parsemime=1;
		fetchfunc= &fast;
	}
	else if (fi->name == "FULL")
	{
		parsemime=1;
		fetchfunc= &full;
	}
	else if (fi->name == "FLAGS")
	{
		fetchfunc= &doflags;
		do_open=0;
	}
	else if (fi->name == "INTERNALDATE")
	{
		fetchfunc= &internaldate;
	}
	else if (fi->name == "RFC822")
	{
		fetchfunc= &dorfc822;
		mimecorrectness=1;
		rc=1;
	}
	else if (fi->name == "RFC822.HEADER")
	{
		fetchfunc= &rfc822header;
		mimecorrectness=1;
	}
	else if (fi->name == "RFC822.SIZE")
	{
		parsemime=1;
		fetchfunc= &rfc822size;
	}
	else if (fi->name == "RFC822.TEXT")
	{
		parsemime=1;
		mimecorrectness=1;
		fetchfunc= &rfc822text;
	}
	else if (fi->name == "UID")
	{
		fetchfunc= &uid;
		do_open=0;
	}
	else	return (0);

	if (do_open && *fp == NULL)
	{
		*fp=open_cached_fp(msgnum);
		if (!*fp)
		{
			*open_err=1;
			return rc;
		}
	}

	if (mimecorrectness && !enabled_utf8)
		parsemime=1;

	if (parsemime && !*mimep)
	{
		*mimep=fetch_alloc_rfc2045(msgnum, *fp);
	}

	if (mimecorrectness && !enabled_utf8 &&
	    ((*mimep)->rfcviolation & RFC2045_ERR8BITHEADER))
	{
		*unicode_err=1;
	}

	(*fetchfunc)(*fp, fi, i, msgnum, *mimep);
	return (rc);
}

static void bodystructure(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	writes("BODYSTRUCTURE ");
	msgbodystructure(writemem, 1, fp, mimep);
}

static void body(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	writes("BODY ");
	msgbodystructure(writemem, 0, fp, mimep);
}

static void envelope(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	writes("ENVELOPE ");
	msgenvelope( &writemem, fp, mimep);
}

void fetchflags(unsigned long n)
{
#if SMAP
	if (smapflag)
	{
		writes("* FETCH ");
		writen(n+1);
	}
	else
#endif
	{
		writes("* ");
		writen(n+1);
		writes(" FETCH (");
	}

	doflags(0, 0, &current_maildir_info, n, 0);

#if SMAP
	if (smapflag)
	{
		writes("\n");
	}
	else
#endif
		writes(")\r\n");
}

void fetchflags_byuid(unsigned long n)
{
	writes("* ");
	writen(n+1);
	writes(" FETCH (");
	uid(0, 0, &current_maildir_info, n, 0);
	writes(" ");
	doflags(0, 0, &current_maildir_info, n, 0);
	writes(")\r\n");
}

void doflags(FILE *fp, const fetchinfo *fi,
	     imapscaninfo *i, unsigned long msgnum,
	     struct rfc2045 *mimep)
{
	char	buf[256];

#if SMAP
	if (smapflag)
	{
		writes(" FLAGS=");
		get_message_flags(&i->msgs.at(msgnum), buf, 0);
		writes(buf);
	}
	else
#endif
	{
		writes("FLAGS ");

		get_message_flags(&i->msgs.at(msgnum), buf, 0);

		writes("(");
		writes(buf);

		if (buf[0])
			strcpy(buf, " ");

		i->msgs.at(msgnum).keywords.enumerate(
			[&]
			(const std::string &kw)
			{
				writes(buf);
				strcpy(buf, " ");
				writes(kw.c_str());
			});
		writes(")");
	}

	i->msgs.at(msgnum).changedflags=0;
}

static void internaldate(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
struct	stat	stat_buf;
char	buf[256];
char	*p, *q;

	writes("INTERNALDATE ");
	if (fstat(fileno(fp), &stat_buf) == 0)
	{
		rfc822_mkdate_buf(stat_buf.st_mtime, buf);

		/* Convert RFC822 date to imap date */

		p=strchr(buf, ',');
		if (p)	++p;
		else	p=buf;
		while (*p == ' ')	++p;
		if ((q=strchr(p, ' ')) != 0)	*q++='-';
		if ((q=strchr(p, ' ')) != 0)	*q++='-';
		writes("\"");
		writes(p);
		writes("\"");
	}
	else
		writes("NIL");
}

static void uid(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	writes("UID ");
	writen(i->msgs.at(msgnum).uid);
}

static void rfc822size(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
off_t start_pos, end_pos, start_body;
off_t nlines, nbodylines;

	writes("RFC822.SIZE ");

	rfc2045_mimepos(mimep, &start_pos, &end_pos, &start_body,
		&nlines, &nbodylines);

	writen(end_pos - start_pos + nlines);
}

static void all(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	doflags(fp, fi, i, msgnum, mimep);
	writes(" ");
	internaldate(fp, fi, i, msgnum, mimep);
	writes(" ");
	rfc822size(fp, fi, i, msgnum, mimep);
	writes(" ");
	envelope(fp, fi, i, msgnum, mimep);
}

static void fast(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	doflags(fp, fi, i, msgnum, mimep);
	writes(" ");
	internaldate(fp, fi, i, msgnum, mimep);
	writes(" ");
	rfc822size(fp, fi, i, msgnum, mimep);
}

static void full(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	doflags(fp, fi, i, msgnum, mimep);
	writes(" ");
	internaldate(fp, fi, i, msgnum, mimep);
	writes(" ");
	rfc822size(fp, fi, i, msgnum, mimep);
	writes(" ");
	envelope(fp, fi, i, msgnum, mimep);
	writes(" ");
	body(fp, fi, i, msgnum, mimep);
}

static void fetchmsgbody(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	writes("BODY");
	print_bodysection_partial(fi, &print_bodysection_output);
	writes(" ");
	dofetchmsgbody(fp, fi, i, msgnum, mimep);
}

static void print_bodysection_output(const std::string &p)
{
	writes(p.c_str());
}

static void print_bodysection_partial(const fetchinfo *fi,
				      void (*func)(const std::string &))
{
	(*func)("[");
	if (fi->hasbodysection)
	{
		(*func)(fi->bodysection);
		if (!fi->bodysublist.empty())
		{
			const char	*p=" (";

			for (auto &subl:fi->bodysublist)
			{
				(*func)(p);
				p=" ";
				(*func)("\"");
				(*func)(subl.name);
				(*func)("\"");
			}
			(*func)(")");
		}
	}
	(*func)("]");
	if (fi->ispartial)
	{
	char	buf[80];

		sprintf(buf, "<%lu>", (unsigned long)fi->partialstart);
		(*func)(buf);
	}
}

static void dofetchmsgbody(FILE *fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	struct rfc2045 *mimep)
{
	const char *p=fi->hasbodysection ? fi->bodysection.c_str():nullptr;
	off_t start_pos, end_pos, start_body;
	off_t nlines, nbodylines;
	unsigned long cnt;
	char	buf[BUFSIZ];
	char	rbuf[BUFSIZ];
	char	*rbufptr;
	int	rbufleft;
	unsigned long bufptr;
	unsigned long skipping;
	int	ismsgrfc822=1;

	off_t start_seek_pos;
	struct rfc2045 *headermimep;

/*
** To optimize consecutive FETCHes, we cache our virtual and physical
** position.  What we do is that on the first fetch we count off the
** characters we read, and keep track of both the physical and the CRLF-based
** offset into the message.  Then, on subsequent FETCHes, we attempt to
** use that information.
*/

off_t cnt_virtual_chars;
off_t cnt_phys_chars;

off_t cache_virtual_chars;
off_t cache_phys_chars;

	headermimep=mimep;

	while (p && isdigit((int)(unsigned char)*p))
	{
	unsigned long n=0;

		headermimep=mimep;

		do
		{
			n=n*10 + (*p++ - '0');
		} while (isdigit((int)(unsigned char)*p));

		if (mimep)
		{
			if (ismsgrfc822)
			{
				const char *ct, *dummy;

				if (mimep->firstpart == 0)
				{
					/* Not a multipart, n must be 1 */
					if (n != 1)
						mimep=0;
					if (*p == '.')
						++p;
					continue;
				}
				ismsgrfc822=0;

				rfc2045_mimeinfo(mimep, &ct,
						 &dummy,
						 &dummy);

				if (ct && strcasecmp(ct, "message/rfc822"
						     ) == 0)
					ismsgrfc822=1;
				/* The content is another message/rfc822 */
			}

			mimep=mimep->firstpart;
			while (mimep)
			{
				if (!mimep->isdummy && --n == 0)
					break;
				mimep=mimep->next;
			}
			headermimep=mimep;

			if (mimep && mimep->firstpart &&
				!mimep->firstpart->isdummy)
				/* This is a message/rfc822 part */
			{
				if (!*p)
					break;

				mimep=mimep->firstpart;
				ismsgrfc822=1;
			}
		}
		if (*p == '.')
			++p;
	}

	if (p && strcmp(p, "MIME") == 0)
		mimep=headermimep;

	if (mimep == 0)
	{
		writes("{0}\r\n");
		return;
	}

	rfc2045_mimepos(mimep, &start_pos, &end_pos, &start_body,
		&nlines, &nbodylines);


	if (p && strcmp(p, "TEXT") == 0)
	{
		start_seek_pos=start_body;
		cnt=end_pos - start_body + nbodylines;
	}
	else if (p && strcmp(p, "HEADER") == 0)
	{
		start_seek_pos=start_pos;
		cnt= start_body - start_pos + (nlines - nbodylines);
	}
	else if (p && strcmp(p, "HEADER.FIELDS") == 0)
	{
		if (start_body - start_pos <= BUFSIZ)
			dofetchheadersbuf(fp, fi, i, msgnum, mimep,
				&dofetchheaderfields);
		else
			dofetchheadersfile(fp, fi, i, msgnum, mimep,
				&dofetchheaderfields);
		return;
	}
	else if (p && strcmp(p, "HEADER.FIELDS.NOT") == 0)
	{
		if (start_body - start_pos <= BUFSIZ)
			dofetchheadersbuf(fp, fi, i, msgnum, mimep,
				&dofetchheadernotfields);
		else
			dofetchheadersfile(fp, fi, i, msgnum, mimep,
				&dofetchheadernotfields);
		return;
	}
	else if (p && strcmp(p, "MIME") == 0)
	{
		if (start_body - start_pos <= BUFSIZ)
			dofetchheadersbuf(fp, fi, i, msgnum, mimep,
				&dofetchheadermime);
		else
			dofetchheadersfile(fp, fi, i, msgnum, mimep,
				&dofetchheadermime);
		return;
	}
	else if (fi->bodysection.empty())
	{
		start_seek_pos=start_pos;

		cnt= end_pos - start_pos + nlines;
	}
	else	/* Last possibility: entire body */
	{
		start_seek_pos=start_body;

		cnt= end_pos - start_body + nbodylines;
	}

	skipping=0;
	if (fi->ispartial)
	{
		skipping=fi->partialstart;
		if (skipping > cnt)	skipping=cnt;
		cnt -= skipping;
		if (fi->ispartial > 1 && cnt > fi->partialend)
			cnt=fi->partialend;
	}

	if (get_cached_offsets(start_seek_pos, &cnt_virtual_chars,
			       &cnt_phys_chars) == 0 &&
	    cnt_virtual_chars <= (off_t)skipping) /* Yeah - cache it, baby! */
	{
		if (fseek(fp, start_seek_pos+cnt_phys_chars, SEEK_SET) == -1)
		{
			writes("{0}\r\n");
			fetcherror("fseek", fi, i, msgnum);
			return;
		}
		skipping -= cnt_virtual_chars;
	}
	else
	{
		if (fseek(fp, start_seek_pos, SEEK_SET) == -1)
		{
			writes("{0}\r\n");
			fetcherror("fseek", fi, i, msgnum);
			return;
		}

		cnt_virtual_chars=0;
		cnt_phys_chars=0;
	}

	cache_virtual_chars=cnt_virtual_chars;
	cache_phys_chars=cnt_phys_chars;

	writes("{");
	writen(cnt);
	writes("}\r\n");
	bufptr=0;
	writeflush();

	rbufptr=0;
	rbufleft=0;

	while (cnt)
	{
	int	c;

		if (!rbufleft)
		{
			rbufleft=fread(rbuf, 1, sizeof(rbuf), fp);
			if (rbufleft < 0)	rbufleft=0;
			rbufptr=rbuf;
		}

		if (!rbufleft)
		{
			fetcherror("unexpected EOF", fi, i, msgnum);
			_exit(1);
		}

		--rbufleft;
		c=(int)(unsigned char)*rbufptr++;
		++cnt_phys_chars;

		if (c == '\n')
		{
			++cnt_virtual_chars;

			if (skipping)
				--skipping;
			else
			{
				if (bufptr >= sizeof(buf))
				{
					writemem(buf, sizeof(buf));
					bufptr=0;
					/*writeflush();*/
				}
				buf[bufptr++]='\r';
				--cnt;

				if (cnt == 0)
					break;
			}
		}

		++cnt_virtual_chars;
		if (skipping)
			--skipping;
		else
		{
			++body_count;

			if (bufptr >= sizeof(buf))
			{
				writemem(buf, sizeof(buf));
				bufptr=0;
				/*writeflush();*/
			}
			buf[bufptr++]=c;
			--cnt;
		}
		cache_virtual_chars=cnt_virtual_chars;
		cache_phys_chars=cnt_phys_chars;
	}
	writemem(buf, bufptr);
	writeflush();
	save_cached_offsets(start_seek_pos, cache_virtual_chars,
			    cache_phys_chars);
}

static int dofetchheaderfields(const fetchinfo *fi, const char *name)
{
	for (auto &subitem:fi->bodysublist)
	{
		int	i, a, b;

		if (subitem.name.empty())	continue;
		for (i=0; subitem.name[i] && name[i]; i++)
		{
			a=(unsigned char)name[i];
			a=toupper(a);
			b=subitem.name[i];
			b=toupper(b);
			if (a != b)	break;
		}
		if (subitem.name[i] == 0 && name[i] == 0)	return (1);
	}

	return (0);
}

static int dofetchheadernotfields(const fetchinfo *fi, const char *name)
{
	return (!dofetchheaderfields(fi, name));
}

static int dofetchheadermime(const fetchinfo *fi, const char *name)
{
	size_t i;
	int	a;
	static const char mv[]="MIME-VERSION";

	for (i=0; i<sizeof(mv)-1; i++)
	{
		a= (unsigned char)name[i];
		a=toupper(a);
		if (a != mv[i])	break;
	}
	if (mv[i] == 0 && name[i] == 0)	return (1);

	for (i=0; i<8; i++)
	{
		a= (unsigned char)name[i];
		a=toupper(a);
		if (a != "CONTENT-"[i])	return (0);
	}
	return (1);
}

static void dofetchheadersbuf(FILE *fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	struct rfc2045 *mimep,
	int (*headerfunc)(const fetchinfo *fi, const char *))
{
off_t start_pos, end_pos, start_body;
off_t nlines, nbodylines;
size_t i,j,k,l;
char	buf[BUFSIZ+2];
int	goodheader;
unsigned long skipping;
unsigned long cnt;
char	*p;
int	ii;

	rfc2045_mimepos(mimep, &start_pos, &end_pos, &start_body,
		&nlines, &nbodylines);
	if (fseek(fp, start_pos, SEEK_SET) == -1)
	{
		writes("{0}\r\n");
		fetcherror("fseek", fi, info, msgnum);
		return;
	}

	ii=fread(buf, 1, start_body - start_pos, fp);
	if (ii < 0 || (i=ii) != (size_t)(start_body - start_pos))
	{
		fetcherror("unexpected EOF", fi, info, msgnum);
		exit(1);
	}
	goodheader= (*headerfunc)(fi, "");

	l=0;
	for (j=0; j<i; )
	{
		if (buf[j] != '\n' && buf[j] != '\r' &&
			!isspace((int)(unsigned char)buf[j]))
		{
			goodheader= (*headerfunc)(fi, "");

			for (k=j; k<i; k++)
			{
				if (buf[k] == '\n' || buf[k] == ':')
					break;
			}

			if (k < i && buf[k] == ':')
			{
				buf[k]=0;
				goodheader=(*headerfunc)(fi, buf+j);
				buf[k]=':';
			}
		}
		else if (buf[j] == '\n')
			goodheader=0;

		for (k=j; k<i; k++)
			if (buf[k] == '\n')
			{
				++k;
				break;
			}

		if (goodheader)
		{
			while (j<k)
				buf[l++]=buf[j++];
		}
		j=k;
	}

	buf[l++]='\n';	/* Always append a blank line */

	cnt=l;
	for (i=0; i<l; i++)
		if (buf[i] == '\n')	++cnt;

	skipping=0;
	if (fi->ispartial)
	{
		skipping=fi->partialstart;
		if (skipping > cnt)	skipping=cnt;
		cnt -= skipping;
		if (fi->ispartial > 1 && cnt > fi->partialend)
			cnt=fi->partialend;
	}

	writes("{");
	writen(cnt);
	writes("}\r\n");
	p=buf;
	while (skipping)
	{
		if (*p == '\n')
		{
			--skipping;
			if (skipping == 0)
			{
				if (cnt)
				{
					writes("\n");
					--cnt;
				}
				break;
			}
		}
		--skipping;
		++p;
	}

	while (cnt)
	{
		if (*p == '\n')
		{
			writes("\r");
			if (--cnt == 0)	break;
			writes("\n");
			--cnt;
			++p;
			continue;
		}
		for (i=0; i<cnt; i++)
			if (p[i] == '\n')
				break;
		writemem(p, i);
		p += i;
		cnt -= i;
		header_count += i;
	}
}

struct fetchheaderinfo {
	unsigned long skipping;
	unsigned long cnt;
	} ;

static void countheader(struct fetchheaderinfo *, const char *, size_t);

static void printheader(struct fetchheaderinfo *, const char *, size_t);

static void dofetchheadersfile(FILE *fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	struct rfc2045 *mimep,
	int (*headerfunc)(const fetchinfo *fi, const char *))
{
	off_t start_pos, end_pos, start_body;
	off_t nlines, nbodylines;
	size_t i, left;
	int	c, pass;
	char	buf1[256];
	int	goodheader;
	struct	fetchheaderinfo finfo;

	finfo.cnt=0;
	for (pass=0; pass<2; pass++)
	{
	void (*func)(struct fetchheaderinfo *, const char *, size_t)=
			pass ? printheader:countheader;

		rfc2045_mimepos(mimep, &start_pos, &end_pos, &start_body,
			&nlines, &nbodylines);
		if (fseek(fp, start_pos, SEEK_SET) == -1)
		{
			writes("{0}\r\n");
			fetcherror("fseek", fi, info, msgnum);
			return;
		}
		if (pass)
		{
			finfo.skipping=0;
			if (fi->ispartial)
			{
				finfo.skipping=fi->partialstart;
				if (finfo.skipping > finfo.cnt)
					finfo.skipping=finfo.cnt;
				finfo.cnt -= finfo.skipping;
				if (fi->ispartial > 1 &&
					finfo.cnt > fi->partialend)
					finfo.cnt=fi->partialend;
			}

			writes("{");
			writen(finfo.cnt+2);	/* BUG */
			writes("}\r\n");
		}
		left=start_body - start_pos;

		goodheader= (*headerfunc)(fi, "");
		while (left)
		{
			for (i=0; i<sizeof(buf1)-1 && i<left; i++)
			{
				c=getc(fp);
				if (c == EOF)
				{
					fetcherror("unexpected EOF", fi, info, msgnum);
					_exit(1);
				}

				if (c == '\n' || c == ':')
				{
					ungetc(c, fp);
					break;
				}
				buf1[i]=c;
			}
			buf1[i]=0;
			left -= i;

			if (buf1[0] != '\n' && buf1[0] != '\r' &&
				!isspace((int)(unsigned char)buf1[0]))
				goodheader= (*headerfunc)(fi, buf1);
			else if (buf1[0] == '\n')
				goodheader=0;

			if (!goodheader)
			{
				while (left)
				{
					c=getc(fp);
					--left;
					if (c == EOF)
					{
						fetcherror("unexpected EOF", fi, info, msgnum);
						_exit(1);
					}
					if (c == '\n')	break;
				}
				continue;
			}

			(*func)(&finfo, buf1, i);

			i=0;
			while (left)
			{
				c=getc(fp);
				if (c == EOF)
				{
					fetcherror("unexpected EOF", fi, info, msgnum);
					_exit(1);
				}
				--left;
				if (i >= sizeof(buf1))
				{
					(*func)(&finfo, buf1, i);
					i=0;
				}
				if (c == '\n')
				{
					(*func)(&finfo, buf1, i);
					buf1[0]='\r';
					i=1;
				}
				buf1[i++]=c;
				if (c == '\n')	break;
			}
			(*func)(&finfo, buf1, i);
			if (pass && finfo.cnt == 0)	break;
		}
	}
	writes("\r\n");	/* BUG */
}

static void countheader(struct fetchheaderinfo *fi, const char *p, size_t s)
{
	fi->cnt += s;
}

static void printheader(struct fetchheaderinfo *fi, const char *p, size_t s)
{
	size_t i;

	if (fi->skipping)
	{
		if (fi->skipping > s)
		{
			fi->skipping -= s;
			return;
		}
		p += fi->skipping;
		s -= fi->skipping;
		fi->skipping=0;
	}
	if (s > fi->cnt)	s=fi->cnt;
	for (i=0; i <= s; i++)
		if (p[i] != '\r')
			++header_count;
	writemem(p, s);
	fi->cnt -= s;
}

static void dorfc822(FILE *fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	struct rfc2045 *rfcp)
{
unsigned long n=0;
int	c;
char	buf[BUFSIZ];
unsigned long i;

	writes("RFC822 ");

	if (fseek(fp, 0L, SEEK_SET) == -1)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}
	while ((c=getc(fp)) != EOF)
	{
		++n;
		if (c == '\n')	++n;
	}

	if (fseek(fp, 0L, SEEK_SET) == -1)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}
	writes("{");
	writen(n);
	writes("}\r\n");

	i=0;
	while (n)
	{
		c=getc(fp);
		if (c == '\n')
		{
			if (i >= sizeof(buf))
			{
				writemem(buf, i);
				i=0;
			}
			buf[i++]='\r';
			if (--n == 0)	break;
		}

		if (i >= sizeof(buf))
		{
			writemem(buf, i);
			i=0;
		}
		buf[i++]=c;
		--n;
		++body_count;
	}
	writemem(buf, i);
}

static void rfc822header(FILE *fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	struct rfc2045 *rfcp)
{
unsigned long n=0;
int	c;
char	buf[BUFSIZ];
unsigned long i;
int	eol;

	writes("RFC822.HEADER ");

	if (fseek(fp, 0L, SEEK_SET) == -1)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}

	eol=0;
	while ((c=getc(fp)) != EOF)
	{
		++n;
		if (c != '\n')
		{
			eol=0;
			continue;
		}
		++n;
		if (eol)	break;
		eol=1;
	}

	if (fseek(fp, 0L, SEEK_SET) == -1)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}
	writes("{");
	writen(n);
	writes("}\r\n");

	i=0;
	while (n)
	{
		c=getc(fp);
		if (c == '\n')
		{
			if (i >= sizeof(buf))
			{
				writemem(buf, i);
				i=0;
			}
			buf[i++]='\r';
			if (--n == 0)	break;
		}

		if (i >= sizeof(buf))
		{
			writemem(buf, i);
			i=0;
		}
		buf[i++]=c;
		--n;
		++header_count;
	}
	writemem(buf, i);
}

static void rfc822text(FILE *fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	struct rfc2045 *rfcp)
{
off_t start_pos, end_pos, start_body;
off_t nlines, nbodylines;
unsigned long i;
int	c;
char	buf[BUFSIZ];
unsigned long l;

	writes("RFC822.TEXT {");

	rfc2045_mimepos(rfcp, &start_pos, &end_pos, &start_body,
		&nlines, &nbodylines);

	if (fseek(fp, start_body, SEEK_SET) == -1)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("0}\r\n");
		return;
	}

	i=end_pos - start_body + nbodylines;

	writen(i);
	writes("}\r\n");

	l=0;
	while (i)
	{
		c=getc(fp);
		if (c == EOF)
		{
			fetcherror("unexpected EOF", fi, info, msgnum);
			_exit(1);
		}
		--i;
		if (l >= sizeof(BUFSIZ))
		{
			writemem(buf, l);
			l=0;
		}
		if (c == '\n' && i)
		{
			--i;
			buf[l++]='\r';
			if (l >= sizeof(BUFSIZ))
			{
				writemem(buf, l);
				l=0;
			}
		}
		buf[l++]=c;
		++body_count;
	}
	writemem(buf, l);
}

/*
** Poorly written IMAP clients (read: Netscape Messenger) like to issue
** consecutive partial fetches for downloading large messages.
**
** To save the time of reparsing the MIME structure, we cache it.
*/

static struct rfc2045 *cached_rfc2045p;
static std::string cached_filename;

void fetch_free_cached()
{
	if (cached_rfc2045p)
	{
		rfc2045_free(cached_rfc2045p);
		cached_rfc2045p=0;
		cached_filename.clear();
	}
}

struct rfc2045 *fetch_alloc_rfc2045(unsigned long msgnum, FILE *fp)
{
	if (cached_rfc2045p &&
	    cached_filename == current_maildir_info.msgs.at(msgnum).filename)
		return (cached_rfc2045p);

	fetch_free_cached();

	cached_filename=current_maildir_info.msgs.at(msgnum).filename;

	if (fseek(fp, 0L, SEEK_SET) == -1)
	{
		write_error_exit(0);
		return (0);
	}
	cached_rfc2045p=rfc2045_fromfp(fp);
	if (!cached_rfc2045p)
	{
		cached_filename.clear();
		write_error_exit(0);
	}
	return (cached_rfc2045p);
}

static FILE *cached_fp=0;
static std::string cached_fp_filename;
static off_t cached_base_offset;
static off_t cached_virtual_offset;
static off_t cached_phys_offset;

FILE *open_cached_fp(unsigned long msgnum)
{
	int	fd;

	if (cached_fp && cached_fp_filename ==
	    current_maildir_info.msgs.at(msgnum).filename)
		return (cached_fp);

	if (cached_fp)
	{
		fclose(cached_fp);
		cached_fp_filename.clear();
		cached_fp=0;
	}

	fd=imapscan_openfile(&current_maildir_info, msgnum);
	if (fd < 0 || (cached_fp=fdopen(fd, "r")) == 0)
	{
		if (fd >= 0)	close(fd);

		if (fd <0 && errno == ENOENT && (cached_fp=tmpfile()) != 0)
		{
			fprintf(cached_fp, unavailable);
			if (fseek(cached_fp, 0L, SEEK_SET) < 0 ||
			    ferror(cached_fp))
			{
				fclose(cached_fp);
				cached_fp=0;
			}
		}

		if (cached_fp == 0)
		{
			fprintf(stderr, "ERR: %s: %s\n",
				getenv("AUTHENTICATED"),
#if	HAVE_STRERROR
				strerror(errno)
#else
				"error"
#endif

				);
			fflush(stderr);
			_exit(1);
		}
	}

	cached_fp_filename=current_maildir_info.msgs.at(msgnum).filename;

	cached_base_offset=0;
	cached_virtual_offset=0;
	cached_phys_offset=0;
	return (cached_fp);
}

void fetch_free_cache()
{
	if (cached_fp)
	{
		fclose(cached_fp);
		cached_fp=0;
		cached_fp_filename.clear();
	}
}

void save_cached_offsets(off_t base, off_t virt, off_t phys)
{
	cached_base_offset=base;
	cached_virtual_offset=virt;
	cached_phys_offset=phys;
}

int get_cached_offsets(off_t base, off_t *virt, off_t *phys)
{
	if (!cached_fp)
		return (-1);
	if (base != cached_base_offset)
		return (-1);

	*virt=cached_virtual_offset;
	*phys=cached_phys_offset;
	return (0);
}
