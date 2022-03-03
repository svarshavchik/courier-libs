/*
** Copyright 1998 - 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#if     HAVE_UNISTD_H
#include        <unistd.h>
#endif
#include	<errno.h>
#include	"imaptoken.h"
#include	"imapscanclient.h"
#include	"imapwrite.h"
#include	"storeinfo.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildiraclt.h"
#include	"outbox.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<sys/stat.h>
#if	HAVE_UTIME_H
#include	<utime.h>
#endif
#include	<time.h>
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#endif
#include <string>

#if SMAP
extern int smapflag;
#endif

extern void mailboxflags(int ro);
extern int current_mailbox_ro;
std::string get_reflagged_filename(std::string fn, struct imapflags &newflags);
extern int is_trash(const char *);
extern bool get_flagname(std::string s, struct imapflags *flags);
extern bool get_flagsAndKeywords(imapflags &flags,
				 mail::keywords::list &keywords);
void get_message_flags( struct imapscanmessageinfo *,
			char *, struct imapflags *);
int reflag_filename(struct imapscanmessageinfo *, struct imapflags *,
			       int);
extern FILE *maildir_mkfilename(const char *, struct imapflags *,
				unsigned long, std::string &, std::string &);
extern int acl_flags_adjust(const char *access_rights,
			    struct imapflags *flags);

extern imapscaninfo current_maildir_info;
extern "C" int fastkeywords();

bool storeinfo_init(struct storeinfo &si)
{
	imaptoken t=currenttoken();
	const char *p;

	if (t->tokentype != IT_ATOM)	return false;
	si.plusminus=0;
	si.silent=0;

	p=t->tokenbuf.c_str();
	if (*p == '+' || *p == '-')
		si.plusminus= *p++;
	if (strncmp(p, "FLAGS", 5))	return false;
	p += 5;
	if (*p)
	{
		if (strcmp(p, ".SILENT"))	return false;
		si.silent=1;
	}

	memset(&si.flags, 0, sizeof(si.flags));

	t=nexttoken_noparseliteral();
	si.keywords.clear();
	if (t->tokentype == IT_LPAREN)
	{
		if (!get_flagsAndKeywords(si.flags, si.keywords))
		{
			return false;
		}
		nexttoken();
	}
	else if (t->tokentype == IT_NIL)
		nexttoken();
	else if (t->tokentype == IT_ATOM)
	{
		if (!get_flagname(t->tokenbuf, &si.flags))
			si.keywords.insert(t->tokenbuf);
		nexttoken();
	}

	return true;
}

int do_store(unsigned long n, int byuid, storeinfo *si)
{
	int	fd;
	struct imapflags new_flags, old_flags;
	int changedKeywords;
	bool kwAllowed=true;

	--n;
	fd=imapscan_openfile(&current_maildir_info, n);
	if (fd < 0)	return (0);

	changedKeywords=0;
	get_message_flags(&current_maildir_info.msgs.at(n), 0, &new_flags);

	old_flags=new_flags;

	if (!current_maildir_info.has_acl(ACL_WRITE[0]))
		kwAllowed=false;

	auto &keywords=current_maildir_info.msgs[n].keywords;

	auto previous_keywords=keywords;

	auto current_keywords=keywords.keywords();
	bool changed=false;

	if (si->plusminus == '+')
	{
		if (si->flags.drafts)	new_flags.drafts=1;
		if (si->flags.seen)	new_flags.seen=1;
		if (si->flags.answered)	new_flags.answered=1;
		if (si->flags.deleted)	new_flags.deleted=1;
		if (si->flags.flagged)	new_flags.flagged=1;

		for (const auto &kw:si->keywords)
		{
			if (!kwAllowed)
			{
				current_maildir_info.msgs[n].changedflags=1;
				break;
			}
			if (current_keywords.insert(kw).second)
			{

				changed=true;
				if (fastkeywords())
					changedKeywords=1;
				current_maildir_info.msgs[n].changedflags=1;
			}
		}
		if (changed)
		{
			keywords.keywords(
				current_maildir_info.keywords,
				current_keywords,
				n
			);
		}
	}
	else if (si->plusminus == '-')
	{
		if (si->flags.drafts)	new_flags.drafts=0;
		if (si->flags.seen)	new_flags.seen=0;
		if (si->flags.answered)	new_flags.answered=0;
		if (si->flags.deleted)	new_flags.deleted=0;
		if (si->flags.flagged)	new_flags.flagged=0;

		for (const auto &kw:si->keywords)
		{
			if (!kwAllowed)
			{
				current_maildir_info.msgs[n].changedflags=1;
				break;
			}
			if (current_keywords.erase(kw))
			{
				changed=true;
				if (fastkeywords())
					changedKeywords=1;
				current_maildir_info.msgs[n].changedflags=1;
			}
		}
		if (changed)
		{
			keywords.keywords(
				current_maildir_info.keywords,
				current_keywords,
				n
			);
		}
	}
	else
	{
		new_flags=si->flags;

		size_t n=0;

		for (const auto &kw:current_keywords)
		{
			if (si->keywords.find(kw) != si->keywords.end())
			{
				++n;
			}
			else
				changed=true;
		}

		if (n != si->keywords.size())
			changed=true;

		if (changed && !kwAllowed)
		{
			changed=false;
			current_maildir_info.msgs[n].changedflags=1;
		}

		if (changed)
		{
			if (fastkeywords())
				changedKeywords=1;
			current_maildir_info.msgs[n].changedflags=1;

			keywords.keywords(
				current_maildir_info.keywords,
				si->keywords,
				n);
			current_keywords=si->keywords;
		}
	}

	if (!current_maildir_info.has_acl(ACL_WRITE[0]))
	{
		new_flags.drafts=old_flags.drafts;
		new_flags.answered=old_flags.answered;
		new_flags.flagged=old_flags.flagged;

		// TODO: set changedKeywords to false
	}

	if (!current_maildir_info.has_acl(ACL_SEEN[0]))
	{
		new_flags.seen=old_flags.seen;
	}

	if (!current_maildir_info.has_acl(ACL_DELETEMSGS[0]))
	{
		new_flags.deleted=old_flags.deleted;
	}

	if (changedKeywords)
	{
		current_maildir_info.msgs[n].changedflags=1;
		imapscan_updateKeywords(current_maildir_info.msgs[n].filename,
					current_keywords);
	}

	auto old_file=current_maildir_info.msgs[n].filename;

	if (reflag_filename(&current_maildir_info.msgs[n], &new_flags, fd))
	{
		close(fd);
		return (-1);
	}
	close(fd);

	if (old_file != current_maildir_info.msgs[n].filename)
		current_maildir_info.msgs[n].changedflags=1;

	if (si->silent)
		current_maildir_info.msgs[n].changedflags=0;

	return (0);
}

static int copy_message(int fd,
			struct do_copy_info *cpy_info,
			struct	imapflags *flags,
			const keywords_t &keywords,
			unsigned long old_uid)
{
	std::string tmpname, newname;
	FILE	*fp;
	struct	stat	stat_buf;
	char	buf[BUFSIZ];

	if (fstat(fd, &stat_buf) < 0)
	{
		return (-1);
	}

	fp=maildir_mkfilename(cpy_info->mailbox, flags, stat_buf.st_size,
			      tmpname, newname);

	if (!fp)
	{
		return (-1);
	}

	while (stat_buf.st_size)
	{
	int	n=sizeof(buf);

		if (n > stat_buf.st_size)
			n=stat_buf.st_size;

		n=read(fd, buf, n);

		if (n <= 0 || (int)fwrite(buf, 1, n, fp) != n)
		{
			fprintf(stderr,
			"ERR: error copying a message, user=%s, errno=%d\n",
				getenv("AUTHENTICATED"), errno);

			fclose(fp);
			unlink(tmpname.c_str());
			return (-1);
		}
		stat_buf.st_size -= n;
	}

	if (fflush(fp) || ferror(fp))
	{
		fclose(fp);
		return (-1);
	}
	fclose(fp);

	cpy_info->uidplus.emplace_back();
	auto new_uidplus_info=--cpy_info->uidplus.end();

	new_uidplus_info->mtime = stat_buf.st_mtime;

	if (check_outbox(tmpname.c_str(), cpy_info->mailbox))
	{
		unlink(tmpname.c_str());
		return (-1);
	}

	auto new_keywords=keywords.keywords();

	if (!new_keywords.empty())
		imapscan_updateKeywords(
			cpy_info->mailbox,
			strrchr(newname.c_str(), '/')+1,
			keywords.keywords()
		);

	new_uidplus_info->tmpfilename=tmpname;
	new_uidplus_info->curfilename=newname;

	new_uidplus_info->old_uid=old_uid;
	return (0);
}

int do_copy_message(unsigned long n, int byuid, void *voidptr)
{
	struct do_copy_info *cpy_info=(struct do_copy_info *)voidptr;
	int	fd;
	struct imapflags new_flags;

	--n;
	fd=imapscan_openfile(&current_maildir_info, n);
	if (fd < 0)	return (0);
	get_message_flags(&current_maildir_info.msgs.at(n), 0, &new_flags);

	if (copy_message(fd, cpy_info, &new_flags,

			 acl_flags_adjust(cpy_info->acls,
					  &new_flags)
			 ? keywords_t{}
			 : current_maildir_info.msgs[n].keywords,
			 current_maildir_info.msgs[n].uid))
	{
		close(fd);
		return (-1);
	}
	close(fd);
	current_maildir_info.msgs[n].copiedflag=1;
	return (0);
}

int do_copy_quota_calc(unsigned long n, int byuid, void *voidptr)
{
	struct copyquotainfo *info=(struct copyquotainfo *)voidptr;
	unsigned long nbytes;
	struct	stat	stat_buf;
	int	fd;
	struct imapflags flags;

	--n;

	fd=imapscan_openfile(&current_maildir_info, n);
	if (fd < 0)	return (0);
	auto filename=current_maildir_info.msgs[n].filename;

	get_message_flags(&current_maildir_info.msgs[n], NULL, &flags);

	(void)acl_flags_adjust(info->acls, &flags);

	auto ff=get_reflagged_filename(filename, flags);

	if (maildirquota_countfile(ff.c_str()))
	{
		if (maildir_parsequota(ff.c_str(), &nbytes))
		{
			if (fstat(fd, &stat_buf) < 0)
			{
				close(fd);
				return (0);
			}
			nbytes=stat_buf.st_size;
		}
		info->nbytes += nbytes;
		info->nfiles += 1;
	}
	close(fd);
	return (0);
}
