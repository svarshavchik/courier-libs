#ifdef rfc822_h
#ifdef rfc2047_h
#ifdef  __cplusplus

namespace rfc822 {
#if 0
}
#endif

std::u32string idn2unicode(std::string &idn);

template<typename out_iter_type> void tokens::unicode_address(
	out_iter_type &iter
) const
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
}

template<typename out_iter_type> void tokens::unicode_name(
	out_iter_type &iter
) const
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

			for (auto c:s)
			{
				*iter++=c;
			}
		});
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

template<typename out_iter_type> void tokens::display_address(
	const std::string &chset,
	out_iter_type &iter
) const
{
	u2iterator u{iter};
	if (u.begin(chset))
	{
		unicode_address(u);

		bool errflag{false};

		if (u.end(errflag) && !errflag)
			return;
	}

	for (char c: std::string_view{"[decoding error]"})
		*iter++=c;
}

template<typename out_iter_type> void tokens::display_name(
	const std::string &chset,
	out_iter_type &iter
) const
{
	u2iterator u{iter};
	if (u.begin(chset))
	{
		unicode_name(u);

		bool errflag{false};

		if (u.end(errflag) && !errflag)
			return;
	}

	for (char c: std::string_view{"[decoding error]"})
		*iter++=c;
}

#if 0
{
#endif
}

#endif
#endif
#endif
