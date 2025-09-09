/*
** Copyright 2001-2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "dbobj.h"
#include "liblock/config.h"
#include "liblock/liblock.h"
#include "maildir/maildirmisc.h"
#include <courier-unicode.h>
#include "numlib/numlib.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#if HAVE_LOCALE_H
#include <locale.h>
#endif
#include <langinfo.h>
#if HAVE_STRINGS_H
#include <strings.h>
#endif
#include <ctype.h>
#include "rfc822/rfc822.h"
#include "rfc2045/rfc2045.h"
#include "rfc2045/rfc2045charset.h"
#include "rfc2045/rfc2045reply.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include "mywait.h"
#include <signal.h>
#if HAVE_SYSEXITS_H
#include <sysexits.h>
#endif

#ifndef EX_TEMPFAIL
#define EX_TEMPFAIL	75
#endif

#include <vector>
#include <string>
#include <string_view>
#include <iterator>
#include <iostream>
#include <set>
#include <charconv>

static const char *recips=0;
static std::string_view dbfile;
static const char *charset;
static unsigned interval=1;
static std::string sender;

static const char temporary_file_msg[]="Cannot write to temporary file";

std::vector<std::tuple<std::string, std::string>> header_list;

std::vector<std::string> extra_headers;

static void usage()
{
	std::cerr <<
		"Usage: mailbot [ options ] [ $MAILER arg arg... ]\n"
		"\n"
		"    -t filename        - text autoresponse\n"
		"    -c charset         - text MIME character set (default "
		  << charset <<
		")\n"
		"    -m filename        - text autoresponse with a MIME header\n"
		"    -r addr1,addr2...  - any 'addr' required in a To/Cc header\n"
		"    -e                 - Prefer replies to Errors-To: or Return-Path: instead\n"
		"                         of From:\n"
		"    -T type            - \"type\": reply, replyall, replydsn, replyfeedback,\n"
		"                         replydraft, forward, forwardatt\n"
		"    -N                 - Omit contents of the original message from replies\n"
		"    -F \"separator\"     - Set the forwarding separator\n"
		"    -S \"salutation\"    - Set salutation for replies\n"
		"    -d $pathname       - database to prevent duplicate autoresponses\n"
		"    -D x               - at least 'x' days before dupes (default: 1)\n"
		"    -s subject         - Subject: on autoresponses\n"
		"    -A \"Header: stuff\" - Additional header on the autoresponse\n"
		"    -M recipient       - format \"replydsn\" as a DSN from 'recipient' (required)\n"
		"    -fuser@domain      - Set responding address for replydsn\n"
		"    -f                 - Set responding address from $SENDER\n"
		"    -R type            - Feedback type, for \"-T feedback\" or \"-T replyfeedback\":\n"
		"                         \"abuse\", \"fraud\", \"other\", or \"virus\"\n"
		"    -n                 - only show the resulting message, do not send it\n"
		"    -a                 - Attach entire message for replydsn, feedback, and\n"
		"                         replyfeedback, instead of only the headers.\n"
		"    -l                 - maildir to read a draft message with the reply\n"
		"                         (required by -T replydraft).\n"
		"    --feedback-original-envelope-id {\"<envelopeid>\"}\n"
		"    --feedback-original-mail-from {\"<mailfrom>\"}\n"
		"    --feedback-reporting-mta {\"dns; hostname\"}\n"
		"    --feedback-source-ip {aaa.bbb.ccc.ddd}\n"
		"    --feedback-incidents {n}\n"
		"    --feedback-authentication-results {\"results\"}\n"
		"    --feedback-original-rcpt-to {\"<rcptto>\"]\n"
		"    --feedback-reported-domain {example.com}\n"
		"                       - optional parameters for -T \"feedback\" and \n"
		"                         -T \"replyfeedback\"\n"
		"    $MAILER arg arg... - run $MAILER (sendmail) to mail the autoresponse\n";

	exit(EX_TEMPFAIL);
}

static void read_headers(rfc822::fdstreambuf &tmpfp)
{
	rfc2045::entity::line_iter<false>::headers h{tmpfp};

	do
	{
		const auto &[name, content] = h.name_content();

		if (header_list.size() < 1000)
		{
			auto &ret=
				header_list.emplace_back(
					std::string{
						name.begin(), name.end()
					},
					std::string{
						content.begin(), content.end()
					}
				);

			rfc2045::entity::tolowercase(std::get<1>(ret));
		}
	} while(h.next());
}

std::string_view hdr(std::string hdrname)
{
	rfc2045::entity::tolowercase(hdrname);

	for (const auto &[header, value] : header_list)
	{
		if (header == hdrname)
			return value;
	}

	return "";
}

/*
** Get the sender's address
*/

static void check_sender()
{
	auto h=hdr("reply-to");

	if (h.size() == 0)
		h=hdr("from");

	if (h.size() == 0)
		exit(0);

	{
		rfc822::tokens t{h};
		rfc822::addresses a{t};

		for (auto &address:a)
		{
			if (address.address.empty())
				continue;
			address.address.print(std::back_inserter(sender));

			rfc2045::entity::tolowercase(sender);

			return;
		}
	}
	exit(0);
}

/*
** Do not autorespond to DSNs
*/

static void check_dsn()
{
	static const char ct[]="multipart/report";

	auto p=hdr("content-type");

	if (p.substr(0, sizeof(ct)-1) == ct)
		exit(0);

	p=hdr("precedence").substr(0, 4);

	if (p == "junk" || p == "bulk" || p == "list")
		exit(0);	/* Just in case */

	p=hdr("auto-submitted");

	if (!p.empty() && p != "no")
		exit(0);

	p=hdr("list-id");

	if (!p.empty())
		exit(0);
}

/*
** Check for a required recipient
*/

static void check_recips()
{
	if (!recips)
		return;

	std::unordered_set<std::string> recipient_addresses;

	{
		rfc822::tokens recip_tokens{recips};
		rfc822::addresses recip_addresses{recip_tokens};

		for (auto &a:recip_addresses)
		{
			std::string address;

			a.address.print(std::back_inserter(address));

			if (address.empty())
				continue;

			rfc2045::entity::tolowercase(address);
			recipient_addresses.insert(std::move(address));
		}
	}

	if (recipient_addresses.empty())
		return;

	for (const auto &[header, contents] : header_list)
	{
		if (header != "to" && header != "cc")
			continue;

		rfc822::tokens recip_tokens{contents};
		rfc822::addresses recip_addresses{recip_tokens};

		for (auto &a:recip_addresses)
		{
			std::string address;

			a.address.print(std::back_inserter(address));

			if (address.empty())
				continue;

			rfc2045::entity::tolowercase(address);

			if (recipient_addresses.find(address) !=
			    recipient_addresses.end())
				return;
		}
	}
	exit(0);
}

/*
** Check the dupe database.
*/

#ifdef DbObj
static void check_db()
{
	struct dbobj db;
	time_t now;

	size_t val_len;
	char *val;

	if (dbfile.empty())
		return;

	std::string dbname;
	std::string lockname;

	dbname.reserve(dbfile.size() + sizeof("." DBNAME));
	lockname.reserve(dbfile.size() + sizeof(".lock"));

	dbname.insert(dbname.end(), dbfile.begin(), dbfile.end());
	lockname=dbname;

	dbname += "." DBNAME;
	lockname += ".lock";

	int lockfd=open(lockname.c_str(), O_RDWR|O_CREAT, 0666);

	if (lockfd < 0 || ll_lock_ex(lockfd))
	{
		perror(lockname.c_str());
		exit(EX_TEMPFAIL);
	}

	dbobj_init(&db);

	if (dbobj_open(&db, dbname.c_str(), "C") < 0)
	{
		perror(dbname.c_str());
		exit(EX_TEMPFAIL);
	}

	time(&now);

	val=dbobj_fetch(&db, sender.data(), sender.size(), &val_len, "");

	if (val)
	{
		time_t t;

		if (val_len >= sizeof(t))
		{
			memcpy(&t, val, sizeof(t));

			if (t > now - interval * 60 * 60 * 24)
			{
				free(val);
				dbobj_close(&db);
				close(lockfd);
				exit(0);
			}
		}
		free(val);
	}

	dbobj_store(&db, sender.data(), sender.size(),
		    (const char *)&now, sizeof(now), "R");
	dbobj_close(&db);
	close(lockfd);
}
#endif

static void opensendmail(int argn, int argc, char **argv)
{
	std::vector<std::vector<char>> newargv;

	if (argn >= argc)
	{
		static const char *sendmail_argv[]={"sendmail", "-f", ""};

		newargv.reserve(4);

		for (auto default_mailer:sendmail_argv)
		{
			std::string_view s{default_mailer};
			newargv.emplace_back(s.begin(),
					     s.end()+1); // Terminating \0
		}
	}
	else
	{
		newargv.reserve(argc-argn+1);

		for (int i=0; argn+i < argc; i++)
		{
			std::string_view s{argv[argn+i]};
			newargv.emplace_back(s.begin(),
					     s.end()+1); // Terminating \0
		}
	}

	signal(SIGCHLD, SIG_DFL);

	std::vector<char *> pointers;

	pointers.reserve(newargv.size()+1);

	for (auto &buffer:newargv)
		pointers.push_back(buffer.data());
	pointers.push_back(0);

	execvp(pointers[0], pointers.data());
	perror(pointers[0]);
	exit(EX_TEMPFAIL);
}

static void savemessage(rfc822::fdstreambuf &original_message,
			rfc822::fdstreambuf &tmpfp, rfc2045::entity &message)
{
	char buf[BUFSIZ];
	int n;

	rfc2045::entity_parser<false> parser;

	while ((n=original_message.sgetn(buf, sizeof(buf))) > 0)
	{
		char *ptr=buf;

		parser.parse(buf, buf+n);

		while (n > 0)
		{
			int i=tmpfp.sputn(ptr, n);

			if (i <= 0)
			{
				perror("fwrite(tempfile)");
				exit(1);
			}

			ptr += i;
			n -= i;
		}
	}

	if (n < 0)
	{
		perror("tempfile");
		exit(1);
	}
	message=parser.parsed_entity();
}

static void write_to_reply_outf(rfc822::fdstreambuf &reply_outf,
				std::string_view content)
{
	auto p=content.data();
	auto s=content.size();

	while (s > 0)
	{
		int n=reply_outf.sputn(p, s);

		if (n <= 0)
		{
			perror(temporary_file_msg);
			exit(1);
		}

		p += n;
		s -= n;
	}
}

// The contentbuf contains a set of headers, followed by the actual
// content. Copy the headers, stop reading contentbuf after reading
// the empty line that follows the headers.
//
// Do not copy the Content-Transfer-Encoding: header, we'll provide
// our own.

static void copy_headers(rfc822::fdstreambuf &contentbuf,
			 rfc822::fdstreambuf &reply_outf)
{
	rfc2045::entity::line_iter<false>::headers h{
		contentbuf
	};

	h.name_lc=false;
	h.keep_eol=true;

	do
	{
		const auto &[name, empty] =
			h.convert_name_check_empty();

		if (name == "content-transfer-encoding")
			continue;

		if (empty)
			continue;

		write_to_reply_outf(reply_outf, h.current_header());
	} while (h.next());
};

// Copy the message content to the output, verbatim
static void copy_body(rfc822::fdstreambuf &contentbuf,
		      rfc822::fdstreambuf &reply_outf)
{
	char buffer[BUFSIZ];

	std::streamsize s;

	while ((s=contentbuf.sgetn(buffer, sizeof(buffer))) > 0)
		write_to_reply_outf(reply_outf, std::string_view(buffer, s));
}

// Retrieve the headers and body from a message in a maildir,
// the replydraft option. All headers except for Content- headers
// are ignored. The Content- headers and the body gets copied
// into the autoreply.

static void copy_draft(rfc822::fdstreambuf &contentbuf,
		       rfc822::fdstreambuf &reply_outf)
{
	rfc2045::entity::line_iter<false>::headers h{
		contentbuf
	};

	h.name_lc=false;
	h.keep_eol=true;

	do
	{
		const auto &[name, empty] =
			h.convert_name_check_empty();

		if (!name.empty() &&
		    std::string_view{name}.substr(0, 8) !=
		    "content-")
			continue;

		write_to_reply_outf(reply_outf, h.current_header());
	} while (h.next());

	write_to_reply_outf(reply_outf, "\n");
	copy_body(contentbuf, reply_outf);
}

rfc822::fdstreambuf find_draft(std::string_view maildirfolder)
{
	std::string draftfile;

	struct stat draft_stat;

	rfc822::fdstreambuf fp;
	static const char * const newcur[2]={"new", "cur"};

	draft_stat.st_mtime=0;

	for (auto subdir:newcur)
	{
		std::string dirbuf;

		dirbuf.reserve(maildirfolder.size()+10);

		dirbuf=maildirfolder;

		dirbuf += "/";

		dirbuf += subdir;

		std::error_code ec;

		std::filesystem::directory_iterator dirp{dirbuf, ec};

		if (ec)
			continue;

		for (auto &de:dirp)
		{
			std::string filename{de.path().filename()};

			if (MAILDIR_DELETED(filename.c_str()))
				continue;

			std::string filenamebuf{de.path()};

			rfc822::fdstreambuf new_file{
				open(filenamebuf.c_str(), O_RDONLY)
			};

			if (new_file.error())
				continue;

			struct stat new_stat;

			if (fstat(new_file.fileno(), &new_stat) < 0)
			{
				continue;
			}

			if (!draftfile.empty())
			{
				if (new_stat.st_mtime < draft_stat.st_mtime)
					continue;

				if (new_stat.st_mtime == draft_stat.st_mtime
				    && filenamebuf > draftfile)
					continue;
			}

			draftfile=filenamebuf;
			fp=std::move(new_file);
			draft_stat=new_stat;
		}
	}
	return fp;
}

int main(int argc, char **argv)
{
	int argn;
	rfc822::fdstreambuf tmpfp;
	rfc822::fdstreambuf reply_outf;
	rfc822::fdstreambuf reply_contentf;
	const char *subj=0;
	const char *txtfile=0, *mimefile=0;
	rfc822::fdstreambuf draftfile;
	const char *mimedsn=0;
	int nosend=0;
	const char *replymode="reply";
	int replytoenvelope=0;
	int donotquote=0;
	int fullmsg=0;
	const char *forwardsep="--- Forwarded message ---";
	const char *replysalut="%F writes:";
	const char *maildirfolder=0;

	const char *feedback_type=0;

	std::vector<std::tuple<const char *, const char *>> fb_list;

	setlocale(LC_ALL, "");
	charset=unicode_default_chset();

	for (argn=1; argn < argc; argn++)
	{
		char optc;
		char *optarg;

		if (argv[argn][0] != '-')
			break;

		if (strcmp(argv[argn], "--") == 0)
		{
			++argn;
			break;
		}

		if (strncmp(argv[argn], "--feedback-", 11) == 0)
		{
			if (++argn >= argc)
				break;

			fb_list.emplace_back(argv[argn-1]+11,
					     argv[argn]);

			continue;
		}

		optc=argv[argn][1];
		optarg=argv[argn]+2;

		if (!*optarg)
			optarg=NULL;

		switch (optc) {
		case 'c':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			if (optarg && *optarg)
			{
				char *p=unicode_convert_tobuf("",
								optarg,
								unicode_u_ucs4_native,
								NULL);

				if (!p)
				{
					fprintf(stderr, "Unknown charset: %s\n",
						charset);
					exit(1);
				}
				free(p);
				charset=optarg;
			}
			continue;
		case 't':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			txtfile=optarg;
			continue;
		case 'm':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			mimefile=optarg;
			continue;
		case 'r':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			recips=optarg;
			continue;
		case 'M':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			mimedsn=optarg;
			continue;
		case 'R':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			feedback_type=optarg;
			continue;
		case 'd':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			dbfile=optarg;
			continue;
		case 'e':
			replytoenvelope=1;
			continue;
		case 'T':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			if (optarg && *optarg)
				replymode=optarg;
			continue;
		case 'N':
			donotquote=1;
			continue;
		case 'a':
			fullmsg=1;
			continue;
		case 'F':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			if (optarg && *optarg)
				forwardsep=optarg;
			continue;
		case 'S':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			if (optarg && *optarg)
				replysalut=optarg;
			continue;
		case 'D':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			interval=optarg ? atoi(optarg):1;
			continue;
		case 'A':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			if (optarg)
			{
				extra_headers.push_back(optarg);
			}
			continue;
		case 's':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			subj=optarg;
			continue;

		case 'f':
			if (optarg && *optarg)
			{
				sender=optarg;
			}
			else
			{
				auto s=getenv("SENDER");
				if (!s)
					continue;
				sender=s;
			}
			continue;
		case 'l':
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];
			maildirfolder=optarg;
			continue;
		case 'n':
			nosend=1;
			continue;
		case 'X':	/* Undocumented option, used in tests */
			if (!optarg && argn+1 < argc)
				optarg=argv[++argn];

			{
				std::string_view timeout{optarg};

				unsigned secs;

				if (std::from_chars(timeout.data(),
						    timeout.data()+
						    timeout.size(),
						    secs).ec != std::errc{})
				{
					usage();
				}

				alarm(secs);
			}
			continue;
		default:
			usage();
		}
	}

	if (strcmp(replymode, "replydraft") == 0)
	{
		if (!maildirfolder)
			usage();
		draftfile=find_draft(maildirfolder);
		if (draftfile.error())
			exit(0);
	}
	else
	{
		if (!txtfile && !mimefile)
			usage();

		if (txtfile && mimefile)
			usage();
	}

	{
		FILE *f=tmpfile();

		if (!f)
		{
			perror(temporary_file_msg);
			exit(1);
		}
		tmpfp=rfc822::fdstreambuf{dup(fileno(f))};

		fclose(f);

		if (tmpfp.error())
		{
			perror(temporary_file_msg);
			exit(1);
		}
	}

	rfc2045::entity message;

	{
		rfc822::fdstreambuf original_message{dup(0)};

		if (original_message.error())
		{
			perror("standard input");
			exit(1);
		}

		savemessage(original_message, tmpfp, message);
	}

	if (tmpfp.pubseekpos(0) != 0)
	{
		perror("fseek(tempfile)");
		exit(1);
	}

	read_headers(tmpfp);

	if (sender.empty())
		check_sender();

	check_dsn();
	check_recips();
#ifdef DbObj
	check_db();
#endif

	rfc2045::reply rfc2045reply;
	std::string_view replymode_s{replymode};

	if (replymode_s == "reply")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::reply;
	} else if (replymode_s == "replyall")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replyall;
	} else if (replymode_s == "replydsn")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replydsn;
	} else if (replymode_s == "replydraft")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replydraft;
	} else if (replymode_s == "forward")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::forward;
	} else if (replymode_s == "forwardatt")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::forwardatt;
	} else if (replymode_s == "feedback")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::feedback;
	} else if (replymode_s == "replyfeedback")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replyfeedback;
	} else if (replymode_s == "replyall")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replyall;
	} else if (replymode_s == "replylist")
	{
		rfc2045reply.replymode=rfc2045::replymode_t::replylist;
	} else
	{
		usage();
	}

	rfc2045reply.replytoenvelope=replytoenvelope;

	rfc2045reply.donotquote=donotquote;

	rfc2045reply.replysalut=replysalut;

	rfc2045reply.forwarddescr="Forwarded message";
	rfc2045reply.charset=charset;
	rfc2045reply.subject=subj ? subj:"";
	rfc2045reply.forwardsep=forwardsep;
	rfc2045reply.fullmsg=fullmsg;

	if (mimedsn && *mimedsn)
	{
		rfc2045reply.dsnfrom=mimedsn;
		rfc2045reply.replymode=rfc2045::replymode_t::replydsn;
	}
	else if (feedback_type && *feedback_type)
	{
		rfc2045reply.feedbacktype=feedback_type;

		switch (rfc2045reply.replymode) {
		case rfc2045::replymode_t::feedback:
		case rfc2045::replymode_t::replyfeedback:
			break;
		default:
			std::cerr << "\"-T feedback\" or \"-T replyfeedback\""
				" required\n";
			exit(1);
		}

		if (fb_list.size() > 0)
		{
			for (auto [n, v] : fb_list)
			{
				rfc2045reply.feedbackheaders.emplace_back(
					n, v
				);
			}
		}
	}

	if (mimefile)
	{
		int fd=open(mimefile, O_RDONLY);

		if (fd < 0)
		{
			perror(mimefile);
			exit(1);
		}

		reply_contentf=rfc822::fdstreambuf(fd);

		if (reply_contentf.error())
		{
			perror(mimefile);
			exit(1);
		}

		// Extract the Content-Type header from the MIME file.

		rfc2045::entity::line_iter<false>::headers h{reply_contentf};

		rfc2045::entity::rfc2231_header content_type{"text/plain"};

		do
		{
			const auto &[name, header] = h.name_content();

			if (name != "content-type")
				continue;

			content_type=rfc2045::entity::rfc2231_header{header};
		} while (h.next());

		if (reply_contentf.pubseekpos(0) != 0)
		{
			perror(mimefile);
			exit(1);
		}

		if (content_type.value != "text/plain")
		{
			std::cerr << mimefile
				  << "%s must specify text/plain MIME type\n";

			exit(1);
		}

		content_type.lowercase_value("charset");

		auto charset=content_type.parameters.find("charset");

		if (charset != content_type.parameters.end())
		{
			bool errflag;

			unicode::iconvert::convert(
				"",
				charset->second.value,
				unicode_u_ucs4_native,
				errflag
			);
			if (errflag)
			{
				std::cerr << "Unknown charset in "
				     << mimefile
				     << "\n";
				exit(1);
			}
			rfc2045reply.charset=charset->second.value;
		}
		rfc2045reply.content_set_charset=[&] {
			copy_headers(reply_contentf, reply_outf);
		};

		rfc2045reply.content_specify=[&] {
			copy_body(reply_contentf, reply_outf);
		};
	}
	else if (txtfile)
	{
		int fd=open(txtfile, O_RDONLY);
		if (fd < 0)
		{
			perror(mimefile);
			exit(1);
		}
		reply_contentf=rfc822::fdstreambuf{fd};

		rfc2045reply.content_specify=[&] {
			copy_body(reply_contentf, reply_outf);
		};
	}
	else if (!draftfile.error())
	{
		reply_contentf=std::move(draftfile);

		rfc2045reply.content_specify=[&] {
			copy_draft(reply_contentf, reply_outf);
		};
	}

	if (!reply_contentf.error())
		fcntl(reply_contentf.fileno(), F_SETFD, FD_CLOEXEC);

	if (nosend)
	{
		reply_outf=rfc822::fdstreambuf(dup(1));

		if (reply_outf.error())
		{
			perror("stdout");
			exit(1);
		}
	}
	else
	{
		FILE *f=tmpfile();

		if (!f)
		{
			perror(temporary_file_msg);
			exit(1);
		}
		reply_outf=rfc822::fdstreambuf{dup(fileno(f))};

		if (reply_outf.error())
		{
			perror(temporary_file_msg);
			exit(1);
		}
	}

	for (auto &h:extra_headers)
	{
		write_to_reply_outf(reply_outf, h);
		write_to_reply_outf(reply_outf, "\n");
	}

	write_to_reply_outf(
		reply_outf,
		"Precedence: junk\n"
		"Auto-Submitted: auto-replied\n"
	);

	fcntl(tmpfp.fileno(), F_SETFD, FD_CLOEXEC);

	rfc2045reply(
		[&]
		(std::string_view p)
		{
			write_to_reply_outf(reply_outf, p);
		}, message, tmpfp
	);


	if (reply_outf.pubseekpos(0) != 0 ||
	    (!nosend &&
	     (
		     close(0), dup(reply_outf.fileno()) < 0
	     )))
	{
		perror(temporary_file_msg);
		exit(1);
	}
	reply_outf=rfc822::fdstreambuf{};
	fcntl(0, F_SETFD, 0);

	if (!nosend)
		opensendmail(argn, argc, argv);
	return (0);
}
