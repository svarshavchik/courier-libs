/*
*/
#ifndef	rfc822_h
#define	rfc822_h

/*
** Copyright 1998 - 2009 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"rfc822/config.h"
#endif

#include	<time.h>

#ifdef  __cplusplus
extern "C" {
#endif
#if 0
}
#endif

#define RFC822_SPECIALS "()<>[]:;@\\,.\""

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

namespace rfc822 {
#if 0
}
#endif

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
	       std::function<void (size_t)> err_func);

	using std::vector<token>::vector;

	~tokens()=default;

	// Print a token sequence, C++ version of rfc822tok_print.
	// The token sequence gets printed to an output iterator, returns the
	// output iterator value after the sequence is printed.

	template<typename iter_type, typename out_iter_type>
	static out_iter_type print(iter_type b, iter_type e, out_iter_type iter)
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
					auto iterp=static_cast<out_iter_type *>(
						voidp
					);

					while (l)
					{
						*(*iterp)++=*s;
						++s;
						--l;
					}
				}, &iter);
			prev_is_atom=isatom;
		}

		return iter;
	}

	// Equivalent to rfc822tok_print

	template<typename out_iter_type> out_iter_type print(out_iter_type iter)
	{
		return print(this->begin(), this->end(), iter);
	}
};

// The C++ equivalent of rfc822addr

struct address {
	tokens name;
	tokens address;

	// Print this address to an output iterator, returning the final value
	// of the output iterator.

	template<typename iter_type> iter_type print(iter_type iter)
	{
		if (address.empty())
			return this->name.print(iter);

		if (!name.empty() && name.begin()->type == '(')
		{
			// old style

			iter=this->address.print(iter);

			*iter++=' ';

			return this->name.print(iter);
		}

		bool print_braces=false;

		if (!name.empty())
		{
			iter=name.print(iter);

			*iter++=' ';
			print_braces=true;
		}
		else
		{
			bool prev_is_atom=false;

			for (auto &t:address)
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
			*iter++='<';
		}
		iter=address.print(iter);
		if (print_braces)
		{
			*iter++='>';
		}
		return iter;
	}
};

// Tokens converted to addresses.

struct addresses : std::vector<address> {

	// Convert tokens to addresses
	//
	// The passed-in tokens may be modified.
	//
	// The string that was tokenized must exist until the address vector is
	// destroyed.

	addresses(tokens &);

	using std::vector<address>::vector;
	~addresses()=default;

	// Default separator printer

	template<typename out_iter_type>
	static out_iter_type print_separator(out_iter_type iter,
					     const char *sep)
	{
		while (*sep)
			*iter++=*sep++;

		return iter;
	}

	// Print a sequence of addresses, comma separated, to an output iterator

	template<typename iter_type, typename out_iter_type,
		typename print_separator_type=out_iter_type(out_iter_type,
							    const char *sep)>
	static out_iter_type print(iter_type b, iter_type e,
				   out_iter_type iter,
				   print_separator_type print_separator_cb=
				   print_separator<out_iter_type>)
	{
		const char *sep="";

		while (b != e)
		{
			if (*sep)
				iter=print_separator_cb(iter, sep);

			sep=", ";

			if (b->address.empty() && !b->name.empty())
				switch ((--b->name.end())->type) {
				case ':':
				case ';':
					sep=" ";
					break;
				}

			iter=b->print(iter);
			++b;
		}
		return iter;
	}

	// The C++ version of rfc822_print(), the addresses are printed to
	// an output iterator, the new value of the output iterator gets
	// returned.

	template<typename out_iter_type,
		 typename print_separator_type=out_iter_type(out_iter_type,
							     const char *sep)>
	out_iter_type print(out_iter_type iter,
			    print_separator_type print_separator_cb=
			    print_separator<out_iter_type>)
	{
		return print(this->begin(), this->end(), iter,
			     print_separator_cb);
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

	template<typename out_iter_type>
        struct wrap_out_iter_impl {

		// The real output iterator for the wrapped address list.
		out_iter_type iter;

		// Width of characters being wrapped to.
		const size_t max_l;

		wrap_out_iter_impl(out_iter_type iter, size_t max_l)
			: iter{std::move(iter)}, max_l{max_l}
		{
		}

		// Accumulated add()ed characters.
		std::string accumulated_line;

		// Index of the most recent space character, captured
		// by flush_if_more().
		size_t last_sep_spc=0;

		void add(char c)
		{
			accumulated_line.push_back(c);
		}

		// If the accumulated line exceeds the maximum length,
		// dump everything up to the index of the space character
		// followed by a newline (the space is effectively replaced
		// by a newline)/

		bool flush_if_more()
		{
			if (accumulated_line.size() <= max_l)
			{
				// Remember the index of the last space.
				last_sep_spc=accumulated_line.size();
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
			}

			auto b=accumulated_line.begin();
			auto e=b+last_sep_spc;

			while (b != e)
			{
				*iter++=*b++;
			}

			if (!excessive)
			{
				// This is going to be a space character,
				// guaranteed./
				++b;
			}
			accumulated_line.erase(accumulated_line.begin(), b);
			*iter++='\n';
			last_sep_spc=0;

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
			for (auto &c:accumulated_line)
				*iter++=c;
		}
	};

	// The actual iterator that gets passed to print(), instead of
	// wrap_out_iter_impl. This one doesn't mind being copied or
	// assigned to. write_out_iter_impl is uncopyable.

	template<typename out_iter_type>
	struct wrap_out_iter {
		wrap_out_iter_impl<out_iter_type> &impl;

		wrap_out_iter(wrap_out_iter_impl<out_iter_type> &impl)
			: impl{impl} {}

		auto &operator=(const wrap_out_iter<out_iter_type> &)
		{
			return *this;
		}

		// Iterator semantics.
		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		auto &operator=(char c)
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

	struct wrap_out_iter_sep {

		template<typename iter> auto operator()(iter i,
							const char *sep)
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

					if (i.impl.flush_if_more())
						continue;
				}

				i=*sep;
			}

			return i;
		}
	};

	// C++ version of rfc822_getaddrs_wrap() that writes out the
	// wrapped addresses to an output iterator. The sequence of addresses
	// is defined by a beginning and an ending iterator sequence.

	template<typename iter_type, typename out_iter_type>
	static out_iter_type print_wrapped(iter_type b, iter_type e,
					   size_t max_l,
					   out_iter_type iter)
	{
		wrap_out_iter_impl wrapper{iter, max_l};

		print(b, e,
		      wrap_out_iter{wrapper},
		      wrap_out_iter_sep{});

		wrapper.end();

		return wrapper.iter;
	}

	// wrap() uses print_wrapped() to write the wrapped addresses into
	// a plain std::string
	//
	// Two passes are done. The first pass counts the number of wrapped
	// character, by passing a pseudo-iterator, a length_counter, that
	// just counts things.

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
	};

	std::string wrap(size_t max_l)
	{
		std::string s;

		// Size up the length.

		auto l=print_wrapped(begin(), end(), max_l, length_counter{});

		s.reserve(l.l);
		print_wrapped(begin(), end(), max_l,
			      std::back_inserter(s));

		return s;
	}
};

#if 0
{
#endif
}


#endif

#endif
