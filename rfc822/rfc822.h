#ifndef	rfc822_h
#define	rfc822_h

/*
** Copyright 1998 - 2025 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif

#include	<time.h>
#include	<courier-unicode.h>

#ifdef  __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#define RFC822_SPECIALS			"()<>[]:;@\\,.\""
#define RFC822_SPECIAL_INNAMES		"()<>[]@\\,.\""
/*
** The text string we want to parse is first tokenized into an array of
** struct rfc822token records.  'ptr' points into the original text
** string, and 'len' has how many characters from 'ptr' belongs to this
** token.
*/

struct rfc822token {
	struct rfc822token *next;	/* Unused by librfc822, for use by
					** clients */
	int token;
/*
  Values for token:

  '(' - comment
  '"' - quoted string
  '<', '>', '@', ',', ';', ':', '.', '[', ']', '%', '!', '=', '?', '/' - RFC atoms.
  0   - atom
*/

#define	rfc822_is_atom(p)	( (p) == 0 || (p) == '"' || (p) == '(' )

	const char *ptr;	/* Pointer to value for the token. */
	size_t len;		/* Length of token value */
} ;

/*
** After the struct rfc822token array is built, it is used to create
** the rfc822addr array, which is the array of addresses (plus
** syntactical fluff) extracted from those text strings.  Each rfc822addr
** record has several possible interpretation:
**
** tokens is NULL - syntactical fluff, look in name/nname for tokens
**                  representing the syntactical fluff ( which is semicolons
**                  and  list name:
**
** tokens is not NULL - actual address.  The tokens representing the actual
**                  address is in tokens/ntokens.  If there are comments in
**                  the address that are possible "real name" for the address
**                  they are saved in name/nname (name may be null if there
**                  is none).
**                  If nname is 1, and name points to a comment token,
**                  the address was specified in old-style format.  Otherwise
**                  the address was specified in new-style route-addr format.
**
** The tokens and name pointers are set to point to the original rfc822token
** array.
*/

struct rfc822addr {
	struct rfc822token *tokens;
	struct rfc822token *name;
} ;

/***************************************************************************
**
** rfc822 tokens
**
***************************************************************************/

struct rfc822t {
	struct rfc822token *tokens;
	int	ntokens;
} ;

/* The passed-in string must exist unti rfc822t_free() is called */

struct rfc822t *rfc822t_alloc_new(const char *p,
	void (*err_func)(const char *, size_t, void *), void *);
	/* Parse addresses */

void rfc822t_free(struct rfc822t *);		/* Free rfc822 structure */

void rfc822tok_print(const struct rfc822token *,
		     void (*)(const char *, size_t, void *), void *);
/* Print the tokens */

/***************************************************************************
**
** rfc822 addresses
**
***************************************************************************/

struct rfc822a {
	struct rfc822addr *addrs;
	int	naddrs;
} ;

/* The passed_in rfc822t object must exist until rfc822a_free() is called */
struct rfc822a *rfc822a_alloc(struct rfc822t *);
void rfc822a_free(struct rfc822a *);		/* Free rfc822 structure */

void rfc822_deladdr(struct rfc822a *, int);

/* rfc822_print "unparses" the rfc822 structure.  Each rfc822addr is "printed"
   (via the attached function).  NOTE: instead of separating addresses by
   commas, the print_separator function is called.
*/

int rfc822_print(const struct rfc822a *a,
		 void (*print_func)(const char *, size_t, void *),
		 void (*print_separator)(const char *, void *), void *);

/* rfc822_print_common is an internal function */

int rfc822_print_common(const struct rfc822a *a,
			char *(*decode_func)(const char *, const char *, int),
			const char *chset,
			void (*print_func)(const char *, size_t, void *),
			void (*print_separator)(const char *, void *), void *);

/* Extra functions */

char *rfc822_gettok(const struct rfc822token *);
char *rfc822_getaddr(const struct rfc822a *, int);
char *rfc822_getaddrs(const struct rfc822a *);
char *rfc822_getaddrs_wrap(const struct rfc822a *, int);

void rfc822_mkdate_buf(time_t, char *);
const char *rfc822_mkdate(time_t);

int rfc822_parsedate_chk(const char *, time_t *);

#define CORESUBJ_RE 1
#define CORESUBJ_FWD 2

char *rfc822_coresubj(const char *, int *);
char *rfc822_coresubj_nouc(const char *, int *);
char *rfc822_coresubj_keepblobs(const char *s);

/*
** Display a header. Takes a raw header value, and formats it for display
** in the given character set.
**
** hdrname -- header name. Determines whether the header contains addresses,
**            or unstructured data.
**
** hdrvalue -- the actual value to format.
**
** display_func -- output function.
**
** err_func -- if this function returns a negative value, to indicate an error,
** this may be called just prior to the error return to indicate where the
** formatting error is, in the original header.
**
** ptr -- passthrough last argument to display_func or err_func.
**
** repeatedly invokes display_func to pass the formatted contents.
**
** Returns 0 upon success, -1 upon a failure.
*/

int rfc822_display_hdrvalue(const char *hdrname,
			    const char *hdrvalue,
			    const char *charset,
			    void (*display_func)(const char *, size_t,
						 void *),
			    void (*err_func)(const char *, size_t, void *),
			    void *ptr);

/*
** Like rfc822_display_hdrvalue, except that the converted header is saved in
** a malloc-ed buffer. The pointer to the malloc-ed buffer is returned, the
** caller is responsible for free-ing it. An error condition is indicated
** by a NULL return value.
*/

char *rfc822_display_hdrvalue_tobuf(const char *hdrname,
				    const char *hdrvalue,
				    const char *charset,
				    void (*err_func)(const char *, size_t,
						     void *),
				    void *ptr);

/*
** Display a recipient's name in a specific character set.
**
** The index-th recipient in the address structure is formatted for the given
** character set. If the index-th entry in the address structure is not
** a recipient address (it represents an obsolete list name indicator),
** this function reproduces it literally.
**
** If the index-th entry in the address structure is a recipient address without
** a name, the address itself is formatted for the given character set.
**
** If 'charset' is NULL, the name is formatted as is, without converting
** it to any character set.
**
** A callback function gets repeatedly invoked to produce the name.
**
** Returns a negative value upon a formatting error.
*/

int rfc822_display_name(const struct rfc822a *rfcp, int index,
			const char *chset,
			void (*print_func)(const char *, size_t, void *),
			void *ptr);

/*
** Display a recipient's name in a specific character set.
**
** Uses rfc822_display_name to place the generated name into a malloc-ed
** buffer. The caller must free it when it is no longer needed.
**
** Returns NULL upon an error.
*/

char *rfc822_display_name_tobuf(const struct rfc822a *rfcp, int index,
				const char *chset);

/*
** Display names of all addresses. Each name is followed by a newline
** character.
**
*/
int rfc822_display_namelist(const struct rfc822a *rfcp,
			    const char *chset,
			    void (*print_func)(const char *, size_t, void *),
			    void *ptr);

/*
** Display a recipient's address in a specific character set.
**
** The index-th recipient in the address structure is formatted for the given
** character set. If the index-th entry in the address structure is not
** a recipient address (it represents an obsolete list name indicator),
** this function produces an empty string.
**
** If 'charset' is NULL, the address is formatted as is, without converting
** it to any character set.
**
** A callback function gets repeatedly invoked to produce the address.
**
** Returns a negative value upon a formatting error.
*/

int rfc822_display_addr(const struct rfc822a *rfcp, int index,
			const char *chset,
			void (*print_func)(const char *, size_t, void *),
			void *ptr);

/*
** Like rfc822_display_addr, but the resulting displayable string is
** saved in a buffer. Returns a malloc-ed buffer, the caller is responsible
** for free()ing it. A NULL return indicates an error.
*/

char *rfc822_display_addr_tobuf(const struct rfc822a *rfcp, int index,
				const char *chset);

/*
** Like rfc822_display_addr, but the user@domain gets supplied in a string.
*/
int rfc822_display_addr_str(const char *tok,
			    const char *chset,
			    void (*print_func)(const char *, size_t, void *),
			    void *ptr);

/*
** Like rfc822_display_addr_str, but the resulting displayable string is
** saved in a buffer. Returns a malloc-ed buffer, the caller is responsible
** for free()ing it. A NULL return indicates an error.
*/
char *rfc822_display_addr_str_tobuf(const char *tok,
				    const char *chset);

/*
** address is a hostname, which is IDN-encoded. 'address' may contain an
** optional 'user@', which is preserved. Returns a malloc-ed buffer, the
** caller is responsible for freeing it.
*/
char *rfc822_encode_domain(const char *address,
			   const char *charset);


/* Internal functions */
void rfc822_tokenize(const char *p,
		     size_t plen,
		     void (*parsed_func)(char token,
					 const char *ptr, size_t len,
					 void *voidp),
		     void *voidp_parsed_func,
		     void (*err_func)(const char *, size_t, void *),
		     void *voidp_err_func);

void rfc822_parseaddr(size_t ntokens,
		      char (*get_nth_token)(size_t, void *),
		      void (*consume_n_tokens)(size_t, void *),
		      void (*make_quoted_token)(size_t, void *),
		      void (*define_addr_name)(size_t, int, void *),
		      void (*define_addr_tokens)(size_t, int, void *),
		      void *voidp);

void rfc822print_token(int token_token,
		       const char *token_ptr,
		       size_t token_len,
		       void (*print_func)(const char *, size_t, void *),
		       void *ptr);

#if 0
{
#endif
#ifdef  __cplusplus
}

#include <string_view>
#include <vector>
#include <functional>
#include <iterator>
#include <type_traits>
#include <streambuf>

namespace rfc822 {
#if 0
}
#endif

struct length_counter {
	using iterator_category=std::output_iterator_tag;
	using value_type=void;
	using pointer=void;
	using reference=void;
	using difference_type=void;

	size_t l=0;

	length_counter &operator=(char) { ++l; return *this; }
	length_counter &operator++(int) { return *this; }
	auto &operator*()
	{
		return *this;
	}

	operator size_t () const { return l; }
};

// C++ version of rfc822token C struct.

struct token {
	int type=0; // rfc822token.token value
	std::string_view str;
};

// C++ version of rfc822t

struct tokens : std::vector<token> {

	// Tokenize a character string.
	//
	// The passed-in string must exist until this object is destroyed.

	tokens(std::string_view str,
	       std::function<void (size_t)> err_func=[](size_t){});

	using std::vector<token>::vector;

	~tokens()=default;

	// Print a token sequence, C++ version of rfc822tok_print.
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
		bool prev_is_atom=false;

		while (b != e)
		{
			auto &t=*b++;

			bool isatom=rfc822_is_atom(t.type);

			if (prev_is_atom && isatom)
			{
				*iter++=' ';
			}

			rfc822print_token(
				t.type, t.str.data(), t.str.size(),
				[](const char *s, size_t l, void *voidp)
				{
					auto iterp=static_cast<
						std::remove_cv_t<
							std::remove_reference_t<
								out_iter_type
								>> *>(voidp);

					while (l)
					{
						*(*iterp)++=*s;
						++s;
						--l;
					}
				}, &iter);
			prev_is_atom=isatom;
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

			bool isatom=rfc822_is_atom(t.type);

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

	// Equivalent to rfc822tok_print, writes to an output iterator.
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

	// Convert this name to Unicode, using RFC2047 decoding.
	//
	// The address is written to an output iterator over char32_t.

	template<typename out_iter_type> auto unicode_name(
		out_iter_type &&iter
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

	template<typename out_iter_type> auto display_name(
		const std::string &chset,
		out_iter_type &&iter
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

	struct do_print {
		const struct address &a;

		do_print(const struct address &a) : a{a} {}
		virtual void emit_address()=0;
		virtual void emit_name()=0;
		virtual void emit_char(char)=0;

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

		do_print_unicode(const struct address &a, out_iter &iter)
			: do_print{a}, iter{iter}
		{
		}

		void emit_address() override
		{
			a.unicode_address(iter);
		}

		void emit_name() override
		{
			a.unicode_name(iter);
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

	template<typename out_iter_type> auto display_name(
		const std::string &chset,
		out_iter_type &&iter
	) const
	{
		return name.display_name(
			chset,
			std::forward<out_iter_type>(iter)
		);
	}
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

	template<typename iter_b, typename iter_e, typename out_iter_type,
		 typename print_separator_cb_t>
	struct do_print_raw : do_print {

		iter_b b;
		iter_e e;
		out_iter_type &out_iter;
		print_separator_cb_t &&print_separator_cb;

		do_print_raw(iter_b &b,
			     iter_e &e,
			     out_iter_type &out_iter,
			     print_separator_cb_t &&print_separator_cb)
			: b{b}, e{e}, out_iter{out_iter},
			  print_separator_cb{std::forward<print_separator_cb_t>(
					  print_separator_cb)}
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

		void print() override
		{
			b->print(out_iter);
			++b;
		}
	};

	// Print a sequence of addresses, comma separated, to an output iterator

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type, typename print_separator_t>
	static auto print_impl(iterb_type &&b, itere_type &&e,
			       out_iter_type &&out_iter,
			       print_separator_t &&print_separator)
	{
		do_print_raw printer{b, e, out_iter,
				     std::forward<print_separator_t>(
					     print_separator
				     )};

		printer.output();

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return out_iter;
	}

	template<typename iterb_type, typename itere_type,
		 typename out_iter_type>
	static auto print_impl(iterb_type &&b, itere_type &&e,
			       out_iter_type &&out_iter)
	{
		print_impl(std::forward<iterb_type>(b),
			   std::forward<itere_type>(e),
			   out_iter,
			   make_default_print_separator<char>(out_iter));

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return out_iter;
	}

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
		print_impl(std::forward<iterb_type>(b),
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

			iter(std::basic_string<char_type>{b, e});

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
			iter(std::move(accumulated_line));
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
	// The last parameter, iter, is a callable object that gets invoked,
	// repeatedly with a std::string containing a line of addressed,
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

	// wrap() uses print_wrapped() to write the wrapped addresses into
	// a vector of std::strings.
	//
	// Two passes are done. The first pass counts the number of wrapped
	// strings, by passing a pseudo-iterator, a length_counter, that
	// just counts things.

	std::vector<std::string> wrap(size_t max_l)
	{
		std::vector<std::string> w;
		size_t l=0;

		// Size up the length.

		print_wrapped(begin(), end(), max_l,
			      [&]
			      (const std::string &s)
			      {
				      ++l;
			      });

		w.reserve(l);

		print_wrapped(begin(), end(), max_l,
			      [&]
			      (std::string &&s)
			      {
				      w.push_back(std::move(s));
			      });
		return w;
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

		print(std::forward<iterb_type>(b),
		      std::forward<itere_type>(e),
		      char32_wrapper,
		      wrap_out_iter_sep{wrapper});

		wrapper.end();

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	// wrap_unicode() uses unicode_wrapped() to write the wrapped
	// addresses into a vecgtor of std::u32strings.
	//
	// Two passes are done. The first pass counts the number of wrapped
	// strings, by passing a pseudo-iterator, a length_counter, that
	// just counts things.

	std::vector<std::u32string> wrap_unicode(size_t max_l) const
	{
		std::vector<std::u32string> w;
		size_t l=0;

		// Size up the length.

		unicode_wrapped(begin(), end(), max_l,
				[&]
				(const std::u32string &s)
				{
					++l;
				});

		w.reserve(l);

		unicode_wrapped(begin(), end(), max_l,
			      [&]
			      (std::u32string &&s)
			      {
				      w.push_back(std::move(s));
			      });
		return w;
	}

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
		unicode_wrapped(
			std::forward<iterb_type>(b),
			std::forward<itere_type>(e),
			max_l,
			[&]
			(std::u32string &&s)
			{
				auto ret=unicode::iconvert::fromu::convert(
					s, chset);

				if (ret.second)
					ret.first +=
						" [decoding error]";
				iter(std::move(ret.first));
			});

		if constexpr(!std::is_same_v<out_iter_type, out_iter_type &>)
			return iter;
	}

	// wrap_unicode() uses unicode_wrapped() to write the wrapped
	// addresses into a vecgtor of std::u32strings.
	//
	// Two passes are done. The first pass counts the number of wrapped
	// strings, by passing a pseudo-iterator, a length_counter, that
	// just counts things.

	std::vector<std::string> wrap_display(size_t max_l,
					      const std::string &chset) const
	{
		std::vector<std::string> w;

		wrap_display(begin(), end(), max_l, chset,
			     [&](std::string s)
			     {
				     w.push_back(std::move(s));
			     });

		return w;
	}
};

// Return true if header_name is To, Cc, Bcc, or other headers that contain
// a list of addresses.

bool header_is_addr(std::string_view header_name);

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


#endif

#include "rfc822/rfc822_2047.h"

#endif
