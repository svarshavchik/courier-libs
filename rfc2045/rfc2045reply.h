#ifndef rfc2045reply_h
#define rfc2045reply_h

#include "rfc2045/rfc2045.h"
#include "rfc2045/rfc3676parser.h"
#include "rfc822/rfc822.h"
#include "rfc822/rfc2047.h"

#include <functional>
#include <vector>
#include <set>
#include <algorithm>

enum class rfc2045::replymode_t {
	forward, forwardatt,
	reply,
	replydsn,
	replydraft,
	feedback,
	replyfeedback,
	replyall,
	replylist,
};

// C++ version of rfc2045_makereply(). The class contains the same fields
// as the rfc2045_mkreplyinfo class, initialize them first.
//
// operator() is the equivalent of rfc2045_makereply(), passing in an output
// that gets invoked, repeatedly, to define the reply. Each invocation passes
// in a std::string_view parameter that defines the next chunk of the reply.

struct rfc2045::reply {

	// replymode must be initialized to one of the following.  It sets
	// the actual template for the generated response.
	//
	// "forward" - forward original message.
	// "forwardatt" - forward original message as an RFC822 attachment
	// "reply" - a standard reply to the original message's sender
	// "replydsn" - a DSN reply to the original message's sender
	// "feedback" - generate a feedback report (RFC 5965)
	// "replyfeedback" - "feedback" to the sender's address.
	// "replyall" - a "reply to all" response.
	// "replylist" - "reply to mailing list" response.  This is a reply
	// that's addressed to the mailing list the original message was sent
	// to.
	replymode_t replymode{replymode_t::forward};

	// Called to write out the sender's signature
	std::function< void() > writesig_func;

	// myaddr_func receives a pointer to an RFC 822 address, and it
	// should return non-zero if the address is the sender's address
	std::function<bool (std::string_view)> myaddr_func;

	// If non-zero, the "reply" or "replydsn" message gets addressed to the
	// "Return-Path" or "Errors-To" address, if available.
	bool replytoenvelope{false};

	// If donotquote is set, the contents of the original message are not
	// quoted by any of the "reply" modes, and replysalut (below) does not
	// get emitted.

	bool donotquote{false};


	// For replydsn, feedback, replyfeedback, attach the entire message
	// instead of just its headers.

	bool fullmsg{false};

	// This should be set to the salutation to be used for the reply.
	// The following %-formats may appear in this string:
	//
	// %% - an explicit % character
	//
	// %n - a newline character
	//
	// %C - the X-Newsgroup: header from the original message
	//
	// %N - the Newsgroups: header from the original message
	//
	// %i - the Message-ID: header from the original message
	//
	// %f - the original message's sender's address
	//
	// %F - the original message's sender's name
	//
	// %S - the Subject: header from the original message
	//
	// %d - the original message's date, in the local timezone
	//
	// %{...}d - use strftime() to format the original message's date.
	//           A plain %d is equivalent to %{%a, %d %b %Y %H:%M:%S %z}d.
	//
	// Example:  "%F writes:"

	std::string_view replysalut;

	// For forwardatt, this is the Content-Description: header,
	// (typically "Forwarded message").
	std::string_view forwarddescr;

	// If not empty, overrides the Subject: header

	std::string_view subject;

	// When reply mode is 'replydsn', dsnfrom must be set to a valid
	// email address that's specified as the address that's generating
	// the DSN.
	std::string_view dsnfrom;

	// When reply mode is 'replyfeedback', feedbacktype must be set to
	// one of the registered feedback types:
	// "abuse", "fraud", "other", "virus".
	std::string_view feedbacktype;

	// Feedback report headers.
	//
	// NOTE: rfc2045_makereply() automatically inserts the
	// Feedback-Type: (from feedbacktype), User-Agent:, Version:, and
	// Arrival-Date: headers.
	//
	// This is an array of tuples: header name and header value
	// strings. The header name string does not contain a colon,
	// one is supplied. And, basically, generates "name: value" from
	// this list.
	//
	// For convenience-sake, the capitalization of the headers get
	// adjusted to match the convention in RFC 5965.

	std::vector<std::tuple<std::string_view, std::string_view>
		    > feedbackheaders;

	// Set the reply/fwd MIME headers. If this not set, the output
	// function receives ``Content-Type: text/plain; format=flowed;
	// delsp=yes; charset="charset" '' with the charset specified below,
	// and "Content-Transfer-Encoding: 8bit".
	//
	// If this is set, this function gets invoked instead of writing
	// the default headers.
	//
	// The output of content_set_charset() should be consistent with the
	// contents of the charset field.
	std::function<void ()> content_set_charset;

	// Set the reply/fwd content.
	//
	// This function gets called at the point where the additional contents
	// of the reply/fwd should go.
	//
	// If this is not a NULL pointer, the effect of this function should
	// be invocation of write_func() with the additional contents of the
	// reply/fwd. The added content should be consistent with the
	// charset field.
	//
	// Note -- this content is likely to end up in a multipart MIME
	// message, as such it should not contain any lines that look like
	// MIME boundaries.
	std::function<void ()> content_specify;

	// This closuere gets invoked to check if the passed in address, an
	// RFC 822 address is a respondent's mailing list.  It is used
	// to figure out which mailing list the original message was sent to
	// (all addresses in the original message get passed into here, one at
	// a time).  In the event that we can't find a mailing list address on
	// the original message, "replylist" will fall back to "replyall".

	std::function<bool (std::string_view)> is_mailinglist;

	// The respondent's local charset
	std::string charset;

	// This is used instead of replysalut for forwards.
	std::string_view forwardsep;

	// Generate the autoreply

	template<typename out_closure_t, typename src_type>
	void operator()(out_closure_t &&out_closure,
			const rfc2045::entity &message,
			src_type &src)
	{
		switch (replymode) {
		case replymode_t::forward:
		case replymode_t::forwardatt:
			mkforward(std::forward<out_closure_t>(out_closure),
				  message,
				  src);
			break;
		default:
			mkreply(std::forward<out_closure_t>(out_closure),
				message,
				src);
			break;
		}
	}

private:
	template<typename out_closure_t, typename src_type>
	void mkforward(out_closure_t &&out_closure,
		       const rfc2045::entity &message,
		       src_type &src);

	template<typename out_closure_t, typename src_type>
	void mkreply(out_closure_t &&out_closure,
		       const rfc2045::entity &message,
		       src_type &src);

	template<typename out_closure_t, typename src_type>
	void copyheaders(out_closure_t &&out_closure,
			 const rfc2045::entity &message,
			 src_type &src);

	template<typename out_closure_t, typename src_type>
	void reply_dsn(out_closure_t &&out_closure,
		       const rfc2045::entity &message,
		       src_type &src);

	template<typename out_closure_t>
	static void dsn_arrival_date(out_closure_t &&out_closure);

	template<typename out_closure_t, typename src_type>
	void reply_feedback(out_closure_t &&out_closure,
			    const rfc2045::entity &message,
			    src_type &src);

	template<typename out_closure_t, typename src_type>
	void reformat(out_closure_t &&out_closure,
		      const rfc2045::entity &message,
		      src_type &src,
		      size_t quote_adjust_level);

	template<typename out_closure_t, typename src_type>
	void mimeforward(out_closure_t &&out_closure,
			 const rfc2045::entity &message,
			 src_type &src);

	std::string mksalutation(std::string_view salutation_template,
				 std::string_view newsgroup,
				 std::string_view message_id,
				 std::string_view newsgroups,
				 std::string_view sender_addr,
				 std::string_view sender_name,
				 std::string_view date,
				 std::string_view subject);

	std::string mkreferences(std::string_view oldref,
				 std::string_view oldmsgid);

	// Helper class that processes the contents of the text/plain
	// autoreply.
	//
	// begin() gets called, passing in the input/output character set.
	//
	// reformat_info inherits from unicode::iconvert and
	// mail::textplainparser. After begin(), the overloaded << operator
	// that's inherited from rfc3676parser gets called to parse the
	// text/plain message.
	//
	// Implements the callbacks from mail::textplainparser that take
	// the parsed flowed text format, format it back into a plain text
	// message, and use unicode::iconvert() to convert it from unicode
	// to the charset. Implements converted() to write it out to
	// the output closure.

	template<typename out_closure_t>
	struct reformat_info : unicode::iconvert, mail::textplainparser {

		out_closure_t &out_closure;

		reformat_info(out_closure_t &out_closure)
			: out_closure{out_closure}
		{
		}

		// Begin reformatting. Initialize the unicode::iconvert
		// and mail::textplainparser superclasses. Returns false if
		// there was a conversion error.

		bool begin(const std::string &charset)
		{
			// We'll convert the Unicode-formatted parsed text/plain
			// content into the output character set.

			if (!unicode::iconvert::begin(
				    unicode_u_ucs4_native,
				    charset
			    ))
			{
				return false;
			}

			return mail::textplainparser::begin(charset, isflowed,
							    isdelsp);
		}

		void end(bool &unicode_errflag)
		{
			// textplainparser initializes the flag
			mail::textplainparser::end(unicode_errflag);

			// unicode::iconvert only sets it to true on an error
			unicode::iconvert::end(unicode_errflag);
		}

		// Converted unicode autoreply, in the output character set.
		int converted(const char *p, size_t n) override
		{
			out_closure(std::string_view{p, n});
			return 0;
		}

		// Whether to add (1) or leave the quoting level of each
		// line of text/plain content untouched.
		//
		// Formatting a reply sets this to 1, quoting the original
		// content.

		size_t quote_level_adjust=0;

		// Whether the text/plain content has format=flowed and
		// delsp=yes set.
		bool isflowed=false;
		bool isdelsp=false;

	private:
		// line_begin(): actual quoting level of the current line
		size_t quote_level=0;

		// line_begin() sets start_line to true, indicating
		// to line_contents() that it's the first time it's called
		// for the flowed line.

		bool start_line=false;

		// The previous call to line_contents() ended with these many
		// spaces.
		size_t trailing_spaces=0;

		// textplainparser
		void line_begin(size_t) override;
		void line_contents(const char32_t *, size_t) override;
		void line_flowed_notify() override;
		void line_end() override;
	};
};

/*
** RFC 3676 parser: Start of a new line in the reply.
*/

template<typename out_closure_t>
void rfc2045::reply::reformat_info<out_closure_t>::line_begin(
	size_t orig_quote_level
)
{
	/*
	** Save quote level, begin conversion from unicode to the native
	** charset.
	*/
	quote_level=orig_quote_level+quote_level_adjust;

	/*
	** Emit quoting indentation, if any.
	*/
	start_line=true;
	trailing_spaces=0;

	for (size_t i=0; i<quote_level; i++)
	{
		(*this)(U">", 1);
	}
}

/*
** RFC 3676: (possibly partial) contents of a deflowed line, as unicode.
*/

template<typename out_closure_t>
void rfc2045::reply::reformat_info<out_closure_t>::line_contents(
	const char32_t *txt, size_t txt_size
)
{
	if (txt_size == 0)
		return;

	/*
	** Space-stuff the initial character.
	*/

	if (start_line)
	{
		if (!isflowed)
		{
			/*
			** If the original content is not flowed, the rfc3676
			** parser does not parse the number of > quote
			** characters and does not set the quote level.
			*/

			if ((quote_level > 0 && *txt != '>') || *txt == ' ')
				(*this)(U" ", 1);
		}
		else
		{
			if (quote_level > 0 || *txt == ' ' || *txt == '>')
				(*this)(U" ", 1);
		}
		start_line=false;
	}

	/*
	** Trim any trailing spaces from the RFC 3676 parsed content.
	*/

	size_t nonspc_cnt;

	for (nonspc_cnt=txt_size; nonspc_cnt > 0; --nonspc_cnt)
		if (txt[nonspc_cnt-1] != ' ')
			break;

	/*
	** If the contents are not totally whitespace, it's ok now to emit
	** any accumulated whitespace from previous content.
	*/

	if (nonspc_cnt)
	{
		while (trailing_spaces)
		{
			(*this)(U" ", 1);
			--trailing_spaces;
		}
		(*this)(txt, nonspc_cnt);
	}

	trailing_spaces += txt_size - nonspc_cnt;
}

/*
** RFC 3676 parser: flowed line break. Replicate it.
*/
template<typename out_closure_t>
void rfc2045::reply::reformat_info<out_closure_t>::line_flowed_notify()
{
	/*
	** It's safe to preserve trailing spaces on flowed lines.
	*/

	while (trailing_spaces)
	{
		(*this)(U" ", 1);
		--trailing_spaces;
	}

	(*this)(U" ", 1);
	line_end();
	line_begin(quote_level-quote_level_adjust);
	/* Undo the adjustment in line_begin() */
}

template<typename out_closure_t>
void rfc2045::reply::reformat_info<out_closure_t>::line_end()
{
	(*this)(U"\n", 1);
}

// C++ equivalent of reformat()

// The passed in rfc2045::entity and its src defines the text/plain content
// to be copied to the out_closure. Use reformat_info to process it.

template<typename out_closure_t, typename src_type>
void rfc2045::reply::reformat(out_closure_t &&out_closure,
			      const rfc2045::entity &message,
			      src_type &src,
			      size_t quote_adjust_level)
{
	reformat_info info{out_closure};

	info.quote_level_adjust=quote_adjust_level;

	// Set isflowed and delsp parameters.

	auto content_type_param=message.content_type.parameters.find("flowed");

	if (content_type_param != message.content_type.parameters.end())
	{
		auto param_value=content_type_param->second.value;

		rfc2045::entity::tolowercase(param_value);

		if (param_value == "flowed")
			info.isflowed=true;
	}

	content_type_param=message.content_type.parameters.find("delsp");

	if (content_type_param != message.content_type.parameters.end())
	{
		auto param_value=content_type_param->second.value;

		rfc2045::entity::tolowercase(param_value);

		if (param_value == "yes")
			info.isdelsp=true;
	}

	if (info.begin(charset))
	{
		// Use mime_decoder to decode the text/plain content,
		// and pass it to the mail::textplainparser.

		rfc822::mime_decoder decoder{
			[&]
			(const char *ptr, size_t n)
			{
				info << std::string_view{ptr, n};
			}, src, charset
		};

		decoder.decode_header=false;
		decoder.decode(message);

		bool unicode_errflag;

		info.end(unicode_errflag);

		if (!unicode_errflag)
		{
			if (info.unicode::iconvert::end(unicode_errflag))
			{
				if (!unicode_errflag)
					return;
			}
		}
	}

	out_closure("\n[ A character set conversion error has occured]\n");
}

// forward/forwardatt autoreplies

template<typename out_closure_t, typename src_type>
void rfc2045::reply::mkforward(out_closure_t &&out_closure,
			       const rfc2045::entity &message,
			       src_type &src)
{
	std::string subject;

	/*
	** Use the original message's subject to set the subject of the
	** forward message.
	*/

	{
		rfc2045::entity::line_iter<false>::headers hi{
			message, src
		};

		do
		{
			const auto &[name, content]=hi.name_content();

			if (name == "subject")
			{
				subject=std::string{content.begin(),
						    content.end()};
			}
		} while (hi.next());
	}
	out_closure("Subject: ");

	if (!this->subject.empty())
	{
		/*
		** ... unless the caller overrides it.
		*/

		out_closure(this->subject);
	}
	else if (!subject.empty())
	{
		char	*s=rfc822_coresubj_keepblobs(subject.c_str());

		if (!s)
		{
			perror("malloc");
			exit(1);
		}

		std::string ss{s};
		free(s);

		out_closure(ss);
		out_closure(" (fwd)");
	}
	out_closure("\nMime-Version: 1.0\n");

	/*
	** To assemble a forward template, two things are needed:
	**
	** 1. The original message, as text/plain.
	**
	** 2. Any attachments in the original message.
	**    A. The attachments get either copied to the forward message, or
	**    B. The original message is attached as a single message/rfc822
	**       entity.
	**
	** 2b is always produced by "forwardatt". If a suitable text/plain
	** part of the original message could not be found, 2b is also
	** produced even by "forward".
	*/

	const rfc2045::entity *textplain_content=nullptr;
	bool attachment_is_message_rfc822=false;
	const rfc2045::entity *first_attachment=nullptr;
	size_t n_attachments=0;
	const rfc2045::entity *top_part=&message;

	if (top_part->content_type.value == "multipart/signed")
	{
		if (!top_part->subentities.empty())
		{
			top_part=&top_part->subentities[0];
		}
	}
	else if (top_part->content_type.value == "multipart/x-mimegpg")
	{
		if (!top_part->subentities.empty())
		{
			auto p=&top_part->subentities[0];

			if (p->content_type.value == "text/x-gpg-output" &&
			    top_part->subentities.size() > 1)
			{
				top_part=++p;
			}
		}
	}

	if (top_part->content_type.value == "text/plain")
	{
		textplain_content=top_part;
	}
	else if (top_part->content_type.value == "multipart/alternative")
	{
		top_part->visit_all(
			[&]
			(const rfc2045::entity &e)
			{
				if (e.content_type.value == "text/plain")
				{
					textplain_content=&e;
					return false;
				}
				return true;
			}
		);
	}
	else if (top_part->content_type.value == "multipart/mixed")
	{
		/*
		** If the first part contained a suitable text/plain,
		** any remaining MIME parts become attachments that
		** get copied to the forward message.
		*/

		if (!top_part->subentities.empty() &&
		    !top_part->subentities[0].visit_all(
			    [&]
			    (const rfc2045::entity &e)
			    {
				    if (e.content_type.value ==	"text/plain")
				    {
					    textplain_content=&e;
					    return false;
				    }
				    return true;
			    }
		    ))
		{
			first_attachment=&top_part->subentities[1];
			n_attachments=top_part->subentities.size()-1;
		}
	}

	if (replymode == replymode_t::forwardatt ||
	    textplain_content == nullptr)
	{
		/*
		** Copy the entire message as the sole message/rfc822
		** attachment in the forward message.
		*/
		textplain_content=nullptr;
		first_attachment=top_part;
		n_attachments=1;
		attachment_is_message_rfc822=1;
	}

	unsigned counter=0;

	auto boundary=message.new_boundary(src, counter);

	if (first_attachment)
	{
		out_closure("Content-Type: multipart/mixed; boundary=\"");
		out_closure(boundary);
		out_closure("\"\n\n");
		out_closure(RFC2045MIMEMSG);
		out_closure("\n--");
		out_closure(boundary);
		out_closure("\n");
	}

	if (content_set_charset)
		content_set_charset();
	else
	{
		out_closure("Content-Type: text/plain; format=flowed; delsp=yes; charset=\"");
		out_closure(charset);
		out_closure("\"\n");
	}

	out_closure("Content-Transfer-Encoding: 8bit\n\n");
	if (content_specify)
		content_specify();

	out_closure("\n");
	if (writesig_func)
		writesig_func();
	out_closure("\n");

	if (!forwardsep.empty())
	{
		out_closure(forwardsep);
		out_closure("\n");
	}

	if (textplain_content)
	{
		/* Copy original headers. */
		rfc2045::entity::line_iter<false>::headers hi{
			message, src
		};

		hi.name_lc=false;
		hi.keep_eol=true;

		std::string hdrbuf;

		do
		{
			const auto &[header, empty] =
				hi.convert_name_check_empty();

			if (header == "subject" ||
			    header == "from" ||
			    header == "to" ||
			    header == "cc" ||
			    header == "date" ||
			    header == "message-id" ||
			    header == "resent-from" ||
			    header == "resent-to" ||
			    header == "resent-cc" ||
			    header == "resent-date" ||
			    header == "resent-message-id")
			{
				const auto &[name, content] = hi.name_content();

				hdrbuf=name;
				hdrbuf += ": ";

				rfc822::display_header(
					name,
					content,
					charset,
					std::back_inserter(hdrbuf)
				);

				if (hdrbuf.back() != '\n')
					hdrbuf.push_back('\n');

				out_closure(hdrbuf);
			}
		} while (hi.next());

		out_closure("\n");

		reformat(std::forward<out_closure_t>(out_closure),
			 *textplain_content,
			 src, 0);
	}

	if (first_attachment)
	{
		/*
		** There are attachments to copy
		*/

		if (attachment_is_message_rfc822)
		{
			/* Copy everything as a message/rfc822 */

			out_closure("\n--");
			out_closure(boundary);
			out_closure("\nContent-Type: ");

			out_closure(first_attachment->all_errors() &
				    RFC2045_ERR8BITHEADER
				    ? RFC2045_MIME_MESSAGE_GLOBAL
				    : RFC2045_MIME_MESSAGE_RFC822);
			out_closure("\n");

			if (!forwarddescr.empty())
			{
				const auto &[message, error] =
					rfc2047::encode(
						forwarddescr.begin(),
						forwarddescr.end(),
						!charset.empty() ?
						std::string{charset.begin(),
							    charset.end()}
						: "utf-8",
						rfc2047_qp_allow_any
					);

				out_closure("Content-Description: ");
				out_closure(message);
				out_closure("\n");
			}

			out_closure("\n");

			mimeforward(std::forward<out_closure_t>(out_closure),
				   *first_attachment, src);
		}
		else
		{
			/* Copy over individual attachments, one by one */

			for ( ; n_attachments--; ++first_attachment)
			{
				out_closure("\n--");
				out_closure(boundary);
				out_closure("\n");

				mimeforward(
					std::forward<out_closure_t>(
						out_closure
					),
					*first_attachment, src);
			}
		}

		out_closure("\n--");
		out_closure(boundary);
		out_closure("--\n");
	}
}

// Copy the original headers of the message into a text/rfc822-headers or
// message/global-headers MIME entity.

template<typename out_closure_t, typename src_type>
void rfc2045::reply::copyheaders(out_closure_t &&out_closure,
				 const rfc2045::entity &message,
				 src_type &src)
{

	out_closure("\nContent-Type: ");

	out_closure(message.errors.code &
		    RFC2045_ERR8BITHEADER
		    ? RFC2045_MIME_MESSAGE_GLOBAL_HEADERS
		    : RFC2045_MIME_MESSAGE_HEADERS);

	// text/rfc822-headers should be 7-bit only. message/global-headers
	// uses utf-8.

	out_closure("; charset=\"utf-8\"\n"
	       "Content-Disposition: attachment\n"
	       "Content-Transfer-Encoding: 8bit\n\n"
	       );

	rfc2045::entity::line_iter<false>::headers hi{
		message, src
	};

	hi.name_lc=false;
	hi.keep_eol=true;

	do
	{
		const auto &[header, value] = hi.name_content();

		if (header == "\n" && value.empty())
			continue;

		out_closure(header);
		out_closure(": ");
		out_closure(value);

		if (value.empty() || value.back() != '\n')
			out_closure("\n");
	} while(hi.next());
}

// Copy an entire MIME entity to the output

template<typename out_closure_t, typename src_type>
void rfc2045::reply::mimeforward(out_closure_t &&out_closure,
				 const rfc2045::entity &message,
				 src_type &src)
{
	if (static_cast<size_t>(src.pubseekpos(message.startpos))
	    != message.startpos)
		return;

	for (size_t cnt=message.endbody-message.startpos; cnt > 0; )
	{
		char buffer[BUFSIZ];

		auto n=cnt;

		if (n > BUFSIZ)
			n=BUFSIZ;

		n=src.sgetn(buffer, n);

		if (n == 0)
			return;

		out_closure(std::string_view{buffer, n});
		cnt -= n;
	}
}

// Handle all reply kinds, except for forwards.
template<typename out_closure_t, typename src_type>
void rfc2045::reply::mkreply(out_closure_t &&out_closure,
			     const rfc2045::entity &message,
			     src_type &src)
{
	std::string oldtocc, oldfrom, oldreplyto, oldtolist;
	std::string subject;
	std::string oldmsgid;
	std::string oldreferences;
	std::string oldenvelope;
	std::string date;
	std::string newsgroup;
	std::string newsgroups;

	std::string whowrote;

	{
		rfc2045::entity::line_iter<false>::headers hi{
			message, src
		};

		do
		{
			const auto &[header, value]= hi.name_content();

			if (header == "subject")
			{
				subject = std::string{value.begin(),
						      value.end()};
			}
			else if (header == "reply-to")
			{
				oldreplyto = std::string{value.begin(),
							 value.end()};
			}
			else if (header == "from")
			{
				oldfrom = std::string{value.begin(),
						      value.end()};
			}
			else if (header == "message-id")
			{
				oldmsgid = std::string{value.begin(),
						       value.end()};
			}
			else if (header == "references")
			{
				oldreferences = std::string{value.begin(),
							    value.end()};
			}
			else if ((header == "return-path" ||
				  header == "errors-to") &&
				 replytoenvelope)
			{
				oldenvelope = std::string{value.begin(),
							  value.end()};
			}
			else if (header == "newsgroups")
			{
				newsgroups = std::string{value.begin(),
							 value.end()};
			}
			else if (header == "x-newsgroup")
			{
				newsgroup = std::string{value.begin(),
							value.end()};
			}
			else if (header == "date")
			{
				date = std::string{value.begin(),
						   value.end()};
			}
			else if ((replymode == replymode_t::replyall
				  || replymode == replymode_t::replylist) &&
				 (
					 header == "to" ||
					 header == "cc"
				 )
			)
			{
				rfc822::tokens tokens{value};
				rfc822::addresses addresses{tokens};

				rfc822::tokens oldtocct{oldtocc};
				rfc822::addresses oldtocca{oldtocct};

				rfc822::tokens oldtolistt{oldtolist};
				rfc822::addresses oldtolista{oldtolistt};

				oldtocca.insert(oldtocca.end(),
						addresses.begin(),
						addresses.end());

				std::string as;

				for (auto &a:addresses)
				{
					as.clear();
					a.address.print(
						std::back_inserter(as)
					);

					if (is_mailinglist &&
					    is_mailinglist(as))
					{
						oldtolista.push_back(a);
					}
				}

				std::string newtocc;

				oldtocca.print(std::back_inserter(newtocc));

				oldtocc=std::move(newtocc);

				std::string newlist;

				oldtolista.print(std::back_inserter(newlist));
				oldtolist=std::move(newlist);
			}
		} while (hi.next());
	}

	/* Write:  "%s writes:" */

	{
		rfc822::tokens oldfromt{oldfrom};
		rfc822::addresses oldfroma(oldfromt);

		std::string sender_name, sender_addr;

		for (auto &a:oldfroma)
		{
			if (a.address.empty())
				continue;

			a.display_name(charset,
				       std::back_inserter(sender_name));
			a.display_address(charset,
					  std::back_inserter(sender_addr));
			break;
		}

		whowrote=rfc2045::reply::mksalutation(
			replysalut,
			newsgroup,
			oldmsgid,
			newsgroups,
			!sender_addr.empty() ? sender_addr
			:"(no address given)",
			!sender_name.empty() ? sender_name:
			!sender_addr.empty() ? sender_addr:"@",
			date,
			subject);
	}

	if (!oldreplyto.empty())
	{
		oldfrom=oldreplyto;
	}

	if (!oldenvelope.empty())
	{
		oldfrom=oldenvelope;
	}

	/*
	** Replytolist: if we found mailing list addresses, drop
	** oldtocc, we'll use oldtolist.
	** Otherwise, drop oldtolist.
	*/

	if (replymode == replymode_t::replylist)
	{
		if (!oldtolist.empty())
		{
			oldtocc.clear();
			oldfrom.clear();
		}
	}
	else
	{
		oldtolist.clear();
	}

	/* Remove duplicate entries from new Cc header */

	if (!oldtocc.empty())
	{
		rfc822::tokens rfccc{oldtocc};

		rfc822::tokens rfcto{oldfrom};

		rfc822::addresses arfcc{rfccc};
		rfc822::addresses arfcto{rfcto};

		std::unordered_set<std::string> to_addresses;

		for (auto &a:arfcto)
		{
			std::string address;

			a.address.print(std::back_inserter(address));

			rfc2045::entity::tolowercase(
				std::find(address.begin(),
					  address.end(),
					  '@'), address.end()
			);
			if (!address.empty())
				to_addresses.insert(std::move(address));
		}

		auto arfccp=arfcc.begin(), arfccq=arfccp;

		for (; arfccp != arfcc.end(); *arfccq++ = *arfccp++)
		{
			std::string address;

			arfccp->address.print(std::back_inserter(address));
			rfc2045::entity::tolowercase(
				std::find(address.begin(),
					  address.end(),
					  '@'), address.end()
			);

			if (!address.empty() && (
			    /* Remove address from Cc if it is my address */

				    (
					    myaddr_func &&
					    myaddr_func(address)
				    )
				    ||
				    /* Remove address from Cc if it appears in
				       To: */

				    to_addresses.find(address)
				    != to_addresses.end()
			    ))
				continue;


			/* Remove outright duplicates in Cc */
			to_addresses.insert(address);
		}

		arfcc.erase(arfccq, arfcc.end());

		std::string new_addresses;

		arfcc.print(std::back_inserter(new_addresses));

		oldtocc=std::move(new_addresses);
	}

	if (replymode == replymode_t::feedback)
	{
		oldtolist.clear();
		oldfrom.clear();
		oldtocc.clear();
	}

	if (!oldtolist.empty())
	{
		out_closure("To: ");
		out_closure(oldtolist);
		out_closure("\n");
	}

	if (!oldfrom.empty())
	{
		out_closure("To: ");
		out_closure(oldfrom);
		out_closure("\n");
	}

	if (!oldtocc.empty())
	{
		out_closure("Cc: ");
		out_closure(oldtocc);
		out_closure("\n");
	}

	if (!oldmsgid.empty() || !oldreferences.empty())
	{
		out_closure("References: ");
		out_closure(mkreferences(oldreferences, oldmsgid));
	}
	if (!oldmsgid.empty())
	{
		out_closure("In-Reply-To: ");
		out_closure(oldmsgid);
		out_closure("\n");
	}

	out_closure("Subject: ");

	if (!this->subject.empty())
	{
		out_closure(this->subject);
	}
	else if (!subject.empty())
	{
		if (replymode == replymode_t::feedback ||
		    replymode == replymode_t::replyfeedback)
		{
			out_closure(subject);
		}
		else
		{
			char	*s=rfc822_coresubj_keepblobs(subject.c_str());

			std::string subj{s ? s:""};

			if (s)
				free(s);

			out_closure("Re: ");
			out_closure(subj);
		}
	}

	if (replymode == replymode_t::replydraft)
	{
		unsigned counter=0;

		out_closure("\n");
		auto boundary=message.new_boundary(src, counter);

		out_closure("Content-Type: multipart/mixed; boundary=\"");
		out_closure(boundary);
		out_closure("\"\n\n");
		out_closure(RFC2045MIMEMSG);
		out_closure("\n--");
		out_closure(boundary);
		out_closure("\n");

		if (content_specify)
			content_specify();

		out_closure("\n--");
		out_closure(boundary);

		copyheaders(std::forward<out_closure_t>(out_closure), message,
			    src);
		out_closure("\n--");
		out_closure(boundary);
		out_closure("--\n");
		return;
	}

	out_closure("\nMime-Version: 1.0\n");

	const char *dsn_report_type{nullptr};

	void (rfc2045::reply::*dsn_report_gen)(out_closure_t &&,
					       const rfc2045::entity &,
					       src_type &);

	if (replymode == replymode_t::replydsn && !dsnfrom.empty())
	{
		dsn_report_type="delivery-status";
		dsn_report_gen=&rfc2045::reply::reply_dsn<out_closure_t,
							  src_type>;
	}
	else if (replymode == replymode_t::replyfeedback ||
		 replymode == replymode_t::feedback)
	{
		dsn_report_type="feedback-report";
		dsn_report_gen=&rfc2045::reply::reply_feedback<out_closure_t,
							       src_type>;
	}

	std::string boundary;

	if (dsn_report_type)
	{
		unsigned counter=0;

		boundary=message.new_boundary(src, counter);

		out_closure("Content-Type: multipart/report;"
		       " report-type=");

		out_closure(dsn_report_type);
		out_closure(";\n    boundary=\"");

		out_closure(boundary);

		out_closure("\"\n"
			    "\n"
			    RFC2045MIMEMSG
			    "\n"
			    "--");
		out_closure(boundary);
		out_closure("\n");
	}

	if (content_set_charset)
	{
		content_set_charset();
	}
	else
	{
		out_closure("Content-Type: text/plain; format=flowed; delsp=yes; charset=\"");
		out_closure(charset);
		out_closure("\"\n");
	}
	out_closure("Content-Transfer-Encoding: 8bit\n\n");

	if (!donotquote)
	{
		if (!whowrote.empty())
		{
			out_closure(whowrote);
			out_closure("\n\n");
		}

		const rfc2045::entity *text_plain{nullptr};

		message.visit_all(
			[&]
			(const rfc2045::entity &e)
			{
				if (e.content_type.value == "text/plain")
				{
					text_plain= &e;
					return false;
				}
				return true;
			});

		if (text_plain)
			reformat(std::forward<out_closure_t>(out_closure),
				 *text_plain,
				 src, 1);
		out_closure("\n");	/* First blank line in the reply */
	}

	if (content_specify)
		content_specify();

	out_closure("\n");
	if (writesig_func)
		writesig_func();
	out_closure("\n");

	if (dsn_report_type)
	{
		/* replydsn or replyfeedback */

		out_closure("\n--");
		out_closure(boundary);
		out_closure("\n");

		(this->*dsn_report_gen)(
			std::forward<out_closure_t>(out_closure),
			message,
			src
		);

		out_closure("\n--");
		out_closure(boundary);

		if (fullmsg)
		{
			out_closure("\nContent-Type: ");

			out_closure(message.all_errors() &
				    RFC2045_ERR8BITHEADER
				    ? RFC2045_MIME_MESSAGE_GLOBAL
				    : RFC2045_MIME_MESSAGE_RFC822);

			out_closure("\n"
			       "Content-Disposition: attachment\n\n");

			mimeforward(std::forward<out_closure_t>(out_closure),
				    message,
				    src);
		}
		else
		{
			copyheaders(
				std::forward<out_closure_t>(out_closure),
				message,
				src);
		}
		out_closure("\n--");
		out_closure(boundary);
		out_closure("--\n");
	}
}

template<typename out_closure_t>
void rfc2045::reply::dsn_arrival_date(out_closure_t &&out_closure)
{
	out_closure("Arrival-Date: ");

	time_t now;

	time(&now);

	out_closure(rfc822_mkdate(now));
	out_closure("\n");
}

template<typename out_closure_t, typename src_type>
void rfc2045::reply::reply_dsn(out_closure_t &&out_closure,
			       const rfc2045::entity &message,
			       src_type &src)
{
	if (std::find_if(dsnfrom.begin(), dsnfrom.end(),
			 []
			 (char c)
			 {
				 return (c & 0x80) != 0;
			 }) != dsnfrom.end())
	{
		out_closure("Content-Type: "
		       RFC2045_MIME_MESSAGE_GLOBAL_DELIVERY_STATUS
		       "; charset=\"utf-8\"\n");
		out_closure("Content-Transfer-Encoding: 8bit\n\n");
	}
	else
	{
		out_closure("Content-Type: "
		       RFC2045_MIME_MESSAGE_DELIVERY_STATUS
		       "\n");
		out_closure("Content-Transfer-Encoding: 7bit\n\n");
	}

	dsn_arrival_date(std::forward<out_closure_t>(out_closure));

	out_closure("\n"
		    "Final-Recipient: rfc822; ");

	out_closure(dsnfrom);

	out_closure("\n"
		    "Action: delivered\n"
		    "Status: 2.0.0\n");
}

template<typename out_closure_t, typename src_type>
void rfc2045::reply::reply_feedback(out_closure_t &&out_closure,
				    const rfc2045::entity &message,
				    src_type &src)
{
	out_closure("Content-Type: message/feedback-report; charset=\"utf-8\"\n");
	out_closure("Content-Transfer-Encoding: 8bit\n\n");

	dsn_arrival_date(std::forward<out_closure_t>(out_closure));
	out_closure("User-Agent: librfc2045 "
	       RFC2045PKG "/" RFC2045VER
	       "\n"
	       "Version: 1\n");

	for (const auto &[header, value] : feedbackheaders)
	{
		std::string header_cpy{header.begin(), header.end()};

		rfc2045::entity::tolowercase(header_cpy);

		char lastch='-';
		for (char &c:header_cpy)
		{
			if (lastch == '-' && c >= 'a' && c <= 'z')
				c += 'A' - 'a';
			lastch=c;
		}

		out_closure(header_cpy);
		out_closure(": ");
		out_closure(value);
		out_closure("\n");
	}
}

#endif
