#ifdef rfc822_h
#ifdef rfc2047_h
#ifdef  __cplusplus

#include <string>
#include <type_traits>
#include <algorithm>

namespace rfc822 {
#if 0
}
#endif

std::u32string idn2unicode(std::string &idn);

template<typename out_iter_type> auto tokens::unicode_address(
	out_iter_type &&iter
) const -> std::conditional_t<std::is_same_v<out_iter_type,
					     out_iter_type &>,
			      void, std::remove_cv_t<
				      std::remove_reference_t<
					      out_iter_type>>>
{
	std::string s;

	s.reserve(print(length_counter{}));

	print(std::back_inserter(s));

	size_t p=s.rfind('@');

	if (p == s.npos)
	{
		p=s.size();
	}
	else
	{
		p++;
	}

	std::string domain;

	domain.reserve(s.size()-p+16);

	auto b=s.begin();

	while (p)
	{
		*iter++=static_cast<char32_t>(static_cast<unsigned char>(*b++));
		--p;

	}

	domain.append(b, s.end());

	auto us=idn2unicode(domain);

	for (auto &c:us)
	{
		*iter++=c;
	}

	if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
		return iter;
}

template<typename out_iter_type> auto tokens::unicode_name(
	out_iter_type &&iter,
	bool unquote
) const -> std::conditional_t<std::is_same_v<out_iter_type,
					     out_iter_type &>,
			      void, std::remove_cv_t<
				      std::remove_reference_t<
					      out_iter_type>>>
{
	std::string s;

	if (unquote)
	{
		s.reserve(this->unquote(this->begin(), this->end(),
					length_counter{}));
		this->unquote(this->begin(), this->end(),
			    std::back_inserter(s));
	}
	else
	{
		s.reserve(this->print(this->begin(), this->end(),
				      length_counter{}));
		this->print(this->begin(), this->end(),
			    std::back_inserter(s));
	}
	std::string fragment;
	std::u32string us;

	rfc2047::decode(
		s.begin(),
		s.end(),
		[&]
		(auto &&charset, auto &&language, auto closure)
		{
			fragment.clear();

			auto closure_iter=std::back_inserter(fragment);

			closure(closure_iter);

			bool errflag{false};

			unicode::iconvert::tou::convert(
				fragment.begin(),
				fragment.end(),
				charset,
				errflag,
				std::back_inserter(us));

			if (errflag)
				for (char c: std::string_view{
						" (encoding error)"})
					us.push_back(c);
		},
		[&](auto &&b, auto &&e, auto error_message)
		{
			std::string_view s{error_message};

			*iter++='(';
			for (auto c:s)
			{
				*iter++=c;
			}
			*iter++=')';
		});

	// Characters that require quoting, in the context of the name
	// portion, are RFC822_SPECIALS except for : and ; at the end of it.

	static constexpr std::string_view special{RFC822_SPECIAL_INNAMES};

	// Determine if we have a quoted string.

	bool quote_inuse=false;

	if (unquote)
		;
	else if (us.size() > 1 && us[0] == '"' && us.back() == '"')
	{
		quote_inuse=true;
	}
	else
	{
		// If not, and there are any special characters or
		// consecutive whitespace: we'll make it a quoted string.

		bool prev_spc=true;

		bool last_was_colon_or_semicolon=false;

		for (auto uc:us)
		{
			if (last_was_colon_or_semicolon)
			{
				quote_inuse=true;
				break;
			}

			if (uc == ' ' || uc == '\t')
			{
				if (prev_spc)
				{
					quote_inuse=true;
					break;
				}

				prev_spc=true;
				continue;
			}

			prev_spc=false;
			if ((uc >= 0 && uc < ' ') ||
			    special.find(uc) < special.size())
			{
				quote_inuse=true;
				break;
			}

			if (uc == ':' || uc == ';')
			{
				last_was_colon_or_semicolon=true;
			}
		}

		if (quote_inuse)
		{
			us.insert(us.begin(), '"');
			us.push_back('"');
		}
	}

	// If we have a quoted string, when we output it we'll make sure that
	// any special characters are \-ed.

	auto b=us.begin(), e=us.end();

	if (quote_inuse)
	{
		*iter++ = '"';
		++b;
		--e;
	}

	while (b != e)
	{
		if (quote_inuse)
		{
			switch (*b) {
			case '\\':
				if (b+1 == e)
				{
					*iter++ = '\\';
				}
				break;
			case '"':
				*iter++='\\';
				break;
			}
		}
		*iter++=*b++;
	}

	if (quote_inuse)
	{
		*iter++ = '"';
	}

	if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
		return iter;
}

template<typename out_iter_type> struct u2iterator : unicode::iconvert::fromu {
	out_iter_type &iter;

	u2iterator(out_iter_type &iter) : iter{iter} {}

	int converted(const char *p, size_t l) override
	{
		while (l--)
			*iter++=*p++;
		return 0;
	}

	using iterator_category=std::output_iterator_tag;
	using value_type=void;
	using pointer=void;
	using reference=void;
	using difference_type=void;

	u2iterator &operator=(char32_t c)
	{
		(*this)(&c, 1);
		return *this;
	}

	u2iterator &operator++(int) { return  *this; }
	u2iterator &operator++() { return *this; }
	u2iterator &operator*() { return *this; }
};

template<typename out_iter_type> auto tokens::display_address(
	const std::string &chset,
	out_iter_type &&iter
) const -> std::conditional_t<std::is_same_v<out_iter_type,
					     out_iter_type &>,
			      void, std::remove_cv_t<
				      std::remove_reference_t<
					      out_iter_type>>>
{
	u2iterator u{iter};
	if (u.begin(chset))
	{
		unicode_address(u);

		bool errflag{false};

		if (u.end(errflag) && !errflag)
		{
			if constexpr(!std::is_same_v<out_iter_type,
				     out_iter_type &>)
				return iter;
			else
				return;
		}
	}

	for (char c: std::string_view{"(decoding error)"})
		*iter++=c;

	if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
		return iter;
}

template<typename out_iter_type> auto tokens::display_name(
	const std::string &chset,
	out_iter_type &&iter,
	bool unquote
) const -> std::conditional_t<std::is_same_v<out_iter_type,
					     out_iter_type &>,
			      void, std::remove_cv_t<
				      std::remove_reference_t<
					      out_iter_type>>>
{
	u2iterator u{iter};
	if (u.begin(chset))
	{
		unicode_name(u, unquote);

		bool errflag{false};

		if (u.end(errflag) && !errflag)
		{
			if constexpr(!std::is_same_v<out_iter_type,
				     out_iter_type &>)
				return iter;
			else
				return;
		}
	}

	for (char c: std::string_view{"(decoding error)"})
		*iter++=c;

	if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
		return iter;
}

inline void display_header_no_sep() {}

// Output iterator used by display_header_unicode(). The Unicode line-
// breaking algorithm is used to determine the potential line breaking
// positions in an unstructured header.

template<typename out_iter, typename wrap_iter>
struct display_header_unicode_lb : unicode::linebreakc_callback_base {

private:
	out_iter &iter;
	wrap_iter &wrap;
public:
	display_header_unicode_lb(out_iter &iter,
				  wrap_iter &wrap)
		: iter{iter}, wrap{wrap}
	{
	}

	using unicode::linebreakc_callback_base::operator<<;

	display_header_unicode_lb &operator*() { return *this; }
	display_header_unicode_lb &operator++() { return *this; }
	display_header_unicode_lb &operator++(int) { return *this; }
	display_header_unicode_lb &operator=(char32_t c)
	{
		this->operator<<(c);
		return *this;
	}
private:
	virtual int callback(int lbc, char32_t c) override
	{
		if (lbc != UNICODE_LB_NONE)
			wrap();
		*iter++=c;
		return 0;
	}
};

// Here's the name of a header, and here's its encoded contents. Convert
// the header to Unicode.
//
// If the header contain addresses, parse them as addresses, decoding RFC 2047
// atoms in the name portion, and punycode in domain names.
//
// Non-address headers get decoded using RFC 2047.
//
// An optional fourth parameter is a closure that gets invoked after
// formatting the comma separators between addresses, in a header that
// contains addresses. In non-address headers the closure gets invoked after
// every potential line break, and before iterating over the next character.

template<typename out_iter, typename mark_sep_t=void (*)()>
auto display_header_unicode(std::string_view headername,
			    std::string_view headercontents,
			    out_iter &&iter,
			    mark_sep_t &&mark_sep=display_header_no_sep)
{
	if (header_is_addr(headername))
	{
		tokens t{headercontents};
		addresses a{t};

		const char32_t *sep=U"";

		for (auto &address:a)
		{
			if (address.address.empty() &&
			    address.name.size() == 1 &&
			    address.name.begin()->type == ';')
				sep=U"";

			if (*sep)
			{
				while (*sep)
					*iter++=*sep++;

				if (!address.address.empty())
					mark_sep();
			}

			address.unicode(iter);

			sep=address.address.empty()
				? U" ":
				U", ";
		}
	}
	else
	{
		display_header_unicode_lb break_iter{iter, mark_sep};

		rfc2047::decode_unicode(
			headercontents.begin(),
			headercontents.end(),
			break_iter,
			[&](auto &&b, auto &&e, auto &&error_message)
			{
				std::string_view emsg{error_message};

				*iter++='(';
				for (auto c:emsg)
				{
					*iter++=c;
				}
				*iter++=')';
			});

		break_iter.finish();
	}

	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return iter;
}

// Call display_unicode(), then convert Unicode to the given character set.
//
// The fourth parameter is an output iterator over char.
//
// An optional fifth parameter is a closure that gets invoked after
// formatting the comma separators between addresses, in a header that
// contains addresses.
//
// This is the equivalent of rfc822_display_hdrvalue_* functions.

template<typename out_iter, typename mark_sep_t=void (*)()>
auto display_header(std::string_view headername,
		    std::string_view headercontents,
		    const std::string &chset,
		    out_iter &&iter,
		    mark_sep_t &&mark_sep=display_header_no_sep)
{
	std::u32string us;

	auto uiter=std::back_inserter(us);

	display_header_unicode(
		headername,
		headercontents,
		uiter,
		[&]
		{
			bool errflag;
			unicode::iconvert::fromu::convert(
				us.begin(), us.end(), chset,
				iter, errflag);
			us.clear();
			mark_sep();
		}
	);

	if (!us.empty())
	{
		bool errflag;
		unicode::iconvert::fromu::convert(
			us.begin(), us.end(), chset,
			iter, errflag);
	}
	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return iter;
}

// Use display_header_unicode() to convert the contents of the given header
// into Unicode strings. Use the Unicode linebreaking algorithm to wrap
// the header contents to the given width.

template<typename out_iter>
auto wrap_header_unicode(std::string_view headername,
			 std::string_view headercontents,
			 size_t target_width,
			 out_iter &&iter)
{
	std::u32string line;
	size_t line_len=0;

	std::u32string us;

	auto flush=[&]
	{
		size_t l=0;

		for (auto &c:us)
		{
			l += unicode_wcwidth(c);
		}

		if (line.empty() || line_len+l <= target_width)
		{
			line += us;
			line_len += l;
			us.clear();
			return;
		}

		*iter++=line;
		line=std::move(us);
		line_len=l;
		us.clear();
	};

	display_header_unicode(
		headername, headercontents,
		std::back_inserter(us),
		flush);

	if (!us.empty())
		flush();

	if (!line.empty())
		*iter++=line;

	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return iter;
}

// Iterator class used by wrap_header(). This is an output iterator over
// std::u32string-s, which converts each Unicode string to the given character
// set and forwards it to the output iterator over std::string-s.

template<typename out_iter>
class wrap_header_iter {
	out_iter &iter;
	const std::string &chset;
public:
	bool conversion_error{false};

	wrap_header_iter(out_iter &iter, const std::string &chset)
		: iter{iter}, chset{chset} {}

	wrap_header_iter(const wrap_header_iter &o)
		: iter{o.iter}
	{
	}

	wrap_header_iter &operator=(const wrap_header_iter &)
	{
		return *this;
	}

	wrap_header_iter &operator*() { return *this; }

	wrap_header_iter &operator++() { return *this; }
	wrap_header_iter &operator++(int) { return *this; }

private:
	std::string s;
public:

	wrap_header_iter &operator=(const std::u32string &us)
	{
		bool errflag=false;

		s.clear();

		unicode::iconvert::fromu::convert(
			us.begin(), us.end(), chset,
			std::back_inserter(s), errflag);

		if (errflag)
			conversion_error=true;
		*iter++=std::move(s);
		return *this;
	}

};

// Call wrap_header_unicode(), then convert the Unicode text to the given
// character set. The passed in iterator is an output iterator over
// std::string-s, and iterates over each wrapped line.
//
// If the output iterator gets passed by value then this returns a tuple
// containing the ending value of the output iterator and a conversion error
// flag, indicating that a conversion error was encountered converting the
// Unicode string to the requested character set.
//
// If the output iterator gets passed by reference then it gets modified in
// place and wrap_header() returns a boolean conversion error flag.

template<typename out_iter>
auto wrap_header(std::string_view headername,
		 std::string_view headercontents,
		 size_t target_width,
		 const std::string &chset,
		 out_iter &&iter)
{
	wrap_header_iter wrap_iter{iter, chset};

	wrap_header_unicode(headername, headercontents, target_width,
			    wrap_iter);

	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return std::tuple{iter, wrap_iter.conversion_error};
	else
		return wrap_iter.conversion_error;
}

// Implement do_print in order to generate Ascii-compatible encoding of
// the name and the address portion of an address in a specific character
// set, using RFC 2047 and international domain names, as appropriate.
template<typename out_iter>
struct rfc822::address::do_encode : do_print {

	out_iter &iter;
	const std::string charset;

	do_encode(const struct address &a, const std::string &charset,
		  out_iter &iter)
		: do_print{a}, iter{iter}, charset{charset}
	{
	}

	void emit_address() override
	{
		std::string s;

		s.reserve(a.address.print(length_counter{}));
		a.address.print(std::back_inserter(s));

		for (auto c:encode_domain(s, charset.c_str()))
			*iter++=c;
	}

	void emit_name() override
	{
		tokens::print_impl impl{iter};

		for (auto &t:a.name)
		{
			if ((t.type == 0 || t.type == '(' || t.type == '"') &&
			    std::find_if(t.str.begin(),
					 t.str.end(),
					 []
					 (char c)
					 {
						 return !!(c & 0x80);
					 }) != t.str.end())
			{
				std::string s;

				s.reserve(rfc822::tokens::unquote(
						  &t, &t+1,
						  rfc822::length_counter{}));
				rfc822::tokens::unquote(
					&t, &t+1,
					std::back_inserter(s));

				s=rfc2047::encode(s, charset,
						  rfc2047_qp_allow_word).first;

				if (t.type == '(')
					*iter++='(';
				auto tcopy=t;
				tcopy.str=s;
				if (tcopy.type == '"')
					tcopy.type=0;
				impl(tcopy);
				if (t.type == '(')
					*iter++=')';
			}
			else
			{
				impl(t);
			}
		}
	}

	void emit_char(char c) override
	{
		*iter++=c;
	}
};

// Encode the address, as an international domain, and use RFC 2047
// implemented in rfc2047.h for the name portion.
//
// The first parameter specifies the character set used by the
// address. The second parameter is an iterator over chars.
// If passed by value the new value of the iterator is returned. If
// passed by reference the output iterator is modified in place, and
// void gets returned from here.

template<typename iter_type> auto rfc822::address::encode(
	const std::string &chset,
	iter_type &&iter
) const -> std::conditional_t<std::is_same_v<iter_type,
					     iter_type &>,
			      void, std::remove_cv_t<
					    std::remove_reference_t<
						    iter_type>>>
{
	do_encode printer{*this, chset, iter};

	printer.output();

	if constexpr(!std::is_same_v<iter_type, iter_type &>)
		return iter;
}

#if 0
{
#endif
}

#endif
#endif
#endif
