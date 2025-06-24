#ifdef rfc822_h
#ifdef rfc2047_h
#ifdef  __cplusplus

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
	out_iter_type &&iter
) const -> std::conditional_t<std::is_same_v<out_iter_type,
					     out_iter_type &>,
			      void, std::remove_cv_t<
				      std::remove_reference_t<
					      out_iter_type>>>
{
	struct proxy {
		out_iter_type &iter;

		proxy(out_iter_type &iter) : iter{iter} {}

		auto &operator=(const proxy &)
		{
			return *this;
		}

		// Iterator semantics.
		// using iterator_category=std::output_iterator_tag;
		// using value_type=void;
		// using pointer=void;
		// using reference=void;
		// using difference_type=void;

		auto &operator=(char32_t c)
		{
			*iter++ = c;
			return *this;
		}

		auto &operator++(int)
		{
			return *this;
		}

		auto &operator++()
		{
			return *this;
		}

		auto &operator*()
		{
			return *this;
		}
	};
	std::string s;

	s.reserve(this->print(this->begin(), this->end(), length_counter{}));

	this->print(this->begin(), this->end(),
		    std::back_inserter(s));

	std::string fragment;

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
				proxy{iter});

			if (errflag)
				for (char c: std::string_view{
						" (encoding error)"})
					*iter++ = static_cast<char32_t>(c);
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
		unicode_name(u);

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

// Here's the name of a header, and here's its encoded contents. Convert
// the header to Unicode.
//
// If the header contain addresses, parse them as addresses, decoding RFC 2047
// atoms in the name portion, and punycode in domain names.
//
// Non-address headers get decoded using RFC 2047.

template<typename out_iter>
auto display_header_unicode(std::string_view headername,
			    std::string_view headercontents,
			    out_iter &&iter)
{
	if (header_is_addr(headername))
	{
		tokens t{headercontents};
		addresses a{t};

		const char32_t *sep=U"";

		for (auto &address:a)
		{
			while (*sep)
				*iter++=*sep++;

			address.unicode(iter);
			sep=U", ";
		}
	}
	else
	{
		rfc2047::decode_unicode(
			headercontents.begin(),
			headercontents.end(),
			iter,
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
	}

	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return iter;
}

// Call display_unicode(), then convert Unicode to the given character set.
//
// The fourth parameter is an output iterator over char.

template<typename out_iter>
auto display_header(std::string_view headername,
		    std::string_view headercontents,
		    const std::string &chset,
		    out_iter &&iter)
{
	std::u32string us;

	auto uiter=std::back_inserter(us);

	display_header_unicode(headername, headercontents, uiter);

	bool errflag;

	unicode::iconvert::fromu::convert(us.begin(), us.end(), chset,
					  iter, errflag);

	if (errflag)
	{
		for (char c: std::string_view{"[unicode conversion error]"})
			*iter++ = c;
	}

	if constexpr(!std::is_same_v<out_iter, out_iter &>)
		return iter;
}

#if 0
{
#endif
}

#endif
#endif
#endif
