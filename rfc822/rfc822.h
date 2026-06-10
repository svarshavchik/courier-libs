#ifndef	rfc822_h
#define	rfc822_h

/*
** Copyright 1998 - 2025 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif

#include	<string>
#include	<string_view>
#include	<vector>
#include	<functional>
#include	<iterator>
#include	<type_traits>
#include	<optional>
#include	<streambuf>
#include	<tuple>
#include	<time.h>
#include	<courier-unicode.h>

#define RFC822_SPECIALS			"()<>[]:;@\\,.\""
#define RFC822_SPECIAL_INNAMES		"()<>[]@\\,.\""

#define CORESUBJ_RE 1
#define CORESUBJ_FWD 2

/* Internal functions */

void rfc822_parseaddr(size_t ntokens,
		      char (*get_nth_token)(size_t, void *),
		      void (*consume_n_tokens)(size_t, void *),
		      void (*make_quoted_token)(size_t, void *),
		      void (*define_addr_name)(size_t, int, void *),
		      void (*define_addr_tokens)(size_t, int, void *),
		      void *voidp);

namespace rfc822 {
#if 0
}
#endif

std::optional<time_t> parse_date(std::string_view);
std::string mkdate(time_t);

/*
** address is a hostname, which is IDN-encoded. 'address' may contain an
** optional 'user@', which is preserved.
** caller is responsible for freeing it.
*/

std::string encode_domain(std::string_view address,
			  const char *charset=unicode::utf_8);

struct length_counter {
	using iterator_category=std::output_iterator_tag;
	using value_type=void;
	using pointer=void;
	using reference=void;
	using difference_type=void;

	size_t l=0;

	template<typename T>
	length_counter &operator=(T &&) { ++l; return *this; }
	length_counter &operator++(int) { return *this; }
	auto &operator*()
	{
		return *this;
	}

	operator size_t () const { return l; }
};

// An RFC 822 token.

struct token {
	int type=0; // rfc822token.token value
	std::string_view str;

	bool is_atom() const
	{
		return type == 0 || type == '"' || type == '(';
	}

	void print(
		std::function<void (const char *, size_t)> print_func
	) const;
};

// C++ version of rfc822t

struct tokens : std::vector<token> {

	// Tokenize a character string.
	//
	// The passed-in string must exist until this object is destroyed.

	tokens(std::string_view str);

	tokens(std::string_view str, const std::function<void (size_t)> &err);

	using std::vector<token>::vector;

	~tokens()=default;

	// Helper class used by print() and rfc822::address::encode.
	//
	// Takes a reference to an output iterator over chars. The ()
	// operator is called with one token after another, and
	// print() gets invoked for it, to write the token
	// to the output iterator. A space is automatically provided between
	// two consecutive atoms.

	template<typename out_iter_type>
	struct print_impl {
		out_iter_type &iter;
		bool prev_is_atom{false};

		print_impl(out_iter_type &iter) : iter{iter}
		{
		}

		void operator()(const token &t)
		{
			bool isatom=t.is_atom();

			if (prev_is_atom && isatom)
			{
				*iter++=' ';
			}

			t.print(
				[this](const char *s, size_t l)
				{
					while (l)
					{
						*iter++=*s;
						++s;
						--l;
					}
				}
			);
			prev_is_atom=isatom;
		}
	};

	// Print a token sequence.
	//
	// The token sequence gets printed to an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_typeb,
		 typename iter_typee, typename out_iter_type>
	static auto print(iter_typeb b, iter_typee e,
			  out_iter_type &&iter)
	{
		print_impl impl{iter};

		while (b != e)
		{
			impl(*b++);
		}

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}


	// unquote is an alternative print() that:
	//
	// Removes the quotes and parenthesis from quoted strings and
	// comment atoms, and removes backslashes from their string contents.
	//
	// The unquoted string is written to an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_typeb,
		 typename iter_typee, typename out_iter_type>
	static auto unquote(iter_typeb b, iter_typee e,
			    out_iter_type &&iter)
	{
		bool prev_is_atom=false;

		while (b != e)
		{
			auto &t=*b++;

			bool isatom=t.is_atom();

			if (prev_is_atom && isatom)
			{
				*iter++=' ';
			}

			if (t.type == '"' || t.type == '(' || t.type == 0)
			{
				auto p=t.str.data();
				auto s=t.str.size();

				if (t.type == '(' && s >= 2)
				{
					++p;
					s -= 2;
				}

				while (s)
				{
					if (*p == '\\' && s > 1)
					{
						++p;
						--s;
					}

					*iter++=*p++;
					--s;
				}
			}
			else *iter++=static_cast<char>(t.type);

			prev_is_atom=isatom;
		}

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	template<typename out_iter_type>
	auto unquote(out_iter_type &&iter) const
	{
		return unquote(this->begin(), this->end(),
			       std::forward<out_iter_type>(iter));
	}

	// Print these tokens to an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.


	template<typename out_iter_type> auto print(out_iter_type &&iter)
		const
	{
		return print(this->begin(), this->end(),
			     std::forward<out_iter_type>(iter));
	}

	// Convert this email address to Unicode, using idn encoding.
	//
	// The address is written to an output iterator over char32_t.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename out_iter_type> auto unicode_address(
		out_iter_type &&iter
	) const -> std::conditional_t<std::is_same_v<out_iter_type,
						     out_iter_type &>,
				      void, std::remove_cv_t<
					      std::remove_reference_t<
						      out_iter_type>>>;

	// Convert this name to Unicode, using RFC2047 decoding. Setting
	// unquote to true uses unquote().
	//
	// The address is written to an output iterator over char32_t.

	template<typename out_iter_type> auto unicode_name(
		out_iter_type &&iter,
		bool unquote=false
	) const -> std::conditional_t<std::is_same_v<out_iter_type,
						     out_iter_type &>,
				      void, std::remove_cv_t<
					      std::remove_reference_t<
						      out_iter_type>>>;


	// Convert this email address for display in the given character
	// set, using idn encoding.
	//
	// The address is written to an output iterator over char.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename out_iter_type> auto display_address(
		const std::string &chset,
		out_iter_type &&iter
	) const -> std::conditional_t<std::is_same_v<out_iter_type,
						     out_iter_type &>,
				      void, std::remove_cv_t<
					      std::remove_reference_t<
						      out_iter_type>>>;

	// Convert this name for display in the given character set,
	// using RFC2047 decoding.
	//
	// The address is written to an output iterator over char32_t.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.
	//
	// The unquote parameter gets forwarded to unicode_name().
	template<typename out_iter_type> auto display_name(
		const std::string &chset,
		out_iter_type &&iter,
		bool unquote=false
	) const -> std::conditional_t<std::is_same_v<out_iter_type,
						     out_iter_type &>,
				      void, std::remove_cv_t<
					      std::remove_reference_t<
						      out_iter_type>>>;
};

// The C++ equivalent of rfc822addr

struct address {
	tokens name;
	tokens address;

	// Inspect the name and the address portion, of the address, and
	// figure out how to format it. It can be:
	//
	// Human Name <address@example.com>
	//
	// "Human Name" <address@example.com>
	//
	// address@example.com (Human Name)
	//
	// The constructor takes a reference to the address. output()
	// invokes emit_address(), emit_name(), and emit_char() in the
	// appropriate order. They are implemented in the subclass and generate
	// the output of a.name and a.address, and the individual filler
	// characters.

	struct do_print {
		const struct address &a;

		do_print(const struct address &a) : a{a} {}
		virtual void emit_address()=0;
		virtual void emit_name()=0;
		virtual void emit_char(char)=0;

		virtual bool old_style();
		void output();
	};

	template<typename out_iter>
	struct do_print_raw : do_print {

		out_iter &iter;

		do_print_raw(const struct address &a, out_iter &iter)
			: do_print{a}, iter{iter}
		{
		}

		void emit_address() override
		{
			a.address.print(iter);
		}

		void emit_name() override
		{
			a.name.print(iter);
		}

		void emit_char(char c) override
		{
			*iter++=c;
		}
	};

	// Print this address to an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_type> auto print(iter_type &&iter) const
	{
		do_print_raw printer{*this, iter};

		printer.output();

		if constexpr(!std::is_same_v<iter_type, iter_type &>)
			return iter;
	}

	template<typename out_iter>
	struct do_print_unicode : do_print {

		out_iter &iter;

		std::u32string name;

		do_print_unicode(const struct address &a, out_iter &iter);

		bool old_style() override
		{
			return !name.empty() && name.front() == '(' &&
				name.back() == ')';
		}

		void emit_address() override
		{
			a.unicode_address(iter);
		}

		void emit_name() override
		{
			for (auto &c:name)
				*iter++=c;
		}

		void emit_char(char c) override
		{
			*iter++=static_cast<char32_t>(c);
		}
	};

	// Print this address in Unicode, for display, converting IDN-encoded
	// domains and RFC 2047-encoded name portions. The Unicode is
	// written to an output iterator over char32_t.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_type> auto unicode(iter_type &&iter) const
	{
		do_print_unicode printer{*this, iter};

		printer.output();

		if constexpr(!std::is_same_v<iter_type, iter_type &>)
			return iter;
	}

	template<typename out_iter>
	struct do_print_display : do_print {

		out_iter &iter;
		const std::string &chset;

		do_print_display(const struct address &a, out_iter &iter,
				 const std::string &chset)
			: do_print{a}, iter{iter}, chset{chset}
		{
		}

		void emit_address() override
		{
			a.display_address(chset, iter);
		}

		void emit_name() override
		{
			a.display_name(chset, iter);
		}

		void emit_char(char c) override
		{
			*iter++=static_cast<char32_t>(c);
		}
	};

	// Convert this address to a given character set, converting IDN-encoded
	// domains and RFC 2047-encoded name portions. The address is
	// written to an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_type> auto display(const std::string &chset,
						  iter_type &&iter) const
	{
		do_print_display printer{*this, iter, chset};

		printer.output();

		if constexpr(!std::is_same_v<iter_type, iter_type &>)
			return iter;
	}

	// If there is no name, calls print() on the address portion, otherwise
	// calls unquote() on the name portion. The output iterator is passed
	// in as a parameter.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iter_type> auto unquote_name(iter_type &&iter)
	{
		if (name.empty())
		{
			return address.print(std::forward<iter_type>(iter));
		}

		return tokens::unquote(
			name.begin(),
			name.end(),
			std::forward<iter_type>(iter)
		);
	}
	// Convert the email address to Unicode, using idn encoding.
	//
	// The address is written to an output iterator that's passed as a
	// parameter.
	//
	// The iterator is over char32_t.
	//
	// If the parameter is a reference the output iterator is
	// updated in place, otherwise unicode_name returns the final value
	// of the output iterator.

	template<typename out_iter_type> auto unicode_address(
		out_iter_type &&iter
	) const
	{
		return address.unicode_address(
			std::forward<out_iter_type>(iter)
		);
	}

	// Convert this name to Unicode, using RFC2047 decoding.
	//
	// The name is written to an output iterator that's passed as a
	// parameter.
	//
	// The iterator is over char32_t.
	//
	// If the parameter is a reference the output iterator is
	// updated in place, otherwise unicode_name returns the final value
	// of the output iterator.

	template<typename out_iter_type> auto unicode_name(
		out_iter_type &&iter
	) const
	{
		return name.unicode_name(
			std::forward<out_iter_type>(iter)
		);
	}

	// Call display_address() on the address portion.

	template<typename out_iter_type> auto display_address(
		const std::string &chset,
		out_iter_type &&iter
	) const
	{
		return address.display_address(
			chset,
			std::forward<out_iter_type>(iter)
		);
	}

	// Calls display_name() on the name portion. The unquote parameter
	// gets forwarded to display_name().

	template<typename out_iter_type> auto display_name(
		const std::string &chset,
		out_iter_type &&iter,
		bool unquote=false
	) const
	{
		return name.display_name(
			chset,
			std::forward<out_iter_type>(iter),
			unquote
		);
	}

	template<typename out_iter> struct do_encode;

	template<typename iter_type> auto encode(const std::string &chset,
						 iter_type &&iter) const
		-> std::conditional_t<std::is_same_v<iter_type,
						     iter_type &>,
				      void, std::remove_cv_t<
					      std::remove_reference_t<
						      iter_type>>>;
};

// Tokens converted to addresses.

struct addresses : std::vector<address> {

	using std::vector<address>::operator[];

	// Convert tokens to addresses
	//
	// The passed-in tokens may be modified.
	//
	// The string that was tokenized must exist until the address vector is
	// destroyed.

	addresses(tokens &);

	using std::vector<address>::vector;
	~addresses()=default;

	// output() loops until eof(). Each iteration of the loop calls ref()
	// to inspect each address an ddtermine whether print_separator()
	// gets called with any grammar that's needed to be produced between
	// addresses, such as commas and/or spaces.
	//
	// Then, print() the address.

	struct do_print {
		void output();

		virtual void print_separator(const char *)=0;

		virtual bool eof()=0;
		virtual const struct address &ref()=0;
		virtual void print()=0;
	};

	using print_separator_t=void(const char *);

	template<typename char_type, typename iter_type>
	static std::function<print_separator_t
			     > make_default_print_separator(iter_type &iter)
	{
		return [&](const char *p)
		{
			while (*p)
			{
				*iter++ = static_cast<char_type>(*p++);
			}
		};
	}

	// Scaffolding for implementing do_print in terms of: iterators
	// to the beginning and end of a sequence that defines the addresses,
	// the output iterator type, and an opaque function object for
	// printing the separator. print() is not implemented, must still
	// be done in a subclass. The subclass must implement print() by
	// doing something to "print" what b refers to, then increment it.

	template<typename iter_b, typename iter_e, typename out_iter_type,
		 typename print_separator_cb_t>
	struct do_print_impl : do_print {

		// The beginning/ending iterator that's passed to the
		// constructor are copied, because they get modified.
		iter_b b;
		iter_e e;

		// Output iterator is passed by reference, and is modified.
		// It must exist until this object is destroyed.
		out_iter_type &out_iter;

		// The print-separator callback is also passed by reference.
		// It must exist until this object is destroyed.
		print_separator_cb_t &print_separator_cb;

		do_print_impl(iter_b &b,
			     iter_e &e,
			     out_iter_type &out_iter,
			     print_separator_cb_t &print_separator_cb)
			: b{b}, e{e}, out_iter{out_iter},
			  print_separator_cb{print_separator_cb}
		{
		}

		void print_separator(const char *s) override
		{
			print_separator_cb(s);
		}

		bool eof() override
		{
			return b == e;
		}

		const struct address &ref() override
		{
			return *b;
		}
	};

	// Implement print() by calling address::print().

	template<typename iter_b, typename iter_e, typename out_iter_type,
		 typename print_separator_cb_t>
	struct do_print_raw : do_print_impl<iter_b, iter_e, out_iter_type,
					    print_separator_cb_t> {

		do_print_raw(iter_b &b,
			     iter_e &e,
			     out_iter_type &out_iter,
			     print_separator_cb_t &print_separator_cb)
			: do_print_impl<iter_b, iter_e, out_iter_type,
					print_separator_cb_t>{
			b, e, out_iter, print_separator_cb}
		{
		}

		void print() override
		{
			this->b->print(this->out_iter);
			++this->b;
		}
	};

	// Implement print() by calling address::unicode().

	template<typename iter_b, typename iter_e, typename out_iter_type,
		 typename print_separator_cb_t>
	struct do_unicode : do_print_impl<iter_b, iter_e, out_iter_type,
					 print_separator_cb_t> {

		do_unicode(iter_b &b,
			   iter_e &e,
			   out_iter_type &out_iter,
			   print_separator_cb_t &print_separator_cb)
			: do_print_impl<iter_b, iter_e, out_iter_type,
					print_separator_cb_t>{
			b, e, out_iter, print_separator_cb}
		{
		}

		void print() override
		{
			this->b->unicode(this->out_iter);
			++this->b;
		}
	};


	// Implement print() by calling address::encode().
	//
	// The character set is passed in by reference, and must exist until
	// this object is destroyed.

	template<typename iter_b, typename iter_e, typename out_iter_type,
		 typename print_separator_cb_t>
	struct do_encode : do_print_impl<iter_b, iter_e, out_iter_type,
					 print_separator_cb_t> {

		const std::string &chset;

		do_encode(iter_b &b,
			  iter_e &e,
			  out_iter_type &out_iter,
			  print_separator_cb_t &print_separator_cb,
			  const std::string &chset)
			: do_print_impl<iter_b, iter_e, out_iter_type,
					print_separator_cb_t>{
			b, e, out_iter, print_separator_cb},
			chset{chset}
		{
		}

		void print() override
		{
			this->b->encode(chset, this->out_iter);
			++this->b;
		}
	};

	struct format {

		// Helper class used by print().
		struct print {

			template<typename iterb_type, typename itere_type,
				 typename out_iter_type,
				 typename print_separator_t>
			static void format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter,
					   print_separator_t &&print_separator)
			{
				do_print_raw printer{b, e, out_iter,
						     print_separator};

				printer.output();
			}

			template<typename iterb_type, typename itere_type,
				 typename out_iter_type>
			static void format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter)
			{
				format(std::forward<iterb_type>(b),
				       std::forward<itere_type>(e),
				       std::forward<out_iter_type>(out_iter),
				       make_default_print_separator<char>(
					       out_iter
				       ));
			}
		};

		// Helper class used by encode()

		struct encode {
			template<typename iterb_type, typename itere_type,
				 typename out_iter_type,
				 typename print_separator_t>
			static void format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter,
					   const std::string &chset,
					   print_separator_t &&print_separator)
			{
				do_encode encoder{b, e, out_iter,
						  print_separator, chset};

				encoder.output();
			}

			template<typename iterb_type, typename itere_type,
				 typename out_iter_type>
			static auto format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter,
					   const std::string &chset)
			{
				format(std::forward<iterb_type>(b),
				       std::forward<itere_type>(e),
				       std::forward<out_iter_type>(out_iter),
				       chset,
				       make_default_print_separator<char>(
					       out_iter
				       ));
			}
		};

		// Helper class used by unicode_wrapped()

		struct unicode {
			template<typename iterb_type, typename itere_type,
				 typename out_iter_type,
				 typename print_separator_t>
			static void format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter,
					   print_separator_t &&print_separator)
			{
				do_unicode encoder{b, e, out_iter,
						   print_separator};

				encoder.output();
			}

			template<typename iterb_type, typename itere_type,
				 typename out_iter_type>
			static auto format(iterb_type &&b, itere_type &&e,
					   out_iter_type &&out_iter)
			{
				format(std::forward<iterb_type>(b),
				       std::forward<itere_type>(e),
				       std::forward<out_iter_type>(out_iter),
				       make_default_print_separator<char32_t>(
					       out_iter
				       ));
			}
		};
	};
	// C++ version of rfc822_print, the addresses are defined as a pair
	// of beginning and ending iterators.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type, typename ...Args>
	static auto print(iterb_type &&b, itere_type &&e,
			  out_iter_type &&out_iter,
			  Args && ...args)
	{
		format::print::format(std::forward<iterb_type>(b),
				      std::forward<itere_type>(e),
				      out_iter,
				      std::forward<Args>(args)...);

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return out_iter;
	}

	// The C++ version of rfc822_print(), the addresses are printed to
	// an output iterator.
	//
	// If the output iterator is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the output value by rvalue (reference) returns the final
	// value of the output iterator.

	template<typename out_iter_type, typename ...Args>
	auto print(out_iter_type &&out_iter, Args && ...args)
	{
		return print(this->begin(), this->end(),
			     std::forward<out_iter_type>(out_iter),
			     std::forward<Args>(args)...);
	}

	// The opposite of display(). Take addresses that are formatted for
	// display, and encode them. Use RFC 2047 for the name portion, and
	// ACE for internationalized domains.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type, typename ...Args>
	static auto encode(iterb_type &&b, itere_type &&e,
			   out_iter_type &&out_iter,
			   std::string chset,
			   Args && ...args)
	{
		format::encode::format(std::forward<iterb_type>(b),
				       std::forward<itere_type>(e),
				       out_iter,
				       chset,
				       std::forward<Args>(args)...);

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return out_iter;
	}

	template<typename out_iter_type, typename ...Args>
	auto encode(out_iter_type &&out_iter,
		    std::string chset, Args && ...args)
	{
		return encode(this->begin(), this->end(),
			      std::forward<out_iter_type>(out_iter),
			      std::move(chset),
			      std::forward<Args>(args)...);
	}

	/////////////////////////////////////////////////////////////////////
	//
	// C++ version of rfc822_getaddrs_wrap.

	// Receives characters from print() via add(). Holds an output iterator
	// for the wrapped address list.
	//
	// flush_if_more() gets called before add() gets called with a space
	// character in the separator sequence, ", " or " ".
	//
	// end() gets called after the entire address list gets written.

	template<typename char_type, typename out_iter_type>
        struct wrap_out_iter_impl {

		// The real output iterator for the wrapped address list.
		out_iter_type &iter;

		// Width of characters being wrapped to.
		const size_t max_l;

		wrap_out_iter_impl(out_iter_type &iter, size_t max_l)
			: iter{iter}, max_l{max_l}
		{
		}

	private:
		// Accumulated add()ed characters.
		std::basic_string<char_type> accumulated_line;

		size_t accumulated_width{0};

		// Index of the most recent space character, captured
		// by flush_if_more().
		size_t last_sep_spc{0};

		// Total with up to but not including the last seen space.
		size_t last_sep_spc_width{0};
	public:

		void add(char_type c)
		{
			accumulated_line.push_back(c);

			if constexpr (std::is_same_v<char_type, char32_t>)
			{
				accumulated_width += unicode_wcwidth(c);
			}
			else
			{
				++accumulated_width;
			}
		}

		// If the accumulated line exceeds the maximum length,
		// dump everything up to the index of the space character
		// followed by a newline (the space is effectively replaced
		// by a newline).

		bool flush_if_more()
		{
			if (accumulated_width <= max_l)
			{
				// Remember the index of the last space.
				last_sep_spc=accumulated_line.size();
				last_sep_spc_width=accumulated_width;
				return false;
			}

			// If last_sep_spc is 0 this means that the first
			// address exceeded the maximum length. Arrange that
			// everything that was add()ed will go out before
			// the newline.
			bool excessive=false;

			if (last_sep_spc == 0)
			{
				excessive=true;

				last_sep_spc=accumulated_line.size();
				last_sep_spc_width=accumulated_width;
			}

			auto b=accumulated_line.begin();
			auto e=b+last_sep_spc;

			*iter++ =std::basic_string<char_type>{b, e};

			if (!excessive)
			{
				// This is going to be a space character,
				// guaranteed.
				++e;
				++last_sep_spc_width;
			}
			accumulated_line.erase(b, e);
			accumulated_width -= last_sep_spc_width;

			last_sep_spc=accumulated_line.size();
			last_sep_spc_width=accumulated_width;

			return excessive;
		}

		void end()
		{
			// Borrow flush_if_more() if what's left is above
			// the limit.
			if (accumulated_line.size() > max_l &&
			    last_sep_spc > 0)
				flush_if_more();

			// And just write it out.
			*iter++=std::move(accumulated_line);
		}
	};

	// The actual iterator that gets passed to print(), instead of
	// wrap_out_iter_impl. This one doesn't mind being copied or
	// assigned to. write_out_iter_impl is uncopyable.

	template<typename char_type, typename out_iter_type>
	struct wrap_out_iter {
		wrap_out_iter_impl<char_type, out_iter_type> &impl;

		wrap_out_iter(
			wrap_out_iter_impl<char_type, out_iter_type> &impl)
			: impl{impl} {}

		auto &operator=(const wrap_out_iter<char_type, out_iter_type> &)
		{
			return *this;
		}

		// Iterator semantics.
		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		auto &operator=(char_type c)
		{
			impl.add(c);
			return *this;
		}

		auto &operator++()
		{
			return *this;
		}

		auto &operator++(int)
		{
			return *this;
		}

		auto &operator*()
		{
			return *this;
		}
	};

	// Pass this as the separator writer, for print(). It detects the
	// guaranteed space in the separator string, and strategically
	// calls flush_if_more().

	template<typename iter>
	struct wrap_out_iter_sep {

		iter &i;

		wrap_out_iter_sep(iter &i) :i{i} {}

		void operator()(const char *sep)
		{
			for ( ; *sep; ++sep)
			{
				if (*sep == ' ')
				{
					// If flush_if_more() uncorked an
					// address in excess of the maximum
					// length, everything that was add()ed
					// then we eat this space. Otherwise
					// it get add()ed like everything else.
					//
					// Otherwise the space is added
					// to the accumulated line, because
					// that's what we promised to
					// flush_is_more().

					if (i.flush_if_more())
						continue;
				}

				i.add(*sep);
			}
		}
	};

	// C++ version of rfc822_getaddrs_wrap() that writes out the
	// wrapped addresses to an output sink. The sequence of addresses
	// is defined by a beginning and an ending iterator sequence.
	//
	// The last parameter, iter, is an output iterator over std::strings,
	// each one containing a line of addresses that does not exceed the
	// given width, unless a single address is bigger than the given width.
	//
	// If the last parameter is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the last parameter by rvalue (reference) returns the final
	// value of the the passed in object.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type>
	static auto print_wrapped(iterb_type &&b, itere_type &&e,
				  size_t max_l,
				  out_iter_type &&iter)
	{
		wrap_out_iter_impl<char, out_iter_type> wrapper{iter, max_l};
		wrap_out_iter char_wrapper{wrapper};

		print(std::forward<iterb_type>(b),
		      std::forward<itere_type>(e),
		      char_wrapper,
		      wrap_out_iter_sep{wrapper});

		wrapper.end();

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	// print_wrapped() these addresses into an output sink that gets
	// invoked with a std::string of each wrapped line.

	template<typename out_iter_type>
	auto print_wrapped(size_t max_l, out_iter_type &&iter)
	{
		return print_wrapped(begin(), end(), max_l,
				     std::forward<out_iter_type>(iter));
	}

	// Use encode() on addresses that were formatted for display() using
	// the specified character set, and write out the wrapped addresses
	// to an output sink. The sequence of addresses is defined by a
	// beginning and an ending iterator sequence.
	//
	// The last parameter, iter, is an output iterator over std::strings,
	// each one containing a line of addresses that does not exceed the
	// given width, unless a single address is bigger than the given width.
	//
	// If the last parameter is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the last parameter by rvalue (reference) returns the final
	// value of the the passed in object.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type>
	static auto encode_wrapped(iterb_type &&b, itere_type &&e,
				   std::string chset,
				   size_t max_l,
				   out_iter_type &&iter)
	{
		wrap_out_iter_impl<char, out_iter_type> wrapper{iter, max_l};
		wrap_out_iter char_wrapper{wrapper};

		encode(std::forward<iterb_type>(b),
		       std::forward<itere_type>(e),
		       char_wrapper,
		       std::move(chset),
		       wrap_out_iter_sep{wrapper});

		wrapper.end();

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	// encode() addresses into an output sink that gets
	// invoked with a std::string of each wrapped line.

	template<typename out_iter_type>
	auto encode_wrapped(std::string chset, size_t max_l,
			    out_iter_type &&out_iter)
	{
		return encode_wrapped(begin(), end(), chset, max_l,
				      std::forward<out_iter_type>(out_iter));
	}

	// Decode IDN-encoded domain names and RFC 2047-encoded names, and
	// wrap the result to the given output. The sequence of addresses
	// is defined by a beginning and an ending iterator sequence.
	//
	// The last parameter, iter, is a callable object that gets invoked,
	// repeatedly with a std::u32string containing a line of addressed,
	// that does not exceed the given width, unless a single address
	// is bigger than the given width.
	//
	// If the last parameter is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the last parameter by rvalue (reference) returns the final
	// value of the the passed in object.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type>
	static auto unicode_wrapped(iterb_type &&b, itere_type &&e,
				    size_t max_l,
				    out_iter_type &&iter)
	{
		wrap_out_iter_impl<char32_t, out_iter_type> wrapper{
			iter, max_l
		};
		wrap_out_iter char32_wrapper{wrapper};

		format::unicode::format(
			std::forward<iterb_type>(b),
			std::forward<itere_type>(e),
			char32_wrapper, wrap_out_iter_sep{wrapper});

		wrapper.end();

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	// wrap_unicode() uses unicode_wrapped() to write the wrapped
	// addresses into a vector of std::u32strings.
	//
	// Two passes are done. The first pass counts the number of wrapped
	// strings, by passing a pseudo-iterator, a length_counter, that
	// just counts things.

	std::vector<std::u32string> wrap_unicode(size_t max_l) const
	{
		std::vector<std::u32string> w;

		// Size up the length.

		w.reserve(unicode_wrapped(begin(), end(), max_l,
					  length_counter{}));

		unicode_wrapped(begin(), end(), max_l,
				std::back_inserter(w));
		return w;
	}

	// Iterator used by wrap_display() that converts each unicode
	// line into the requested character set.

	template<typename out_iter_type>
	struct wrap_display_wrapper {
		out_iter_type &iter;
		const std::string &chset;

		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		wrap_display_wrapper(out_iter_type &iter,
				     const std::string &chset)
			: iter{iter}, chset{chset}
		{
		}

		wrap_display_wrapper(const wrap_display_wrapper &w)=default;

		wrap_display_wrapper &operator=(const wrap_display_wrapper &)
		{
		}

		template<typename u32string_t>
		wrap_display_wrapper &operator=(u32string_t &&s)
		{
			auto ret=unicode::iconvert::fromu::convert(
				s, chset);

			if (ret.second)
				ret.first += " [decoding error]";

			*iter++=ret.first;
			return *this;
		}

		wrap_display_wrapper &operator++() { return *this; }
		wrap_display_wrapper &operator++(int) {return *this; }
		wrap_display_wrapper &operator*() { return *this; }
	};

	// Decode IDN-encoded domain names and RFC 2047-encoded names,
	// wrap the result, converted to the given character set,
	// to the given output. The sequence of addresses
	// is defined by a beginning and an ending iterator sequence.
	//
	// The last parameter, iter, is a callable object that gets invoked,
	// repeatedly with a std::string containing a line of addressed,
	// that does not exceed the given width, in the converted character
	// set, unless a single address
	// is bigger than the given width.
	//
	// If the last parameter is an lvalue reference it gets updated
	// in place and the return type is void.
	//
	// Passing the last parameter by rvalue (reference) returns the final
	// value of the the passed in object.

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type>
	static auto wrap_display(iterb_type &&b, itere_type &&e,
				 size_t max_l,
				 const std::string &chset,
				 out_iter_type &&iter)
	{
		wrap_display_wrapper wrapper{iter, chset};

		unicode_wrapped(
			std::forward<iterb_type>(b),
			std::forward<itere_type>(e),
			max_l, wrapper
		);

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	template<typename out_iter_type>
	auto wrap_display(size_t max_l,
			  const std::string &chset,
			  out_iter_type &&iter) const
	{
		return wrap_display(begin(), end(), max_l, chset,
				    std::forward<out_iter_type>(iter));
	}

	auto wrap_display(size_t max_l,
			  const std::string &chset) const
	{
		std::vector<std::string> lines;

		wrap_display(max_l, chset, std::back_inserter(lines));

		return lines;
	}
};

// US-ASCII case-insensitive comparison

bool headercmp(std::string_view, std::string_view);

// Return true if header_name is To, Cc, Bcc, or other headers that contain
// a list of addresses.

bool header_is_addr(std::string_view header_name,
		    bool include_in_reply_to=true);

// The passed in string is the raw subject line. Uses display_header() to
// do any RF-C2047 decoding, afterwards, strip off Re: Fwd: and [BLOB]s from
// the subject line, convert it to uppercase. Returns:
//
// - the stripped, uppercased, subject line, in UTF-8
// - A bitmask, of what was stripped off: CORESUBJ_RE, CORESUBJ_FWD

std::tuple<std::string, int> coresubj(std::string_view str);

// Same as coresubj() but without doing RFC-2047 decoding and without
// the uppercase conversion.
std::tuple<std::string, int> coresubj_nouc(std::string_view str);

// Same as coresubj_nouc(), but [BLOB]s are not removed from the subject line.
std::tuple<std::string, int> coresubj_keepblobs(std::string_view str);

// Subclass std::streambuf and implement it on top of a file descriptor. The
// file descriptor is owned by fdstreambuf and is closed in the destructor.
//
// Maybe someday this nonsense will really be a part of std...

class fdstreambuf : public std::streambuf {

	int fd{-1};

	char *defaultbuf{nullptr};

public:

	fdstreambuf() noexcept : fdstreambuf(-1) {}

	explicit fdstreambuf(int fd) noexcept : fd{fd} {}

	fdstreambuf(fdstreambuf &&o) noexcept
	{
		operator=(std::move(o));
	}
	~fdstreambuf();

	static fdstreambuf tmpfile();
	static fdstreambuf tmpfile(const char *directory);

	int fileno() const { return fd; }

	bool error() const { return fd < 0; }

	pos_type tell()
	{
		return pubseekoff(0, std::ios_base::cur);
	}
	fdstreambuf &operator=(fdstreambuf &&) noexcept;

protected:
	fdstreambuf *setbuf(char *, std::streamsize) override;

	pos_type seekoff(off_type off, std::ios_base::seekdir dir,
			 std::ios_base::openmode which) override;

	pos_type seekpos(pos_type pos,
			 std::ios_base::openmode which) override;

	int sync() override;

	int_type underflow() override;

	std::streamsize xsgetn(char* s, std::streamsize count ) override;

	int_type overflow(int_type ch) override;

	std::streamsize xsputn(const char *s, std::streamsize count ) override;

	int_type pbackfail(int_type c) override;
};

#if 0
{
#endif
}

#include "rfc822/rfc822_2047.h"

#endif
