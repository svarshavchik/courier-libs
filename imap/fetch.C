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
#include	<memory>
#include	<sstream>

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
extern char *rfc2045id(const rfc2045::entity &);

extern void snapshot_needed();

extern void msgenvelope(void (*)(const char *, size_t),
			rfc822::fdstreambuf &, const rfc2045::entity &);
extern void msgbodystructure( void (*)(const char *, size_t), int,
			      rfc822::fdstreambuf &, const rfc2045::entity &);

extern int is_trash(const char *);
extern void get_message_flags(struct imapscanmessageinfo *,
	char *, struct imapflags *);
extern void append_flags(std::string &buf, struct imapflags &flags);

static int fetchitem(rfc822::fdstreambuf *&, int *, const fetchinfo *,
		     imapscaninfo *,  unsigned long,
		     const rfc2045::entity *&, int *);

static void bodystructure(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	const rfc2045::entity &);

static void body(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	const rfc2045::entity &);

static void fetchmsgbody(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	const rfc2045::entity &);

static void dofetchmsgbody(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	const rfc2045::entity *);

static void envelope(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *,  unsigned long,
	const rfc2045::entity &);

void doflags(rfc822::fdstreambuf &, const fetchinfo *,
	     imapscaninfo *, unsigned long, const rfc2045::entity &);

void doflags(imapscaninfo *, unsigned long);

static void internaldate(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

static void uid(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

static void uid(imapscaninfo *, unsigned long);

static void all(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

static void fast(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

static void full(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

static void rfc822size(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long, const rfc2045::entity &);

#if 0
static void do_envelope(rfc822::fdstreambuf &, const fetchinfo *,
	struct imapscanmessageinfo *, const rfc2045::entity &);
#endif

static void dofetchheadersfile(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long,
	const rfc2045::entity &,
	int (*)(const fetchinfo *fi, const char *));

static void print_bodysection_partial(const fetchinfo *,
				      void (*)(const std::string &));
static void print_bodysection_output(const std::string &);

static int dofetchheaderfields(const fetchinfo *, const char *);
static int dofetchheadernotfields(const fetchinfo *, const char *);
static int dofetchheadermime(const fetchinfo *, const char *);

static void dorfc822(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long,
	const rfc2045::entity &);

static void rfc822header(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long,
	const rfc2045::entity &);

static void rfc822text(rfc822::fdstreambuf &, const fetchinfo *,
	imapscaninfo *, unsigned long,
	const rfc2045::entity &);

const rfc2045::entity &fetch_alloc_rfc2045(
	unsigned long,
	rfc822::fdstreambuf &
);
rfc822::fdstreambuf &open_cached_fp(unsigned long);

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
	rfc822::fdstreambuf	*fp;
	const rfc2045::entity	*rfc2045p;
	int	seen;
	int	open_err;
	int	unicode_err=0;
	int	report_unicode_err=0;

	fp=nullptr;
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
	rfc2045p=nullptr;

	const char *sep="";

	for (auto &fi:filist)
	{
		writes(sep);
		int rc=fetchitem(fp, &open_err,
				 &fi, &current_maildir_info, n-1,
				 rfc2045p, &unicode_err);

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
				fp->fileno());
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

static int fetchitem(
	rfc822::fdstreambuf *&fp,
	int *open_err,
	const fetchinfo *fi,
	imapscaninfo *i,
	unsigned long msgnum,
	const rfc2045::entity *&mimep,
	int *unicode_err)
{
	void (*fetchfunc)(rfc822::fdstreambuf &, const fetchinfo *,
			  imapscaninfo *, unsigned long,
			  const rfc2045::entity &);
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

	if (do_open && !fp)
	{
		fp=&open_cached_fp(msgnum);
		if (fp->error())
		{
			*open_err=1;
			return rc;
		}
	}

	if (mimecorrectness && !enabled_utf8)
		parsemime=1;

	if (parsemime && !mimep)
	{
		mimep=&fetch_alloc_rfc2045(msgnum, *fp);
	}

	if (mimecorrectness && !enabled_utf8 &&
	    (mimep->errors.code & RFC2045_ERR8BITHEADER))
	{
		*unicode_err=1;
	}

	(*fetchfunc)(*fp, fi, i, msgnum, *mimep);
	return (rc);
}

static void bodystructure(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	writes("BODYSTRUCTURE ");
	msgbodystructure(writemem, 1, fp, mimep);
}

static void body(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	writes("BODY ");
	msgbodystructure(writemem, 0, fp, mimep);
}

static void envelope(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
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

	doflags(&current_maildir_info, n);

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
	uid(&current_maildir_info, n);
	writes(" ");
	doflags(&current_maildir_info, n);
	writes(")\r\n");
}

void doflags(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	     imapscaninfo *i, unsigned long msgnum,
	     const rfc2045::entity &mimep)
{
	doflags(i, msgnum);
}

void doflags(imapscaninfo *i, unsigned long msgnum)
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

static void internaldate(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
struct	stat	stat_buf;
char	*p, *q;

	writes("INTERNALDATE ");
	if (fstat(fp.fileno(), &stat_buf) == 0)
	{
		auto buf=rfc822::mkdate(stat_buf.st_mtime);

		/* Convert RFC822 date to imap date */

		p=strchr(&buf[0], ',');
		if (p)	++p;
		else	p=&buf[0];
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

static void uid(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	uid(i, msgnum);
}

static void uid(imapscaninfo *i, unsigned long msgnum)
{
	writes("UID ");
	writen(i->msgs.at(msgnum).uid);
}

static void rfc822size(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	writes("RFC822.SIZE ");
	writen(mimep.endbody - mimep.startpos + mimep.nlines);
}

static void all(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	doflags(fp, fi, i, msgnum, mimep);
	writes(" ");
	internaldate(fp, fi, i, msgnum, mimep);
	writes(" ");
	rfc822size(fp, fi, i, msgnum, mimep);
	writes(" ");
	envelope(fp, fi, i, msgnum, mimep);
}

static void fast(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	doflags(fp, fi, i, msgnum, mimep);
	writes(" ");
	internaldate(fp, fi, i, msgnum, mimep);
	writes(" ");
	rfc822size(fp, fi, i, msgnum, mimep);
}

static void full(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
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

static void fetchmsgbody(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
	writes("BODY");
	print_bodysection_partial(fi, &print_bodysection_output);
	writes(" ");
	dofetchmsgbody(fp, fi, i, msgnum, &mimep);
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

static void dofetchmsgbody(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *i, unsigned long msgnum,
	const rfc2045::entity *mimep)
{
	const char *p=fi->hasbodysection ? fi->bodysection.c_str():nullptr;
	unsigned long cnt;
	char	buf[BUFSIZ];
	char	rbuf[BUFSIZ];
	char	*rbufptr;
	int	rbufleft;
	unsigned long bufptr;
	unsigned long skipping;
	int	ismsgrfc822=1;

	off_t start_seek_pos;
	const rfc2045::entity *headermimep=mimep;

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
				if (mimep->subentities.empty())
				{
					/* Not a multipart, n must be 1 */
					if (n != 1)
						mimep=nullptr;
					if (*p == '.')
						++p;
					continue;
				}
				ismsgrfc822=0;

				if (mimep->content_type.value ==
					"message/rfc822")
					ismsgrfc822=1;
				/* The content is another message/rfc822 */
			}

			mimep = mimep->subentities.size() < n ? nullptr:
				&mimep->subentities[n-1];
			headermimep=mimep;

			if (mimep && rfc2045::message_content_type(
				mimep->content_type.value
			) && !mimep->subentities.empty())
			{
				if (!*p)
					break;

				mimep = &mimep->subentities[0];
				ismsgrfc822=1;
			}
		}
		if (*p == '.')
			++p;
	}

	if (p && strcmp(p, "MIME") == 0)
		mimep=headermimep;

	if (mimep == nullptr)
	{
		writes("{0}\r\n");
		return;
	}

	if (p && strcmp(p, "TEXT") == 0)
	{
		start_seek_pos=mimep->startbody;
		cnt=mimep->endbody - mimep->startbody + mimep->nbodylines;
	}
	else if (p && strcmp(p, "HEADER") == 0)
	{
		start_seek_pos=mimep->startpos;
		cnt= mimep->startbody - mimep->startpos + (mimep->nlines -
			mimep->nbodylines);
	}
	else if (p && strcmp(p, "HEADER.FIELDS") == 0)
	{
		dofetchheadersfile(fp, fi, i, msgnum, *mimep,
				&dofetchheaderfields);
		return;
	}
	else if (p && strcmp(p, "HEADER.FIELDS.NOT") == 0)
	{
		dofetchheadersfile(fp, fi, i, msgnum, *mimep,
				&dofetchheadernotfields);
		return;
	}
	else if (p && strcmp(p, "MIME") == 0)
	{
		dofetchheadersfile(fp, fi, i, msgnum, *mimep,
				&dofetchheadermime);
		return;
	}
	else if (fi->bodysection.empty())
	{
		start_seek_pos=mimep->startpos;

		cnt= mimep->endbody - mimep->startpos + mimep->nlines;
	}
	else	/* Last possibility: entire body */
	{
		start_seek_pos=mimep->startbody;

		cnt= mimep->endbody - mimep->startbody + mimep->nbodylines;
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
		if (fp.pubseekpos(start_seek_pos+cnt_phys_chars) !=
		    start_seek_pos+cnt_phys_chars)
		{
			writes("{0}\r\n");
			fetcherror("fseek", fi, i, msgnum);
			return;
		}
		skipping -= cnt_virtual_chars;
	}
	else
	{
		if (fp.pubseekpos(start_seek_pos) !=
		    start_seek_pos)
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
			rbufleft=fp.sgetn(rbuf, sizeof(rbuf));
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

struct fetchheaderinfo {
	unsigned long skipping;
	unsigned long cnt;
	} ;

static void countheader(struct fetchheaderinfo *, const char *, size_t);

static void dofetchheaders(
	std::streambuf &fp,
	off_t header_startpos,
	off_t header_endpos,
	const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &mimep,
	int (*headerfunc)(const fetchinfo *fi, const char *));

static void printheader(struct fetchheaderinfo *, const char *, size_t);

static void dofetchheadersfile(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &mimep,
	int (*headerfunc)(const fetchinfo *fi, const char *))
{
	if (mimep.startbody-mimep.startpos > BUFSIZ)
	{
		dofetchheaders(fp, mimep.startpos, mimep.startbody,
			fi, info, msgnum, mimep, headerfunc);
		return;
	}

	std::stringstream sbuf;

	size_t n=mimep.startbody-mimep.startpos;
	{
		if (static_cast<size_t>(fp.pubseekpos(mimep.startpos))
		    != mimep.startpos)
		{
			writes("{0}\r\n");
			fetcherror("fseek", fi, info, msgnum);
			return;
		}

		char buffer[n];
		size_t rd=fp.sgetn(buffer, n);

		if (rd != n)
		{
			writes("{0}\r\n");
			fetcherror("sgetn", fi, info, msgnum);
			return;
		}
		sbuf << std::string_view{buffer, n};
	}

	dofetchheaders(*sbuf.rdbuf(), 0, n, fi, info, msgnum, mimep, headerfunc);
}

static void dofetchheaders(
	std::streambuf &fp,
	off_t header_startpos,
	off_t header_endpos,
	const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &mimep,
	int (*headerfunc)(const fetchinfo *fi, const char *))
{
	off_t start_pos, start_body;
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

		start_pos=header_startpos;
		start_body=header_endpos;
		if (fp.pubseekpos(start_pos) !=start_pos)
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
				c=fp.sbumpc();
				if (c == EOF)
				{
					fetcherror("unexpected EOF", fi, info, msgnum);
					_exit(1);
				}

				if (c == '\n' || c == ':')
				{
					fp.sungetc();
					break;
				}
				buf1[i]=c;
			}
			buf1[i]=0;
			left -= i;

			if (buf1[0] && buf1[0] != '\r' &&
				!isspace((int)(unsigned char)buf1[0]))
				goodheader= (*headerfunc)(fi, buf1);
			else if (buf1[0] == 0)
				goodheader=0;

			if (!goodheader)
			{
				while (left)
				{
					c=fp.sbumpc();
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
				c=fp.sbumpc();
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
	for (i=0; i < s; i++)
		if (p[i] != '\r')
			++header_count;
	writemem(p, s);
	fi->cnt -= s;
}

static void dorfc822(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &rfcp)
{
unsigned long n=0;
int	c;
char	buf[BUFSIZ];
unsigned long i;

	writes("RFC822 ");

	if (fp.pubseekpos(0) !=0)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}
	while ((c=fp.sbumpc()) != EOF)
	{
		++n;
		if (c == '\n')	++n;
	}

	if (fp.pubseekpos(0) !=0)
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
		c=fp.sbumpc();
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

static void rfc822header(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &rfcp)
{
unsigned long n=0;
int	c;
char	buf[BUFSIZ];
unsigned long i;
int	eol;

	writes("RFC822.HEADER ");

	if (fp.pubseekpos(0) !=0)
	{
		fetcherror("fseek", fi, info, msgnum);
		writes("{0}\r\n");
		return;
	}

	eol=0;
	while ((c=fp.sbumpc()) != EOF)
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

	if (fp.pubseekpos(0) !=0)
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
		c=fp.sbumpc();
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

static void rfc822text(rfc822::fdstreambuf &fp, const fetchinfo *fi,
	imapscaninfo *info, unsigned long msgnum,
	const rfc2045::entity &mimep)
{
off_t start_body;
off_t end_pos;
off_t nbodylines;
unsigned long i;
int	c;
char	buf[BUFSIZ];
unsigned long l;

	writes("RFC822.TEXT {");

	start_body=mimep.startbody;
	end_pos=mimep.endbody;
	nbodylines=mimep.nbodylines;
	if (fp.pubseekpos(start_body) !=start_body)
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
		c=fp.sbumpc();
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

static rfc2045::entity cached_rfc2045p;
static std::string cached_filename;

void fetch_free_cached()
{
	cached_rfc2045p=rfc2045::entity{};
	cached_filename.clear();
}

const rfc2045::entity &fetch_alloc_rfc2045(
	unsigned long msgnum,
	rfc822::fdstreambuf &sb
)
{
	if (cached_filename == current_maildir_info.msgs.at(msgnum).filename)
		return (cached_rfc2045p);

	fetch_free_cached();

	cached_filename=current_maildir_info.msgs.at(msgnum).filename;

	if (sb.pubseekpos(0) != 0)
		write_error_exit(0);

	std::istreambuf_iterator<char> b{&sb}, e;

	rfc2045::entity::line_iter<false>::iter parser{b, e};

	parser.do_parse_rfc2231=false;
	cached_rfc2045p.parse(parser);
	return (cached_rfc2045p);
}

static rfc822::fdstreambuf cached_fp;
static std::string cached_fp_filename;
static off_t cached_base_offset;
static off_t cached_virtual_offset;
static off_t cached_phys_offset;

rfc822::fdstreambuf &open_cached_fp(unsigned long msgnum)
{
	int	fd;

	if (cached_fp_filename ==
	    current_maildir_info.msgs.at(msgnum).filename)
		return (cached_fp);

	cached_fp=rfc822::fdstreambuf{};
	cached_fp_filename.clear();

	fd=imapscan_openfile(&current_maildir_info, msgnum);
	if (fd < 0)
	{
		if (errno == ENOENT &&
			!(cached_fp=rfc822::fdstreambuf::tmpfile()).error())
		{
			std::string_view sv{unavailable};

			if (cached_fp.sputn(sv.data(), sv.size())
			    < (std::streamsize) sv.size() ||
			    cached_fp.pubseekpos(0) != 0 ||
			    cached_fp.error())
			{
				cached_fp=rfc822::fdstreambuf{};
			}
		}

		if (cached_fp.error())
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
	else
	{
		cached_fp=rfc822::fdstreambuf{fd};
	}

	cached_fp_filename=current_maildir_info.msgs.at(msgnum).filename;

	cached_base_offset=0;
	cached_virtual_offset=0;
	cached_phys_offset=0;
	return (cached_fp);
}

void fetch_free_cache()
{
	cached_fp=rfc822::fdstreambuf{};
	cached_fp_filename.clear();
}

void save_cached_offsets(off_t base, off_t virt, off_t phys)
{
	cached_base_offset=base;
	cached_virtual_offset=virt;
	cached_phys_offset=phys;
}

int get_cached_offsets(off_t base, off_t *virt, off_t *phys)
{
	if (cached_fp.error())
		return (-1);
	if (base != cached_base_offset)
		return (-1);

	*virt=cached_virtual_offset;
	*phys=cached_phys_offset;
	return (0);
}
