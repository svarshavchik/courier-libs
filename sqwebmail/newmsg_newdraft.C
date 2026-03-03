/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	"cgi/cgi.h"
#include	"sqconfig.h"
#include	"sqwebmail.h"
#include	"auth.h"
#include	"pref.h"
#include	"maildir.h"
#include	"folder.h"
#include	"mailinglist.h"
#include	"maildir/maildirmisc.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"rfc2045/rfc2045charset.h"
#include	"rfc2045/rfc2045reply.h"
#include	<stdlib.h>
#include	<unordered_set>
#include	<string>
#include	<sstream>

#include	<fcntl.h>
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif

extern const char *sqwebmail_mailboxid;
extern const char *sqwebmail_content_charset;

extern std::string get_msgfilename(const char *, size_t *);

std::string newmsg_newdraft(const char *folder, const char *pos,
			    const char *forwardsep, const char *replysalut)
{
	size_t	pos_n;

	const	char *mimeidptr;
	rfc2045::replymode_t replymode;

	if (*cgi("reply"))
	{
		replymode=rfc2045::replymode_t::reply;
	}
	else if (*cgi("replyall"))
	{
		replymode=rfc2045::replymode_t::replyall;
	}
	else if (*cgi("replylist"))
	{
		replymode=rfc2045::replymode_t::replylist;
	}
	else if (*cgi("forward"))
	{
		replymode=rfc2045::replymode_t::forward;
	}
	else if (*cgi("forwardatt"))
	{
		replymode=rfc2045::replymode_t::forwardatt;
	}
	else
	{
		return "";
	}

	pos_n=atol(pos);
	auto filename=get_msgfilename(folder, &pos_n);

	if (filename.empty())	return ("");

	rfc822::fdstreambuf fp{
		maildir_semisafeopen(filename.c_str(), O_RDONLY, 0)
	};

	if (fp.error())
		return ("");

	rfc2045::entity message;

	{
		std::istreambuf_iterator<char> b{&fp}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	mimeidptr=cgi("mimeid");

	const rfc2045::entity *rfc2045partp{nullptr};

	if (*mimeidptr)
	{
		rfc2045partp=message.find(mimeidptr);

		if (!rfc2045partp || !rfc2045_message_content_type(
			    rfc2045partp->content_type.value.c_str()
		    ) || rfc2045partp->subentities.empty())
		{
			rfc2045partp=nullptr;
		}
		else
		{
			rfc2045partp= &rfc2045partp->subentities[0];
		}
	}

	if (!rfc2045partp)
		rfc2045partp=&message;

	std::string draftfilename;

	int draftfd=maildir_createmsg(INBOX "." DRAFTS, 0, draftfilename);
	if (draftfd < 0)
		enomem();

	maildir_writemsgstr(draftfd, "From: ");
	{
	const char *f=pref_from;

		if (!f || !*f)	f=login_fromhdr();
		if (!f)	f="";

		f=rfc2047_encode_header_tobuf("to", f,
					      sqwebmail_content_charset);

		maildir_writemsgstr(draftfd, f);
		maildir_writemsgstr(draftfd, "\n");
	}

	{
		std::unordered_set<std::string> mailinglists;
		std::unordered_set<std::string_view> mailinglists_sv;

		char *ml=getmailinglists();

		{
			std::istringstream i{std::string{ml}};
			free(ml);

			std::string s;

			while (std::getline(i, s))
				mailinglists.insert(s);
			for (auto &s:mailinglists)
				mailinglists_sv.insert(s);
		}

		rfc2045::reply ri{replymode};

		ri.replysalut=replysalut;
		ri.forwardsep=forwardsep;
		ri.myaddr_func=[]
			(auto addr)
		{
			return addr == login_returnaddr();
		};
		ri.is_mailinglist=[&]
			(std::string_view ml)
		{
			return mailinglists_sv.find(ml) !=
				mailinglists_sv.end();
		};
		ri.charset=sqwebmail_content_charset;

		switch (replymode) {
		case rfc2045::replymode_t::forward:
		case rfc2045::replymode_t::forwardatt:
			break;
		default:
			{
				char *basename=maildir_basename(filename.c_str());

				maildir_writemsgstr(draftfd,
						    "X-Reply-To-Folder: ");
				maildir_writemsgstr(draftfd, folder);
				maildir_writemsgstr(draftfd,
						    "\nX-Reply-To-Msg: ");

				maildir_writemsgstr(draftfd, basename);
				free(basename);
				maildir_writemsgstr(draftfd, "\n");
			}
		}

		ri(
			[&]
			(std::string_view chunk)
			{
				maildir_writemsg(draftfd,
						 chunk.data(),
						 chunk.size());
			},
			*rfc2045partp,
			fp
		);
	}

	if (maildir_closemsg(draftfd, INBOX "." DRAFTS,
			     draftfilename.c_str(), 1, 0))
	{
		cgi_put("error", "quota");
	}

	return(draftfilename);
}
