/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	"rfc2047.h"
#include	<idn2.h>

rfc822::tokens::tokens(std::string_view str,
		       std::function<void (size_t)> err_func)
{
	size_t ntokens=0;

	rfc822_tokenize(str.data(), str.size(),
			[]
			(char c, const char *, size_t, void *voidp)
			{
				++*static_cast<size_t *>(voidp);
			}, &ntokens,

			[]
			(const char *ptr, size_t p, void *voidp)
			{
			}, nullptr);

	this->reserve(ntokens);

	rfc822_tokenize(str.data(), str.size(),
			[]
			(char c, const char *ptr, size_t n, void *voidp)
			{
				static_cast<tokens *>(voidp)->push_back(
					{
						c,
						{ ptr, n}
					}
				);
			}, this,
			[]
			(const char *ptr, size_t p, void *voidp)
			{
				auto f=static_cast<std::function<void (size_t)
								 > *>(voidp);

				(*f)(p);
			}, &err_func);
}

namespace {

	struct rfc822a_info {
		rfc822::tokens &t;
		rfc822::addresses &addrs;

		size_t pos=0;

		size_t naddrs=0;

		rfc822::tokens addr_name;
	};
};

static char get_nth_token(size_t n, void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);

	return info->t.at(info->pos+n).type;
}

static void consume_n_tokens(size_t n, void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);

	info->pos += n;
}

static void make_quoted_token(size_t n, void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);

	auto &first_token=info->t.at(info->pos);
	first_token = {
		// quoted string
		'"',
		{
			first_token.str.data(),

			// We know that all the ptrs point
			// to parts of the same string.

			static_cast<size_t>(
				info->t.at(info->pos+n-1).str.data() +
				info->t.at(info->pos+n-1).str.size()
				- first_token.str.data()
			),
		}
	};
}

static void make_quoted_token_ignore(size_t n, void *voidp)
{
}

static void define_addr_name_ignore(size_t n, int convert_quotes, void *voidp)
{
}

static void define_addr_tokens_ignore(size_t n, int name_from_comment,
				      void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);
	++info->naddrs;
}

static void define_addr_name(size_t n, int convert_quotes, void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);

	if (n == 0)
		info->addr_name.clear();
	else
		info->addr_name=rfc822::tokens{
			info->t.begin()+info->pos,
			info->t.begin()+info->pos+n
		};

	if (convert_quotes)
	{
		/* Any comments in the name part are changed to quotes */

		for (auto &t:info->addr_name)
			if (t.type == '(')
				t.type='"';
	}
}

static void define_addr_tokens(size_t n, int name_from_comment, void *voidp)
{
	rfc822a_info *info=reinterpret_cast<rfc822a_info *>(voidp);

	if (name_from_comment)
	{
		rfc822::token save_token;

		define_addr_name(0, 0, voidp);

		/*
		** Ok, now get rid of embedded comments in the address.
		** Consider the last comment to be the real name
		*/

		auto b=info->t.begin()+info->pos,
			e=info->t.begin()+info->pos+n,
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
			info->addr_name=rfc822::tokens{
				&save_token, &save_token+1
			};
		}

		n=p-(info->t.begin()+info->pos);
	}

	info->addrs.push_back(
		{
			std::move(info->addr_name),
			rfc822::tokens{
				info->t.begin()+info->pos,
				info->t.begin()+info->pos+n
			}
		});
}

rfc822::addresses::addresses(tokens &addrvec)
{
	rfc822a_info info{addrvec, *this};

	rfc822_parseaddr(addrvec.size(),
			 get_nth_token, consume_n_tokens,
			 make_quoted_token_ignore,
			 define_addr_name_ignore, define_addr_tokens_ignore,

			 &info);

	this->reserve(info.naddrs);
	info.pos=0;

	rfc822_parseaddr(addrvec.size(),
			 get_nth_token, consume_n_tokens,
			 make_quoted_token,
			 define_addr_name, define_addr_tokens,
			 &info);
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
	free(u32_ptr);

	return u32;
}

void rfc822::address::do_print::output()
{
	if (a.address.empty())
	{
		emit_name();
		return;
	}

	if (!a.name.empty() && a.name.begin()->type == '(')
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
			bool is_atom=rfc822_is_atom(t.type);

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

rfc2047::qpdecoder_base::qpdecoder_base()
	: handler{&qpdecoder_base::do_char}
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
		if (p[i] == '=')
			break;

	if (i == 0)
	{
		handler=&qpdecoder_base::do_prev_equal;
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
