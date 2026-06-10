/*
** Copyright 2025-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<idn2.h>
#include	<vector>
#include	<functional>

namespace {
#if 0
}
#endif

void tokenize(const char *p,
	      size_t plen,
	      std::function<void (char c, const char *, size_t)> parsed_func,
	      const std::function<void (size_t)> &err_func
)
{
	size_t	i=0;
	bool	inbracket=false;

	char	tokp_token;
	const char *tokp_ptr;
	size_t  tokp_len;

	while (plen)
	{
		if (isspace((int)(unsigned char)*p))
		{
			++p; --plen;
			i++;
			continue;
		}

#define SPECIALS "<>@,;:.[]()%!\"\\?=/"

		switch (*p)	{
		int	level;

		case '(':

			tokp_token='(';
			tokp_ptr=p;
			tokp_len=0;

			level=0;
			for (;;)
			{
				if (plen == 0)
				{
					err_func(i);
					tokp_token='"';
					parsed_func(tokp_token,
						    tokp_ptr, tokp_len);
					return;
				}
				if (*p == '(')
					++level;
				if (*p == ')' && --level == 0)
				{
					++p; --plen;
					i++;
					tokp_len++;
					break;
				}
				if (*p == '\\' && plen > 1)
				{
					++p; --plen;
					i++;
					tokp_len++;
				}

				i++;
				tokp_len++;
				++p; --plen;
			}
			parsed_func(tokp_token, tokp_ptr, tokp_len);
			continue;

		case '"':
			++p; --plen;
			i++;

			tokp_token='"';
			tokp_ptr=p;
			tokp_len=0;

			while (*p != '"')
			{
				if (plen == 0)
				{
					err_func(i);
					parsed_func(tokp_token,
						    tokp_ptr, tokp_len);
					return;
				}
				if (*p == '\\' && plen > 1)
				{
					tokp_len++;
					++p; --plen;
					i++;
				}
				tokp_len++;
				++p; --plen;
				i++;
			}
			parsed_func(tokp_token, tokp_ptr, tokp_len);
			++p; --plen;
			i++;
			continue;
		case '\\':
		case ')':
			err_func(i);
			++p; --plen;
			++i;
			continue;

		case '=':

			if (plen > 1 && p[1] == '?')
			{
				size_t j;

				/* exception: =? ... ?= */

				for (j=2; j < plen; j++)
				{
					if (p[j] == '?' && j+1 < plen &&
					    p[j+1] == '=')
						break;

					if (p[j] == '?' || p[j] == '=')
						continue;

					if (strchr(RFC822_SPECIALS, p[j]) ||
					    isspace(p[j]))
						break;
				}

				if (j+1 < plen && p[j] == '?' && p[j+1] == '=')
				{
					j += 2;

					tokp_token=0;
					tokp_ptr=p;
					tokp_len=j;
					parsed_func(tokp_token,
						    tokp_ptr, tokp_len);

					p += j; plen -= j;
					i += j;
					continue;
				}
			}
			/* FALLTHROUGH */

		case '<':
		case '>':
		case '@':
		case ',':
		case ';':
		case ':':
		case '.':
		case '[':
		case ']':
		case '%':
		case '!':
		case '?':
		case '/':

			if ( (*p == '<' && inbracket) ||
				(*p == '>' && !inbracket))
			{
				err_func(i);
				++p; --plen;
				++i;
				continue;
			}

			if (*p == '<')
				inbracket=true;

			if (*p == '>')
				inbracket=false;

			tokp_token= *p;
			tokp_ptr=p;
			tokp_len=1;
			parsed_func(tokp_token, tokp_ptr, tokp_len);

			if (*p == '<' && plen > 1 && p[1] == '>')
					/* Fake a null address */
			{
				tokp_token=0;
				tokp_ptr=p+1;
				tokp_len=0;
				parsed_func(tokp_token, tokp_ptr, tokp_len);
			}
			++p; --plen;
			++i;
			continue;
		default:

			tokp_token=0;
			tokp_ptr=p;
			tokp_len=0;

			size_t j=i;

			while (plen &&
			       !isspace((int)(unsigned char)*p) &&
			       strchr(SPECIALS, *p) == 0)
			{
				++tokp_len;
				++p; --plen;
				++i;
			}
			if (i == j)	/* Idiot check */
			{
				err_func(i);

				tokp_token='"';
				tokp_ptr=p;
				tokp_len=1;
				parsed_func(tokp_token,
					    tokp_ptr, tokp_len);
				++p; --plen;
				++i;
				continue;
			}
			parsed_func(tokp_token,
				    tokp_ptr, tokp_len);
		}
	}
}

#if 0
{
#endif
}

void rfc822::token::print(std::function<void (const char *, size_t)> print_func)
	const
{
	char c;
	const char *token_ptr=str.data();
	size_t token_len=str.size();

	if (type == 0 || type == '(')
	{
		print_func(token_ptr, token_len);
		return;
	}

	if (type != '"')
	{
		c= (char)type;
		print_func(&c, 1);
		return;
	}

	c='"';

	print_func(&c, 1);

	while (token_len)
	{
		size_t i;

		for (i=0; i<token_len; ++i)
		{
			if (token_ptr[i] == '"')
				break;

			if (token_ptr[i] == '\\')
			{
				if (i+1 == token_len)
					break;
				++i;
			}
		}

		if (i)
		{
			print_func(token_ptr, i);
			token_ptr += i;
			token_len -= i;
			continue;
		}

		c='\\';
		print_func(&c, 1);
		print_func(token_ptr, 1);
		++token_ptr;
		--token_len;
	}
	c='"';

	print_func(&c, 1);
}

rfc822::tokens::tokens(std::string_view str)
	: tokens{str, [](size_t){}}
{
}

rfc822::tokens::tokens(std::string_view str,
		       const std::function<void (size_t)> &err_func)
{
	size_t ntokens=0;

	tokenize(str.data(), str.size(),
		 [&]
		 (char c, const char *, size_t)
		 {
			 ++ntokens;
		 }, [](size_t){}
	);

	this->reserve(ntokens);

	tokenize(str.data(), str.size(),
		 [this]
		 (char c, const char *ptr, size_t n)
		 {
			 push_back(
				 {
					 c,
					 { ptr, n}
				 }
			 );
		 },
		 err_func
	);
}

namespace {
#if 0
}
#endif

struct addr_parser {
	rfc822::tokens &t;
	rfc822::addresses &addrs;

	size_t pos=0;

	size_t naddrs=0;

	rfc822::tokens addr_name;

	char get_nth_token(size_t n)
	{
		return t.at(pos+n).type;
	}

	void consume_n_tokens(size_t n)
	{
		pos += n;
	}

	void make_quoted_token(size_t n)
	{
		auto &first_token=t.at(pos);

		first_token = {
			// quoted string
			'"',
			{
				first_token.str.data(),

				// We know that all the ptrs point
				// to parts of the same string.

				static_cast<size_t>(
					t.at(pos+n-1).str.data() +
					t.at(pos+n-1).str.size()
					- first_token.str.data()
				),
			}
		};
	}

	void make_quoted_token_ignore(size_t n)
	{
	}

	void define_addr_name_ignore(size_t n, int convert_quotes)
	{
	}

	void define_addr_tokens_ignore(size_t n, int name_from_comment)
	{
		++naddrs;
	}

	void define_addr_name(size_t n, int convert_quotes)
	{
		if (n == 0)
			addr_name.clear();
		else
			addr_name=rfc822::tokens{
				t.begin()+pos,
				t.begin()+pos+n
			};

		if (convert_quotes)
		{
			/* Any comments in the name part are changed
			   to quotes */

			for (auto &t:addr_name)
				if (t.type == '(')
					t.type='"';
		}
	}

	void define_addr_tokens(size_t n, int name_from_comment)
	{
		if (name_from_comment)
		{
			rfc822::token save_token;

			define_addr_name(0, 0);

			/*
			** Ok, now get rid of embedded comments in the
			** address.
			**
			** Consider the last comment to be the real
			** name.
			*/

			auto b=t.begin()+pos,
				e=t.begin()+pos+n,
				p=b;

			for (; b != e; ++b)
			{
				if (b->type == '(')
				{
					save_token=*b;
					continue;
				}

				*p++=*b;
			}

			if (save_token.str.size())
			{
				addr_name=rfc822::tokens{
					&save_token, &save_token+1
				};
			}

			n=p-(t.begin()+pos);
		}

		addrs.push_back(
			{
				std::move(addr_name),
				rfc822::tokens{
					t.begin()+pos,
					t.begin()+pos+n
				}
			});
	}
	void parseaddr(
		size_t ntokens,
		char (addr_parser::*get_nth_token)(size_t),
		void (addr_parser::*consume_n_tokens)(size_t),
		void (addr_parser::*make_quoted_token)(size_t),
		void (addr_parser::*define_addr_name)(size_t, int),
		void (addr_parser::*define_addr_tokens)(size_t, int)
	);
};
/*
** Parse rfc822 tokens into discrete addresses.
**
** - ntokens: number of tokens
**
** - get_nth_token: retrieve nth token, the parameter goes from 0 to ntokens-1
**
** - consume_n_token: mark the first n token as being processed, subsequent
**   calls to get_nth_token are 0-based from the remaining unprocessed
**   tokens.
**
** - make_quoted_token: take the first n tokens, and replace them with a single
**   quote token, '"'.
**
** - define_addr_name, define_addr_tokens - specify the next address's
**   recipient name, and the address itself
**
** define_addr_name and define_addr_tokens get called to define the next
** address's recipient name, if there is one, and the address itself.
**
** define_addr_name is always called before define_addr_tokens, except if
** name_from_comment is set to true, the second parameter to define_addr_tokens.
** Both define_addr_name and define_attr_tokens' first parameter is the
** number of tokens that comprise the recipient name, or the recipient address.
** They are interleaved with consume_n_token calls that end up discarding
** all tokens that do not comprise the name or the address portion.
**
** The second parameter to define_addr_name is convert_quotes, if set any
** '(' tokens in the name portion should be replaced with '"', this is to
** adjust invalid formatting.
**
** define_addr_tokens' second parameter, name_from_comment, is set when it is
** called to set the address's tokens, but any '(' from the number of tokens
** specified by the first parameter should be removed and set to the address's
** name (and define_addr_name is not called, beforehand, in this case). The
** current implementation only grabs the last '(' token (there should only
** be one).
*/

void addr_parser::parseaddr(
	size_t ntokens,
	char (addr_parser::*get_nth_token)(size_t),
	void (addr_parser::*consume_n_tokens)(size_t),
	void (addr_parser::*make_quoted_token)(size_t),
	void (addr_parser::*define_addr_name)(size_t, int),
	void (addr_parser::*define_addr_tokens)(size_t, int)
)
{
	int	flag;
	char c;

	while (ntokens)
	{
		size_t	i;

		/* atoms (token=0) or quoted strings, followed by a : token
		is a list name. */

		for (i=0; i<ntokens; i++)
		{
			c=(this->*get_nth_token)(i);

			if (c && c != '"')
				break;
		}

		if (i < ntokens && c == ':')
		{
			++i;

			(this->*define_addr_name)(i, 0);
			(this->*define_addr_tokens)(0, 0);
			ntokens -= i;
			(this->*consume_n_tokens)(i);
			continue;  /* Group=phrase ":" */
		}

		/* Spurious commas are skipped, ;s are recorded */

		c=(this->*get_nth_token)(0);

		if (c == ',' || c == ';')
		{
			if (c == ';')
			{
				(this->*define_addr_name)(1, 0);
				(this->*define_addr_tokens)(0, 0);
			}
			--ntokens;
			(this->*consume_n_tokens)(1);
			continue;
		}

		/* If we can find a '<' before the next comma or semicolon,
		we have new style RFC path address */

		for (i=0; i<ntokens; i++)
		{
			c=(this->*get_nth_token)(i);

			if (c == ';' || c == ',' || c == '<')
				break;
		}

		if (i < ntokens && c == '<')
		{
			size_t	j;

			/* Ok -- what to do with the stuff before '>'???
			If it consists exclusively of atoms, leave them alone.
			Else, make them all a quoted string. */

			for (j=0; j<i; j++)
			{
				c=(this->*get_nth_token)(j);

				if (! (c == 0 || c == '('))
					break;
			}

			if (j == i)
			{
				(this->*define_addr_name)(i, 1);
			}
			else	/* Intentionally corrupt the original toks */
			{
				(this->*make_quoted_token)(i);
				(this->*define_addr_name)(1, 1);
			}

			/* Now that's done and over with, see what can
			be done with the <...> part. */

			++i;
			ntokens -= i;
			(this->*consume_n_tokens)(i);
			for (i=0; i<ntokens && (this->*get_nth_token)(i) != '>'; i++)
				;

			(this->*define_addr_tokens)(i, 0);
			ntokens -= i;
			(this->*consume_n_tokens)(i);
			if (ntokens)	/* Skip the '>' token */
			{
				--ntokens;
				(this->*consume_n_tokens)(1);
			}
			continue;
		}

		/* Ok - old style address.  Assume the worst */

		/* Try to figure out where the address ends.  It ends upon:
		a comma, semicolon, or two consecutive atoms. */

		flag=0;
		for (i=0; i<ntokens; i++)
		{
			c=(this->*get_nth_token)(i);

			if (c == ',' || c == ';')
				break;

			if (c == '(')	continue;
					/* Ignore comments */
			if (c == 0 || c == '"')
				/* Atom */
			{
				if (flag)	break;
				flag=1;
			}
			else	flag=0;
		}
		if (i == 0)	/* Must be spurious comma, or something */
		{
			--ntokens;
			(this->*consume_n_tokens)(1);
			continue;
		}

		(this->*define_addr_tokens)(i, 1);
		ntokens -= i;
		(this->*consume_n_tokens)(i);
	}
}
#if 0
{
#endif
};

rfc822::addresses::addresses(tokens &addrvec)
{
	addr_parser info{addrvec, *this};

	info.parseaddr(
		addrvec.size(),
		&addr_parser::get_nth_token,
		&addr_parser::consume_n_tokens,
		&addr_parser::make_quoted_token_ignore,
		&addr_parser::define_addr_name_ignore,
		&addr_parser::define_addr_tokens_ignore
	);

	this->reserve(info.naddrs);
	info.pos=0;

	info.parseaddr(
		addrvec.size(),
		&addr_parser::get_nth_token,
		&addr_parser::consume_n_tokens,
		&addr_parser::make_quoted_token,
		&addr_parser::define_addr_name,
		&addr_parser::define_addr_tokens
	);
}

std::u32string rfc822::idn2unicode(std::string &idn)
{
	// Invalid UTF-8 can make libidn go off the deep end. Add
	// padding as a workaround.

	for (size_t i=0; i<16; i++)
		idn.push_back(0);

	uint32_t *u32_ptr;

	auto err=idn2_to_unicode_8z4z(idn.c_str(), &u32_ptr, 0);

	if (err != IDNA_SUCCESS)
	{
		return std::u32string{
			U"[encoding error: "
		} + std::u32string{idn.begin(), idn.end()} + U"]";
	}

	std::u32string u32{reinterpret_cast<char32_t *>(u32_ptr)};

	for (auto &uc:u32)
		uc=unicode_lc(uc);
	free(u32_ptr);

	return u32;
}

bool rfc822::address::do_print::old_style()
{
	return !a.name.empty() && a.name.begin()->type == '(';
}

void rfc822::address::do_print::output()
{
	if (a.address.empty())
	{
		emit_name();
		return;
	}

	if (old_style())
	{
		// old style

		emit_address();
		emit_char(' ');
		emit_name();
		return;
	}

	bool print_braces=false;

	if (!a.name.empty())
	{
		emit_name();
		emit_char(' ');
		print_braces=true;
	}
	else
	{
		bool prev_is_atom=false;

		for (auto &t:a.address)
		{
			bool is_atom=t.is_atom();

			if (is_atom && prev_is_atom)
			{
				print_braces=true;
				break;
			}
			prev_is_atom=is_atom;
		}
	}

	if (print_braces)
	{
		emit_char('<');
	}
	emit_address();
	if (print_braces)
	{
		emit_char('>');
	}
}

void rfc822::addresses::do_print::output()
{
	const char *sep="";

	while (!eof())
	{
		int trailer=0;

		auto &this_address=ref();

		if (this_address.address.empty() && !this_address.name.empty())
			trailer=(--this_address.name.end())->type;

		if (trailer == ';')
			if (*sep == ',')
				sep="";

		if (*sep)
			print_separator(sep);

		sep=", ";

		switch (trailer) {
		case ':':
		case ';':
			sep=" ";
			break;
		}

		print();
	}
}

rfc2047::qpdecoder_base::qpdecoder_base(bool mime_encoded_word)
	: mime_encoded_word{mime_encoded_word},
	  handler{&qpdecoder_base::do_char}
{
}

rfc2047::qpdecoder_base::~qpdecoder_base()=default;

void rfc2047::qpdecoder_base::process_char(const char *p, size_t n)
{
	while (n)
	{
		auto done=(this->*handler)(p, n);

		p += done;
		n -= done;
	}
}

size_t rfc2047::qpdecoder_base::do_char(const char *p, size_t n)
{
	size_t i;

	for (i=0; i<n; ++i)
		if (p[i] == '=' || (mime_encoded_word && p[i] == '_'))
			break;

	if (i == 0)
	{
		if (p[0] == '_')
		{
			emit(" ", 1);
		}
		else
		{
			handler=&qpdecoder_base::do_prev_equal;
		}
		return 1;
	}

	emit(p, i);
	return i;
}

void rfc2047::qpdecoder_base::finished()
{
	if (handler != &qpdecoder_base::do_char)
		report_error();
}

void rfc2047::qpdecoder_base::report_error()
{
	handler=&qpdecoder_base::do_char;
	if (error_occured)
		return;
	error_occured=true;

	std::string_view msg{" [quoted-printable decoding error] "};
	emit(msg.data(), msg.size());
}

size_t rfc2047::qpdecoder_base::do_prev_equal(const char *p, size_t n)
{
	if (*p == '\r')
	{
		handler=&qpdecoder_base::do_prev_equal_cr;
		return 1;
	}
	if (*p == '\n')
	{
		handler=&qpdecoder_base::do_char;
		return 1;
	}

	auto found=strchr(rfc2047_xdigit, *p);
	if (!found)
	{
		report_error();
		return 1;
	}

	int h=found-rfc2047_xdigit;

	if (h > 15) h-=6;

	nybble=(h << 4);
	handler=&qpdecoder_base::do_prev_equal_h1;
	return 1;
}

size_t rfc2047::qpdecoder_base::do_prev_equal_cr(const char *p, size_t n)
{
	handler=&qpdecoder_base::do_char;
	if (*p != '\n')
	{
		report_error();
	}
	return 1;
}

size_t rfc2047::qpdecoder_base::do_prev_equal_h1(const char *p, size_t n)
{
	handler=&qpdecoder_base::do_char;

	auto found=strchr(rfc2047_xdigit, *p);
	if (!found)
	{
		report_error();
		return 1;
	}

	int l=found-rfc2047_xdigit;

	if (l > 15) l-=6;

	char c=static_cast<char>(nybble | l);

	emit(&c, 1);
	return 1;
}

rfc2047::base64decoder_base::base64decoder_base()=default;
rfc2047::base64decoder_base::~base64decoder_base()=default;

void rfc2047::base64decoder_base::process_char(const char *p, size_t n)
{
	size_t bufsiz=n+4;

	if (bufsiz > BUFSIZ)
		bufsiz=BUFSIZ;

	char out_buf[bufsiz];
	size_t out_bufptr=0;

	while (n)
	{
		char ch = *p++;
		--n;

		if (ch >= '0' && ch <= '9')
			;
		else if (ch >= 'A' && ch <= 'Z')
			;
		else if (ch >= 'a' && ch <= 'z')
			;
		else switch (ch) {
			case '+':
			case '/':
			case '=':
				break;
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				continue;
			}

		if (seen_end)
		{
			report_error();
			return;
		}

		if (bufferp && buffer[bufferp-1] == '=' && ch != '=')
		{
			report_error();
			return;
		}
		buffer[bufferp++]=ch;

		if (bufferp < 4)
			continue;

		bufferp=0;

		int w=rfc2047_decode64tab[buffer[0]];
		int x=rfc2047_decode64tab[buffer[1]];
		int y=rfc2047_decode64tab[buffer[2]];
		int z=rfc2047_decode64tab[buffer[3]];

		unsigned char g= (w << 2) | (x >> 4);
		unsigned char h= (x << 4) | (y >> 2);
		unsigned char i= (y << 6) | z;

		char out[3]={static_cast<char>(g),
			     static_cast<char>(h),
			     static_cast<char>(i)};

		size_t n=1;

		if (buffer[2] != '=')
			++n;
		if (buffer[3] != '=')
			++n;
		else seen_end=true;

		if (out_bufptr + n > bufsiz)
		{
			emit(out_buf, out_bufptr);
			out_bufptr=0;
		}

		for (size_t i=0; i<n; ++i)
		{
			out_buf[out_bufptr++]=out[i];
		}
	}

	if (out_bufptr)
		emit(out_buf, out_bufptr);
}

void rfc2047::base64decoder_base::finished()
{
	if (bufferp)
		report_error();
}

void rfc2047::base64decoder_base::report_error()
{
	if (error_occured)
		return;
	error_occured=true;

	std::string_view msg{" [base64 decoding error] "};
	emit(msg.data(), msg.size());
}

static std::string rfc822_encode_domain_int(
	std::string_view pfix,
	std::string_view domain
)
{
	int err;
	char *p;
	size_t s=domain.size()+16;

	std::string cpy;

	cpy.reserve(s);

	/*
	** Invalid UTF-8 can make libidn go off the deep end. Add
	** padding as a workaround.
	*/

	cpy.append(domain.begin(), domain.end());
	cpy.append(16, 0);

	err=idn2_to_ascii_8z(cpy.data(), &p, 0);

	std::string_view pv{p};
	if (err != IDNA_SUCCESS)
	{
		return "[error]";
	}

	std::string q;

	q.reserve(pv.size()+pfix.size());

	q.append(pfix.begin(), pfix.end());
	q.append(pv.begin(), pv.end());
	free(p);
	return q;
}

namespace {
	struct encode_domain_converter : unicode::iconvert {

		std::string s;

		int converted(const char *p, size_t n) override
		{
			s.append(p, p+n);
			return 0;
		}

		using iconvert::operator();
	};
}

std::string rfc822::encode_domain(std::string_view address,
				  const char *charset)
{
	bool errflag{false};

	encode_domain_converter converter;

	if (converter.begin(charset, unicode::utf_8))
	{
		converter(address.data(), address.size());

		converter.end(errflag);
	}
	else
	{
		errflag=true;
	}

	auto at=converter.s.find('@');

	if (at == converter.s.npos)
		at=0;
	else
		++at;

	auto s=rfc822_encode_domain_int(
		std::string_view{converter.s}.substr(0, at),
		std::string_view{converter.s}.substr(at)
	);

	if (errflag)
	{
		s += "(error)";
	}
	return s;
}
