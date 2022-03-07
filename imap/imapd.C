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
#include	<errno.h>
#include	<ctype.h>
#include	<signal.h>
#include	<fcntl.h>
#include	<pwd.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#if	HAVE_UTIME_H
#include	<utime.h>
#endif
#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#if HAVE_LOCALE_H
#include	<locale.h>
#endif
#if HAVE_SYS_UTSNAME_H
#include	<sys/utsname.h>
#endif

#include	<courierauth.h>
#include	"maildir/maildiraclt.h"
#include	"maildir/maildirnewshared.h"

#include	<sys/types.h>
#include	<sys/stat.h>

#include	"imaptoken.h"
#include	"imapwrite.h"
#include	"imapscanclient.h"

#include	"mysignal.h"
#include	"imapd.h"
#include	"fetchinfo.h"
#include	"searchinfo.h"
#include	"storeinfo.h"
#include	"mailboxlist.h"
#include	"thread.h"
#include	"outbox.h"

#include	"maildir/config.h"
#include	"maildir/maildiraclt.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirrequota.h"
#include	"maildir/maildirgetquota.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirwatch.h"
#include	"maildir/maildirkeywords.h"
#include	"maildir/maildirinfo.h"
#include	"maildir/loginexec.h"
#include	"rfc822/rfc822.h"
#include	"rfc2045/rfc2045.h"

#include	<courier-unicode.h>
#include	"maildir/maildirkeywords.h"
#include	"courierauth.h"

#include	<string>
#include	<algorithm>
#include	<functional>
#include	<vector>
#include	<optional>
#include	<tuple>

#define KEYWORD_IMAPVERBOTTEN " (){%*\"\\]"
#define KEYWORD_SMAPVERBOTTEN ","

extern void fetchflags(unsigned long);
extern void fetchflags_byuid(unsigned long);
extern int do_fetch(unsigned long, int, const std::list<fetchinfo> &);
extern unsigned long header_count, body_count;
extern void fetch_free_cached();
extern void imapscanfail(const char *p);
extern void mainloop();
extern void bye_msg(const char *);

extern time_t start_time;

extern int keywords();
extern int fastkeywords();

extern void initcapability();
extern void imapcapability();
extern int magictrash();

#if SMAP
int smapflag=0;

extern void snapshot_needed();
extern void snapshot_save();
extern void smap();
extern void smap_fetchflags(unsigned long);
#endif

static const char *protocol;

std::string dot_trash = "." TRASH;
std::string trash = TRASH;

FILE *debugfile=0;
#if 0
char *imapscanpath;
#endif

imapscaninfo current_maildir_info{""};
int current_mailbox_ro;

dev_t homedir_dev;
ino_t homedir_ino;

int enabled_utf8=0;

void rfc2045_error(const char *p)
{
	if (write(2, p, strlen(p)) < 0)
		_exit(1);
	_exit(0);
}

void writemailbox(const std::string &mailbox)
{
	if (mailbox.empty())
	{
		writeqs("");
		return;
	}

	auto encoded=maildir::imap_filename_to_foldername(
		enabled_utf8,
		mailbox
	);

	if (encoded.empty())
	{
		fprintf(stderr, "ERR: imap_filename_to_foldername(%s) failed\n",
			mailbox.c_str());
		exit(1);
	}
	writeqs(encoded.c_str());
}

static int uselocks()
{
	const	char *p;

	if ((p=getenv("IMAP_USELOCKS")) != 0 && *p != '1')
		return 0;

	return 1;
}

bool imapmaildirlock(imapscaninfo *scaninfo,
		     const std::string &maildir,
		     const std::function< bool() >&callback)
{
	if (!uselocks())
		return callback();

	maildir::watch::lock lock{scaninfo->watcher};

	return callback();
}

extern int maildirsize_read(const char *,int *,off_t *,unsigned *,unsigned *,struct stat *);

int maildir_info_suppress(const char *maildir)
{
	struct stat stat_buf;

	if (stat(maildir, &stat_buf) < 0 ||
	    /* maildir inaccessible, perhaps another server? */

	    (stat_buf.st_dev == homedir_dev &&
	     stat_buf.st_ino == homedir_ino)
		    /* Exclude ourselves from the shared list */

	    )
	{
		return 1;
	}

	return 0;
}


void quotainfo_out(const char* qroot)
{
	char    quotabuf[QUOTABUFSIZE];
	char	qresult[200]="";
	char	qbuf[200];

	if ((maildir_getquota(".", quotabuf) == 0) && (strcmp(qroot,"ROOT") == 0))
	{
		struct maildirsize quotainfo;

		if (maildir_openquotafile(&quotainfo, ".") == 0)
			maildir_closequotafile(&quotainfo);
		else
			quotainfo.quota.nbytes=quotainfo.size.nbytes=
				quotainfo.quota.nmessages=
				quotainfo.size.nmessages=0;

		if (quotainfo.quota.nbytes > 0)
		{
			sprintf(qbuf,"STORAGE %ld %ld",
				(long)((quotainfo.size.nbytes+1023)/1024),
				(long)((quotainfo.quota.nbytes+1023)/1024));
			strcat(qresult,qbuf);
		}
		if (quotainfo.quota.nmessages > 0)
		{
			sprintf(qbuf,"MESSAGE %d %d",
				quotainfo.size.nmessages,
				quotainfo.quota.nmessages);
			if (strcmp(qresult,"")!=0) strcat(qresult," ");
			strcat(qresult,qbuf);
		}
	}

	writes("* ");
	writes("QUOTA \"");
	writes(qroot);
	writes("\"");
	if (strcmp(qresult,"")!=0)
	{
		writes(" (");
		writes(qresult);
		writes(")");
	};
	writes("\r\n");
}

int is_trash(const char *m)
{
	if (dot_trash != m)
	{
		/*
		 * not trying to delete .Trash but folder inside of .Trash
		 */
		return (0);
	}
	else
	{
		/*
		 * trying to delete .Trash - stop them
		 */
		return (1);
	}
}

void emptythistrash(const std::string &folder, unsigned l)
{
	maildir_getnew(".", folder.c_str(), NULL, NULL);

	std::string dir=maildir::name2dir(".", INBOX "."+folder);

	if (dir.empty())
		return;

	const char *p=dir.c_str();

	// TODO: this is not necessary, this avoids a make check failure.
	// When the C++ branch gets merged this can removed:

	if (strncmp(p, "./", 2) == 0)
		p += 2;

	maildir_purge(p, l * 24 * 60 * 60);
}

void emptytrash()
{
	unsigned l;

	auto all_settings=getenv("IMAP_EMPTYTRASH");

	if (!all_settings)
		return;

	std::string s=all_settings;

	if (s.find(':') == s.npos && s.find(',') == s.npos)
	{
		l=atoi(all_settings);

		if (l <= 0)
			l=1;

		emptythistrash(trash, l);
		return;
	}

	for (auto b=s.begin(), e=s.end(); b != e; )
	{
		if (*b == ',')
		{
			++b;
			continue;
		}

		auto p=b;
		b=std::find(b, e, ',');

		std::string folder{p, b};

		auto n=folder.find(':');

		if (n == folder.npos)
		{
			continue;
		}

		l=atoi(folder.substr(n+1).c_str());
		if (l <= 0)	l=1;

		emptythistrash(folder.substr(0, n), l);
	}
}

#if 0
int is_draft(const char *m)
{
#if 1
	/* Fix some PINE bugs first */

	if (strcmp(m, "." DRAFTS))	return (0);
	return (1);
#else
	return (0);
#endif
}
#endif

static bool is_reserved(const std::string &m)
{
	size_t p=0;

	if (m.size() > 2 && m[0] == '.' && m[1] == '/')
		p += 2;

	if (is_trash(m.substr(p).c_str()))	return (true);
	return (false);
}

int is_reserved_name(const char *name)
{
	if (strncmp(name, INBOX, strlen(INBOX)) == 0)
		return is_trash(name+strlen(INBOX));
	return 0;
}

static std::string decode_valid_mailbox_utf8(const std::string &p,
					     int autosubscribe)
{
	auto mi=maildir::info_imap_find(p, getenv("AUTHENTICATED"));

	if (!mi)
	{
		return "";
	}

	if (mi.regular_maildir())
	{
		auto r=maildir::name2dir(mi.homedir, mi.maildir);

		r += "/.";

		if (access(r.c_str(), 0) == 0)
		{
			r.pop_back();
			r.pop_back();
			return r;
		}

		r.clear();
		return r;
	}

	if (mi.mailbox_type == MAILBOXTYPE_OLDSHARED)
	{
		const char *q;
		char *r;

		if ((q=strchr(p.c_str(), '.')) == NULL)
		{
			errno=EINVAL;
			return "";
		}

		r=maildir_shareddir(".", q+1);
		if (!r)
		{
			errno=EINVAL;
			return "";
		}

		if (access(r, 0) == 0)
		{
			std::string ret{r};
			free(r);
			return ret;
		}

		maildir_shared_subscribe(".", q+1);
		if (access(r, 0) == 0)
		{
			std::string ret{r};
			free(r);
			return ret;
		}
	}
	return "";
}

std::string decode_valid_mailbox(const std::string &mailbox, int autosubscribe)
{
	auto p=maildir::imap_foldername_to_filename(enabled_utf8, mailbox);

	if (p.empty())
	{
		errno=EINVAL;
		return "";
	}

	return decode_valid_mailbox_utf8(p, autosubscribe);
}

class maildir_info_and_mailbox : public maildir::info {

public:
	std::string mailbox;

	maildir_info_and_mailbox() = default;
	maildir_info_and_mailbox(maildir::info &&info,
				 const std::string &mailbox)
		: maildir::info{std::move(info)},
		  mailbox{mailbox}
	{
	}

	using maildir::info::operator bool;
};

maildir_info_and_mailbox get_maildir_info_and_mailbox(const std::string &str)
{
	auto mailbox=maildir::imap_foldername_to_filename(enabled_utf8, str);

	if (mailbox.empty())
		return {};

	maildir_info_and_mailbox ret{maildir::info_imap_find(
			mailbox, getenv("AUTHENTICATED")),
		mailbox};

	return ret;
}

static int decode_date_time(std::string &str, time_t *tret)
{

	/* Convert to format rfc822_parsedt likes */

	for (auto &c:str)
	{
		if (c == ' ')
			break;

		if (c == '-')
			c=' ';
	}

	return (rfc822_parsedate_chk(str.c_str(), tret));
}

bool get_flagname(std::string s, struct imapflags *flags)
{
	if (s.empty() || s[0] != '\\')
		return false;

	std::transform(s.begin(), s.end(),
		       s.begin(),
		       []
		       (char c)
		       {
			       if (c >= 'a' && c <= 'z')
				       c += 'A'-'a';
			       return c;
		       });

	if (s == "\\SEEN")
		flags->seen=1;
	else if (s == "\\ANSWERED")
		flags->answered=1;
	else if (s == "\\DRAFT")
		flags->drafts=1;
	else if (s == "\\DELETED")
		flags->deleted=1;
	else if (s == "\\FLAGGED")
		flags->flagged=1;
	else return false;
	return true;
}

bool valid_keyword(const std::string &kw)
{
	if (!keywords())
		return false;

	/* Check for valid keyword names */

	for (auto p:kw)
	{
		if ((unsigned char)p <= ' '
		    || strchr(KEYWORD_IMAPVERBOTTEN, p))
			return false;
	}
	return true;
}

bool get_flagsAndKeywords(imapflags &flags,
			  mail::keywords::list &keywords)
{
	imaptoken t;

	while ((t=nexttoken_nouc())->tokentype == IT_ATOM)
	{
		if (!get_flagname(t->tokenbuf, &flags))
		{
			if (!valid_keyword(t->tokenbuf))
				return false;

			keywords.insert(t->tokenbuf);
		}
	}
	return t->tokentype == IT_RPAREN;
}

void get_message_flags(
	struct imapscanmessageinfo *mi,
	char *buf, struct imapflags *flags)
{
	const char *DRAFT="\\Draft";
	const char *FLAGGED="\\Flagged";
	const char *REPLIED="\\Answered";
	const char *SEEN="\\Seen";
	const char *DELETED="\\Deleted";
	const char *RECENT="\\Recent";

	const char *SPC=" ";

	if (buf)
		*buf=0;

	if (flags)
		flags->seen=flags->answered=flags->deleted=flags->flagged
		=flags->recent=flags->drafts=0;

	auto p=mi->filename.rfind(MDIRSEP[0]);

	if (p == mi->filename.npos ||
	    mi->filename.size()-p < 2 ||
	    mi->filename[p+1] != '2' ||
	    mi->filename[p+2] != ',') return;

#if SMAP
	if (smapflag)
	{
		SPC=",";
		DRAFT="DRAFT";
		FLAGGED="MARKED";
		REPLIED="REPLIED";
		SEEN="SEEN";
		DELETED="DELETED";
		RECENT="RECENT";
	}
#endif

	if (mi->filename.find('D', p) != mi->filename.npos)
	{
		if (buf) strcat(buf, DRAFT);
		if (flags)  flags->drafts=1;
	}

	if (mi->filename.find('F', p) != mi->filename.npos)
	{
		if (buf) strcat(strcat(buf, *buf ? SPC:""), FLAGGED);
		if (flags)	flags->flagged=1;
	}
	if (mi->filename.find('R', p) != mi->filename.npos)
	{
		if (buf) strcat(strcat(buf, *buf ? SPC:""), REPLIED);
		if (flags)	flags->answered=1;
	}

	if (mi->filename.find('S', p) != mi->filename.npos)
	{
		if (buf) strcat(strcat(buf, *buf ? SPC:""), SEEN);
		if (flags)	flags->seen=1;
	}

	if (mi->filename.find('T', p) != mi->filename.npos)
	{
		if (buf) strcat(strcat(buf, *buf ? SPC:""), DELETED);
		if (flags)	flags->deleted=1;
	}

	if (mi->recentflag)
	{
		if (flags) flags->recent=1;
		if (buf) strcat(strcat(buf, *buf ? SPC:""), RECENT);
	}
}

static std::string parse_mailbox_error(
	const char *tag,
	imaptoken curtoken,
	bool ok_hierarchy,	/* RFC 2060 errata - DELETE can take
				** a trailing hierarchy separator if the
				** IMAP server supports subfolders of
				** a real folder (such as this one).
				*/

	bool autosubscribe)	/* Really DUMB clients that do a LIST,
				** and don't bother to check if a folder is
				** subscribed to, or not (Pine)
				*/
{
	if (curtoken->tokentype != IT_NUMBER &&
		curtoken->tokentype != IT_ATOM &&
		curtoken->tokentype != IT_QUOTED_STRING)
	{
		writes(tag);
		writes(" BAD Invalid command\r\n");
		return ("");
	}

	if (ok_hierarchy && !curtoken->tokenbuf.empty() &&
	    curtoken->tokenbuf.back() == HIERCH)
		curtoken->tokenbuf.pop_back();

	auto ret=decode_valid_mailbox(curtoken->tokenbuf, autosubscribe);

	if (ret.empty())
	{
		writes(tag);
		writes(" NO Mailbox does not exist, or must be subscribed to.\r\n");
	}

	return (ret);
}

/*
		STORE NEW MESSAGE INTO A MAILBOX
*/

void append_flags(std::string &buf, struct imapflags &flags)
{
	if (flags.drafts)	buf += "D";
	if (flags.flagged)	buf += "F";
	if (flags.answered)	buf += "R";
	if (flags.seen)	buf += "S";
	if (flags.deleted)	buf += "T";
}

	/* First, figure out the filenames used in tmp and new */

FILE *maildir_mkfilename(const char *mailbox, struct imapflags *flags,
			 unsigned long s,
			 std::string &tmpname,
			 std::string &newname)
{
	char	uniqbuf[80];
	static unsigned uniqcnt=0;
	FILE	*fp;

	sprintf(uniqbuf, "%u", uniqcnt++);

	maildir::tmpcreate_info createInfo;
	createInfo.openmode=0666;
	createInfo.maildir=mailbox;
	createInfo.uniq=uniqbuf;
	createInfo.msgsize=s;

	const char *p=getenv("HOSTNAME");

	if (p) createInfo.hostname=p;
	createInfo.doordie=true;

	fp=createInfo.fp();

	if (!fp)
		return nullptr;

	tmpname=createInfo.tmpname;
	newname=createInfo.newname;

	std::string suffix = MDIRSEP "2,";

	append_flags(suffix, *flags);

	/* Ok, this message will really go to cur, not new */

	size_t slash=newname.rfind('/');

	static const char curstr[]="cur";

	std::copy(curstr, curstr+3, newname.begin()+(slash-3));
	newname += suffix;

	return fp;
}

void set_time(const std::string &tmpname, time_t timestamp)
{
#if	HAVE_UTIME
	if (timestamp)
	{
	struct	utimbuf ub;

		ub.actime=ub.modtime=timestamp;
		utime(tmpname.c_str(), &ub);
	}
#else
#if	HAVE_UTIMES
	if (timestamp)
	{
	struct	timeval	tv;

		tv.tv_sec=timestamp;
		tv.tv_usec=0;
		utimes(tmpname.c_str(), &tv);
	}
#endif
#endif
}

static std::optional<std::tuple<unsigned long, unsigned long>> store_mailbox(
	const char *tag, const char *mailbox,
	struct	imapflags *flags,
	const mail::keywords::list &keywords,
	time_t	timestamp,
	imaptoken curtoken,
	int *utf8_error)
{
	unsigned long nbytes=curtoken->tokennum;
	std::string tmpname, newname;
	char	*p;
	char    *e;
	FILE	*fp;
	unsigned long n;
	static const char nowrite[]=" NO [ALERT] Cannot create message - no write permission or out of disk space.\r\n";
	int	lastnl;
	int     rb;
	int	errflag;
	struct rfc2045 *rfc2045_parser;
	const char *errmsg=nowrite;

	fp=maildir_mkfilename(mailbox, flags, 0, tmpname, newname);

	if (!fp)
	{
		writes(tag);
		writes(nowrite);
		return std::nullopt;
	}

	writes("+ OK\r\n");
	writeflush();
	lastnl=0;

	rfc2045_parser=rfc2045_alloc();

	while (nbytes)
	{
		read_string(&p, &n, nbytes);
		nbytes -= n;
		if (p[n-1] == '\n') lastnl = 1;
		else lastnl = 0;

		while (n)
		{
			e = (char *)memchr(p, '\r', n);
			if (e && p == e)
			{
				rb=1;
			}
			else if (e)
			{
				rfc2045_parse(rfc2045_parser, p, e-p);
				rb = fwrite(p, 1, e-p, fp);
			}
			else
			{
				rfc2045_parse(rfc2045_parser, p, n);
				rb = fwrite(p, 1, n, fp);
			}
			n -= rb;
			p += rb;
		}
	}
	if (!lastnl)
	{
		putc('\n', fp);
		rfc2045_parse(rfc2045_parser, "\n", 1);
	}

	errflag=0;

	if (fflush(fp) || ferror(fp))
	{
		fprintf(stderr,
                        "ERR: error storing a message, user=%s, errno=%d\n",
                                getenv("AUTHENTICATED"), errno);
		errflag=1;
	}

	if ((rfc2045_parser->rfcviolation & RFC2045_ERR8BITHEADER) &&
	    curtoken->tokentype != IT_LITERAL8_STRING_START)
	{
		/* in order to [ALERT] the client */
		*utf8_error=1;
	}

	rfc2045_free(rfc2045_parser);

	if (errflag)
	{
		fclose(fp);
		unlink(tmpname.c_str());
		writes(tag);
		writes(errmsg);
		return std::nullopt;
	}

	nbytes=ftell(fp);
	if (nbytes == (unsigned long)-1 ||
	    (p=maildir_requota(newname.c_str(), nbytes)) == 0)

	{
		fclose(fp);
		unlink(tmpname.c_str());
		writes(tag);
		writes(nowrite);
		return std::nullopt;
	}

	fclose(fp);

	if (maildirquota_countfolder(mailbox) &&
	    maildirquota_countfile(p))
	{
		struct maildirsize quotainfo;

		if (maildir_quota_add_start(mailbox, &quotainfo, nbytes, 1,
					    getenv("MAILDIRQUOTA")))
		{
			unlink(tmpname.c_str());
			free(p);
			writes(tag);
			writes(" NO [ALERT] You exceeded your mail quota.\r\n");
			return std::nullopt;
		}
		maildir_quota_add_end(&quotainfo, nbytes, 1);
	}

	if (check_outbox(tmpname.c_str(), mailbox))
	{
		unlink(tmpname.c_str());
		writes(tag);
		writes(" NO [ALERT] Unable to send E-mail message.\r\n");
		free(p);
		return std::nullopt;
	}

	std::vector<uidplus_info> new_uidplus_info_vec;

	new_uidplus_info_vec.emplace_back();

	auto &new_uidplus_info=new_uidplus_info_vec.back();
	int rc;

	std::vector<char> tmpname_buf;

	tmpname_buf.reserve(tmpname.size()+1);
	tmpname_buf.insert(tmpname_buf.end(),
			   tmpname.begin(),
			   tmpname.end());
	tmpname_buf.push_back(0);
	new_uidplus_info.tmpfilename=&tmpname_buf[0];
	new_uidplus_info.curfilename=p;
	new_uidplus_info.mtime=timestamp;

	if (!keywords.empty())
	{
		imapscan_updateKeywords(mailbox,
					strrchr(p, '/')+1,
					keywords);
	}

	imapscaninfo new_maildir_info{mailbox};

	rc=imapscan_maildir(&new_maildir_info, 0, 0,
			    new_uidplus_info_vec);

	if (rc)
	{
		free(p);
		writes(tag);
		writes(nowrite);
		return std::nullopt;
	}

	free(p);

	return std::tuple<unsigned long, unsigned long>(
		new_maildir_info.uidv,
		new_uidplus_info.uid
	);
}


/************** Create and delete folders **************/

#if 0
static int checksubdir(const char *s)
{
DIR	*dirp;
struct	dirent *de;

	dirp=opendir(s);
	while (dirp && (de=readdir(dirp)) != 0)
		if (de->d_name[0] != '.')
		{
			closedir(dirp);
			return (1);
		}
	if (dirp)	closedir(dirp);
	return (0);
}
#endif

bool mddelete(const std::string &s)
{
	struct stat stat_buf;

	/* If the top level maildir is a sym link, don't delete it */

	if (stat(s.c_str(), &stat_buf) < 0 &&
	    S_ISLNK(stat_buf.st_mode))
		return false;

	trap_signals();
	int rc=maildir_del(s.c_str());
	if (release_signals())	_exit(0);
	return (rc == 0);
}

int mdcreate(const char *mailbox)
{
	trap_signals();
	if (maildir_make(mailbox, 0700, 0700, 1) < 0)
	{
		if (release_signals())	_exit(0);
		return (-1);
	}

	if (release_signals())	_exit(0);
	return (0);
}

/****************************************************************************/

/* do_msgset parses a message set, and calls a processing func for each msg */

/****************************************************************************/

static bool do_msgset(const std::string &msgset,
		      const std::function<bool (unsigned long)> &msgfunc,
		      bool isuid)
{
	unsigned long i, j;
	unsigned long last=0;

	if (current_maildir_info.msgs.size() > 0)
	{
		last=current_maildir_info.msgs.size();
		if (isuid)
		{
			last=current_maildir_info.msgs[last-1].uid;
		}
	}

	for (auto b=msgset.begin(), e=msgset.end();
	     b != e &&
		     (isdigit((int)(unsigned char)*b) || *b == '*'); )
	{
		i=0;
		if (*b == '*')
		{
			i=last;
			++b;
		}
		else while (b != e && isdigit((int)(unsigned char)*b))
		{
			i=i*10 + (*b++-'0');
		}
		if (b == e || *b != ':')
			j=i;
		else
		{
			j=0;
			++b;
			if (b != e && *b == '*')
			{
				j=last;
				++b;
			}
			else while (b != e && isdigit((int)(unsigned char)*b))
			{
				j=j*10 + (*b++-'0');
			}
		}
		if (j < i)
		{
#if 0
	/* BUGS issue */
			writes("* NO Invalid message set: ");
			writen(i);
			writes(":");
			writen(j);
			writes("\r\n");
#endif
		}
		else if (isuid)
		{
		unsigned long k;

			for (k=0; k<current_maildir_info.msgs.size(); k++)
				if (current_maildir_info.msgs[k].uid >= i)
					break;
			if (k >= current_maildir_info.msgs.size() ||
				current_maildir_info.msgs[k].uid > j)
			{
#if 0
	/* BUGS issue */
				writes("* NO Invalid message: UID ");
				writen(i);
				if (j > i)
				{
					writes(":");
					writen(j);
				}
				writes("\r\n");
#endif
			}
			else while (k < current_maildir_info.msgs.size() &&
				current_maildir_info.msgs[k].uid <= j)
			{
				if (!msgfunc(k+1))
					return (false);
				++k;
			}
		}
		else
		{
			do
			{
				if (i == 0 ||
				    i > current_maildir_info.msgs.size())
				{
					writes("* NO Invalid message sequence number: ");
					writen(i);
					writes("\r\n");
					break;
				}

				if (!msgfunc(i))
					return (false);
			} while (i++ < j);
		}

		if (b == e || *b++ != ',')	break;
	}
	return (true);
}

/** Show currently defined flags and keywords **/

static void mailboxflags(int ro, const mail::keywords::list &keywordlist)
{
#if SMAP
	if (smapflag)
		return;
#endif

	writes("* FLAGS (");

	for (const auto &kw:keywordlist)
	{
		writes(kw.c_str());
		writes(" ");
	}

	writes("\\Draft \\Answered \\Flagged"
	       " \\Deleted \\Seen \\Recent)\r\n");
	writes("* OK [PERMANENTFLAGS (");


	if (ro)
	{
		writes(")] No permanent flags permitted\r\n");
	}
	else
	{
		for (const auto &kw:keywordlist)
		{
			writes(kw.c_str());
			writes(" ");
		}

		if (keywords())
			writes("\\* ");

		writes("\\Draft \\Answered \\Flagged \\Deleted \\Seen)] Limited\r\n");
	}
}

void mailboxflags(int ro)
{
	mailboxflags(ro,
		     current_maildir_info.keywords->enumerate_keywords());
}

/****************************************************************************/

/* Show how many messages are in the mailbox                                */

/****************************************************************************/

static void mailboxmetrics()
{
unsigned long i,j;

#if SMAP
	if (smapflag)
	{
		writes("* EXISTS ");
		writen(current_maildir_info.msgs.size());
		writes("\n");
		return;
	}
#endif


	writes("* ");
	writen(current_maildir_info.msgs.size());
	writes(" EXISTS\r\n");

	writes("* ");
	i=0;

	for (j=0; j<current_maildir_info.msgs.size(); j++)
		if (current_maildir_info.msgs[j].recentflag)
			++i;
	writen(i);
	writes(" RECENT\r\n");
}

/****************************************************************************/

/* Do the NOOP stuff                                                        */

/****************************************************************************/

void doNoop(int real_noop)
{
	unsigned long i, j;
	bool needstats=false;
	unsigned long expunged;
#if SMAP
	unsigned long smap_expunge_count=0;
	unsigned long smap_expunge_bias=0;

	unsigned long smap_last=0;
	unsigned long smap_range=0;

	bool takeSnapshot=true;
#endif

	if (current_maildir_info.current_mailbox.empty())
		return;

	imapscaninfo new_maildir_info{&current_maildir_info};
	if (imapscan_maildir(&new_maildir_info, 0, current_mailbox_ro))
		return;

	j=0;
	expunged=0;
	for (i=0; i<current_maildir_info.msgs.size(); i++)
	{
		while (j < new_maildir_info.msgs.size() &&
			new_maildir_info.msgs[j].uid <
				current_maildir_info.msgs[i].uid)
		{
			/* How did this happen??? */

			new_maildir_info.msgs[j].changedflags=1;
			++j;
			needstats=true;
#if SMAP
			takeSnapshot=false;
#endif
		}

		if (j >= new_maildir_info.msgs.size() ||
			new_maildir_info.msgs[j].uid >
				current_maildir_info.msgs[i].uid)
		{
#if SMAP
			if (smapflag)
			{
				takeSnapshot=false;

				if (smap_expunge_count > 100)
				{
					if (smap_range > 0)
					{
						writes("-");
						writen(smap_last + smap_range
						       - smap_expunge_bias
						       + 1);
					}
					writes("\n");
					smap_expunge_count=0;
				}

				if (smap_expunge_count == 0)
				{
					smap_expunge_bias=expunged;

					writes("* EXPUNGE ");
					writen(i+1-smap_expunge_bias);
					smap_last=i;
					smap_range=0;
				}
				else if (smap_last + smap_range + 1 == i)
				{
					++smap_range;
				}
				else
				{
					if (smap_range > 0)
					{
						writes("-");
						writen(smap_last + smap_range
						       - smap_expunge_bias
						       + 1);
					}
					writes(" ");
					writen(i+1-smap_expunge_bias);
					smap_last=i;
					smap_range=0;
				}
				++smap_expunge_count;
			}
			else
#endif
			{
				writes("* ");
				writen(i+1-expunged);
				writes(" EXPUNGE\r\n");
				needstats=true;
			}

			++expunged;
			continue;
		}

		/* Must be the same */

		if (new_maildir_info.msgs[j].update_from(
			    current_maildir_info.msgs[i]
		    ))
		{
#if SMAP
			takeSnapshot=false;
#endif
		}

		++j;
	}

#if SMAP
	if (smapflag && smap_expunge_count)
	{
		if (smap_range > 0)
		{
			writes("-");
			writen(smap_last + smap_range - smap_expunge_bias + 1);
		}
		writes("\n");
	}

#endif

	while (j < new_maildir_info.msgs.size())
	{
#if SMAP
		if (smapflag)
			takeSnapshot=false;
#endif
		++j;
		needstats=true;
	}

	/**********************************************************
	 **
	 ** current_maildir_info: existing keywords
	 ** new_maildir_info: new keywords
	 **
	 */

	mail::keywords::list all_keywords;
	bool changed=false;
	size_t n_new_keywords=0;

	// Get all existing keywords

	current_maildir_info.keywords->enumerate_keywords(
		[&]
		(const std::string &kw)
		{
			all_keywords.insert(kw);
		});

	// Enumerate the keywords in the new_maildir_info. Count them,
	// and note if there are any new ones.

	new_maildir_info.keywords->enumerate_keywords(
		[&]
		(const std::string &kw)
		{
			if (all_keywords.insert(kw).second)
				changed=true;
			++n_new_keywords;
		});

	if (changed)
		// We added more keywords
		mailboxflags(current_mailbox_ro, all_keywords);

	// If the number of new keywords is different than the number of
	// new and old keywords, it must mean that after all updates are
	// sent we are going to end up deleting unused keywords.

	changed = n_new_keywords != all_keywords.size();

	current_maildir_info=std::move(new_maildir_info);

#if SMAP
	if (takeSnapshot)
	{
		if (real_noop && smapflag)
			snapshot_save();
	}
	else
		snapshot_needed();
#endif

	if (needstats)
		mailboxmetrics();

	for (j=0; j < current_maildir_info.msgs.size(); j++)
		if (current_maildir_info.msgs[j].changedflags)
		{
#if SMAP
			if (smapflag)
				smap_fetchflags(j);
			else
#endif
				fetchflags(j);
		}

	if (changed)
		mailboxflags(current_mailbox_ro);
}

static std::string alloc_filename(const std::string &mbox,
				  const std::string &name)
{
	std::string ret;

	ret.reserve(mbox.size() + name.size() + sizeof("/cur/")-1);

	ret += mbox;
	ret += "/cur/";
	ret += name;
	return ret;
}

/****************************************************************************/

/* Do the ID stuff                                                        */

/****************************************************************************/
static int doId()
{
	const char *ev = getenv("IMAP_ID_FIELDS");
	unsigned int flags=0;
	imaptoken curtoken;

	if (!ev)
		return -1;

	flags = atoi(ev);

	/* The data sent by the client isn't used for anything, but make sure
	 * it is syntactically correct. */
	curtoken = nexttoken();
	switch (curtoken->tokentype) {
	case IT_NIL:
		break;
	case IT_LPAREN:
		{
		unsigned int k = 0;

		curtoken = nexttoken();

		fprintf(stderr, "INFO: ID, user=%s, ip=[%s], port=[%s]",
			getenv("AUTHENTICATED"),
			getenv("TCPREMOTEIP"),
			getenv("TCPREMOTEPORT"));

		while ((k < 30) && (curtoken->tokentype != IT_RPAREN)) {
			k++;
			if (curtoken->tokentype != IT_QUOTED_STRING)
			{
				fprintf(stderr, "\n");
				fflush(stderr);
				return -1;
			}
			fprintf(stderr, ", %s=", curtoken->tokenbuf.c_str());

			curtoken = nexttoken();
			if ((curtoken->tokentype != IT_QUOTED_STRING) &&
					(curtoken->tokentype != IT_NIL))
			{
				fprintf(stderr, "\n");
				fflush(stderr);
				return -1;
			}
			fprintf(stderr, "%s",
				curtoken->tokentype == IT_QUOTED_STRING
				? curtoken->tokenbuf.c_str():"(nil)");
			curtoken = nexttoken();
		}
		fprintf(stderr, "\n");
		fflush(stderr);

		/* no strings sent */
		if (k == 0)
			return -1;

		/* at most 30 pairs allowed */
		if ((k >= 30) && (curtoken->tokentype != IT_RPAREN))
			return -1;

		break;
		}
	default:
		return -1;
	}

	writes("* ID (\"name\" \"Courier-IMAP");

	if (flags & 1) {
		const char *sp = strchr(PROGRAMVERSION, ' ') + 1;
		writes("\" \"version\" \"");
		writemem(sp, strchr(sp, '/') - sp);
	}

#if HAVE_SYS_UTSNAME_H
	if (flags & 6) {
		struct utsname uts;
		if (uname(&uts) == 0)
		{
			if (flags & 2) {
				writes("\" \"os\" \"");
				writeqs(uts.sysname);
			}
			if (flags & 4) {
				writes("\" \"os-version\" \"");
				writeqs(uts.release);
			}

		}
	}
#endif

	writes("\" \"vendor\" \"S. Varshavchik.\")\r\n");

	return 0;
}

/****************************************************************************/

/* Do the IDLE stuff                                                        */

/****************************************************************************/

extern int doidle(time_t, int);

static bool doenhancedidle(maildir::watch &watcher,
			   bool &fallback_mode,
			   const std::function<bool (bool)> &started_handler);
static void imapidle();

void imapenhancedidle()
{
	if (current_maildir_info.current_mailbox.empty())
	{
		imapidle();
		return;
	}

	maildir::watch watcher{current_maildir_info.current_mailbox};

	bool fallback_mode=false;

	bool quit=false;

	std::function<bool (bool)> started_handler=
		[&]
		(bool started)
		{
			if (!started)
			{
				quit=true;
				return false;
			}
#if SMAP
			if (smapflag)
			{
				writes("+OK Entering idle mode\n");
			}
			else
#endif
				writes("+ entering idle mode\r\n");
			doNoop(0);

			return true;
		};

	for (;;)
	{
		if (doenhancedidle(watcher, fallback_mode, started_handler)
		    || quit)
			break;

		started_handler=[](bool){return true;};
		doNoop(0);
	}
}

static bool doenhancedidle(maildir::watch &watcher,
			   bool &fallback_mode,
			   const std::function<bool (bool)> &started_handler)
{
	if (!fallback_mode)
	{
		maildir::watch::contents watching{watcher};

		if (watching.started())
		{
			started_handler(true);

			writeflush();
			int fd;
			int timeout;

			while (!watching.check(fd, timeout))
			{
				if (doidle(timeout, fd))
					return true;
			}
			return false;
		}

		fallback_mode=true;

		if (!started_handler(false))
		{
			writeflush();
			return false;
		}
	}
	writeflush();
	return doidle(60, -1);
}

static void imapidle()
{
	const char * envp = getenv("IMAP_IDLE_TIMEOUT");
	const int idleTimeout = envp ? atoi(envp) : 60;

#if SMAP
	if (smapflag)
	{
		writes("+OK Entering idle mode...\n");
	}
	else
#endif
		writes("+ entering idle mode\r\n");
	doNoop(0);
	writeflush();
	while (!doidle(idleTimeout, -1))
	{
		doNoop(0);
		writeflush();
	}
}

/****************************************************************************/

/* Do the EXPUNGE stuff                                                     */

/****************************************************************************/

void do_expunge(unsigned long from, unsigned long to, int force);

void expunge()
{
	do_expunge(0, current_maildir_info.msgs.size(), 0);
}

void do_expunge(unsigned long expunge_start,
		unsigned long expunge_end,
		int force)
{
unsigned long j;
struct imapflags flags;
int	move_to_trash=0;
struct stat stat_buf;
const char *cp=getenv("IMAP_LOG_DELETIONS");
int log_deletions= cp && *cp != '0';

	fetch_free_cache();

	if (magictrash() &&
	    !is_trash(strncmp(current_maildir_info.current_mailbox.c_str(), "./", 2) == 0?
		      current_maildir_info.current_mailbox.c_str()+2:current_maildir_info.current_mailbox.c_str()))
		move_to_trash=1;

	for (j=expunge_start; j < expunge_end; j++)
	{
		int file_counted=0;

		get_message_flags(&current_maildir_info.msgs.at(j), 0, &flags);

		if (!flags.deleted && !force)
			continue;

		auto old_name=alloc_filename(
			current_maildir_info.current_mailbox.c_str(),
			current_maildir_info.msgs[j].filename);

		if (stat(old_name.c_str(), &stat_buf) < 0)
		{
			continue;
		}

		if (maildirquota_countfolder(current_maildir_info.current_mailbox.c_str()) &&
		    maildirquota_countfile(old_name.c_str()))
			file_counted=1;

		if (is_sharedsubdir(current_maildir_info.current_mailbox.c_str()))
		{
			maildir_unlinksharedmsg(old_name.c_str());
		}
		else if (move_to_trash && current_maildir_info
			 .msgs[j].copiedflag == 0)
		{
			int will_count=0;

			auto new_name=alloc_filename(
				dot_trash,
				current_maildir_info.msgs[j].filename);

			if (maildirquota_countfolder(dot_trash.c_str()))
				will_count=1;

			if (file_counted != will_count)
			{
				unsigned long filesize=0;

				if (maildir_parsequota(old_name.c_str(),
						       &filesize))
				{
					if (stat(old_name.c_str(), &stat_buf))
						stat_buf.st_size=0;
					filesize=(unsigned long)
						stat_buf.st_size;
				}

				maildir_quota_deleted(".",
						      (long)filesize *
						      (will_count
						       -file_counted),
						      will_count
						      -file_counted);
			}

			auto last=new_name.rfind('/');

			if (last == new_name.npos)
				last=0;

			last=new_name.find(MDIRSEP[0], last);

			if (last != new_name.npos &&
			    new_name.substr(last, 3) == MDIRSEP "2,")
			{
				/* Don't mark it as deleted in the Trash */

				auto n=new_name.find('T', last);

				if (n != new_name.npos)
					new_name.erase(n, 1);

				/* Don't mark it as a draft msg in the Trash */

				n=new_name.find('D', last);
				if (n != new_name.npos)
					new_name.erase(n, 1);
			}

			if (log_deletions)
				fprintf(stderr, "INFO: EXPUNGED, user=%s, ip=[%s], port=[%s], old_name=%s, new_name=%s: only new_name will be left\n",
					getenv("AUTHENTICATED"),
					getenv("TCPREMOTEIP"),
					getenv("TCPREMOTEPORT"),
					old_name.c_str(), new_name.c_str());

			if (rename(old_name.c_str(), new_name.c_str()))
			{
				mdcreate(dot_trash.c_str());
				rename(old_name.c_str(), new_name.c_str());
			}

			unlink(old_name.c_str());
		}
		else
		{
			unlink(old_name.c_str());

			if (file_counted)
			{
				unsigned long filesize=0;

				if (maildir_parsequota(old_name.c_str(),
						       &filesize))
				{
					if (stat(old_name.c_str(), &stat_buf))
						stat_buf.st_size=0;
					filesize=(unsigned long)
						stat_buf.st_size;
				}

				maildir_quota_deleted(".",-(long)filesize, -1);
			}
		}

		if (log_deletions)
			fprintf(stderr, "INFO: EXPUNGED, user=%s, ip=[%s], port=[%s], old_name=%s\n",
				getenv("AUTHENTICATED"),
				getenv("TCPREMOTEIP"),
				getenv("TCPREMOTEPORT"),
				old_name.c_str());
	}
}


static FILE *newsubscribefile(std::string &tmpname)
{
	char    uniqbuf[80];
	static  unsigned tmpuniqcnt=0;
	FILE	*fp;
	struct maildir_tmpcreate_info createInfo;

	maildir_tmpcreate_init(&createInfo);

	sprintf(uniqbuf, "imapsubscribe%u", tmpuniqcnt++);

	createInfo.uniq=uniqbuf;
	createInfo.hostname=getenv("HOSTNAME");
	createInfo.doordie=1;

	if ((fp=maildir_tmpcreate_fp(&createInfo)) == NULL)
		write_error_exit(0);

	tmpname=createInfo.tmpname;
	maildir_tmpcreate_free(&createInfo);

	return (fp);
}

static int sub_strcmp(const char *a, const char *b)
{
	if (strncasecmp(a, "inbox", 5) == 0 &&
		strncasecmp(b, "inbox", 5) == 0)
	{
		a += 5;
		b += 5;
	}
	return (strcmp(a, b));
}

static void subscribe(const char *f)
{
	std::string newf;
	FILE *newfp=newsubscribefile(newf);
	FILE *oldfp;

	if ((oldfp=fopen(SUBSCRIBEFILE, "r")) != 0)
	{
	char	buf[BUFSIZ];

		while (fgets(buf, sizeof(buf), oldfp) != 0)
		{
		char *p=strchr(buf, '\n');

			if (p)	*p=0;
			if (sub_strcmp(buf, f) == 0)
			{
				fclose(oldfp);
				fclose(newfp);
				unlink(newf.c_str());
				return;	/* Already subscribed */
			}
			fprintf(newfp, "%s\n", buf);
		}
		fclose(oldfp);
	}
	fprintf(newfp, "%s\n", f);
	if (fflush(newfp) || ferror(newfp))
		write_error_exit(0);
	fclose(newfp);
	rename(newf.c_str(), SUBSCRIBEFILE);
}

static void unsubscribe(const char *f)
{
	std::string newf;
	FILE *newfp=newsubscribefile(newf);
	FILE *oldfp;

	if ((oldfp=fopen(SUBSCRIBEFILE, "r")) != 0)
	{
	char	buf[BUFSIZ];

		while (fgets(buf, sizeof(buf), oldfp) != 0)
		{
		char *p=strchr(buf, '\n');

			if (p)	*p=0;
			if (sub_strcmp(buf, f) == 0)
				continue;
			fprintf(newfp, "%s\n", buf);
		}
		fclose(oldfp);
	}
	if (fflush(newfp) || ferror(newfp))
		write_error_exit(0);
	fclose(newfp);
	rename(newf.c_str(), SUBSCRIBEFILE);
}

void dirsync(std::string folder)
{
#if EXPLICITDIRSYNC
	folder += "/new";

	int fd=open(folder.c_str(), O_RDONLY);

	if (fd >= 0)
	{
		fsync(fd);
		close(fd);
	}

	folder.erase(folder.end()-3, folder.end());

	folder += "/cur";

	fd=open(folder.c_str(), O_RDONLY);

	if (fd >= 0)
	{
		fsync(fd);
		close(fd);
	}

#endif
}

/*
** After adding messages to a maildir, compute their new UIDs.
*/

static int uidplus_fill(const char *mailbox,
			std::vector<uidplus_info> &uidplus_list,
			unsigned long *uidv)
{
	imapscaninfo scan_info{mailbox};

	if (imapscan_maildir(&scan_info, 0, 0, uidplus_list))
		return -1;

	*uidv=scan_info.uidv;

	return 0;
}

static void uidplus_writemsgset(std::vector<uidplus_info> &uidplus_list,
				int new_uids)
{
#define UIDN(u) ( new_uids ? (u)->uid:(u)->old_uid )

	unsigned long uid_start, uid_end;
	const char *comma="";

	writes(" ");
	for (auto b=uidplus_list.begin(), e=uidplus_list.end(); b != e; )
	{
		uid_start=UIDN(b);
		uid_end=uid_start;

		while (b+1 != e &&
		       UIDN(b+1) == uid_end + 1)
		{
			++b;
			++uid_end;
		}

		writes(comma);
		writen(uid_start);
		if (uid_end != uid_start)
		{
			writes(":");
			writen(uid_end);
		}
		comma=",";
		++b;
	}

#undef UIDN
}

static void uidplus_abort(std::vector<uidplus_info> &uidplus)
{
	for (auto &u:uidplus)
	{
		unlink(u.tmpfilename.c_str());
		unlink(u.curfilename.c_str());
	}
}

static void rename_callback(const char *old_path, const char *new_path)
{
	struct stat stat_buf;

	if (stat(new_path, &stat_buf) ||
	    !S_ISDIR(stat_buf.st_mode))
		return; // Not a directory, something in courierimaphieracl

	imapscaninfo minfo{new_path};

	std::string p=new_path;

	p += "/" IMAPDB;
	unlink(p.c_str());
	imapscan_maildir(&minfo, 0,0);
}

static int broken_uidvs()
{
	const char *p=getenv("IMAP_BROKENUIDV");

	return (p && atoi(p) != 0);
}

static void logoutmsg()
{
	bye_msg("INFO: LOGOUT");
}

void bye()
{
	exit(0);
}

std::string get_myrightson(const std::string &mailbox);

static void list_acl(const std::string &,
		     maildir::aclt_list &);

static bool get_acllist(maildir::aclt_list &l,
			const std::string &mailbox);

static void list_myrights(const std::string &mailbox,
			  const std::string &myrights);
static void list_postaddress(const std::string &mailbox);

static void accessdenied(const char *cmd, const std::string &folder,
			 const char *acl_required);

static int list_callback(const mailbox_scan_info &info,
			 const char *cmd)
{
	const char *sep="";
	maildir::aclt_list l;

	std::string myrights;

	/*
	** If we're going to list ACLs, grab the ACLs now, because
	** get_acllist2() may end up calling myrights(), which generates
	** its own output.
	*/

	auto flags=info.flags;

	if (flags & (LIST_MYRIGHTS | LIST_ACL))
	{
		myrights=get_myrightson(info.mailbox);

		if (flags & LIST_MYRIGHTS)
		{
			if (!maildir_acl_canlistrights(myrights.c_str()))
			{
				flags &= ~LIST_MYRIGHTS;
#if 0
				/* F.Y.I. only, should not be enabled
				   'cause make check may fail on systems
				   which return directory entries in a
				   different order */

				writes("* ACLFAILED \"");
				writemailbox(mailbox);
				writes("\"");
				accessdenied("LIST(MYRIGHTS)",
					     mailbox,
					     ACL_LOOKUP
					     ACL_READ
					     ACL_INSERT
					     ACL_CREATE
					     ACL_DELETEFOLDER
					     ACL_EXPUNGE
					     ACL_ADMINISTER);
#endif
			}
		}

		if (flags & LIST_ACL)
		{
			if (myrights.find(ACL_ADMINISTER[0]) ==
			    myrights.npos)
			{
				flags &= ~LIST_ACL;
#if 0
				/* F.Y.I. only, should not be enabled
				   'cause make check may fail on systems
				   which return directory entries in a
				   different order */

				writes("* ACLFAILED \"");
				writemailbox(mailbox);
				writes("\"");
				accessdenied("LIST(ACL)",
					     mailbox,
					     ACL_ADMINISTER);
#endif
			}
		}
	}

	if (flags & LIST_ACL)
	{
		if (!get_acllist(l, info.mailbox))
		{
			fprintf(stderr,
				"ERR: Error reading ACLs for %s: %s\n",
				info.mailbox.c_str(), strerror(errno));
			flags &= ~LIST_ACL;
		}
	}

	writes("* ");
	writes(cmd);
	writes(" (");

#define DO_FLAG(flag, flagname) \
	if (flags & (flag)) { writes(sep); writes(flagname); \
				sep=" "; }

	DO_FLAG(MAILBOX_NOSELECT, "\\Noselect");
	DO_FLAG(MAILBOX_UNMARKED, "\\Unmarked");
	DO_FLAG(MAILBOX_MARKED, "\\Marked");
	DO_FLAG(MAILBOX_NOCHILDREN, "\\HasNoChildren");
	DO_FLAG(MAILBOX_NOINFERIORS, "\\Noinferiors");
	DO_FLAG(MAILBOX_CHILDREN, "\\HasChildren");
#undef DO_FLAG

	writes(") ");
	writes(info.hiersep.c_str());
	writes(" \"");
	writemailbox(info.mailbox.c_str());
	writes("\"");

	if (flags & (LIST_ACL|LIST_MYRIGHTS|LIST_POSTADDRESS))
	{
		writes(" (");
		if (flags & LIST_ACL)
			list_acl(info.mailbox, l);
		if (flags & LIST_MYRIGHTS)
			list_myrights(info.mailbox, myrights);
		if (flags & LIST_POSTADDRESS)
			list_postaddress(info.mailbox);
		writes(")");
	}

	writes("\r\n");

	return 0;
}

static void list_postaddress(const std::string &mailbox)
{
	writes("(POSTADDRESS NIL)");
}

/*
** Wrapper for maildir_aclt_read and maildir_aclt_write
*/

bool acl_read_folder(
	// Freshly constructed list
	maildir::aclt_list &aclt_list,
	const std::string &maildir,
	const std::string &path)
{
	/* Handle legacy shared. namespace */

	if (path == SHARED)
	{
		aclt_list.add("anyone", ACL_LOOKUP);
		return true;
	}

	if (strncmp(path.c_str(), SHARED ".", sizeof(SHARED)) == 0)
	{
		if (strchr(path.c_str()+sizeof(SHARED), '.') == 0)
		{
			aclt_list.add("anyone", ACL_LOOKUP);
		}

		auto p=maildir::shareddir(maildir, path.substr(sizeof(SHARED)));
		if (p.empty())
		{
			aclt_list.add("anyone", ACL_LOOKUP);
			return true;
		}

		p += "/new";

		aclt_list.add("anyone",
			      access(p.c_str(), W_OK) == 0 ?
			      ACL_LOOKUP ACL_READ
			      ACL_SEEN ACL_WRITE ACL_INSERT

			      ACL_DELETEFOLDER /* Wrong, but needed for ACL1 compatibility */

			      ACL_DELETEMSGS ACL_EXPUNGE:
			      ACL_LOOKUP ACL_READ ACL_SEEN
			      ACL_WRITE);
		return true;
	}

	auto p=maildir::name2dir(".", path);

	if (p.empty())
		return false;

	aclt_list.read(maildir, p[0] == '.' && p[1] == '/' ? p.substr(2):p);
	return true;
}

static bool acl_write_folder(maildir::aclt_list &aclt_list,
			     const std::string &maildir,
			     const std::string &path,
			     const std::string &owner,
			     std::string &err_failedrights)
{
	if (path == SHARED ||
	    path.substr(0, sizeof(SHARED)) == SHARED ".")
	{
		/* Legacy */

		return 1;
	}

	auto p=maildir::name2dir(".", path);

	if (p.empty())
		return false;

	return aclt_list.write(maildir, p[0] == '.' && p[1] == '/'
			       ? p.substr(2):p,
			       owner, err_failedrights) == 0;
}

static void writeacl(const std::string &);

static void myrights()
{
	writes("* OK [MYRIGHTS \"");
	writeacl(current_maildir_info.current_mailbox_acl);
	writes("\"] ACL\r\n");
}

std::string compute_myrights(maildir::aclt_list &l,
			     const std::string &l_owner);

static bool get_acllist(maildir::aclt_list &l,
			const std::string &folder,
			std::string &computed_mailbox_owner)
{
	auto mi=maildir::info_imap_find(folder, getenv("AUTHENTICATED"));

	if (!mi)
		return false;

	std::string v;

	if (!mi.homedir.empty() && !mi.maildir.empty())
	{
		if (!acl_read_folder(l, mi.homedir, mi.maildir))
			return false;

		v=maildir::name2dir(mi.homedir, mi.maildir);
	}
	else if (mi.mailbox_type == MAILBOXTYPE_OLDSHARED)
	{
		if (!acl_read_folder(l, ".", folder)) /* It handles it */
			return false;
	}
	else if (mi.mailbox_type == MAILBOXTYPE_NEWSHARED)
	{
		/* Intermediate #shared node.  Punt */

		l.clear();

		l.add("anyone", ACL_LOOKUP);
	}
	else
	{
		/* Unknown mailbox type, no ACLs */

		l.clear();
	}

	computed_mailbox_owner=mi.owner;

	/* Detect if ACLs on the currently-open folder have changed */

	if (
#if SMAP
	    !smapflag &&
#endif

	    !v.empty() && v == current_maildir_info.current_mailbox)
	{
		auto new_acl=compute_myrights(l, computed_mailbox_owner);

		if (new_acl != current_maildir_info.current_mailbox_acl)
		{
			current_maildir_info.current_mailbox_acl=new_acl;
			myrights();
		}
	}
	return true;
}

static bool get_acllist(maildir::aclt_list &l,
			const std::string &folder)
{
	std::string ignore;

	return get_acllist(l, folder, ignore);
}

static void list_acl(const std::string &mailbox,
		     maildir::aclt_list &l)
{
	writes("(\"ACL\" (");
	for (auto &acl:l)
	{
		writes("(\"");
		writeqs(acl.identifier.c_str());
		writes("\" \"");
		writeacl(acl.acl.c_str());
		writes("\")");
	}
	writes("))");
}

static void writeacl(const std::string &aclstr)
{
	int n=0;

	for (auto cp:aclstr)
		if (cp == ACL_EXPUNGE[0])
			n |= 1;
		else if (cp == ACL_DELETEMSGS[0])
			n |= 2;
		else if (cp == ACL_DELETEFOLDER[0])
			n |= 4;

	if (n != 7)
	{
		writeqs(aclstr.c_str());
		return;
	}

	std::string p{aclstr};

	p[p.find(ACL_EXPUNGE[0])]=ACL_DELETE_SPECIAL[0];

	p.erase(std::remove_if(p.begin(), p.end(),
			       []
			       (char c)
			       {
				       return c == ACL_EXPUNGE[0] ||
					       c == ACL_DELETEMSGS[0] ||
					       c == ACL_DELETEFOLDER[0];
			       }), p.end());

	writeqs(p.c_str());
}

static void writeacl1(std::string p)
{
	if (p.find(ACL_DELETEFOLDER[0]) != p.npos &&
	    p.find(ACL_DELETEMSGS[0]) != p.npos)
	{
		auto n=p.find(ACL_EXPUNGE[0]);

		if (n != p.npos)
		{
			p[n]=ACL_DELETE_SPECIAL[0];
		}
	}
	/* See no evil */

	p.erase(std::remove_if(p.begin(), p.end(),
			       []
			       (char c)
			       {
				       return (c == ACL_EXPUNGE[0] ||
					       c == ACL_DELETEMSGS[0] ||
					       c == ACL_DELETEFOLDER[0]);
			       }), p.end());

	writeqs(p.c_str());
}

std::string get_myrightson(const std::string &mailbox)
{
	maildir::aclt_list l;
	std::string mailbox_owner;

	if (!get_acllist(l, mailbox, mailbox_owner))
		return "";

	return compute_myrights(l, mailbox_owner);
}

std::string get_myrightson_folder(const std::string &folder)
{
	auto p=maildir::imap_foldername_to_filename(enabled_utf8, folder);

	if (p.empty())
		return NULL;

	return get_myrightson(p);
}

std::string compute_myrights(maildir::aclt_list &l, const std::string &l_owner)
{
	maildir::aclt aa;

	const char *user=getenv("AUTHENTICATED");
	const char *options=getenv("OPTIONS");

	if (!options)
		options="";

	if (!user)
		user="";

	if (l.computerights(aa, user, l_owner, options) < 0)
		return "";

	return aa;
}

void check_rights(const std::string &mailbox, char *rights_buf)
{
	auto r=get_myrightson(mailbox);
	char *p, *q;

	for (p=q=rights_buf; *p; p++)
	{
		if (r.find(*p) == r.npos)
			continue;

		*q++ = *p;
	}
	*q=0;
}

static void list_myrights(const std::string &mailbox,
			  const std::string &r)
{

	writes("(\"MYRIGHTS\" \"");
	writeacl(r);
	writes("\")");
}

/*
** Combine mailbox patterns; remove duplicate mailboxes.
**
** mailbox_merge takes the mailbox_scan_info generated from a pattern,
** combines it with the current mailbox list (kept as an array), sorts the
** end result, then removes dupes.
*/

static int aclmailbox_merge(const std::vector<mailbox_scan_info> &l,
			    std::vector<mailbox_scan_info> &mailbox_list)
{
	mailbox_list.insert(mailbox_list.end(),
			    l.begin(), l.end());

	std::sort(mailbox_list.begin(),
		  mailbox_list.end(),
		  []
		  (const mailbox_scan_info &a,
		   const mailbox_scan_info &b)
		  {
			  return a.mailbox < b.mailbox;
		  });

	/* Remove dupes */

	mailbox_list.erase(
		std::unique(
			mailbox_list.begin(),
			mailbox_list.end(),
			[]
			(const mailbox_scan_info &a,
			 const mailbox_scan_info &b)
			{
				return a.mailbox == b.mailbox;
			}), mailbox_list.end());

	return 0;
}

static int aclstore(const char *, std::vector<mailbox_scan_info> &);
static int aclset(const char *, std::vector<mailbox_scan_info> &);
static int acldelete(const char *, std::vector<mailbox_scan_info> &);

static int aclcmd(const char *tag)
{
	char aclcmd[11];
	std::vector<mailbox_scan_info> mailboxlist;
	imaptoken curtoken;
	int rc;
	int (*aclfunc)(const char *,
		       std::vector<mailbox_scan_info> &mblist);

	/* Expect ACL followed only by: STORE/DELETE/SET */

	if ((curtoken=nexttoken())->tokentype != IT_ATOM ||
	    curtoken->tokenbuf.size() > sizeof(aclcmd)-1)
	{
		errno=EINVAL;
		return -1;
	}

	strcpy(aclcmd, curtoken->tokenbuf.c_str());

	switch ((curtoken=nexttoken_nouc())->tokentype) {
	case IT_LPAREN:
		while ((curtoken=nexttoken_nouc())->tokentype != IT_RPAREN)
		{
			if (curtoken->tokentype != IT_QUOTED_STRING &&
				curtoken->tokentype != IT_ATOM &&
				curtoken->tokentype != IT_NUMBER)
			{
				writes(tag);
				writes(" BAD Invalid command\r\n");
				return 0;
			}

			auto p=maildir::imap_foldername_to_filename(
				enabled_utf8,
				curtoken->tokenbuf);
			if (p.empty())
			{
				errno=EINVAL;
				return -1;
			}

			std::vector<mailbox_scan_info> mblist;

			if (!mailbox_scan("", p.c_str(), 0,
					  [&]
					  (const mailbox_scan_info &info)
					  {
						  mblist.push_back(
							  {
								  info
							  });

						  return true;
					  }) ||
			    aclmailbox_merge(mblist, mailboxlist))
			{
				return -1;
			}
		}
		break;
	case IT_QUOTED_STRING:
	case IT_ATOM:
	case IT_NUMBER:

		{
			std::vector<mailbox_scan_info> mblist;

			auto p=maildir::imap_foldername_to_filename(
				enabled_utf8,
				curtoken->tokenbuf);

			if (p.empty() ||
			    !mailbox_scan("", p.c_str(), LIST_CHECK1FOLDER,
					  [&]
					  (const mailbox_scan_info &info)
					  {
						  mblist.push_back(
							  {
								  info
							  });
						  return true;
					  }) ||
			    aclmailbox_merge(mblist, mailboxlist))
			{
				return -1;
			}
		}
		break;
	case IT_ERROR:
		writes(tag);
		writes(" BAD Invalid command\r\n");
		return 0;
	}

	rc=0;

	aclfunc=strcmp(aclcmd, "STORE") == 0 ? aclstore:
		strcmp(aclcmd, "DELETE") == 0 ? acldelete:
		strcmp(aclcmd, "SET") == 0 ? aclset:NULL;

	rc= aclfunc ? (*aclfunc)(tag, mailboxlist): -1;

	if (rc == 0)
	{
		for (auto &m:mailboxlist)
		{
			if (!m.mailbox.empty())
				list_callback({
						m.hiersep,
						m.mailbox,
						LIST_ACL | m.flags,
					}, "LIST");
		}
	}

	if (rc == 0)
	{
		writes(tag);
		writes(" OK ACL ");
		writes(aclcmd);
		writes(" completed.\r\n");
	}
	else
	{
		errno=EINVAL;
	}
	return rc;
}

static maildir::aclt fix_acl_delete(std::string s)
{
	auto iter=std::remove(s.begin(), s.end(), ACL_DELETE_SPECIAL[0]);

	if (iter == s.end())
		return maildir::aclt{s};

	s.erase(iter, s.end());

	s += ACL_DELETEFOLDER ACL_DELETEMSGS ACL_EXPUNGE;

	return maildir::aclt{s};
}

void aclminimum(const std::string &identifier)
{
	if (identifier == "administrators" ||
	    identifier == "group=administrators")
	{
		writes(ACL_ALL);
		return;
	}

	if (identifier == "-administrators" ||
	    identifier == "-group=administrators")
	{
		writes("\"\"");
		return;
	}

	writes(*identifier.c_str() == '-' ? "\"\"":ACL_ADMINISTER ACL_LOOKUP);
	writes(" " ACL_CREATE
	       " " ACL_EXPUNGE
	       " " ACL_INSERT
	       " " ACL_POST
	       " " ACL_READ
	       " " ACL_SEEN
	       " " ACL_DELETEMSGS
	       " " ACL_WRITE
	       " " ACL_DELETEFOLDER);
}

static void aclfailed(const std::string &mailbox,
		      const std::string &identifier="")
{
	if (identifier.empty())
	{
		writes("* ACLFAILED \"");
		writemailbox(mailbox.c_str());
		writes("\" ");
		writes(strerror(errno));
		writes("\r\n");
		return;
	}

	writes("* RIGHTS-INFO \"");
	writemailbox(mailbox.c_str());
	writes("\" \"");
	writeqs(identifier.c_str());
	writes("\" ");

	aclminimum(identifier);
	writes("\r\n");
}

static maildir::info acl_settable_folder(const std::string &mailbox)
{
	auto mi=maildir::info_imap_find(mailbox, getenv("AUTHENTICATED"));

	if (!mi)
	{
		aclfailed(mailbox);
		return mi;
	}

	if (mi.homedir.empty() || mi.maildir.empty())
	{
		writes("* ACLFAILED \"");
		writemailbox(mailbox);
		writes("\" ACLs may not be modified for special mailbox\r\n");
		mi={};
	}
	return mi;
}

bool acl_lock(const std::string &maildir,
	      const std::function< bool() >&callback)
{
	imapscaninfo ii{maildir};

	return imapmaildirlock(&ii, maildir, callback);
}

static bool acl_update(maildir::aclt_list &aclt_list,
		       maildir::info &mi,
		       const std::string &identifier,
		       const char *newrights,
		       std::string &acl_error)
{
	acl_error.clear();

	if (newrights[0] == '+')
	{
		if (newrights[1] == 0)
			return true;

		maildir::aclt newacl=fix_acl_delete(newrights+1);

		auto iter=aclt_list.lookup(identifier);

		if (iter != aclt_list.end())
		{
			newacl += iter->acl;
		}

		aclt_list.add(identifier, newacl);
	}
	else if (newrights[0] == '-')
	{
		auto iter=aclt_list.lookup(identifier);

		if (iter != aclt_list.end())
		{
			auto oldacl=iter->acl;

			oldacl -= fix_acl_delete(newrights+1);

			if (oldacl.empty())
			{
				aclt_list.erase(iter);
			}
			else
			{
				iter->acl=oldacl;
			}
		}
	}
	else
	{
		if (newrights[0] == 0)
		{
			aclt_list.del(identifier);
		}
		else
		{
			aclt_list.add(identifier,
				      fix_acl_delete(newrights));
		}
	}

	return acl_write_folder(aclt_list, mi.homedir, mi.maildir,
				mi.owner, acl_error);
}

static int aclstore(const char *tag,
		    std::vector<mailbox_scan_info> &mailboxes)
{
	imaptoken curtoken;

	if ((curtoken=nexttoken_nouc())->tokentype != IT_QUOTED_STRING &&
	    curtoken->tokentype != IT_ATOM &&
	    curtoken->tokentype != IT_NUMBER)
		return -1;

	std::string identifier=curtoken->tokenbuf;

	if ((curtoken=nexttoken_nouc())->tokentype != IT_QUOTED_STRING &&
	    curtoken->tokentype != IT_ATOM &&
	    curtoken->tokentype != IT_NUMBER)
	{
		return -1;
	}

	for (auto &mailbox:mailboxes)
	{
		maildir::aclt_list aclt_list;

		std::string acl_error;

		auto mi=acl_settable_folder(mailbox.mailbox);

		if (!mi)
		{
			mailbox.mailbox.clear();
			continue;
		}

		{
			CHECK_RIGHTSM(mailbox.mailbox,
				      acl_rights,
				      ACL_ADMINISTER);
			if (acl_rights[0] == 0)
			{
				writes("* ACLFAILED \"");
				writemailbox(mailbox.mailbox);
				writes("\"");
				accessdenied("ACL STORE",
					     mailbox.mailbox,
					     ACL_ADMINISTER);
				mailbox.mailbox.clear();
				continue;
			}
		}

		if (!acl_read_folder(aclt_list, mi.homedir, mi.maildir))
		{
			aclfailed(mailbox.mailbox);
			continue;
		}

		if (!acl_lock(
			    mi.homedir,
			    [&]
			    {
				    return acl_update(
					    aclt_list,
					    mi,
					    identifier,
					    curtoken->tokenbuf.c_str(),
					    acl_error
				    );
			    }))
		{
			aclfailed(mailbox.mailbox, acl_error);
			continue;
		}
	}
	return 0;
}

struct aclset_info {
	struct maildir_info *mi;
	maildir_aclt_list *newlist;
	const char **acl_error;
};

static int aclset(const char *tag,
		  std::vector<mailbox_scan_info> &mailboxes)
{
	imaptoken curtoken;
	maildir::aclt_list newlist;

	while ((curtoken=nexttoken_nouc())->tokentype != IT_EOL)
	{
		if (curtoken->tokentype != IT_QUOTED_STRING &&
		    curtoken->tokentype != IT_ATOM &&
		    curtoken->tokentype != IT_NUMBER)
			return -1;

		std::string identifier=curtoken->tokenbuf;

		if ((curtoken=nexttoken_nouc())->tokentype
		    != IT_QUOTED_STRING &&
		    curtoken->tokentype != IT_ATOM &&
		    curtoken->tokentype != IT_NUMBER)
		{
			return -1;
		}

		newlist.add(
			identifier,
			fix_acl_delete(curtoken->tokenbuf)
		);
	}

	for (auto &mailbox:mailboxes)
	{
		std::string acl_error;

		auto mi=acl_settable_folder(mailbox.mailbox);

		if (!mi)
		{
			mailbox.mailbox.clear();
			continue;
		}

		{
			CHECK_RIGHTSM(mailbox.mailbox,
				      acl_rights,
				      ACL_ADMINISTER);
			if (acl_rights[0] == 0)
			{
				writes("* ACLFAILED \"");
				writemailbox(mailbox.mailbox);
				writes("\"");
				accessdenied("ACL SET", mailbox.mailbox,
					     ACL_ADMINISTER);
				mailbox.mailbox.clear();
				continue;
			}
		}

		if (!acl_lock(mi.homedir,
			      [&]
			      {
				      return acl_write_folder(
					      newlist,
					      mi.homedir,
					      mi.maildir,
					      mi.owner,
					      acl_error
				      );
			      }))
		{
			aclfailed(mailbox.mailbox, acl_error);
			mailbox.mailbox.clear();
			continue;
		}
	}

	return 0;
}

static bool do_acldelete(const char *mailbox,
			 const char *identifier,
			 maildir::info &info);

static int acldelete(const char *tag,
		     std::vector<mailbox_scan_info> &mailboxes)
{
	imaptoken curtoken;
	const char *identifier;

	if ((curtoken=nexttoken_nouc())->tokentype != IT_QUOTED_STRING &&
	    curtoken->tokentype != IT_ATOM &&
	    curtoken->tokentype != IT_NUMBER)
		return -1;

	identifier=curtoken->tokenbuf.c_str();

	for (auto &mailbox:mailboxes)
	{
		auto mi=acl_settable_folder(mailbox.mailbox);

		if (!mi)
			continue;

		{
			CHECK_RIGHTSM(mailbox.mailbox,
				      acl_rights,
				      ACL_ADMINISTER);
			if (acl_rights[0] == 0)
			{
				writes("* ACLFAILED \"");
				writemailbox(mailbox.mailbox);
				writes("\"");
				accessdenied("ACL DELETE",
					     mailbox.mailbox,
					     ACL_ADMINISTER);
				mailbox.mailbox.clear();
				continue;
			}
		}

		if (!acl_lock(
			    mi.homedir,
			    [&]
			    {
				    return do_acldelete(
					    mailbox.mailbox.c_str(),
					    identifier,
					    mi);
			    }))
			mailbox.mailbox.clear();
	}
	return 0;
}

static bool do_acldelete(const char *mailbox,
			 const char *identifier,
			 maildir::info &mi)
{
	maildir::aclt_list aclt_list;
	std::string acl_error;

	if (!acl_read_folder(aclt_list, mi.homedir, mi.maildir))
	{
		writes("* NO Error reading ACLs for ");
		writes(mailbox);
		writes(": ");
		writes(strerror(errno));
		writes("\r\n");
		return false;
	}

	aclt_list.del(identifier);

	if (!acl_write_folder(aclt_list, mi.homedir, mi.maildir,
			      mi.owner, acl_error))
	{
		aclfailed(mailbox, acl_error);
		return false;
	}
	return true;
}


static void accessdenied(const char *cmd, const std::string &folder,
			 const char *acl_required)
{
	writes(" NO Access denied for ");
	writes(cmd);
	writes(" on ");
	writes(folder.c_str());
	writes(" (ACL \"");
	writes(acl_required);
	writes("\" required)\r\n");
}

/* Even if the folder does not exist, if there are subfolders it exists
** virtually.
*/

static int folder_exists(const std::string &folder)
{
	int flag=0;

	if (!mailbox_scan("", folder.c_str(), LIST_CHECK1FOLDER,
			  [&]
			  (const mailbox_scan_info &info)
			  {
				  flag=1;
				  return true;
			  }))
		return 0;
	return flag;
}

int do_folder_delete(const std::string &mailbox_name)
{
	maildir::aclt_list l;
	std::string acl_error;

	auto mi=maildir::info_imap_find(mailbox_name, getenv("AUTHENTICATED"));

	if (!mi)
		return -1;

	if (mi.homedir.empty() || mi.maildir.empty())
		return -1;

	if (!acl_read_folder(l, mi.homedir, mi.maildir))
		return -1;

	if (mi.maildir != INBOX)
	{
		auto p=maildir::name2dir(mi.homedir, mi.maildir);

		if (!p.empty() && !is_reserved(p) && mddelete(p))
		{
			if (folder_exists(mailbox_name.c_str()))
			{
				acl_write_folder(l, mi.homedir,
						 mi.maildir,
						 getenv("AUTHENTICATED"),
						 acl_error);
			}
			maildir_quota_recalculate(mi.homedir.c_str());
			return 0;
		}
	}
	return -1;
}

int acl_flags_adjust(const char *access_rights,
		     struct imapflags *flags)
{
	if (strchr(access_rights, ACL_DELETEMSGS[0]) == NULL)
		flags->deleted=0;
	if (strchr(access_rights, ACL_SEEN[0]) == NULL)
		flags->seen=0;
	if (strchr(access_rights, ACL_WRITE[0]) == NULL)
	{
		flags->answered=flags->flagged=flags->drafts=0;
		return 1;
	}
	return 0;
}

static int append(const char *tag, const std::string &mailbox,
		  const std::string &path)
{
	imapflags flags;
	mail::keywords::list keywords;

	time_t	timestamp=0;
	char access_rights[8];
	imaptoken curtoken;
	int need_rparen;
	int utf8_error=0;

	if (access(path.c_str(), 0))
	{
		writes(tag);
		writes(" NO [TRYCREATE] Must create mailbox before append\r\n");
		return (0);
	}

	{
		CHECK_RIGHTSM(mailbox,
			      append_rights,
			      ACL_INSERT ACL_DELETEMSGS
			      ACL_SEEN ACL_WRITE);

		if (strchr(append_rights, ACL_INSERT[0]) == NULL)
		{
			writes(tag);
			accessdenied("APPEND",
				     mailbox,
				     ACL_INSERT);
			return 0;
		}

		strcpy(access_rights, append_rights);
	}

	if (path == current_maildir_info.current_mailbox && current_mailbox_ro)
	{
		writes(tag);
		writes(" NO Current box is selected READ-ONLY.\r\n");
		return (0);
	}

	curtoken=nexttoken_noparseliteral();

	if (curtoken->tokentype == IT_LPAREN)
	{
		if (!get_flagsAndKeywords(flags, keywords))
		{
			return (-1);
		}
		curtoken=nexttoken_noparseliteral();
	}
	else if (curtoken->tokentype == IT_NIL)
		curtoken=nexttoken_noparseliteral();

	if (curtoken->tokentype == IT_QUOTED_STRING)
	{
		if (decode_date_time(curtoken->tokenbuf, &timestamp))
		{
			return (-1);
		}
		curtoken=nexttoken_noparseliteral();
	}
	else if (curtoken->tokentype == IT_NIL)
		curtoken=nexttoken_noparseliteral();

	need_rparen=0;

	if (enabled_utf8 && curtoken->tokentype == IT_ATOM &&
	    curtoken->tokenbuf == "UTF8")
	{
		/* See also: https://bugs.python.org/issue34138 */

		curtoken=nexttoken();
		if (curtoken->tokentype != IT_LPAREN)
		{
			return (-1);
		}

		curtoken=nexttoken_noparseliteral();
		if (curtoken->tokentype != IT_LITERAL8_STRING_START)
		{
			/* Don't break the protocol level */
			convert_literal_tokens(curtoken);
			return (-1);
		}
		need_rparen=1;
	}
	else if (curtoken->tokentype != IT_LITERAL_STRING_START)
	{
		/* Don't break the protocol level */
		convert_literal_tokens(curtoken);
		return (-1);
	}

	acl_flags_adjust(access_rights, &flags);

	auto ret=store_mailbox(tag, path.c_str(), &flags,
			       acl_flags_adjust(access_rights, &flags)
			       ? mail::keywords::list{}:keywords,
			       timestamp,
			       curtoken, &utf8_error);

	if (!ret)
	{
		unread('\n');
		return (0);
	}

	auto &[new_uidv, new_uid] = *ret;

	if (need_rparen)
	{
		if (nexttoken()->tokentype != IT_RPAREN)
		{
			return (-1);
		}
	}

	if (nexttoken()->tokentype != IT_EOL)
	{
		return (-1);
	}

	dirsync(path.c_str());
	if (utf8_error) {
		writes("* OK [ALERT] Your IMAP client does not appear to "
		       "correctly implement Unicode messages, "
		       "see https://datatracker.ietf.org/doc/html/rfc6855\r\n");
	}
	writes(tag);
	writes(" OK [APPENDUID ");
	writen(new_uidv);
	writes(" ");
	writen(new_uid);
	writes("] APPEND Ok.");
	writes("\r\n");
	return (0);
}


/* Check for 'c' rights on the parent directory. */

static int check_parent_create(const char *tag,
			       const char *cmd,
			       std::string folder)
{
	size_t parentPtr=folder.rfind(HIERCH);

	if (parentPtr)
	{
		folder=folder.substr(0, parentPtr);
		CHECK_RIGHTSM(folder,
			      create_rights,
			      ACL_CREATE);

		if (create_rights[0])
		{
			return 0;
		}
	}

	writes(tag);
	accessdenied(cmd, folder, ACL_CREATE);
	return -1;
}

/* Convert ACL1 identifiers to ACL2 */

static std::string acl2_identifier(const char *tag,
				   const char *identifier)
{
	const char *ident_orig=identifier;
	int isneg=0;

	if (*identifier == '-')
	{
		isneg=1;
		++identifier;
	}

	if (strcmp(identifier, "anyone") == 0 ||
	    strcmp(identifier, "anonymous") == 0 ||
	    strcmp(identifier, "authuser") == 0 ||
	    strcmp(identifier, "owner") == 0)
	{
		return ident_orig;
	}

	if (strchr(identifier, '='))
	{
		writes(tag);
		writes(" NO Invalid ACL identifier.\r\n");
		return "";
	}

	auto pfix=strcmp(identifier, "administrators") == 0 ? "group=":"user=";

	std::string s;

	s.reserve(strlen(identifier)+strlen(pfix)+isneg);

	if (isneg)
		s="-";

	s += pfix;
	s += identifier;

	return s;
}

const char *folder_rename(maildir::info &mi1,
			  maildir::info &mi2)
{
	if (mi1.homedir.empty() || mi1.maildir.empty())
	{
		return "Invalid mailbox name";
	}

	if (mi2.homedir.empty() || mi2.maildir.empty())
	{
		return "Invalid new mailbox name";
	}

	if (!current_maildir_info.current_mailbox.empty())
	{
		auto mailbox=maildir::name2dir(mi1.homedir,
					       mi1.maildir);

		if (mailbox.empty())
		{
			return "Invalid mailbox name";
		}

		if (current_maildir_info.current_mailbox.substr(
			    0,
			    mailbox.size()
		    ) == mailbox &&
		    (current_maildir_info.current_mailbox.size() ==
		     mailbox.size() ||
		     current_maildir_info.current_mailbox[
			     mailbox.size()
		     ] == HIERCH))
		{
			return "Can't RENAME the currently-open folder";
		}
	}

	if (mi1.homedir != mi2.homedir)
	{
		return "Cannot move a folder to a different account.";
	}

	if (mi1.maildir == INBOX ||
	    mi2.maildir == INBOX)
	{
		return "INBOX rename not implemented.";
	}

	if (is_reserved_name(mi1.maildir.c_str()) ||
	    is_reserved_name(mi2.maildir.c_str()))
	{
		return "Reserved folder name - cannot rename.";
	}

	/* Depend on maildir_name2dir returning ./.folder, see
	** maildir_rename() call. */

	auto old_mailbox=maildir::name2dir(".", mi1.maildir);

	if (old_mailbox.empty() ||
	    old_mailbox.substr(0, 2) != "./")
	{
		return "Internal error in RENAME: maildir_name2dir failed"
			" for the old folder rename.";
	}

	auto new_mailbox=maildir::name2dir(".", mi2.maildir);

	if (new_mailbox.empty() ||
	    new_mailbox.substr(0, 2) != "./")
	{
		return "Internal error in RENAME: maildir_name2dir failed"
			" for the new folder rename.";
	}

	fetch_free_cache();

	old_mailbox=old_mailbox.substr(2);
	new_mailbox=new_mailbox.substr(2);
	if (maildir_rename(mi1.homedir.c_str(),
			   old_mailbox.c_str(), new_mailbox.c_str(),
			   MAILDIR_RENAME_FOLDER |
			   MAILDIR_RENAME_SUBFOLDERS,
			   &rename_callback))
	{
		return "@RENAME failed: ";
	}

	maildir_quota_recalculate(mi1.homedir.c_str());

	return NULL;
}

static bool validate_charset(const char *tag, std::string &charset)
{
	unicode_convert_handle_t conv;
	char32_t *ucptr;
	size_t ucsize;

	if (charset.empty())
		charset="UTF-8";

	if (enabled_utf8 && charset != "UTF-8")
	{
		writes(tag);
		writes(" BAD [BADCHARSET] Only UTF-8 charset is valid after enabling RFC 6855 support\r\n");
		return false;
	}

	conv=unicode_convert_tou_init(charset.c_str(), &ucptr, &ucsize, 1);

	if (!conv)
	{
		writes(tag);
		writes(" NO [BADCHARSET] The requested character set is not supported.\r\n");
		return false;
	}
	if (unicode_convert_deinit(conv, NULL) == 0)
		free(ucptr);
	return true;
}

extern "C" int do_imap_command(const char *tag, int *flushflag)
{
	imaptoken curtoken=nexttoken();
	bool uid=false;

	if (curtoken->tokentype != IT_ATOM)	return (-1);

	/* Commands that work in authenticated state */

	if (curtoken->tokenbuf == "CAPABILITY")
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);
		writes("* CAPABILITY ");
		imapcapability();
		writes("\r\n");
		writes(tag);
		writes(" OK CAPABILITY completed\r\n");
		return (0);
	}
	if (curtoken->tokenbuf == "NOOP")
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);
		doNoop(1);
		writes(tag);
		writes(" OK NOOP completed\r\n");
		return (0);
	}
	if (curtoken->tokenbuf == "ID")
	{
		if (doId() < 0)
			return (-1);
		writes(tag);
		writes(" OK ID completed\r\n");
		return (0);
	}
       if (curtoken->tokenbuf == "IDLE")
       {
               if (nexttoken()->tokentype != IT_EOL)   return (-1);

	       read_eol();

	       imapenhancedidle();
	       curtoken=nexttoken();
	       if (curtoken->tokenbuf == "DONE")
	       {
		       doNoop(0);
		       writes(tag);
		       writes(" OK IDLE completed\r\n");
		       return (0);
               }
               return (-1);
       }
	if (curtoken->tokenbuf == "LOGOUT")
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);
		fetch_free_cache();
		writes("* BYE Courier-IMAP server shutting down\r\n");
		writes(tag);
		writes(" OK LOGOUT completed\r\n");
		writeflush();
		emptytrash();
		logoutmsg();
		bye();
	}

	if (curtoken->tokenbuf == "LIST"
		|| curtoken->tokenbuf == "LSUB")
	{
		std::string reference, name;
		int	rc;
		char	cmdbuf[5];
		int	list_flags=0;

		strcpy(cmdbuf, curtoken->tokenbuf.c_str());

		curtoken=nexttoken_nouc();
		if (curtoken->tokentype == IT_LPAREN)
		{
			while ((curtoken=nexttoken())->tokentype != IT_RPAREN)
			{
				if (curtoken->tokentype != IT_QUOTED_STRING &&
				    curtoken->tokentype != IT_ATOM &&
				    curtoken->tokentype != IT_NUMBER)
					return (-1);

				if (curtoken->tokenbuf == "ACL")
					list_flags |= LIST_ACL;
				if (curtoken->tokenbuf == "MYRIGHTS")
					list_flags |= LIST_MYRIGHTS;
				if (curtoken->tokenbuf == "POSTADDRESS")
					list_flags |= LIST_POSTADDRESS;
			}

			curtoken=nexttoken_nouc();
		}


		if (curtoken->tokentype != IT_NIL)
		{
			if (curtoken->tokentype != IT_QUOTED_STRING &&
				curtoken->tokentype != IT_ATOM &&
				curtoken->tokentype != IT_NUMBER)
			{
				writes(tag);
				writes(" BAD Invalid command\r\n");
				return (0);
			}
			if (!curtoken->tokenbuf.empty())
			{
				reference=maildir::imap_foldername_to_filename(
					enabled_utf8, curtoken->tokenbuf
				);
				if (reference.empty())
				{
					return -1;
				}
			}
		}
		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NIL)
		{
			if (curtoken->tokentype != IT_QUOTED_STRING &&
				curtoken->tokentype != IT_ATOM &&
				curtoken->tokentype != IT_NUMBER)
			{
				writes(tag);
				writes(" BAD Invalid command\r\n");
				return(0);
			}
			if (!curtoken->tokenbuf.empty())
			{
				name=maildir::imap_foldername_to_filename(
					enabled_utf8,
					curtoken->tokenbuf);

				if (name.empty())
				{
					return -1;
				}
			}
		}
		if (nexttoken()->tokentype != IT_EOL)	return (-1);

		if (strcmp(cmdbuf, "LIST"))
			list_flags |= LIST_SUBSCRIBED;

		rc=mailbox_scan(reference.c_str(), name.c_str(),
				list_flags,
				[&]
				(const mailbox_scan_info &info)
				{
					return list_callback(
						info,
						cmdbuf) == 0;
				}) == true ? 0:-1;

		if (rc == 0)
		{
			writes(tag);
			writes(" OK ");
			writes(cmdbuf);
			writes(" completed\r\n");
		}
		else
		{
			writes(tag);
			writes(" NO ");
			writes(strerror(errno));
			writes("\r\n");
			rc=0;
		}
		writeflush();
		return (rc);
	}

	if (curtoken->tokenbuf == "APPEND")
	{
		imaptoken tok=nexttoken_nouc();

		if (tok->tokentype != IT_NUMBER &&
			tok->tokentype != IT_ATOM &&
			tok->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		auto mi=get_maildir_info_and_mailbox(tok->tokenbuf);

		if (!mi)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		if (!mi.homedir.empty() && !mi.maildir.empty())
		{
			auto p=maildir::name2dir(mi.homedir, mi.maildir);
			int rc;

			if (p.empty())
			{
				writes(tag);
				accessdenied("APPEND",
					     mi.mailbox,
					     ACL_INSERT);
				return 0;
			}

			rc=append(tag, mi.mailbox, p);
			return (rc);
		}
		else if (mi.mailbox_type == MAILBOXTYPE_OLDSHARED)
		{
			auto q=strchr(mi.mailbox.c_str(), '.');

			std::string p;

			if (q)
				p=maildir::shareddir(".", q+1);

			if (!p.empty())
			{
				int rc;

				p += "/shared";
				rc=append(tag, mi.mailbox, p);
				return rc;
			}
		}
		writes(tag);
		accessdenied("APPEND", "folder", ACL_INSERT);
		return (0);
	}

	if (curtoken->tokenbuf == "GETQUOTAROOT")
	{
		char	qroot[20];
		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		auto minfo=get_maildir_info_and_mailbox(curtoken->tokenbuf);

		if (!minfo)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		switch (minfo.mailbox_type) {
		case MAILBOXTYPE_INBOX:
			strcpy(qroot, "ROOT");
			break;
		case MAILBOXTYPE_OLDSHARED:
			strcpy(qroot, "SHARED");
			break;
		case MAILBOXTYPE_NEWSHARED:
			strcpy(qroot, "PUBLIC");
			break;
		}

		writes("*");
		writes(" QUOTAROOT \"");
		writemailbox(minfo.mailbox);
		writes("\" \"");
		writes(qroot);
		writes("\"\r\n");
		quotainfo_out(qroot);
		writes(tag);
		writes(" OK GETQUOTAROOT Ok.\r\n");
		return(0);
	}


	if (curtoken->tokenbuf == "SETQUOTA")
	{
		writes(tag);
		writes(" NO SETQUOTA No permission.\r\n");
		return(0);
	}

	if (curtoken->tokenbuf == "ENABLE")
	{
		while (nexttoken()->tokentype != IT_EOL)
		{
			switch (curtoken->tokentype) {
			case IT_NUMBER:
			case IT_ATOM:
			case IT_QUOTED_STRING:
				if (curtoken->tokenbuf == "UTF8=ACCEPT")
				{
					enabled_utf8=1;
				}
				continue;
			default:
				return -1;
			}
		}

		writes("* ENABLED");
		if (enabled_utf8)
			writes(" UTF8=ACCEPT");
		writes("\r\n");
		writes(tag);
		writes(" OK Options enabled\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "GETQUOTA")
	{
		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
			return (-1);

		quotainfo_out(curtoken->tokenbuf.c_str());
		writes(tag);
		writes(" OK GETQUOTA Ok.\r\n");
		return(0);
	}

	if (curtoken->tokenbuf == "STATUS")
	{
		int	get_messages=0,
			get_recent=0,
			get_uidnext=0,
			get_uidvalidity=0,
			get_unseen=0;

		const char *p;
		std::string orig_mailbox;
		int	oneonly;

		curtoken=nexttoken_nouc();

		auto new_mailbox=parse_mailbox_error(tag, curtoken, false,
						     false);

		if (new_mailbox.empty())
		{
			return (0);
		}

		orig_mailbox=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (orig_mailbox.empty())
		{
			return -1;
		}
		curtoken=nexttoken();

		oneonly=0;
		if (curtoken->tokentype != IT_LPAREN)
		{
			if (curtoken->tokentype != IT_ATOM)
			{
				return (-1);
			}
			oneonly=1;
		}
		else	nexttoken();

		while ((curtoken=currenttoken())->tokentype == IT_ATOM)
		{
			if (curtoken->tokenbuf == "MESSAGES")
				get_messages=1;
			if (curtoken->tokenbuf == "RECENT")
				get_recent=1;
			if (curtoken->tokenbuf == "UIDNEXT")
				get_uidnext=1;
			if (curtoken->tokenbuf == "UIDVALIDITY")
				get_uidvalidity=1;
			if (curtoken->tokenbuf == "UNSEEN")
				get_unseen=1;
			nexttoken();
			if (oneonly)	break;
		}

		if ((!oneonly && curtoken->tokentype != IT_RPAREN) ||
			nexttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		{
			CHECK_RIGHTSM(orig_mailbox, status_rights, ACL_READ);

			if (!status_rights[0])
			{
				writes(tag);
				accessdenied("STATUS", orig_mailbox,
					     ACL_READ);
				return 0;
			}
		}

		imapscaninfo other_info{new_mailbox},
			*infoptr;

		if (new_mailbox == current_maildir_info.current_mailbox)
		{
			infoptr= &current_maildir_info;
		}
		else
		{
			infoptr=&other_info;

			if (imapscan_maildir(infoptr, 1, 1))
			{
				writes(tag);
				writes(" NO [ALERT] STATUS failed\r\n");
				return (0);
			}
		}

		writes("*");
		writes(" STATUS \"");
		writemailbox(orig_mailbox.c_str());
		writes("\" (");
		p="";
		if (get_messages)
		{
			writes("MESSAGES ");
			writen(infoptr->msgs.size()+infoptr->left_unseen);
			p=" ";
		}
		if (get_recent)
		{
		unsigned long n=infoptr->left_unseen;
		unsigned long i;

			for (i=0; i<infoptr->msgs.size(); i++)
				if (infoptr->msgs[i].recentflag)
					++n;
			writes(p);
			writes("RECENT ");
			writen(n);
			p=" ";
		}

		if (get_uidnext)
		{
			writes(p);
			writes("UIDNEXT ");
			writen(infoptr->nextuid);
			p=" ";
		}

		if (get_uidvalidity)
		{
			writes(p);
			writes("UIDVALIDITY ");
			writen(infoptr->uidv);
			p=" ";
		}

		if (get_unseen)
		{
			writes(p);
			writes("UNSEEN ");
			writen(infoptr->unseen());
		}
		writes(")\r\n");
		writes(tag);
		writes(" OK STATUS Completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "CREATE")
	{
		std::string orig_mailbox;
		int	isdummy;

		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return 0;
		}

		isdummy=0;

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
		{
			curtoken->tokenbuf.pop_back();
			isdummy=1;	/* Ignore hierarchy creation */
		}

		auto mailboxstr=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (mailboxstr.empty())
			return -1;

		auto mi=maildir::info_imap_find(mailboxstr,
						getenv("AUTHENTICATED"));

		if (!mi)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		if (mi.homedir.empty() || mi.maildir.empty())
		{
			writes(tag);
			accessdenied("CREATE",
				     curtoken->tokenbuf,
				     ACL_CREATE);
			return (0);
		}

		auto mailbox=maildir::name2dir(mi.homedir, mi.maildir);
		if (mailbox.empty())
		{
			writes(tag);
			writes(" NO Invalid mailbox name\r\n");
			return (0);
		}

		if (mailbox == ".")
		{
			writes(tag);
			writes(" NO INBOX already exists!\r\n");
			return (0);
		}

		if (check_parent_create(tag, "CREATE", curtoken->tokenbuf))
		{
			return (0);
		}

		if (isdummy)
			curtoken->tokenbuf.push_back(HIERCH);

		orig_mailbox=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (orig_mailbox.empty())
		{
			return (-1);
		}

		auto mailbox_tokenbuf=curtoken->tokenbuf;

		if (nexttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		if (!isdummy)
		{
			int did_exist;
			maildir::aclt_list l;

			if ((did_exist=folder_exists(orig_mailbox)) != 0)
			{
				if (!acl_read_folder(l,
						     mi.homedir,
						     mi.maildir))
				{
					writes(tag);
					writes(" NO Cannot create this folder"
					       ".\r\n");
					return (0);
				}

				size_t p=mi.maildir.find('.');

				if (p != mi.maildir.npos)
				{
					maildir::acl_delete(
						mi.homedir,
						mi.maildir.c_str()+p
					);
					/* Clear out fluff */
				}
			}

			if (mdcreate(mailbox.c_str()))
			{
				writes(tag);
				writes(" NO Cannot create this folder.\r\n");
				return (0);
			}
			if (did_exist)
			{
				std::string acl_error;

				acl_write_folder(l, mi.homedir,
						 mi.maildir, "",
						 acl_error);
			}
		}
		writes(tag);
		writes(" OK \"");
		writemailbox(orig_mailbox);
		writes("\" created.\r\n");

		/*
		** This is a dummy call to acl_read_folder that initialized
		** the default ACLs for this folder to its parent.
		*/

		{
			CHECK_RIGHTSM(mailbox_tokenbuf, create_rights,
				      ACL_CREATE);
		}

		imapscaninfo minfo{mailbox};
		imapscan_maildir(&minfo, 0,0);
		return (0);
	}

	if (curtoken->tokenbuf == "DELETE")
	{
		std::string mailbox_name;

		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
			/* Ignore hierarchy DELETE */
		{
			if (nexttoken()->tokentype != IT_EOL)
				return (-1);
			writes(tag);
			writes(" OK Folder directory delete punted.\r\n");
			return (0);
		}

		std::string foldername=curtoken->tokenbuf;

		auto new_mailbox=
			parse_mailbox_error(tag, curtoken, true, false);

		if (new_mailbox.empty())
			return 0;

		mailbox_name=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (mailbox_name.empty())
		{
			return (-1);
		}
		if (nexttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		if (new_mailbox == current_maildir_info.current_mailbox)
		{
			writes(tag);
			writes(" NO Cannot delete currently-open folder.\r\n");
			return (0);
		}

		if (strncmp(mailbox_name.c_str(), SHARED HIERCHS,
			    sizeof(SHARED HIERCHS)-1) == 0)
		{
			maildir_shared_unsubscribe(
				0,
				mailbox_name.substr(
					sizeof(SHARED HIERCHS)-1
				).c_str());
			writes(tag);
			writes(" OK UNSUBSCRIBEd a shared folder.\r\n");
			return (0);
		}

		{
			CHECK_RIGHTSM(foldername,
				      delete_rights,
				      ACL_DELETEFOLDER);
			if (delete_rights[0] == 0)
			{
				writes(tag);
				accessdenied("DELETE",
					     foldername,
					     ACL_DELETEFOLDER);
				return 0;
			}
		}

		if (!broken_uidvs())
			sleep(2); /* Make sure we never recycle them*/

		fetch_free_cache();

		if (do_folder_delete(mailbox_name.c_str()))
		{
			writes(tag);
			writes(" NO Cannot delete this folder.\r\n");
		}
		else
		{
			writes(tag);
			writes(" OK Folder deleted.\r\n");
		}

		return (0);
	}

	if (curtoken->tokenbuf == "RENAME")
	{
		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
		    curtoken->tokentype != IT_ATOM &&
		    curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
			curtoken->tokenbuf.pop_back();

		auto mailbox=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (mailbox.empty())
			return -1;

		auto mi1=maildir::info_imap_find(
			mailbox,
			getenv("AUTHENTICATED"));

		if (!mi1)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		if (mi1.homedir.empty() || mi1.maildir.empty())
		{
			writes(tag);
			writes(" NO Invalid mailbox\r\n");
			return (0);
		}

		{
			CHECK_RIGHTSM(mailbox,
				      rename_rights, ACL_DELETEFOLDER);

			if (rename_rights[0] == 0)
			{
				writes(tag);
				accessdenied("RENAME", curtoken->tokenbuf,
					     ACL_DELETEFOLDER);
				return (0);
			}
		}

		curtoken=nexttoken_nouc();
		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
			curtoken->tokenbuf.pop_back();

		mailbox=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (mailbox.empty())
		{
			return -1;
		}

		auto mi2=maildir::info_imap_find(mailbox,
						 getenv("AUTHENTICATED"));

		if (!mi2)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		if (check_parent_create(tag, "RENAME", mailbox))
		{
			return 0;
		}

		if (nexttoken()->tokentype != IT_EOL)
			return (-1);

		if (!broken_uidvs())
			sleep(2);
		/* Make sure IMAP uidvs are not recycled */


		auto errmsg=folder_rename(mi1, mi2);

		if (errmsg)
		{
			writes(tag);
			writes(" NO ");
			writes(*errmsg == '@' ? errmsg+1:errmsg);
			if (*errmsg == '@')
				writes(strerror(errno));
			writes("\r\n");
		}
		else
		{
			writes(tag);
			writes(" OK Folder renamed.\r\n");
		}

		return (0);
	}

	if (curtoken->tokenbuf == "SELECT" ||
		curtoken->tokenbuf == "EXAMINE")
	{
		curtoken=nexttoken_nouc();

		current_maildir_info=imapscaninfo{""};

		auto new_mailbox=
			parse_mailbox_error(tag, curtoken, false, true);

		if (new_mailbox.empty())
		{
			return 0;
		}

		auto current_mailbox_acl=
			get_myrightson_folder(curtoken->tokenbuf);

		if (current_mailbox_acl.find( ACL_READ[0]) ==
		    current_mailbox_acl.npos)
		{
			writes(tag);
			accessdenied("SELECT/EXAMINE", curtoken->tokenbuf,
				     ACL_READ);
			return 0;
		}

		if (nexttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		current_maildir_info=imapscaninfo{new_mailbox};
		current_maildir_info.current_mailbox_acl=current_mailbox_acl;

		/* check if this is a shared read-only folder */

		int ro=1;

		for (auto c:current_maildir_info.current_mailbox_acl)
			if (strchr(ACL_INSERT ACL_EXPUNGE
				   ACL_SEEN ACL_WRITE ACL_DELETEMSGS,
				   c))
			{
				ro=0;
				break;
			}

		if (is_sharedsubdir(new_mailbox.c_str()) &&
		    !maildir::shared_isrw(new_mailbox))
			ro=1;

		if (curtoken->tokenbuf[0] == 'E')
			ro=1;
		current_mailbox_ro=ro;

		if (imapscan_maildir(&current_maildir_info, 0, ro))
		{
			writes(tag);
			writes(" NO Unable to open this mailbox.\r\n");
			current_maildir_info=imapscaninfo{""};
			return (0);
		}

		mailboxflags(ro);
		mailboxmetrics();
		writes("* OK [UIDVALIDITY ");
		writen(current_maildir_info.uidv);
		writes("] Ok\r\n");
		myrights();
		writes(tag);

		writes(ro ? " OK [READ-ONLY] Ok\r\n":" OK [READ-WRITE] Ok\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "SUBSCRIBE")
	{
		curtoken=nexttoken_nouc();
		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
		{
			if (nexttoken()->tokentype != IT_EOL)
				return (-1);
			writes(tag);
			writes(" OK Folder directory subscribe punted.\r\n");
			return (0);
		}

		auto mi=get_maildir_info_and_mailbox(curtoken->tokenbuf);

		if (!mi)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}
		if (nexttoken()->tokentype != IT_EOL)
			return (-1);

		if (mi.mailbox_type != MAILBOXTYPE_OLDSHARED)
		{
			subscribe(mi.mailbox.c_str());
			writes(tag);
			writes(" OK Folder subscribed.\r\n");
			return (0);
		}

		auto p=mi.mailbox.find('.');

		std::string s;

		if (p != mi.mailbox.npos)
			s=maildir::shareddir(".", mi.mailbox.substr(p+1));

		if (s.empty() || access(s.c_str(), 0) == 0)
		{
			writes(tag);
			writes(" OK Already subscribed.\r\n");
			return (0);
		}

		if (s.empty() ||
		    !maildir::shared_subscribe("",
					       mi.mailbox.substr(
						       mi.mailbox.find('.')+1)))
		{
			writes(tag);
			writes(" NO Cannot subscribe to this folder.\r\n");
			return (0);
		}
		writes(tag);
		writes(" OK SUBSCRIBE completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "UNSUBSCRIBE")
	{
		curtoken=nexttoken_nouc();
		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		if (!curtoken->tokenbuf.empty() &&
		    curtoken->tokenbuf.back() == HIERCH)
		{
			if (nexttoken()->tokentype != IT_EOL)
				return (-1);
			writes(tag);
			writes(" OK Folder directory unsubscribe punted.\r\n");
			return (0);
		}

		auto mi=get_maildir_info_and_mailbox(curtoken->tokenbuf);

		if (!mi)
		{
			writes(tag);
			writes(" NO Invalid mailbox name.\r\n");
			return (0);
		}

		if (nexttoken()->tokentype != IT_EOL)
			return (-1);

		if (mi.mailbox_type != MAILBOXTYPE_OLDSHARED)
		{
			unsubscribe(mi.mailbox.c_str());
			writes(tag);
			writes(" OK Folder unsubscribed.\r\n");
			return (0);
		}

		auto p=mi.mailbox.find('.');

		std::string s;

		if (p != mi.mailbox.npos)
			s=maildir::shareddir(".", mi.mailbox.substr(p+1));

		if (s.empty() || access(s.c_str(), 0))
		{
			writes(tag);
			writes(" OK Already unsubscribed.\r\n");
			return (0);
		}

		fetch_free_cache();

		if (s.empty() ||
		    maildir_shared_unsubscribe(
			    0,
			    strchr(mi.mailbox.c_str(), '.')+1))
		{
			writes(tag);
			writes(" NO Cannot subscribe to this folder.\r\n");
			return (0);
		}
		writes(tag);
		writes(" OK UNSUBSCRIBE completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "NAMESPACE")
	{
		if (nexttoken()->tokentype != IT_EOL)
			return (-1);
		writes("* NAMESPACE ((\"INBOX.\" \".\")) NIL "
		       "((\"#shared.\" \".\")(\""
			SHARED ".\" \".\"))\r\n");
		writes(tag);
		writes(" OK NAMESPACE completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "ACL")
	{
		if (aclcmd(tag))
		{
			writes(tag);
			writes(" ACL FAILED: ");
			writes(strerror(errno));
			writes("\r\n");
		}
		return 0;
	}

	/* RFC 2086 */

	if (curtoken->tokenbuf == "SETACL" ||
	    curtoken->tokenbuf == "DELETEACL")
	{
		int doset=curtoken->tokenbuf[0] == 'S';
		const char *origcmd=doset ? "SETACL":"DELETEACL";

		curtoken=nexttoken_nouc();

		if (parse_mailbox_error(tag, curtoken, false, false).empty())
			return 0;

		auto mailbox=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);
		if (mailbox.empty())
			return -1;

		auto mi=maildir::info_imap_find(mailbox,
						getenv("AUTHENTICATED"));

		if (!mi)
		{
			writes(tag);
			writes(" NO Invalid mailbox.\r\n");
			return 0;
		}

		if (mi.homedir.empty() || mi.maildir.empty())
		{
			writes(tag);
			writes(" NO Cannot set ACLs for this mailbox\r\n");
			return 0;
		}

		switch ((curtoken=nexttoken_nouc())->tokentype) {
		case IT_QUOTED_STRING:
		case IT_ATOM:
		case IT_NUMBER:
			break;
		default:
			return -1;
		}

		auto identifier=acl2_identifier(tag,
						curtoken->tokenbuf.c_str());

		if (identifier.empty())
			return 0;

		if (doset)
		{
			switch ((curtoken=nexttoken_nouc())->tokentype) {
			case IT_QUOTED_STRING:
			case IT_ATOM:
			case IT_NUMBER:
				break;
			default:
				return -1;
			}
		}

		{
			CHECK_RIGHTSM(mailbox.c_str(),
				      acl_rights,
				      ACL_ADMINISTER);
			if (acl_rights[0] == 0)
			{
				writes(tag);
				accessdenied(origcmd, mailbox.c_str(),
					     ACL_ADMINISTER);
				return 0;
			}
		}

		maildir::aclt_list aclt_list;

		if (!acl_read_folder(aclt_list, mi.homedir, mi.maildir))
		{
			writes(tag);
			writes(" NO Cannot read existing ACLs.\r\n");
			return 0;
		}

		std::string acl_error;
		if (!acl_update(aclt_list, mi, identifier,
				doset ? curtoken->tokenbuf.c_str():"",
				acl_error))
		{
			writes(tag);
			writes(!acl_error.empty() ?
			       " NO Cannot modify ACLs as requested.\r\n" :
			       " NO Cannot modify ACLs on this mailbox.\r\n");
		}
		else
		{
			(void)get_myrightson(mailbox);

			/* Side effect - change current folder's ACL */

			writes(tag);
			writes(" OK ACLs updated.\r\n");
		}
		return 0;
	}

	if (curtoken->tokenbuf == "GETACL")
	{
		maildir::aclt_list l;

		curtoken=nexttoken_nouc();

		if (parse_mailbox_error(tag, curtoken, false, false).empty())
			return 0;

		auto f=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (f.empty())
			return -1;

		{
			CHECK_RIGHTSM(f,
				      acl_rights,
				      ACL_ADMINISTER);
			if (acl_rights[0] == 0)
			{
				writes(tag);
				accessdenied("GETACL", f,
					     ACL_ADMINISTER);
				return 0;
			}
		}

		if (!get_acllist(l, f))
		{
			writes(tag);
			writes(" NO Cannot retrieve ACLs for mailbox.\r\n");
			return 0;
		}

		writes("* ACL \"");
		writemailbox(f);
		writes("\"");
		for (const auto &acl:l)
		{
			auto ident=acl.identifier.c_str();
			bool isneg=false;

			if (*ident == '-')
			{
				isneg=true;
				++ident;
			}

			if (strchr(ident, '='))
			{
				if (strncmp(ident, "user=", 5))
					continue; /* Hear no evil */
				ident += 5;
			}

			writes(" \"");
			if (isneg)
				writes("-");
			writeqs(ident);
			writes("\" \"");

			writeacl1(acl.acl);
			writes("\"");
		}

		writes("\r\n");
		writes(tag);
		writes(" OK GETACL completed.\r\n");
		return 0;
	}

	if (curtoken->tokenbuf == "LISTRIGHTS")
	{
		maildir::aclt_list l;
		std::string mailbox_owner;

		curtoken=nexttoken_nouc();

		if (parse_mailbox_error(tag, curtoken, false, false).empty())
			return 0;

		auto mb=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (mb.empty())
			return -1;

		{
			auto myrights=get_myrightson(mb);

			if (myrights.find(ACL_LOOKUP[0]) == myrights.npos &&
			    myrights.find(ACL_READ[0]) == myrights.npos &&
			    myrights.find(ACL_INSERT[0]) == myrights.npos &&
			    myrights.find(ACL_CREATE[0]) == myrights.npos &&
			    myrights.find(ACL_DELETEFOLDER[0]) == myrights.npos &&
			    myrights.find(ACL_EXPUNGE[0]) == myrights.npos &&
			    myrights.find(ACL_ADMINISTER[0]) == myrights.npos)
			{
				writes(tag);
				accessdenied("GETACL", mb.c_str(),
					     ACL_ADMINISTER);
				return 0;
			}
		}

		if (!get_acllist(l, mb, mailbox_owner))
		{
			writes(tag);
			writes(" NO Cannot retrieve ACLs for mailbox.\r\n");
			return 0;
		}

		switch ((curtoken=nexttoken_nouc())->tokentype) {
		case IT_QUOTED_STRING:
		case IT_ATOM:
		case IT_NUMBER:
			break;
		default:
			return -1;
		}

		writes("* LISTRIGHTS \"");
		writemailbox(mb.c_str());
		writes("\" \"");
		writeqs(curtoken->tokenbuf.c_str());
		writes("\"");

		if (*curtoken->tokenbuf.c_str() == '-' &&
		    (MAILDIR_ACL_ANYONE(curtoken->tokenbuf.c_str()+1) ||
		     (mailbox_owner.substr(0, 5) == "user=" &&
		      mailbox_owner.substr(5) == curtoken->tokenbuf.c_str()+1)))
		{
			writes(" \"\" "
			       ACL_CREATE " "
			       ACL_DELETE_SPECIAL " "
			       ACL_INSERT " "
			       ACL_POST " "
			       ACL_READ " "
			       ACL_SEEN " "
			       ACL_WRITE "\r\n");
		}
		else if (mailbox_owner.substr(0, 5) == "user=" &&
			 mailbox_owner.substr(5) == curtoken->tokenbuf)
		{
			writes(" \""
			       ACL_ADMINISTER
			       ACL_LOOKUP "\" "
			       ACL_CREATE " "
			       ACL_DELETE_SPECIAL " "
			       ACL_INSERT " "
			       ACL_POST " "
			       ACL_READ " "
			       ACL_SEEN " "
			       ACL_WRITE "\r\n");
		}
		else
		{
			writes(" \"\" "
			       ACL_ADMINISTER " "
			       ACL_CREATE " "
			       ACL_DELETE_SPECIAL " "
			       ACL_INSERT " "
			       ACL_LOOKUP " "
			       ACL_POST " "
			       ACL_READ " "
			       ACL_SEEN " "
			       ACL_WRITE "\r\n");
		}
		writes(tag);
		writes(" OK LISTRIGHTS completed.\r\n");
		return 0;
	}

	if (curtoken->tokenbuf == "MYRIGHTS")
	{
		curtoken=nexttoken_nouc();

		if (parse_mailbox_error(tag, curtoken, false, false).empty())
			return 0;

		auto f=maildir::imap_foldername_to_filename(
			enabled_utf8,
			curtoken->tokenbuf);

		if (f.empty())
			return -1;

		{
			auto myrights=get_myrightson(f);

			if (myrights.find(ACL_LOOKUP[0]) == myrights.npos &&
			    myrights.find(ACL_READ[0]) == myrights.npos &&
			    myrights.find(ACL_INSERT[0]) == myrights.npos &&
			    myrights.find(ACL_CREATE[0]) == myrights.npos &&
			    myrights.find(ACL_DELETEFOLDER[0]) == myrights.npos &&
			    myrights.find(ACL_EXPUNGE[0]) == myrights.npos &&
			    myrights.find(ACL_ADMINISTER[0]) == myrights.npos)
			{
				writes(tag);
				accessdenied("GETACL", f,
					     ACL_ADMINISTER);
				return 0;
			}
		}

		auto mb=get_myrightson(f);

		writes("* MYRIGHTS \"");
		writemailbox(f);
		writes("\" \"");

		writeacl1(mb);
		writes("\"\r\n");
		writes(tag);
		writes(" OK MYRIGHTS completed.\r\n");
		return 0;
	}

	/* mailbox commands */

	if (current_maildir_info.current_mailbox.empty())	return (-1);

	if (curtoken->tokenbuf == "UID")
	{
		uid=true;
		if ((curtoken=nexttoken())->tokentype != IT_ATOM)
			return (-1);
		if (curtoken->tokenbuf != "COPY" &&
		    curtoken->tokenbuf != "FETCH" &&
		    curtoken->tokenbuf != "SEARCH" &&
		    curtoken->tokenbuf != "THREAD" &&
		    curtoken->tokenbuf != "SORT" &&
		    curtoken->tokenbuf != "STORE" &&
		    curtoken->tokenbuf != "EXPUNGE")
			return (-1);
	}

	if (curtoken->tokenbuf == "CLOSE")
	{
		if (nexttoken()->tokentype != IT_EOL)
			return (-1);

		if (!current_mailbox_ro
		    && current_maildir_info.has_acl(ACL_EXPUNGE[0]))
			expunge();
		current_maildir_info=imapscaninfo{""};
		writes(tag);
		writes(" OK mailbox closed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "FETCH")
	{
		std::list<fetchinfo> filist;

		curtoken=nexttoken();
		if (!ismsgset(curtoken))	return (-1);

		std::string msgset{curtoken->tokenbuf};

		if ((curtoken=nexttoken())->tokentype != IT_LPAREN)
		{
			if (curtoken->tokentype != IT_ATOM)
			{
				return (-1);
			}
			if (!fetchinfo_alloc(true, filist))
				return -1;
		}
		else
		{
			(void)nexttoken();

			if (!fetchinfo_alloc(false, filist))
				return -1;
			if (currenttoken()->tokentype != IT_RPAREN)
				return -1;

			nexttoken();
		}

		if (currenttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		do_msgset(msgset,
			  [&]
			  (unsigned long n)
			  {
				  return do_fetch(n, uid, filist) == 0;
			  }, uid);
		writes(tag);
		writes(" OK FETCH completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "STORE")
	{
		storeinfo storeinfo_s;

		curtoken=nexttoken();
		if (!ismsgset(curtoken))	return (-1);
		std::string msgset{curtoken->tokenbuf};

		(void)nexttoken();

		if (!storeinfo_init(storeinfo_s) ||
			currenttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		/* Do not change \Deleted if this is a readonly mailbox */

		if (current_mailbox_ro && storeinfo_s.flags.deleted)
		{
			writes(tag);
			writes(" NO Current box is selected READ-ONLY.\r\n");
			return (0);
		}

		fetch_free_cache();

		mail::keywords::list all_keywords;

		current_maildir_info.keywords->enumerate_keywords(
			[&]
			(const std::string &kw)
			{
				all_keywords.insert(kw);
			});

		bool has_new_keyword=false;

		for (auto &ke:storeinfo_s.keywords)
		{
			if (all_keywords.insert(ke).second)
				has_new_keyword=1;
		};

		if (has_new_keyword)
			mailboxflags(current_mailbox_ro, all_keywords);

		bool flag=do_msgset(
			msgset,
			[&]
			(unsigned long n)
			{
				if (do_store(n, uid, &storeinfo_s) == 0)
				{
					--n;
					if (n < current_maildir_info.msgs.size()
					)
					{
						if (uid)
							fetchflags_byuid(n);
						else
							fetchflags(n);
					}
					return true;
				}
				return false;
			}, uid);

		size_t n=0;

		current_maildir_info.keywords->enumerate_keywords(
			[&]
			(const std::string &kw)
			{
				++n;
			});

		if (n != all_keywords.size())
			mailboxflags(current_mailbox_ro);

		if (!flag)
		{
			writes(tag);
			writes(" NO [ALERT] You exceeded your mail quota.\r\n");
			return (0);
		}

		writes(tag);
		writes(" OK STORE completed.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "SEARCH")
	{
		std::string charset;
		contentsearch cs;
		unsigned long i;

		curtoken=nexttoken_okbracket();
		if (curtoken->tokentype == IT_ATOM &&
			curtoken->tokenbuf == "CHARSET")
		{
			if (enabled_utf8)
			{
				writes(tag);
				writes(" NO CHARSET is not valid in UTF8 mode "
				       "as per RFC 6855\r\n");
				return (0);
			}

			curtoken=nexttoken();
			if (curtoken->tokentype != IT_ATOM &&
				curtoken->tokentype != IT_QUOTED_STRING)
				return (-1);

			charset=curtoken->tokenbuf;
			curtoken=nexttoken();
		}

		if (!validate_charset(tag, charset))
		{
			return (0);
		}

		auto si=cs.alloc_parsesearch();

		if (si == cs.searchlist.end())
		{
			return (-1);
		}
		if (currenttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

#if 0
		writes("* OK ");
		debug_search(si);
		writes("\r\n");
#endif
		writes("* SEARCH");

		cs.search_internal(
			si, charset,
			[uid]
			(unsigned long i)
			{
				writes(" ");
				writen(uid ? current_maildir_info.msgs[i].uid
				       :i+1);
			});

		writes("\r\n");

		for (i=0; i<current_maildir_info.msgs.size(); i++)
			if (current_maildir_info.msgs[i].changedflags)
				fetchflags(i);
		writes(tag);
		writes(" OK SEARCH done.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "THREAD")
	{
		std::string charset;
		contentsearch cs;
		unsigned long i;

		/* The following jazz is mainly for future extensions */

		void (contentsearch::*thread_func)(searchiter,
						   const std::string &, bool);
		search_type thread_type;

		{
			const char *p=getenv("IMAP_DISABLETHREADSORT");
			int n= p ? atoi(p):0;

			if (n > 0)
			{
				writes(tag);
				writes(" NO This command is disabled by the system administrator.\r\n");
				return (0);
			}
		}

		curtoken=nexttoken();
		if (curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
			return (-1);

		if (curtoken->tokenbuf == "ORDEREDSUBJECT")
		{
			thread_func=&contentsearch::dothreadorderedsubj;
			thread_type=search_orderedsubj;
		}
		else if (curtoken->tokenbuf == "REFERENCES")
		{
			thread_func=&contentsearch::dothreadreferences;
			thread_type=search_references1;
		}
		else
		{
			return (-1);
		}

		curtoken=nexttoken();
		if (curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
			return (-1);

		charset=curtoken->tokenbuf;
		curtoken=nexttoken();

		auto si=cs.alloc_parsesearch();

		if (si == cs.searchlist.end())
		{
			return (-1);
		}

		si=cs.alloc_searchextra(si, thread_type);

		if (currenttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		if (!validate_charset(tag, charset))
		{
			return (0);
		}

		writes("* THREAD ");
		(cs.*thread_func)(si, charset, uid);
		writes("\r\n");

		for (i=0; i<current_maildir_info.msgs.size(); i++)
			if (current_maildir_info.msgs[i].changedflags)
				fetchflags(i);
		writes(tag);
		writes(" OK THREAD done.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "SORT")
	{
		std::string charset;
		contentsearch cs;
		unsigned long i;
		std::vector<search_type> ts;

		{
		const char *p=getenv("IMAP_DISABLETHREADSORT");
		int n= p ? atoi(p):0;

			if (n > 0)
			{
				writes(tag);
				writes(" NO This command is disabled by the system administrator.\r\n");
				return (0);
			}
		}

		curtoken=nexttoken();
		if (curtoken->tokentype != IT_LPAREN)	return (-1);
		while ((curtoken=nexttoken())->tokentype != IT_RPAREN)
		{
			search_type st;

			if (curtoken->tokentype != IT_ATOM &&
				curtoken->tokentype != IT_QUOTED_STRING)
			{
				return (-1);
			}

			if (curtoken->tokenbuf == "SUBJECT")
			{
				st=search_orderedsubj;
			}
			else if (curtoken->tokenbuf == "ARRIVAL")
			{
				st=search_arrival;
			}
			else if (curtoken->tokenbuf == "CC")
			{
				st=search_cc;
			}
			else if (curtoken->tokenbuf == "DATE")
			{
				st=search_date;
			}
			else if (curtoken->tokenbuf == "FROM")
			{
				st=search_from;
			}
			else if (curtoken->tokenbuf == "REVERSE")
			{
				st=search_reverse;
			}
			else if (curtoken->tokenbuf == "SIZE")
			{
				st=search_size;
			}
			else if (curtoken->tokenbuf == "TO")
			{
				st=search_to;
			}
			else
			{
				return (-1);
			}

			ts.push_back(st);
		}

		if (ts.empty()	/* No criteria */
		    || ts.back() == search_reverse)
				/* Can't end with the REVERSE keyword */
		{
			return (-1);
		}

		curtoken=nexttoken();
		if (curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			return (-1);
		}

		charset=curtoken->tokenbuf;
		curtoken=nexttoken();

		auto si=cs.alloc_parsesearch();

		if (si == cs.searchlist.end())
		{
			return (-1);
		}

		for (auto b=ts.begin(), e=ts.end(); b != e;)
		{
			--e;

			si=cs.alloc_searchextra(si, *e);
		}
		ts.clear();

		if (currenttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		if (!validate_charset(tag, charset))
		{
			return (0);
		}

		writes("* SORT");
		cs.dosortmsgs(si, charset, uid);
		writes("\r\n");

		for (i=0; i<current_maildir_info.msgs.size(); i++)
			if (current_maildir_info.msgs[i].changedflags)
				fetchflags(i);
		writes(tag);
		writes(" OK SORT done.\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "CHECK")
	{
		if (nexttoken()->tokentype != IT_EOL)	return (-1);
		doNoop(0);
		writes(tag);
		writes(" OK CHECK completed\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "EXPUNGE")
	{
		if (!current_maildir_info.has_acl(ACL_EXPUNGE[0]))
		{
			writes(tag);
			accessdenied("EXPUNGE", "current mailbox",
				     ACL_EXPUNGE);
			return 0;
		}

		if (current_mailbox_ro)
		{
			writes(tag);
			writes(" NO Cannot expunge read-only mailbox.\r\n");
			return 0;
		}

		if (uid)
		{
			curtoken=nexttoken();
			if (!ismsgset(curtoken))	return (-1);
			std::string msgset{curtoken->tokenbuf};
			if (nexttoken()->tokentype != IT_EOL)	return (-1);

			do_msgset(msgset,
				  [&]
				  (unsigned long n)
				  {
					  do_expunge(n-1, n, 0);
					  return true;
				  }, true);
		}
		else
		{
			if (nexttoken()->tokentype != IT_EOL)	return (-1);
			expunge();
		}
		doNoop(0);
		writes(tag);
		writes(" OK EXPUNGE completed\r\n");
		return (0);
	}

	if (curtoken->tokenbuf == "COPY")
	{
	struct maildirsize quotainfo;
	struct copyquotainfo cqinfo;
	int	has_quota;
	int	isshared;
	struct do_copy_info copy_info;
	unsigned long copy_uidv;
	char access_rights[8];

		curtoken=nexttoken();
		if (!ismsgset(curtoken))	return (-1);
		std::string msgset{curtoken->tokenbuf};

		curtoken=nexttoken_nouc();

		if (curtoken->tokentype != IT_NUMBER &&
			curtoken->tokentype != IT_ATOM &&
			curtoken->tokentype != IT_QUOTED_STRING)
		{
			writes(tag);
			writes(" BAD Invalid command\r\n");
			return (0);
		}

		auto dest_mailbox=decode_valid_mailbox(curtoken->tokenbuf, 1);

		if (dest_mailbox.empty())
		{
			if (maildir::info_imap_find(curtoken->tokenbuf,
						    getenv("AUTHENTICATED")))
			{
				if (nexttoken()->tokentype == IT_EOL)
				{
					writes(tag);
					writes(" NO [TRYCREATE] Mailbox does not exist.\r\n");
					return (0);
				}
			}
			return (-1);
		}

		{
			auto f=maildir::imap_foldername_to_filename(
				enabled_utf8,
				curtoken->tokenbuf);
			if (f.empty())
				return -1;
			CHECK_RIGHTSM(f,
				      append_rights,
				      ACL_INSERT ACL_DELETEMSGS
				      ACL_SEEN ACL_WRITE);

			if (strchr(append_rights, ACL_INSERT[0]) == NULL)
			{
				writes(tag);
				accessdenied("COPY",
					     f,
					     ACL_INSERT);
				return 0;
			}
			strcpy(access_rights, append_rights);
		}

		if (nexttoken()->tokentype != IT_EOL)
		{
			return (-1);
		}

		if (access(dest_mailbox.c_str(), 0))
		{
			writes(tag);
			writes(" NO [TRYCREATE] Mailbox does not exist.\r\n");
			return (0);
		}

		fetch_free_cache();
		cqinfo.destmailbox=dest_mailbox.c_str();
		cqinfo.acls=access_rights;

		/*
		** If the destination is a shared folder, copy it into the
		** real shared folder.
		*/

		isshared=0;
		if (is_sharedsubdir(cqinfo.destmailbox))
		{
			dest_mailbox += "/shared";
			cqinfo.destmailbox=dest_mailbox.c_str();
			isshared=1;
		}

		cqinfo.nbytes=0;
		cqinfo.nfiles=0;

		has_quota=0;
		if (!isshared && maildirquota_countfolder(cqinfo.destmailbox))
		{
			if (maildir_openquotafile(&quotainfo,
						  ".") == 0)
			{
				if (quotainfo.fd >= 0)
					has_quota=1;
				maildir_closequotafile(&quotainfo);
			}

			if (has_quota > 0 &&
			    !do_msgset(msgset,
				       [&]
				       (unsigned long n)
				       {
					       return do_copy_quota_calc(
						       n, uid, &cqinfo
					       ) == 0;
				       },
				      uid))
				has_quota= -1;
		}

		if (has_quota > 0 && cqinfo.nfiles > 0)
		{

			if (maildir_quota_add_start(".", &quotainfo,
						    cqinfo.nbytes,
						    cqinfo.nfiles,
						    getenv("MAILDIRQUOTA")))
			{
				writes(tag);
				writes(
			" NO [ALERT] You exceeded your mail quota.\r\n");
				return (0);
			}

			maildir_quota_add_end(&quotainfo,
					      cqinfo.nbytes,
					      cqinfo.nfiles);
		}

		if (is_outbox(dest_mailbox.c_str()))
		{
			int counter=0;

			// Count selected messages (if there's >1
			// copy to OUTBOX should fail).

			const char *p=getenv("OUTBOX_MULTIPLE_SEND");

			bool allow_multiple=p && atoi(p);

			if (!do_msgset(msgset,
				      [&]
				      (unsigned long)
				      {

					      ++counter;

					      if (allow_multiple)
						      counter=1;

					      /* Suppress the error, below */

					      return true;
				      }, uid) ||
			    counter > 1)
			{
				writes(tag);
				writes(" NO [ALERT] Only one message may be sent at a time.\r\n");
				return (0);
			}
		}

		copy_info.mailbox=dest_mailbox.c_str();
		copy_info.acls=access_rights;

		if (has_quota < 0 ||
		    !do_msgset(msgset,
			       [&]
			       (unsigned long n)
			       {
				       return do_copy_message(
					       n, uid, &copy_info
				       ) == 0;
			       }, uid) ||
		    uidplus_fill(copy_info.mailbox, copy_info.uidplus,
				 &copy_uidv))
		{
			uidplus_abort(copy_info.uidplus);
			writes(tag);
			writes(" NO [ALERT] COPY failed - no write permission or out of disk space.\r\n");
			return (0);
		}

		dirsync(dest_mailbox.c_str());

		writes(tag);
		writes(" OK");

		if (!copy_info.uidplus.empty())
		{
			writes(" [COPYUID ");
			writen(copy_uidv);
			uidplus_writemsgset(copy_info.uidplus, 0);
			uidplus_writemsgset(copy_info.uidplus, 1);
			writes("]");
		}

		writes(" COPY completed.\r\n");

		return (0);
	}
	return (-1);
}

static void dogethostname()
{
	char	buf[2048];

	if (gethostname(buf, sizeof(buf)) < 0)
		strcpy(buf, "courier-imap");
	setenv("HOSTNAME", buf, 1);
}

static void chkdisabled(const char *ip, const char *port)
{
	const char *p;
	if (auth_getoptionenvint("disableimap"))
	{
		writes("* BYE IMAP access disabled for this account.\r\n");
		writeflush();
		exit(0);
	}

	if (    auth_getoptionenvint("disableinsecureimap")
	    && ((p=getenv("IMAP_TLS")) == NULL || !atoi(p)))
	{
		writes("* BYE IMAP access disabled via insecure connection.\r\n");
		writeflush();
		exit(0);
	}

	fprintf(stderr, "INFO: LOGIN, user=%s, ip=[%s], port=[%s], protocol=%s%s\n",
		getenv("AUTHENTICATED"), ip, port,
		protocol,
		(p=getenv("IMAP_TLS")) != 0 && atoi(p) ? ", starttls=1" : "");
}

static int chk_clock_skew()
{
	static const char fn[]="tmp/courier-imap.clockskew.chk";
	struct stat stat_buf;
	int fd;
	time_t t;

	unlink(fn);
	fd=open(fn, O_RDWR|O_TRUNC|O_CREAT, 0666);
	time(&t);

	if (fd < 0)
		return 0; /* Something else is wrong */

	if (fstat(fd, &stat_buf) < 0)
	{
		close(fd);
		return -1; /* Something else is wrong */
	}
	close(fd);
	unlink(fn);

	if (stat_buf.st_mtime < t - 30 || stat_buf.st_mtime > t+30)
		return -1;
	return 0;
}

#if SMAP

static int is_smap()
{
	const char *p;

	p=getenv("PROTOCOL");

	if (p && strcmp(p, "SMAP1") == 0)
		return 1;
	return 0;
}

#else

#define is_smap() 0

#endif


int main(int argc, char **argv)
{
	const char *ip;
	const char *p;
	const char *tag;
	const char *port;
	mode_t oldumask;

#ifdef HAVE_SETVBUF_IOLBF
	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
#endif
	time(&start_time);
	if (argc > 1 && strcmp(argv[1], "--version") == 0)
	{
		printf("%s\n", PROGRAMVERSION);
		exit(0);
	}

	if ((tag=getenv("IMAPLOGINTAG")) != 0)
	{
		if (getenv("AUTHENTICATED") == NULL)
		{
			printf("* BYE AUTHENTICATED environment variable not set.\r\n");
			fflush(stdout);
			exit(0);
		}
	}
	else
	{
		const char *p;

		setenv("TCPREMOTEIP", "127.0.0.1", 1);
		setenv("TCPREMOTEPORT", "0", 1);

		p=getenv("AUTHENTICATED");
		if (!p || !*p)
		{
			struct passwd *pw=getpwuid(getuid());

			if (!pw)
			{
				fprintf(stderr,
					"ERR: uid %lu not found in passwd file\n",
					(unsigned long)getuid());
				exit(1);
			}

			setenv("AUTHENTICATED", pw->pw_name, 1);
		}
	}

#if HAVE_SETLOCALE
	setlocale(LC_CTYPE, "C");
#endif

	ip=getenv("TCPREMOTEIP");
	if (!ip || !*ip)	exit(0);

	port=getenv("TCPREMOTEPORT");
	if (!port || !*port)	exit(0);

	protocol=getenv("PROTOCOL");

	if (!protocol || !*protocol)
		protocol="IMAP";

	setenv("IMAP_STARTTLS", "NO", 1);	/* No longer grok STARTTLS */

	/* We use select() with a timeout, so use non-blocking filedescs */

	if (fcntl(0, F_SETFL, O_NONBLOCK) ||
	    fcntl(1, F_SETFL, O_NONBLOCK))
	{
		perror("fcntl");
		exit(1);
	}

	{
	struct	stat	buf;

		if ( stat(".", &buf) < 0 || buf.st_mode & S_ISVTX)
		{
			fprintf(stderr, "INFO: LOCKED, user=%s, ip=[%s], port=[%s]\n",
				getenv("AUTHENTICATED"), ip, port);

			if (is_smap())
				writes("-ERR ");
			else
				writes("* BYE ");

			writes("Your account is temporarily unavailable (+t bit set on home directory).\r\n");
			writeflush();
			exit(0);
		}
	}

	if (argc > 1)
		p=argv[1];
	else
		p=getenv("MAILDIR");

	if (!p)
		p="./Maildir";
#if 0
	imapscanpath=getimapscanpath(argv[0]);
#endif
	if (chdir(p))
	{
		fprintf(stderr, "chdir %s: %s\n", p, strerror(errno));
		write_error_exit(strerror(errno));
	}
	maildir_loginexec();

	if (auth_getoptionenvint("disableshared"))
	{
		maildir_acl_disabled=1;
		maildir_newshared_disabled=1;
	}

	/* Remember my device/inode */

	{
		struct	stat	buf;

		if ( stat(".", &buf) < 0)
			write_error_exit("Cannot stat current directory");

		homedir_dev=buf.st_dev;
		homedir_ino=buf.st_ino;

		errno=0;

		p=getenv("IMAP_MAILBOX_SANITY_CHECK");

		if (!p || !*p) p="1";

		if (atoi(p))
		{
			if ( buf.st_uid != geteuid() ||
			     buf.st_gid != getegid())
				write_error_exit("Account's mailbox directory is not owned by the correct uid or gid");
		}
	}

	p=getenv("HOSTNAME");
	if (!p)
		dogethostname();

	if ((p=getenv("IMAP_TRASHFOLDERNAME")) != 0 && *p)
	{
		trash = p;

		dot_trash=".";
		dot_trash += trash;
	}

#if 0
	mdcreate("." DRAFTS);
#endif

	if ((p=getenv("IMAPDEBUGFILE")) != 0 && *p &&
	    access(p, 0) == 0)
	{
		oldumask = umask(027);
		debugfile=fopen(p, "a");
		umask(oldumask);
		if (debugfile==NULL)
			write_error_exit(0);
	}
	initcapability();

	emptytrash();
	signal(SIGPIPE, SIG_IGN);

	libmail_kwVerbotten=KEYWORD_IMAPVERBOTTEN;

	if (!keywords())
		libmail_kwEnabled=0;

	maildir_info_munge_complex((p=getenv("IMAP_SHAREDMUNGENAMES")) &&
				   atoi(p));

#if SMAP
	if (is_smap())
	{
		if (chk_clock_skew())
		{
			writes("-ERR Clock skew detected. Check the clock on the file server\r\n");
			writeflush();
			exit(0);
		}

		writes("+OK SMAP1 LOGIN Ok.\n");

		smapflag=1;

		libmail_kwVerbotten=KEYWORD_SMAPVERBOTTEN;

		chkdisabled(ip, port);
		smap();
		logoutmsg();
		emptytrash();
		return (0);
	}
#endif

	if (chk_clock_skew())
	{
		writes("* BYE Clock skew detected. Check the clock on the file server\r\n");
		writeflush();
		exit(0);
	}

	{
		struct maildirwatch *w;

		if ((w=maildirwatch_alloc(".")) == NULL)
		{
			writes("* OK [ALERT] Inotify initialization error\r\n");
		}
		else
		{
			maildirwatch_free(w);
		}
	}

	if ((tag=getenv("IMAPLOGINTAG")) != 0)
	{
		writes(tag);
		writes(" OK LOGIN Ok.\r\n");
	}
	else
		writes("* PREAUTH Ready.\r\n");
	writeflush();
	chkdisabled(ip, port);
	mainloop();
	fetch_free_cached();
	bye();
	return (0);
}
