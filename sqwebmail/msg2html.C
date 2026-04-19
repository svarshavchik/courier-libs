#include "config.h"
/*
** Copyright 2007-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include "msg2html.h"
#include "buf.h"
#include <courier-unicode.h>
#include "numlib/numlib.h"
#include "gpglib/gpglib.h"
#include <charconv>
#include "cgi/cgi.h"
#include "rfc822/rfc822.h"
#include "rfc822/rfc2047.h"
#include "rfc2045/rfc3676parser.h"
#include "md5/md5.h"
#include "filter.h"
#include "html.h"
#include <unordered_map>
#include <vector>
#include <ctype.h>

static void (*get_known_handler(const rfc2045::entity &mime,
				struct msg2html_info *info))
	(std::streambuf &fd,
	 const rfc2045::entity &message,
	 std::string &, struct msg2html_info *);

static void (*get_handler(const rfc2045::entity &mime,
			  struct msg2html_info *info))
(std::streambuf &, const rfc2045::entity &,
 std::string &,
 struct msg2html_info *);

static const char validurlchars[]=
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	":/.~%+?&#=@;-_,";

struct msg2html_info *msg2html_alloc(const char *charset)
{
	msg2html_info *p= new msg2html_info;

	if (!p)
		return NULL;

	p->output_character_set=charset;
	return p;
}

void msg2html_add_smiley(struct msg2html_info *i,
			 const char *txt, const char *imgurl)
{
	i->smiley_index.insert(*txt);
	i->smileys.emplace_back(txt, imgurl);
}

void msg2html_free(struct msg2html_info *p)
{
	delete p;
}

namespace {
	struct html_escape_cb {

		virtual void operator()(std::string_view) const=0;
	};

	struct html_escape_stdout : html_escape_cb {

		void operator()(std::string_view sv) const override
		{
			fwrite(sv.data(), sv.size(), 1, stdout);
		}
	};

	struct html_escape_stdstring : html_escape_cb {

		mutable std::string s;
		void operator()(std::string_view sv) const override
		{
			s.append(sv.data(), sv.size());
		}
	};

	struct urlescapeiter {
		std::string s;

		urlescapeiter &operator*()
		{
			return *this;
		}

		urlescapeiter &operator++()
		{
			return *this;
		}

		urlescapeiter &operator++(int)
		{
			return *this;
		}

		urlescapeiter &operator=(char c)
		{
			if (strchr(validurlchars, c))
			{
				s.push_back(c);
				return *this;
			}

			static const char hex[]="0123456789ABCDEF";

			s.push_back('%');
			s.push_back(hex[(c >> 4) & 15]);
			s.push_back(hex[c & 15]);
			return *this;
		}

		operator std::string() const
		{
			return s;
		}
	};
};

static void do_html_escape(const char *p, size_t n,
			   const html_escape_cb &cb)
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

		if (p-q)
			cb({q, (size_t)(p-q)});

		cb(buf);
		p++;
		q=p;
	}

	if (p-q)
		cb({q, (size_t)(p-q)});
}

static void html_escape(const char *p, size_t n)
{
	do_html_escape(p, n, html_escape_stdout{});
}

static std::string html_escapestr(const char *p, size_t n)
{
	html_escape_stdstring ss;

	do_html_escape(p, n, ss);

	return std::move(ss.s);
}

/*
** Consider header name: all lowercase, except the very first character,
** and the first character after every "-"
*/

static void header_uc(std::string &h)
{
	bool uc=true;

	for (auto &c:h)
	{
		if (c == '-')
		{
			uc=true;
			continue;
		}

		if (uc)
		{
			c=toupper( (int)(unsigned char) c);
			uc=false;
		}
		else
		{
			c=tolower((int)(unsigned char) c);
		}
	}
}

static void show_email_header(std::string_view h)
{
	html_escape(h.data(), h.size());
}

static void print_header_uc(struct msg2html_info *info, std::string_view h_orig)
{
	std::string h{h_orig.begin(), h_orig.end()};

	header_uc(h);

	printf("<tr valign=\"baseline\"><th align=\"right\" class=\"message-rfc822-header-name\">");

	if (info->email_header)
		(*info->email_header)(h, show_email_header);
	else
		show_email_header(h);
	printf(":<span class=\"tt\">&nbsp;</span></th>");

}

namespace {


	struct print_addresses : rfc822::addresses::do_print {

		msg2html_info *info;
		rfc822::addresses::iterator b, e;

		print_addresses(
			msg2html_info *info,
			rfc822::addresses::iterator b,
			rfc822::addresses::iterator e
		) : info{info}, b{b}, e{e}
		{
		}

		void print_separator(const char *ptr) override
		{
			printf("%s", ptr);
		};

		bool eof()
		{
			return b == e;
		}

		const rfc822::address &ref() override
		{
			return *b;
		}

		void print() override;
	};


	void print_addresses::print()
	{
		std::string s, name, address;

		printf("<span class=\"message-rfc822-header-contents\">");
		b->display(info->output_character_set,
			   std::back_inserter(s));

		if (info->email_address_start && !b->address.empty())
		{
			b->display_name(info->output_character_set,
					std::back_inserter(name),
					true);
			b->display_address(info->output_character_set,
					   std::back_inserter(address));
			(*info->email_address_start)(name.c_str(),
						     address.c_str());
		}

		html_escape(s.c_str(), s.size());

		if (info->email_address_end && !b->address.empty())
			(*info->email_address_end)();
		printf("</span>");
		++b;
	}
}

static void showmsgrfc822_addressheader(struct msg2html_info *info,
					std::string_view h)
{
	rfc822::tokens tokens{h};
	rfc822::addresses addresses{tokens};

	print_addresses do_print{info, addresses.begin(), addresses.end()};

	do_print.output();
}

namespace {
	struct do_print_rfc2369_address : rfc822::address::do_print {

		msg2html_info *info;

		do_print_rfc2369_address(const rfc822::address &a,
					 msg2html_info *info)
			: do_print{a}, info{info}
		{
		}

		void emit_address()
		{
			std::string disp_address;

			a.display_address(info->output_character_set,
					  std::back_inserter(disp_address));

			if (info->get_textlink)
			{
				std::string raw_address=
					a.address.print(
						urlescapeiter{}
					);

				disp_address=html_escapestr(
					disp_address.data(),
					disp_address.size()
				);

				printf("%s", info->get_textlink(
					       raw_address, disp_address
				       ).c_str());
			}
			else
			{
				html_escape(
					disp_address.c_str(),
					disp_address.size()
				);
			}
		}

		void emit_name() override
		{
			std::string name;

			a.display_name(
				info->output_character_set,
				std::back_inserter(name)
			);
			html_escape(name.c_str(), name.size());
		}

		void emit_char(char c) override
		{
			html_escape(&c, 1);
		}
	};
}

static void showmsgrfc2369_header(struct msg2html_info *info,
				  std::string_view h)
{
	rfc822::tokens tokens{h};
	rfc822::addresses addresses{tokens};

	const char *sep="";

	for (auto &a:addresses)
	{
		printf("%s", sep);

		do_print_rfc2369_address print{a, info};

		print.output();
		sep=", ";
	}
}

static void showmsgrfc822_headerp(const char *p, size_t l, void *dummy)
{
	if (fwrite(p, l, 1, stdout) != 1)
	    ; /* ignore */
}

static int showmsgrfc822_header(const char *output_chset,
				const std::string_view p,
				const char *chset)
{
	char32_t *uc;
	size_t ucsize;

	int conv_err;

	if (unicode_convert_tou_tobuf(p.data(), p.size(), chset,
				      &uc, &ucsize,
				      &conv_err))
	{
		conv_err=1;
		uc=NULL;
	}

	filter_info info{output_chset, showmsgrfc822_headerp, NULL};

	if (uc)
	{
		info(uc, ucsize);
		free(uc);
	}
	info.flush();

	if (info.conversion_error)
		conv_err=1;

	return conv_err;
}

static void showmsgrfc822_body(std::streambuf &fd,
			       const rfc2045::entity &message,
			       std::string &idptr, bool flag,
			       struct msg2html_info *info)
{
	std::string save_subject;
	std::string save_date;

	rfc2045::entity::line_iter<false>::headers headers{message, fd};

	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" class=\"message-rfc822-header\">\n");

	do
	{
		const auto &[header, content] = headers.name_content();

		if (header.empty() && content.empty())
			continue;
		if (header =="list-help" ||
			header =="list-subscribe" ||
			header =="list-unsubscribe" ||
			header =="list-owner" ||
			header =="list-archive" ||
			header =="list-post")
		{
			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			showmsgrfc2369_header(info, content);
			printf("</span></td></tr>\n");
			continue;
		}

		auto	isaddress=rfc822::header_is_addr(header, false);

		if (info->fullheaders)
		{
			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			showmsgrfc822_header(info->output_character_set,
					     content,
					     "utf-8");
			printf("</span></td></tr>\n");
			continue;
		}
		if (header == "subject")
		{
			save_subject.clear();

			rfc822::display_header(
				"subject", content,
				info->output_character_set,
				std::back_inserter(save_subject));
			continue;
		}
		if (header == "date")
		{
			save_date=content;
			continue;
		}
		if (isaddress)
		{
			print_header_uc(info, header);
			printf("<td><span class=\"message-rfc822-header-contents\">");
			showmsgrfc822_addressheader(info, content);
			printf("</span></td></tr>\n");
		}
	} while (headers.next());

	if (!save_date.empty())
	{
		time_t	t;
		struct tm *tmp=0;
		char	date_buf[256];

		if (rfc822_parsedate_chk(save_date.c_str(), &t) == 0)
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
	}

	if (!save_subject.empty())
	{
		char subj_header[20];

		strcpy(subj_header, "Subject");
		print_header_uc(info, subj_header);

		printf("<td><span class=\"message-rfc822-header-contents\">");
		showmsgrfc822_header(info->output_character_set, save_subject,
				     info->output_character_set);
		printf("</span></td></tr>\n");
	}

	if (flag && info->message_rfc822_action)
		(*info->message_rfc822_action)(idptr);

	printf("</table>\n<hr width=\"100%%\" />\n");

	if (!flag && info->gpgdir && libmail_gpg_has_gpg(info->gpgdir) == 0
	    && message.has_mimegpg()
	    && info->gpg_message_action)
		(*info->gpg_message_action)();

	auto s=idptr.size();
	if (s)
		idptr += ".";
	idptr += "1";
	(*get_handler(message, info))(fd, message, idptr, info);

	idptr.resize(s);
}

void msg2html(std::streambuf &fd, const rfc2045::entity &message,
	      struct msg2html_info *info)
{
	if (!info->mimegpgfilename)
		info->mimegpgfilename="";

	std::string id;
	showmsgrfc822_body(fd, message, id, false, info);
}

static void showmsgrfc822(std::streambuf &fd,
			  const rfc2045::entity &message, std::string &id,
			  struct msg2html_info *info)
{
	if (message.subentities.size())
	{
		showmsgrfc822_body(fd, message.subentities[0], id, true, info);
	}
}

static void showunknown(std::streambuf &fd,
			const rfc2045::entity &message,
			std::string &id,
			struct msg2html_info *info)
{
	rfc2045::entity::rfc2231_header content_disposition{
		message.content_disposition
	};

	/* Punt for image/ MIMEs */

	if (std::string_view{
			message.content_type.value
		}.substr(0, 6) == "image/" &&
		content_disposition.value != "attachment")
	{
		if (info->inline_image_action)
			(*info->inline_image_action)(id,
						     message.content_type.value,
						     info->arg);
		return;
	}

	std::string content_name;

	bool found_name=false;

	auto iter=message.content_type.parameters.find("name");

	if (iter != message.content_type.parameters.end())
	{
		found_name=true;
	}

	auto iter2=content_disposition.parameters.find("filename");
	if (iter2 != content_disposition.parameters.end())
	{
		found_name=true;
		iter=iter2;
	}

	if (found_name)
	{
		content_name=iter->second.value;

		if (content_name.find("=?") != content_name.npos &&
		    content_name.find("?=") != content_name.npos)
		{
			// RFC2047 header encoding (not compliant to RFC2047)

			std::string decoded_content_name;

			rfc822::display_header(
				"subject", content_name,
				info->output_character_set,
				std::back_inserter(decoded_content_name)
			);

			content_name=std::move(decoded_content_name);
		}
		else
		{
			content_name=iter->second.value_in_charset(
				info->output_character_set
			);
		}
	}

	if (info->unknown_attachment_action)
		(*info->unknown_attachment_action)(
			id,
			message.content_type.value,
			content_name,
			message.endbody-message.startbody,
			info->arg);
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

static void append_idnum(std::string &id, size_t idnum)
{
	char buffer[20];

	id += ".";

	auto res=std::to_chars(buffer, buffer+sizeof(buffer), idnum);

	id.insert(id.end(), buffer, res.ptr);
}

static void drop_idnum(std::string &id)
{
	size_t p=id.rfind('.');

	if (p == id.npos)
		id="1";
	else
		id.resize(p);
}

static void showmultipart(std::streambuf &fd,
			  const rfc2045::entity &message,
			  std::string &id,
			  struct msg2html_info *info)
{
	std::optional<int> gpg_status;
	size_t id_len=id.size();

	if (info->is_gpg_enabled && (gpg_status=message.is_decoded()))
	{
		const char *style;
		showmultipartdecoded_start(*gpg_status, &style);

		for (size_t i=0; i<message.subentities.size(); ++i)
		{
			if (i == 1)
			{
				printf("<blockquote class=\"%s\">",
				       style);
			}

			append_idnum(id, i+1);

			(*get_handler(message.subentities[i], info))(
				fd, message.subentities[i], id, info);
			id.resize(id_len);
			if (i == 1)
			{
				printf("</blockquote>");
			}
			else if (i+1 < message.subentities.size())
				printf("<hr width=\"100%%\" />\n");
		}
		showmultipartdecoded_end();
	}
	else if (message.content_type.value == "multipart/alternative")
	{
		size_t idnum=0;

		for (size_t i=0; i<message.subentities.size(); ++i)
		{
			bool found=false;

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

			if (idnum == 0)
				found=true;
			else if (const rfc2045::entity *s;
				 (s=message.subentities[i].is_multipart_signed()
				 ) != nullptr)
			{
				if (get_known_handler(*s, info))
					found=true;
			}
			else if ( *info->mimegpgfilename &&
				  message.subentities[i].is_decoded())
			{
				if (auto s=message.subentities[
					    i
				    ].decoded_content(); s &&
				    get_known_handler(*s, info))
					found=true;
			}
			else if (get_known_handler(
					 message.subentities[i], info
				 ))
			{
				found=true;
			}

			if (found)
			{
				idnum=i+1;
			}
		}

		if (idnum)
		{
			append_idnum(id, idnum);

			(*get_handler(message.subentities[idnum-1], info))(
				fd,
				message.subentities[idnum-1],
				id,
				info
			);
			id.resize(id_len);
		}
	}
	else if (message.content_type.value == "multipart/related")
	{
		std::string sid;

		// Use rfc822 addresses parser to chop off the <> from the
		// start attribute. A bit overkill, but we don't have to
		// figure out what to do if <> isn't there, if there are
		// commas, etc... Let this approach handle the GIGO principle.

		if (auto start=message.content_type.parameters.find("start");
		    start != message.content_type.parameters.end())
		{
			rfc822::tokens tokens{start->second.value};
			rfc822::addresses addresses{tokens};

			if (!addresses.empty())
			{
				auto &a=addresses.front();

				sid.reserve(a.unquote_name(
						    rfc822::length_counter{}
					    ));
				a.unquote_name(std::back_inserter(sid));
			}
		}

		/*
		** We can't just walz in, search for the Content-ID:,
		** and skeddaddle, that's because we need to keep track of
		** our MIME section.  So we pretend that we're multipart/mixed,
		** see below, and abort at the first opportunity.
		*/

		for (size_t i=0; i<message.subentities.size(); i++)
		{
			auto &r=message.subentities[i];

			if (!sid.empty() && r.content_id_value() != sid)
			{
				auto qq=r.is_multipart_signed();

				if (!qq) continue;

				/* Don't give up just yet */

				if (qq->content_id_value() != sid)
				{
					/* Ok, we can give up now */
					continue;
				}
			}
			append_idnum(id, i+1);

			(*get_handler(r, info))(fd, r, id, info);
			id.resize(id_len);

			break;
			/* In all cases, we stop after dumping something */
		}
	}
	else
	{
		size_t i=0;
		for (auto &r:message.subentities)
		{
			if (i)
				printf("<hr width=\"100%%\" />\n");

			++i;

			append_idnum(id, i);
			(*get_handler(r, info))(fd, r, id, info);
			id.resize(id_len);
		}
	}
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
	auto handle=*(unicode_convert_handle_t *)dummy;

	if (handle)
		unicode_convert_uc(handle, uc, n);
}

/* Recursive search for a Content-ID: header that we want */

static std::tuple<const rfc2045::entity *, std::string> find_cid(
	const rfc2045::entity &message,
	std::string &id,
	const char *cidurl)
{
	if (message.content_id_value() == cidurl)
		return {&message, id};

	size_t id_len=id.size();
	size_t n=0;

	for (auto &subentity:message.subentities)
	{

		append_idnum(id, ++n);

		auto ret=find_cid(subentity, id, cidurl);

		id.resize(id_len);

		if (std::get<0>(ret))
			return ret;
	}
	return {nullptr, ""};
}

/*
** Convert cid: url to a http:// reference that will access the indicated
** MIME section.
*/

struct convert_cid_info {
	const rfc2045::entity *message;
	const std::string &id;
	struct msg2html_info *info;
};

static void add_decoded_link(const rfc2045::entity *, std::string_view, int);

static std::string convertcid(
	const char *cidurl,
	struct convert_cid_info &cid_info
)
{
	auto message=cid_info.message;
	std::string id=cid_info.id;
	size_t move_up=0;
	std::string mimegpgfilename;

	mimegpgfilename.reserve(
		cgi_encode::estimate(cid_info.info->mimegpgfilename));

	cgi_encode::encode(std::back_inserter(mimegpgfilename),
			   cid_info.info->mimegpgfilename);

	while (message->content_type.value != "multipart/related")
	{
		if (!message->get_parent_entity())
			return "";

		++move_up;
		message=message->get_parent_entity();
	}

	auto savep=message;

	while (move_up)
	{
		drop_idnum(id);
		--move_up;
	}

	auto [found, found_id]=find_cid(*message, id, cidurl);

	if (!found)
		/* Sometimes broken MS software needs to go one step higher */
	{
		if (savep->get_parent_entity())
		{
			savep=savep->get_parent_entity();

			drop_idnum(id);

			std::tie(found, found_id)=find_cid(*savep, id, cidurl);
		}
	}

	if (!found)	/* Not found, punt */
	{
		return "";
	}

	std::string p;

	if (!cid_info.info->get_url_to_mime_part)
		p="";
	else
		p=cid_info.info->get_url_to_mime_part(found_id.c_str());

	if (mimegpgfilename.size() && found->get_parent_entity())
	{
		auto is_decoded=found->get_parent_entity()->is_decoded();

		if (is_decoded)
		{
			std::string id_parent=id;
			drop_idnum(id_parent);
			add_decoded_link(found->get_parent_entity(), id_parent,
					 *is_decoded);
		}
	}

	return p;
}

/*
** When we output a multipart/related link to some content that has been
** signed/encoded, we save the decoding status, for later.
**
** Note -- we collapse multiple links to the same content.
*/

typedef std::unordered_map<
	const rfc2045::entity *, // ptr
	std::tuple<
		std::string, // mimeid
		int // status
		>
	> decoded_t;

decoded_t decoded;

// And we want to keep track of the order of the encountered content

std::vector<decoded_t::iterator> decoded_list;

static void add_decoded_link(const rfc2045::entity *ptr,
			     std::string_view mimeid,
			     int status)
{
	auto done=decoded.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(ptr),
		std::forward_as_tuple(
			std::string{mimeid.begin(),
				    mimeid.end()}, status)
	);

	if (done.second)
	{
		decoded_list.push_back(done.first);
	}
}

static void showtexthtml(std::streambuf &fd,
			 const rfc2045::entity &message,
			 std::string &id,
			 struct msg2html_info *info)
{
	const auto &content_base=message.content_base;

	const auto &mime_charset=message.content_type_charset();

	if (info->html_content_follows)
		(*info->html_content_follows)();

	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\"><tr><td>\n");

	auto h=unicode_convert_init(unicode_u_ucs4_native,
				    info->output_character_set,
				    text_to_stdout, NULL);

	auto hf_info=htmlfilter_alloc(&convert_unicode, &h);

	if (hf_info)
	{
		convert_cid_info cid_info{&message, id, info};

		htmlfilter_set_http_prefix(hf_info, info->wash_http_prefix);
		htmlfilter_set_convertcid(hf_info,
			[&]
			(const char *ptr)
			{
				return convertcid(ptr, cid_info);
			}
		);

		htmlfilter_set_contentbase(hf_info, content_base.c_str());

		htmlfilter_set_mailto_prefix(hf_info, info->wash_mailto_prefix);

		rfc822::mime_unicode_decoder decoder(
			[&]
			(const char32_t *ptr, size_t n)
			{
				htmlfilter(hf_info, ptr, n);
			},
			fd
		);

		decoder.decode_header=false;

		if (info->charset_warning)
			decoder.report_decoding_error=false;

		decoder.decode<false>(message);

		htmlfilter_free(hf_info);

		int errptr=0;

		if (h)
			unicode_convert_deinit(h, &errptr);
		if ((decoder.decoding_error || errptr) && info->charset_warning)
			(*info->charset_warning)(mime_charset,
						 info->arg);

		printf("</td></tr>");
	}

	for (auto &l:decoded_list)
	{
		printf("<tr><td>");

		const char *style;
		auto &[mimeid, status] = l->second;

		showmultipartdecoded_start(status, &style);

		size_t mimeid_l=mimeid.size();
		size_t partnum=0;

		for (auto &q:l->first->subentities)
		{
			append_idnum(mimeid, ++partnum);
			printf("<div class=\"%s\">", style);
			(*get_handler(q, info))(fd, q, mimeid, info);
			mimeid.resize(mimeid_l);
			printf("</div>\n");
			break;
		}
		showmultipartdecoded_end();
		printf("</td></tr>\n");
	}
	decoded_list.clear();
	decoded.clear();
	printf("</table>\n");

}

static void showdsn(std::streambuf &fd,
		    const rfc2045::entity &message,
		    std::string &id,
		    struct msg2html_info *info)
{

	if (fd.pubseekpos(message.startbody, std::ios_base::in) ==
	    static_cast<std::streambuf::off_type>(-1))
	{
		printf("Seek error.");
		return;
	}

	printf("<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n");

	rfc2045::entity::line_iter<false>::headers headers{
		fd,
		message.endbody-message.startbody
	};

	bool need_sep=false;
	do
	{
		const auto &[header, value]=headers.name_content();

		if (header.empty() && value.empty())
		{
			need_sep=true;
			continue;
		}

		if (need_sep)
			printf("<tr><td colspan=\"2\"><hr /></td></tr>\n");

		need_sep=false;
		print_header_uc(info, header);
		printf("<td><span class=\"message-rfc822-header-contents\">");

		/* showmsgrfc822_addressheader(value); */
		html_escape(value.data(), value.size());
		printf("</span></td></tr>\n");
	} while (headers.next());
	printf("</table>\n");
}

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
	bool flowed{false};

	int conv_err=0; /* A transcoding error has occurred */

	/*
	** Optionally replace smiley sequences with image URLs
	*/

	const smiley_index_t *smiley_index=nullptr; /* First character in all smiley seqs */
	const std::vector<smiley> *smileys=nullptr; /* All smiley seqs */

	/*
	** Flag - convert text/plain content to HTML using wiki-style
	** formatting codes. Implies flowed format, as well.
	*/
	int wikifmt=0;

	/*
	** Whether a paragraph is now open. Only used with flowed format.
	*/

	int paragraph_open=0;

	/*
	** Whether the <LI> tag is open, when doing wiki-style formatting.
	*/
	int li_open=0;

	/*
	** Quotation level of flowed format line.
	*/
	size_t cur_quote_level=0;

	/*
	** Whether this line's quotation level is different than the previous
	** line's.
	*/
	size_t quote_level_has_changed=0;

	/*
	** Whether process_text() is getting invoked at the start of the
	** line.
	*/
	int start_of_line=0;

	/* wikifmt settings */

	int ttline=0; /* Line begun with a space, <tt> is now open */

	int text_decor_state=0;	/* Future text should have these decorations */
	int text_decor_state_cur=0; /* Current decorations in place */

	int text_decor_apostrophe_cnt=0; /* Apostrophe accumulator */

	char32_t text_decor_uline_prev=0;
	/* Previous character, used when scanning for underline enable */

	/*
	** Although we're getting the parsed text incrementally, with no
	** guarantees how many characters in each chunk, we want to accumulate
	** some context at the beginning of the line in order to be able to
	** handle wikifmt codes.
	*/

	char32_t lookahead_buf[64];
	size_t lookahead_saved=0;

	/*
	** Current list level
	*/
	char current_list_level[16]={};

	/*
	** Close paragraph, </p> or </hX>
	*/

	char paragraph_close[8]={};

	/*
	** Handler that searches for http/https/mailto URLs in plain text and
	** highlights them.
	*/
	size_t (*text_url_handler)(struct msg2html_textplain_info *,
				   const char32_t *,
				   size_t)=nullptr;

	/*
	** Output filter for unescaped text. Replaces HTML codes.
	*/
	filter_info info;

	/*
	** A URL being accumulated.
	*/
	std::string urlbuf;

	/*
	** Caller-provided function to take a URL and return an HTML
	** sequence to display the URL.
	**
	** The characters in the provided URLs come from validurlchars, and
	** are "safe".
	**
	** The caller returns a malloced buffer, or NULL.
	*/

	std::function<std::string (std::string_view url,
				   std::string_view disp_url)
		> get_textlink;
	msg2html_textplain_info(const char *output_character_set,
				void (*output_func)(const char *p,
						    size_t n, void *arg),
				void *arg
	) : info{output_character_set, output_func, arg}
	{
	}
};

/*
** Convenience function. Accumulated latin chars that should be generated
** without escaping them.
*/
static void text_emit_passthru(struct msg2html_textplain_info *info,
			       std::string_view str)
{
	for (unsigned char ch:str)
	{
		char32_t widechar=ch;
		info->info.passthru(&widechar, 1);
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
		info->info(&uc, 1);
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
		info->info(&uc, 1);
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
		bool flag{false};
		bool smiley_found{false};

		/* Look for the first char that might be a smiley */

		for (i=0; i<txt_size; ++i)
		{
			if ((unsigned char)txt[i] == txt[i] &&
			    info->smiley_index &&
			    info->smiley_index->find(txt[i]) !=
			    info->smiley_index->end())
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
		for (const auto &[code, url]: *info->smileys)
		{
			size_t j;

			if (code.size() > txt_size)
			{
				flag=true;
				continue; /* Not enough context */
			}

			for (j=0; j < code.size(); j++)
			{
				if ( (unsigned char)txt[j] != txt[j])
					break;

				if ((unsigned char)txt[j] !=
				    (unsigned char)code[j])
					break;
			}

			if (j == code.size())
			{
				process_text(txt, 0, info);
				/* May be needed to start a paragraph */

				text_emit_passthru(info, url);

				txt += j;
				txt_size -= j;
				smiley_found=true;
				break;
			}
		}

		if (smiley_found) /* A smiley was found */
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
	info->info(&nl, 1);
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
				info->info(&uc, 1);
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
				info->info(&uc, 1);
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

			info->info(&uc, 1);
		}
		return 0;
	}


	{
		char32_t uc='\n';

		info->info(&uc, 1);
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

		info->info(&uc, 1);

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
			info->urlbuf.clear();
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
		if (!info->urlbuf.empty())
		{
			emit_char_buffer(info, info->urlbuf.c_str(),
					 info->urlbuf.size(),
					 text_process_decor);
			info->urlbuf.clear();
		}
		return 0;
	}

	/*
	** Accumulate this content, until notified otherwise.
	*/

	for (i=0; i<txt_size; i++)
	{
		if (info->urlbuf.size() > 32)
		{
			/*
			** Too long, can't be a method name.
			*/

			emit_char_buffer(info, info->urlbuf.c_str(),
					 info->urlbuf.size(),
					 text_process_decor);

			info->text_url_handler=text_contents_nourl;
			return i+text_contents_nourl(info, txt+i, txt_size-i);
		}

		if (txt[i] == ':') /* Bingo? */
		{

			if (info->urlbuf == "http" ||
			    info->urlbuf == "https" ||
			    info->urlbuf == "mailto")
			{
				/* Bingo! */
				info->urlbuf.push_back(':');
				++i;

				info->text_url_handler=
					text_contents_collecturl;
				return i;
			}
		}

		if (txt[i] < 'a' || txt[i] > 'z')
		{
			/* Hit another non-alphabetic character, reset */

			emit_char_buffer(info, info->urlbuf.c_str(),
					 info->urlbuf.size(),
					 text_process_decor);

			info->text_url_handler=text_contents_notalpha;
			return i+text_contents_notalpha(info, txt+i,
							txt_size-i);
		}

		info->urlbuf.push_back(txt[i]);
	}

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
	if (info->get_textlink)
	{
		auto link=info->get_textlink(info->urlbuf, info->urlbuf);

		text_emit_passthru(info, link.c_str());
		return;
	}

	/* Caller doesn't want the URL to be marked up */

	emit_char_buffer(info, info->urlbuf.c_str(), info->urlbuf.size(),
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
	size_t url_size=info->urlbuf.size();
	size_t i;

	text_process_decor_apostrophe(info);
	set_text_decor(info, info->text_decor_state);

	info->text_url_handler=text_contents_notalpha;

	while (url_size > 0)
	{
		if (strchr(",.;:", info->urlbuf[url_size-1]) == NULL)
			break;
		--url_size;
	}

	/* A practical joker typed in "mailto:", and nothing else */

	for (i=0; i<url_size; ++i)
		if (info->urlbuf[i] == ':')
			break;

	if (i == url_size)
	{
		emit_char_buffer(info, info->urlbuf.c_str(),
				 info->urlbuf.size(),
				 text_process_decor);

		return;
	}

	auto overflow=info->urlbuf.substr(url_size);
	info->urlbuf.resize(url_size);
	doemiturl(info);

	emit_char_buffer(info, overflow.c_str(),
			 overflow.size(),
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

		if (info->urlbuf.size() < 8192)
			info->urlbuf.push_back(txt[i]);
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
		info->info(uc, cnt);
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

		info->info(uc, i);
		uc += i;
		cnt -= i;
	}
}

struct msg2html_textplain_info *
msg2html_textplain_start(const char *message_charset,
			 const char *output_character_set,
			 bool isflowed,
			 bool isdelsp,
			 const std::function<
			 std::string (std::string_view url,
				      std::string_view disp_url)
			 > &get_textlink,

			 const smiley_index_t *smiley_index,
			 const std::vector<smiley> *smileys,
			 int wikifmt,

			 void (*output_func)(const char *p,
					     size_t n, void *arg),
			 void *arg)
{
	msg2html_textplain_info *tinfo=new msg2html_textplain_info{
		output_character_set,
		output_func,
		arg
	};

	tinfo->flowed=isflowed;
	tinfo->get_textlink=get_textlink;
	tinfo->smiley_index=smiley_index;
	tinfo->smileys=smileys;
	tinfo->wikifmt=wikifmt;

	tinfo->text_url_handler=text_contents_notalpha;

	tinfo->conv_err=0;
	{
		struct rfc3676_parser_info pinfo;

		memset(&pinfo, 0, sizeof(pinfo));

		pinfo.charset=message_charset;
		pinfo.isflowed=isflowed ? 1:0;
		pinfo.isdelsp=isdelsp ? 1:0;
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

	tinfo->info.flush();

	if (tinfo->info.conversion_error)
		tinfo->conv_err=1;

	errptr=tinfo->conv_err;

	delete tinfo;
	return errptr;
}

static void output_html_func(const char *p, size_t n, void *dummy)
{
        if (fwrite(p, 1, n, stdout) != n)
                ; /* ignore */
}

static void showtextplain(std::streambuf &fd,
			  const rfc2045::entity &message,
			  std::string &id,
			  struct msg2html_info *info)
{
	auto isflowed=message.content_type.format_flowed();
	bool isdelsp=false;
	if (isflowed)
		isdelsp=message.content_type.delsp_yes();

	if (info->noflowedtext)
		isflowed=isdelsp=false;

	auto mime_charset_str=message.content_type_charset();
	std::string mime_charset{mime_charset_str.begin(),
		mime_charset_str.end()
	};

	auto tinfo=msg2html_textplain_start(mime_charset.c_str(),
					    info->output_character_set,
					    isflowed, isdelsp,
					    info->get_textlink,
					    &info->smiley_index,
					    &info->smileys,
					    0,
					    output_html_func, NULL);

	if (!tinfo)
		return;

	rfc822::mime_decoder decoder{
		[&]
		(const char *ptr, size_t n)
		{
			msg2html_textplain(tinfo, ptr, n);
		},
		fd
	};

	decoder.decode_header=false;
	decoder.decode(message);

	auto rc=msg2html_textplain_end(tinfo);

	if ((rc || decoder.decoding_error) && info->charset_warning)
		(*info->charset_warning)(mime_charset, info->arg);
}



static void showkey(std::streambuf &fd,
		    const rfc2045::entity &message,
		    std::string &id,
		    struct msg2html_info *info)
{
	if (info->application_pgp_keys_action)
	{
		std::string s;

		rfc822::display_header(
			"content-description",
			message.content_description,
			info->output_character_set,
			std::back_inserter(s)
		);
		(*info->application_pgp_keys_action)(id, s);
	}
}

static void (*get_known_handler(const rfc2045::entity &mime,
				struct msg2html_info *info))
(std::streambuf &, const rfc2045::entity &, std::string &,
 struct msg2html_info *)
{
	if ( std::string_view{mime.content_type.value}.substr(0, 10)
	     == "multipart/")
		return ( &showmultipart );

	if (mime.content_type.value == "application/pgp-keys"
	    && info->gpgdir && libmail_gpg_has_gpg(info->gpgdir) == 0)
		return ( &showkey );

	rfc2045::entity::rfc2231_header content_disposition{
		mime.content_disposition
	};

	if (content_disposition.value == "attachment")
		return nullptr;

	if (mime.content_type.value == "text/plain" ||
	    rfc2045_message_headers_content_type(
		    mime.content_type.value.c_str()) ||
	    mime.content_type.value == "text/x-gpg-output")
		return ( &showtextplain );
	if (rfc2045_delivery_status_content_type(
		    mime.content_type.value.c_str()))
		return ( &showdsn);
	if (info->showhtml && mime.content_type.value == "text/html")
		return ( &showtexthtml );
	if (rfc2045_message_content_type(mime.content_type.value.c_str()))
		return ( &showmsgrfc822);

	return nullptr;
}

static void (*get_handler(const rfc2045::entity &mime,
			  struct msg2html_info *info))
(std::streambuf &, const rfc2045::entity &,
 std::string &,
 struct msg2html_info *)
{
	void (*func)(std::streambuf &, const rfc2045::entity &,
		     std::string &,
		     struct msg2html_info *);

	if ((func=get_known_handler(mime, info)) == 0)
		func= &showunknown;

	return (func);
}

static void download_func(const char *, size_t);

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


void msg2html_download(std::streambuf &fd,
		       const char *mimeid, int dodownload,
		       const char *system_charset)
{
	rfc2045::entity message;

	{
		std::istreambuf_iterator<char> b{&fd}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	auto part=&message;

	if (*mimeid)
		part=message.find(mimeid);

	if (!part)
	{
		printf("Content-Type: text/plain\n\n"
		       "Invalid download link \"%s\".\n", mimeid);
		return;
	}

	part->content_type_charset();

	std::string content_type=part->content_type.value;

	std::string content_name;

	auto content_name_attr=part->content_type.parameters.find("name");

	if (content_name_attr != part->content_type.parameters.end())
	{
		content_name=content_name_attr->second.value_in_charset();
	}
	if (dodownload)
	{
		std::string disposition_filename;

		rfc2045::entity::rfc2231_header content_disposition{
			part->content_disposition
		};

		auto disp_fn_attr=
			content_disposition.parameters.find("filename");

		if (disp_fn_attr !=
		    content_disposition.parameters.end())
		{
			if (std::string_view{content_type}.substr(0, 5)
			    == "text/plain")
			{
				disposition_filename=
					disp_fn_attr->second.value_in_charset(
						part->content_type_charset()
					);
			}
			else
			{
				disposition_filename=
					disp_fn_attr->second.value_in_charset();
			}
		}

		std::string p=disposition_filename;

		if (p.empty()) p=content_name;
		if (p.empty()) p=*mimeid ? "attachment.dat":"message.dat";
		disposition_attachment(stdout, p.c_str(), 1);
		content_type="application/octet-stream";
	} else {
		if (!content_name.empty())
			disposition_attachment(stdout, content_name.c_str(), 0);
	}

	printf(
		!content_name.empty() ?
		"Content-Type: %s; charset=%s; name=\"%s\"\n\n":
		"Content-Type: %s; charset=%s\n\n",
		content_type.c_str(),
		part->content_type.value.c_str(),
		content_name.c_str()
	);

	if (*mimeid == 0)	/* Download entire message */
	{
		part->decode_body_to(fd, download_func,	0);
	}
	else
	{
		part->decode_body(fd, download_func);
	}
}

static void download_func(const char *p, size_t cnt)
{
	(void)fwrite(p, 1, cnt, stdout);
}

void msg2html_showmimeid(std::string_view idptr, const char *p)
{
	if (!p)
		p="&amp;mimeid=";

	printf("%s", p);

	fwrite(idptr.data(), idptr.size(), 1, stdout);
}
