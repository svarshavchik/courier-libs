/*
** Copyright 1998 - 2025 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/
#ifndef	rfc2045_h
#define	rfc2045_h

#include	"rfc2045/rfc2045_config.h" /* VPATH build */
#include	"numlib/numlib.h"
#include	<sys/types.h>
#include	<string.h>
#include	<stdio.h>

#ifdef  __cplusplus
#include <vector>
#include <string>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <unordered_set>
#include <map>
#include <optional>
#include <variant>
#include <charconv>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "rfc822/rfc822.h"
#include "rfc822/rfc2047.h"
#endif

#define RFC2045_MIME_MESSAGE_RFC822 "message/rfc822"
#define RFC2045_MIME_MESSAGE_GLOBAL "message/global"

#define RFC2045_MIME_MESSAGE_DELIVERY_STATUS "message/delivery-status"
#define RFC2045_MIME_MESSAGE_GLOBAL_DELIVERY_STATUS \
	"message/global-delivery-status"

#define RFC2045_MIME_MESSAGE_HEADERS "text/rfc822-headers"
#define RFC2045_MIME_MESSAGE_GLOBAL_HEADERS "message/global-headers"

#ifdef __cplusplus
extern "C" {
#endif

#if 0
}
#endif

int rfc2045_message_content_type(const char *);
int rfc2045_delivery_status_content_type(const char *);
int rfc2045_message_headers_content_type(const char *);

#if 0
{
#endif

#ifdef __cplusplus
}
#endif

#define	RFC2045_ISMIME1(p)	((p) && atoi(p) == 1)
#define	RFC2045_ISMIME1DEF(p)	(!(p) || atoi(p) == 1)

#define	RFC2045_ERR8BITHEADER	1	/* 8 bit characters in headers */
	/* But this is now OK, in UTF8 mode */

#define	RFC2045_ERR8BITCONTENT	2	/* 8 bit contents, but no 8bit
					content-transfer-encoding */
#define	RFC2045_ERR2COMPLEX	4	/* Too many nested contents */
#define RFC2045_ERRBADBOUNDARY	8	/* Overlapping MIME boundaries */

struct rfc2045 {
#ifdef __cplusplus
	class entity;
	class entity_info;
	class entity_parse_meta;
	class entity_parser_base;
	template<bool crlf> class entity_parser;

	enum class cte { error=0, sevenbit='7', eightbit='8', qp='Q',
			 base64='B'};

	// Look at the first character of Content-Transfer-Encoding

	static cte to_cte(char ch)
	{
		switch (ch) {
		case '7':
			return cte::sevenbit;
		case '8':
			return cte::eightbit;
		case 'q':
		case 'Q':
			return cte::qp;
		case 'b':
		case 'B':
			return cte::base64;
		}

		return cte::error;
	}

#endif
	struct rfc2045 *parent;
	unsigned pindex;
	struct rfc2045 *next;

	off_t	startpos,	/* At which offset in msg the header starts */
		endpos,		/* Where it ends */
		startbody,	/* Where the body of the msg starts */
		endbody;	/* endpos - trailing CRLF terminator */
	off_t	nlines;		/* Number of lines in message */
	off_t	nbodylines;	/* Number of lines only in the body */
	char *mime_version;
	char *content_type;
	struct rfc2045attr *content_type_attr;	/* Content-Type: attributes */

	char *content_disposition;
	char *boundary;
	struct rfc2045attr *content_disposition_attr;
	char *content_transfer_encoding;
	int content_8bit;		/*
					** Set if content_transfer_encoding is
					** 8bit
					*/
	char *content_id;
	char *content_description;
	char *content_language;
	char *content_md5;
	char *content_base;
	char *content_location;
	struct  rfc2045ac *rfc2045acptr;
	int	has8bitchars;	/* For rewriting */
	int	hasraw8bitchars; /* For rewriting */
	int	haslongline;	/* For rewriting */
	unsigned rfcviolation;	/* Boo-boos */

	unsigned numparts;	/* # of parts allocated */

	char	*rw_transfer_encoding;	/* For rewriting */

	/* Use quoted-printable for 8bit content */
#define	RFC2045_RW_7BIT	1

	/*
	** Convert quoted-printable, if the resulting line length is not
	** excessive.
	*/

#define	RFC2045_RW_8BIT	2

	/*
	** Convert quoted printable without checking for maximum resulting
	** line length.
	*/
#define RFC2045_RW_8BIT_ALWAYS 3

	/* Subsections */

	struct rfc2045 *firstpart, *lastpart;

	/* Working area */

	char *workbuf;
	size_t workbufsize;
	size_t workbuflen;
	int	workinheader;
	int	workclosed;
	int	isdummy;
	int	informdata;	/* In a middle of a long form-data part */
	char *header;
	size_t headersize;
	size_t headerlen;

	int	(*decode_func)(struct rfc2045 *, const char *, size_t);
	void	*misc_decode_ptr;
	int	(*udecode_func)(const char *, size_t, void *);
} ;

struct rfc2045attr {
	struct rfc2045attr *next;
	char *name;
	char *value;
	} ;

#ifdef __cplusplus
extern "C" {
#endif
#if 0
}
#endif

struct rfc2045 *rfc2045_alloc();
void rfc2045_parse(struct rfc2045 *, const char *, size_t);
void rfc2045_parse_partial(struct rfc2045 *);
void rfc2045_free(struct rfc2045 *);

void rfc2045_mimeinfo(const struct rfc2045 *,
	const char **,
	const char **,
	const char **);

const char *rfc2045_boundary(const struct rfc2045 *);
int rfc2045_isflowed(const struct rfc2045 *);
int rfc2045_isdelsp(const struct rfc2045 *);
char *rfc2045_related_start(const struct rfc2045 *);
const char *rfc2045_content_id(const struct rfc2045 *);
const char *rfc2045_content_description(const struct rfc2045 *);
const char *rfc2045_content_language(const struct rfc2045 *);
const char *rfc2045_content_md5(const struct rfc2045 *);

void rfc2045_mimepos(const struct rfc2045 *, off_t *, off_t *, off_t *,
	off_t *, off_t *);
unsigned rfc2045_mimepartcount(const struct rfc2045 *);

void rfc2045_xdump(struct rfc2045 *);

struct rfc2045id {
	struct rfc2045id *next;
	int idnum;
} ;

void rfc2045_decode(struct rfc2045 *,
		    void (*)(struct rfc2045 *, struct rfc2045id *, void *),
		    void *);

struct rfc2045 *rfc2045_find(struct rfc2045 *, const char *);


/*
** Source of an rfc2045-formatted content (internal)
*/

struct rfc2045src {
	void (*deinit_func)(void *);

	int (*seek_func)(off_t pos, void *);
	ssize_t (*read_func)(char *buf, size_t cnt, void *);

	void *arg;
};
/* Read from a filedesc, returns a malloced buffer */

struct rfc2045src *rfc2045src_init_fd(int fd);

/* Destroy a rfc2045src */

void rfc2045src_deinit(struct rfc2045src *);

/************************/

void rfc2045_cdecode_start(struct rfc2045 *,
	int (*)(const char *, size_t, void *), void *);
int rfc2045_cdecode(struct rfc2045 *, const char *, size_t);
int rfc2045_cdecode_end(struct rfc2045 *);

const char *rfc2045_getdefaultcharset();
void rfc2045_setdefaultcharset(const char *);
struct rfc2045 *rfc2045_fromfd(int);
#define	rfc2045_fromfp(f)	(rfc2045_fromfd(fileno((f))))
struct rfc2045 *rfc2045header_fromfd(int);
#define        rfc2045header_fromfp(f)        (rfc2045header_fromfd(fileno((f))))

extern void rfc2045_error(const char *);


struct  rfc2045ac {
	void (*start_section)(struct rfc2045 *);
	void (*section_contents)(const char *, size_t);
	void (*end_section)();
	} ;

struct rfc2045 *rfc2045_alloc_ac();
int rfc2045_ac_check(struct rfc2045 *, int);
int rfc2045_rewrite(struct rfc2045 *p, struct rfc2045src *src, int fdout_arg,
		    const char *appname);
int rfc2045_rewrite_func(struct rfc2045 *p, struct rfc2045src *src,
			 int (*funcarg)(const char *, int, void *),
			 void *funcargarg,
			 const char *appname);

/* Internal functions */

int rfc2045_try_boundary(struct rfc2045 *, struct rfc2045src *, const char *);
char *rfc2045_mk_boundary(struct rfc2045 *, struct rfc2045src *);
const char *rfc2045_getattr(const struct rfc2045attr *, const char *);
int rfc2045_attrset(struct rfc2045attr **, const char *, const char *);

/* MIME content base/location */

char *rfc2045_content_base(struct rfc2045 *p);
	/* This joins Content-Base: and Content-Location:, as best as I
	** can figure it out.
	*/

char *rfc2045_append_url(const char *, const char *);
	/* Do this with two arbitrary URLs */

/* MISC mime functions */

struct rfc2045 *rfc2045_searchcontenttype(struct rfc2045 *, const char *);
	/* Assume that the "real" message text is the first MIME section here
	** with the given content type.
	*/

int rfc2045_decodemimesection(struct rfc2045src *, /* Message to decode */
			      struct rfc2045 *,	/* MIME section to decode */
			      int (*)(const char *, size_t, void *),
			      /*
			      ** Callback function that receives decoded
			      ** content.
			      */
			      void *	/* 3rd arg to the callback function */
			      );
/*
** Decode a given MIME section.
*/

int rfc2045_decodetextmimesection(struct rfc2045src *, /* Message to decode */
				  struct rfc2045 *, /* MIME section */
				  const char *,	/* Convert to this character set */
				  int *, /* Set to non-0 if MIME section contained chars that could not be converted to the requested charset */
				  int (*)(const char *, size_t, void *),
				  /*
				  ** Callback function that receives decoded
				  ** content.
				  */
				  void * /* 3rd arg to the callback function */
			      );
	/*
	** Like decodemimesction(), except that the text is automatically
	** convert to the specified character set (this function falls back
	** to decodemimesection() if libunicode.a is not available, or if
	** either the specified character set, or the MIME character set
	** is not supported by libunicode.a
	*/


	/*
	** READ HEADERS FROM A MIME SECTION.
	**
	** Call rfc2045header_start() to allocate a structure for the given
	** MIME section.
	**
	** Call rfc2045header_get() to repeatedly get the next header.
	** Function returns < 0 for a failure (out of memory, or something
	** like that).  Function returns 0 for a success.  Example:
	**
	** rfc2045header_get(ptr, &header, &value, 0);
	**
	** If success: check if header is NULL - end of headers, else
	** "header" and "value" will contain the RFC 822 header.
	**
	** Last argument is flags:
	*/

#define RFC2045H_NOLC 1		/* Do not convert header to lowercase */
#define RFC2045H_KEEPNL 2	/* Preserve newlines in the value string
				** of multiline headers.
				*/

struct rfc2045headerinfo *
	rfc2045header_start(struct rfc2045src *,/* Readonly source */
			    struct rfc2045 *	/* MIME section to read */
			    );

int rfc2045header_get(struct rfc2045headerinfo *,
		      char **,	/* Header return */
		      char **,	/* Value return */
		      int);	/* Flags */

void rfc2045header_end(struct rfc2045headerinfo *);


/*
** Generic MIME header parsing code.
**
** header - something like "text/plain; charset=us-ascii; format=flowed".
**
** header_type_cb - callback function, receives the "text/plain" parameter.
**
** header_param_cb - callback function, repeatedly invoked to process the
** additional parameters.  In this example, receives "charset" and "us-ascii".
** Note -t he first parameter will always be in lowercase.
**
** void_arg - passthrough parameter to the callback functions.
*/

int rfc2045_parse_mime_header(const char *header,
			      void (*header_type_cb)(const char *, void *),
			      void (*header_param_cb)(const char *,
						      const char *,
						      void *),
			      void *void_arg);

/*
** The rfc2045_makereply function is used to generate an initial
** reply to a MIME message.  rfc2045_makereply takes the following
** structure:
*/

struct rfc2045_mkreplyinfo {

	struct rfc2045src *src; /* Original message source */

	struct rfc2045 *rfc2045partp;
	/*
	** rfc2045 structure for the message to reply.  This may actually
	** represent a single message/rfc822 section within a larger MIME
	** message digest, in which case we format a reply to this message.
	*/

	void *voidarg;	/* Transparent argument passed to the callback
			** functions.
			*/

	/*
	** The following callback functions are called to generate the reply
	** message.  They must be initialized.
	*/

	void (*write_func)(const char *, size_t, void *);
	/* Called to write out the content of the message */

	void (*writesig_func)(void *);
	/* Called to write out the sender's signature */

	int (*myaddr_func)(const char *, void *);
	/* myaddr_func receives a pointer to an RFC 822 address, and it
	** should return non-zero if the address is the sender's address
	*/

	const char *replymode;
	/*
	** replymode must be initialized to one of the following.  It sets
	** the actual template for the generated response.
	**
	** "forward" - forward original message.
	** "forwardatt" - forward original message as an RFC822 attachment
	** "reply" - a standard reply to the original message's sender
	** "replydsn" - a DSN reply to the original message's sender
	** "feedback" - generate a feedback report (RFC 5965)
	** "replyfeedback" - "feedback" to the sender's address.
	** "replyall" - a "reply to all" response.
	** "replylist" - "reply to mailing list" response.  This is a reply
	** that's addressed to the mailing list the original message was sent
	** to.
	*/

	int replytoenvelope;
	/*
	** If non-zero, the "reply" or "replydsn" message gets addressed to the
	** "Return-Path" or "Errors-To" address, if available.
	*/

	int donotquote;

	/*
	** If donotquote is set, the contents of the original message are not
	** quoted by any of the "reply" modes, and replysalut (below) does not
	** get emitted.
	*/

	int fullmsg;
	/*
	** For replydsn, feedback, replyfeedback, attach the entire message
	** instead of just its headers.
	*/

	const char *replysalut;
	/*
	** This should be set to the salutation to be used for the reply.
	** The following %-formats may appear in this string:
	**
	** %% - an explicit % character
	**
	** %n - a newline character
	**
	** %C - the X-Newsgroup: header from the original message
	**
	** %N - the Newsgroups: header from the original message
	**
	** %i - the Message-ID: header from the original message
	**
	** %f - the original message's sender's address
	**
	** %F - the original message's sender's name
	**
	** %S - the Subject: header from the original message
	**
	** %d - the original message's date, in the local timezone
	**
	** %{...}d - use strftime() to format the original message's date.
	**           A plain %d is equivalent to %{%a, %d %b %Y %H:%M:%S %z}d.
	**
	** Example:  "%F writes:"
	*/

	const char *forwarddescr;
	/*
	** For forwardatt, this is the Content-Description: header,
	** (typically "Forwarded message").
	*/

	/*
	** If not NULL, overrides the Subject: header
	*/

	const char *subject;

	/*
	** When reply mode is 'replydsn', dsnfrom must be set to a valid
	** email address that's specified as the address that's generating
	** the DSN.
	*/
	const char *dsnfrom;

	/*
	** When reply mode is 'replyfeedback', feedbacktype must be set to
	** one of the registered feedback types:
	** "abuse", "fraud", "other", "virus".
	*/
	const char *feedbacktype;

	/*
	** Feedback report headers.
	**
	** NOTE: rfc2045_makereply() automatically inserts the
	** Feedback-Type: (from feedbacktype), User-Agent:, Version:, and
	** Arrival-Date: headers.
	**
	** This is an array of alternating header name and header value
	** strings. The header name string does not contain a colon,
	** rfc2045_makereply supplies one. And, basically, generates
	** "name: value" from this list.
	**
	** For convenience-sake, the capitalization of the headers get
	** adjusted to match the convention in RFC 5965.
	**
	** The list, which must contain an even number of strings, is terminated
	** by a NULL pointer.
	*/
	const char * const *feedbackheaders;

	/*
	** Set the reply/fwd MIME headers. If this is a NULL pointer,
	** write_func() receives ``Content-Type: text/plain; format=flowed;
	** delsp=yes; charset="charset" '' with the charset specified below,
	** and "Content-Transfer-Encoding: 8bit".
	**
	** If this is not a NULL pointer, the effect of
	** this function should be invocation of write_func() to perform the
	** analogous purpose.
	**
	** The output of content_set_charset() should be consistent with the
	** contents of the charset field.
	*/

	void (*content_set_charset)(void *);

	/*
	** Set the reply/fwd content.
	**
	** This function gets called at the point where the additional contents
	** of the reply/fwd should go.
	**
	** If this is not a NULL pointer, the effect of this function should
	** be invocation of write_func() with the additional contents of the
	** reply/fwd. The added content should be consistent with the
	** charset field.
	**
	** Note -- this content is likely to end up in a multipart MIME
	** message, as such it should not contain any lines that look like
	** MIME boundaries.
	*/

	void (*content_specify)(void *);

	const char *mailinglists;
	/*
	** This should be set to a whitespace-delimited list of mailing list
	** RFC 822 addresses that the respondent is subscribed to.  It is used
	** to figure out which mailing list the original message was sent to
	** (all addresses in the original message are compared against this
	** list).  In the event that we can't find a mailing list address on
	** the original message, "replylist" will fall back to "replyall".
	*/

	const char *charset;
	/* The respondent's local charset */

	const char *forwardsep;
	/* This is used instead of replysalut for forwards. */
} ;

int rfc2045_makereply(struct rfc2045_mkreplyinfo *);

/********** Search message content **********/

/*
** Callback passed rfc2045_decodemsgtoutf8()
*/

struct rfc2045_decodemsgtoutf8_cb {

	int flags; /* Optional flags, see below */

	/* Define a non-null function pointer. It gets the name of a header,
	** and the raw, unformatted, header contents.
	** If returns non-0, the header gets converted and sent to output.
	** If null, all headers are sent
	*/

	int (*headerfilter_func)(const char *name, const char *raw, void *arg);

	/* The output function */
	int (*output_func)(const char *data, size_t cnt, void *arg);

	/* If not null, gets invoked after decoding a single header */
	int (*headerdone_func)(const char *headername, void *arg);

	void *arg; /* Passthrough arg to _funcs */
};

#define RFC2045_DECODEMSG_NOBODY 0x01
/* Do not decode MIME content, headers only */

#define RFC2045_DECODEMSG_NOHEADERS 0x02
/*
** Do not decode MIME headers, only body. This is the same as using a
** headerfilter_func that always returns 0
*/

#define RFC2045_DECODEMSG_NOHEADERNAME 0x04
/*
** Do not prepend name: to converted header content.
*/


#define RFC2045_DECODEMSG_NOATTACHHEADERS 0x08
/*
** Do not decode MIME headers of attachments. Decode only the message's
** main headers.
*/

/*
** Convert a message into a utf8 bytestream. The output produced by this
** function is a catentation of decoded header and text content data, converted
** to utf8.
**
** This is fed into an output function. The output function takes a single
** octet, and returns 0 if the octet was processed, or a negative value if
** the output was aborted.
*/

int rfc2045_decodemsgtoutf8(struct rfc2045src *src, /* The message */
			    struct rfc2045 *p, /* The parsed message */

			    /* The callback */
			    struct rfc2045_decodemsgtoutf8_cb *callback);


/********** Decode RFC 2231 attributes ***********/

/*
** rfc2231_decodeType() decodes an RFC 2231-encoded Content-Type: header
** attribute, and rfc2231_decodeDisposition() decodes the attribute in the
** Content-Disposition: header.
**
** chsetPtr, langPtr, and textPtr should point to a char ptr.  These
** functions automatically allocate the memory, the caller's responsible for
** freeing it.  A NULL argument may be provided if the corresponding
** information is not wanted.
*/

int rfc2231_decodeType(struct rfc2045 *rfc, const char *name,
		       char **chsetPtr,
		       char **langPtr,
		       char **textPtr);

int rfc2231_decodeDisposition(struct rfc2045 *rfc, const char *name,
			      char **chsetPtr,
			      char **langPtr,
			      char **textPtr);

/*
** The following two functions convert the decoded string to the local
** charset via unicodelib.  textPtr cannot be null, this time, because this
** is the only return value.   A NULL myChset is an alias for the default
** charset.
*/

int rfc2231_udecodeType(struct rfc2045 *rfc, const char *name,
			const char *myChset,
			char **textPtr);

int rfc2231_udecodeDisposition(struct rfc2045 *rfc, const char *name,
			       const char *myChset,
			       char **textPtr);

/*
** Build an RFC 2231-encoded name*=value.
**
** name, value, charset, language: see RFC 2231.
**
** (*cb_func) gets invoked 1 or more time, receives a "name=value" pair
** each time.
**
** cb_func must return 0; a non-0 return terminates rfc2231_attrCreate, which
** passes through the return code.
**
*/
int rfc2231_attrCreate(const char *name, const char *value,
		       const char *charset,
		       const char *language,
		       int (*cb_func)(const char *param,
				      const char *value,
				      void *void_arg),
		       void *cb_arg);

/** NON-PUBLIC DATA **/

struct rfc2231param {
	struct rfc2231param *next;

	int paramnum;
	int encoded;

	const char *value;
};

void rfc2231_paramDestroy(struct rfc2231param *paramList);
int rfc2231_buildAttrList(struct rfc2231param **paramList,
			  const char *name,

			  const char *attrName,
			  const char *attrValue);

void rfc2231_paramDecode(struct rfc2231param *paramList,
			 char *charsetPtr,
			 char *langPtr,
			 char *textPtr,
			 int *charsetLen,
			 int *langLen,
			 int *textLen);

/*
** Encode an E-mail address as utf-8 address type specified in RFC 6533.
** The e-mail address parameter must be encoded in UTF-8.
**
** The E-mail address is encoded as "rfc822" address type if it has only
** ASCII characters, or if use_rfc822 is set to non0.
**
** A malloc-ed address gets returned.
*/

char *rfc6533_encode(const char *address, int use_rfc822);

/*
** Decode a utf-8 or an rfc-822 address type. Returns a malloc-ed buffer,
** or NULL if the address cannot be decoded.
**
** Assumes valid UTF-8 coding, and does not verify it.
**
** Does verify, for both rfc-822 and utf-8 formats, that the returned address
** does not contain control characters.
*/

char *rfc6533_decode(const char *address);

#if 0
{
#endif

#ifdef  __cplusplus
}

/*
  Build an RFC 2231-encoded name*=value.

  name, value, charset, language: see RFC 2231.

  The callback gets called 1 or more time, with two const char *parameters,

  NOTE: the sum total of name+charset+language cannot exceed 60 characters.

  RFC2231 encoding is not used if:

  - the sum total of octets in name and value does not exceed 75 characters,

  - the value does not contain 8 bit or special characters.

  - language is an empty string.

  - charset is either empty, "us-ascii", "utf-8" or "iso-8859-1".

  If so the callback gets invoked once, with the name and address passed in,
  as is. Otherwise RFC 2231 encoding is used and the callback gets invoked
  one or more times.

  rfc2331_attr_encode returns the value returned from the callback (possibly
  void). Additionally, if the callback returns an integral value: a non-zero
  return from the callback aborts encoding (if RFC 2231 encoding is in use)
  and returns the non-0 returned value, and a 0 value is returned if all
  calls to the callback returned 0.
*/


inline bool rfc2231_do_encode(char c)
{
	switch (c) {
	case '(':
	case ')':
	case '\'':
	case '"':
	case '\\':
	case '%':
	case ':':
	case ';':
	case '=':
		return true;
	}

	return c <= ' ' || c >= 127;
}

template<typename C, bool integral_return=std::is_integral_v<decltype(
	std::declval<C &&>()(std::declval<const char *>(),
			     std::declval<const char *>()))>>
auto rfc2231_attr_encode(std::string_view name,
			 std::string_view value,
			 std::string_view charset,
			 std::string_view language,
			 C &&callback)
{
	if (name.size() > 60 ||
	    charset.size() > 60 ||
	    language.size() > 60 ||
	    name.size()+charset.size()+language.size() > 60)
	{
		// Sanity check

		errno=EINVAL;
		if constexpr(integral_return)
		{
			return -1;
		}
		else
		{
			return;
		}
	}

	if (name.size() + value.size() <= 75 &&
	    language.empty() &&
	    std::find_if(value.begin(), value.end(), rfc2231_do_encode) ==
	    value.end())
	{
		if (charset.size() < 20)
		{
			char buf[20];

			*std::transform(charset.begin(),
					charset.end(),
					buf,
					[]
					(char c)
					{
						if (c >= 'A' && c <= 'Z')
						{
							c += 'a'-'A';
						}
						return c;
					})=0;

			std::string_view sbuf{buf};

			if (sbuf.empty() ||
			    sbuf == "utf-8" ||
			    sbuf == "us-ascii" ||
			    sbuf == "iso-8859-1")
			{
				char name_buf[name.size()+1];
				char value_buf[value.size()+3];

				*std::copy(name.data(), name.data()+
					   name.size(), name_buf)=0;

				bool quotes_needed=std::find_if(
					value.begin(),
					value.end(),
					[]
					(char c)
					{
						static const char specials[]=
							RFC822_SPECIALS;

						return std::find(
							std::begin(specials),
							std::end(specials),
							c) !=
							std::end(specials);
					}) != value.end();

				char *p=value_buf;

				if (quotes_needed)
					*p++='"';
				p=std::copy(value.data(), value.data()+
					    value.size(), p);

				if (quotes_needed)
					*p++='"';
				*p=0;

				return callback(name_buf, value_buf);
			}
		}
	}

	size_t n=0;

	auto vb=value.begin();
	auto ve=value.end();

	// name+charset+language is less than 60 chars

	char buf[100];

	do
	{
		auto b=std::begin(buf);
		auto e=b+70;

		b=std::copy(name.begin(), name.end(), b);
		*b++='*';

		b=std::to_chars(b, e, n).ptr;

		*b++='*';
		*b++=0;
		char *value_begin=b;

		if (n == 0)
		{
			b=std::copy(charset.begin(), charset.end(), b);
			*b++='\'';
			b=std::copy(language.begin(), language.end(), b);
			*b++='\'';
		}

		++n;

		while (b < e && vb < ve)
		{
			if (rfc2231_do_encode(*vb))
			{
				*b++='%';
				*b++="0123456789ABCDEF"[ (*vb >> 4) & 15];
				*b++="0123456789ABCDEF"[ *vb & 15];
			}
			else
				*b++=*vb;
			++vb;
		}

		*b=0;

		if constexpr(integral_return)
		{
			auto v=callback(buf, value_begin);

			if (v != 0)
				return v;
		}
		else
		{
			callback(buf, value_begin);
		}
	} while (vb < ve);

	if constexpr (integral_return)
	{
		return 0;
	}
}

/*
  C++ MIME parser.

  Summary:

  std::ostreambuf_iterator<char) b{input_stream};
  std::ostreambuf_iterator<char> e{};

  rfc2045::entity::line_iter<false>::iter parser{b, e}

  rfc2045::entity entity;

  entity.parse(parser);

  The iterator class's constructor takes a reference to an iterator to
  the beginning of an input sequence that defines a MIME message and an ending
  iterator value.

  The iterators must be passed by reference. parse() updates them, and normally
  the beginning iterator gets advanced to the ending iterator, unless a fatal
  parsing error occured.

  The iter class is a template, and the template parameters get deduced from
  the constructor's parameters. line_iter's template parameter must be
  explicit, the "false" value specifies that NL gets recognized as the newline
  sequence, "true" value specifies that CRNL gets recognized as the newline
  sequence of the MIME message.

  parse() reads the sequence that defines the MIME message and fills in the
  class's members that specify where the MIME entity's header and body
  starts and ends (header always starts at 0), and the number of lines in the
  header and the body.

  A MIME entity that contains other entities fills in the subentities class
  member with an entity for each sub-entity (whose headers will not obviously
  start at position 0 in the input sequence).

  iter.longquotedlinesize=1020;

  quoted-printable-encoded MIME entities may split logical lines into
  multiple physical lines. An "=" at the end of the line in a quoted-printable
  MIME entity indicates that the logical lines continues on the next line.

  longquotedlinesize sets the maximum number of characters in a logical line
  that continues into multiple physical lines.

  if (entity.haslongquotedline)

  A MIME entity has this flag set if it has a logical line that exceeds the
  longquotedlinesize characters. Long quoted lines have no effect on general
  MIME parsing, this is merely tracked and serves as an indication that the
  MIME entity should not be reencoded into 7bit or 8bit encoding because this
  will result in very long lines.

  if (entity.has8bitheader)

  The has8bitheader flag is set if the MIME entity's headers contain characters
  outside of the 7bit US-ASCII range. They are presumed to use UTF-8.

  if (entity.has8bitbody)

  The has8bitbody flag is set if the MIME entity's body contains characters
  outside of the 7-bit US-ASCII range.

  if (entity.has8bitcontentchar)

  The has8bitbody flag is set if the MIME entity's body contains characters
  outside of the 7-bit US-ASCII range. Additionally it is set if the MIME
  entity uses quoted-printable encoding and at least one encoded character
  outside of the US-ASCII 7 bit range.

  Note that subentities exist in the body portion of their mutlipart (and
  message) MIME entities. A subentity with has8bitheader or has8bitbody
  flag set results in its parent entity having has8bitbody flag set too,
  and has8bitcontentchar set for a subentity results in the parent entity's
  has8bitcontentchar set, too.

  Other rfc2045::entity members:

  startpos, endpos - character offsets in the input sequence that define the
  starting position, and one past the ending position of the MIME entity's
  header section. A blank line that separates a MIME entity header from its
  body is considered to be a part of the header.

  startbody, endbody - character offsets in the input sequence that define the
  starting position, and one past the ending position, of the MIME entity's
  body section.

  nlines, nbodylines - number of lines in the header and the body portion of
  the MIME entity. Ntoe - a MIME entity that does not have a trailing newline
  seuqnece ends with a partial line. This line is included in the count.

  errors - a bitmask of errors that occured while parsing. RFC2045_ERRFATAL is
  set when the error is fatal and parsing is aborted. parse() may return before
  the starting iterator becomes equal to the ending iterator.

  mime1 - indicates the presence of a "MIME-Version: 1.0" header, an explicit
  one, or an implicit one for a subentity of a MIME 1.0 entity.

  content_type, content_type_charset, content_transfer_encoding - corresponding
  parts of the header, converted to lowercase.
 */

class rfc2045::entity_info {
 public:
	// Sub-entities of multipart MIME entities
	//
	// message MIME entities will have a single subentity, the MIME
	// message itself, unless it uses a quoted-printable MIME encoding

	std::vector<rfc2045::entity> subentities;

	size_t	startpos=0,	/* Position where this entity's header begins */
		endpos=0,	/* Where it ends */
		startbody=0,	/* Where the body of the entity starts */
		endbody=0;	/* endpos - trailing CRLF terminator */
	size_t	nlines=0;	/* Number of lines in message */
	size_t	nbodylines=0;	/* Number of lines only in the body */

	typedef unsigned errors_t;
	errors_t errors=0;	/* RFC2045_ERR codes, here or subentities */
	bool	mime1=false;    /* Mime-Version: 1.0 in effect */

	cte content_transfer_encoding{cte::sevenbit};

	std::string content_type{"text/plain"}; /* content-type in lowercase */
	std::string content_type_charset{"iso-8859-1"};
	std::string content_type_boundary;

	bool has8bitheader{false};
	bool has8bitbody{false};
	bool has8bitcontentchar{false};
	bool haslongquotedline{false};

	bool multipart() const
	{
		size_t i=content_type.find('/');

		if (i > content_type.size())
			i=content_type.size();

		return std::string_view{content_type.data(), i} == "multipart";
	}
};

#define RFC2045_ERR8BITINQP		0x0010
#define RFC2045_ERRBADHEXINQP		0x0020
#define RFC2045_ERRUNDECLARED8BIT	0x0040
#define RFC2045_ERRWRONGBOUNDARY	0x0080
#define RFC2045_ERRLONGUNFOLDEDHEADER	0x0100
#define RFC2045_ERRUNKNOWNTE		0x0200
#define RFC2045_ERRINVALIDBASE64	0x0400
#define RFC2045_ERRFATAL		0x8000

/*
  Metadata that's tracked during parsing.

  Keeps track of MIME entities that are currently being parsed.

  Each new entity, when parsing starts, gets push_backed into parsing_entities
  and popped back when its parsing finishes.

*/

struct rfc2045::entity_parse_meta {
	// Currently parsing these entities.
	std::vector<entity *> parsing_entities;

	// How many entities in total were parsed already.
	unsigned short nparsed=0;

	// No EOF condition
	struct eof_no {};

	// Reached end of input
	struct eof_yes {};

	// Logical or physical EOF indication
	typedef std::variant<eof_no,
			     eof_yes,
			     entity * // Read multipart boundary delimiter here
			     > eof_t;

	// Stack guard object that takes care of adding and removing
	// pointers from the parsing_entities list.
	struct scope {
		entity_parse_meta &info;

		scope(entity_parse_meta &info, entity *e);

		~scope();
	};

	// Processed a line in the header portion of the currently open
	// MIME entity. Update metrics.

	void consumed_header_line(size_t c);

	// Processed a line in the body portion of the currently open
	// MIME entity. Update metrics.

	void consumed_body_line(size_t c);

	void report_error(rfc2045::entity_info::errors_t code);
	bool fatal_error();
};

class rfc2045::entity : public entity_info {

 public:

	template<typename iter>
	static void tolowercase(iter b, iter e)
	{
		std::for_each(b, e,
			      []
			      (char &c)
			      {
				      if (c >= 'A' && c <= 'Z')
				      {
					      c += 'a'-'A';
				      }
			      });
	}

	template<typename container>
	static inline void tolowercase(container &c)
	{
		tolowercase(c.begin(), c.end());
	}

	// Factory for iterators that use LF(false) or CRLF(true) newline
	// sequence.

	template<bool crlf> struct line_iter {
		template<typename beg_iter_type, typename end_iter_type>
		struct iter;
	};

	entity() {}

	// A parameter of a structured MIME header.
	struct header_parameter_value {

		// If RFC 2231 was used to encode the character set
		std::string charset;

		// If RFC 2231 was used to encode the language
		std::string language;

		// Finally, the value.
		std::string value;

		template<typename C, typename L, typename V>
		header_parameter_value(C && charset,
				       L && language,
				       V && value)
			: charset{std::forward<C>(charset)},
			  language{std::forward<L>(language)},
			  value{std::forward<V>(value)}
		{
		}

		bool operator==(const header_parameter_value &o) const
		{
			return charset == o.charset &&
				language == o.language &&
				value == o.value;
		}
	};

	// A structured MIME header, a value, and parameters, optionally encoded
	// using RFC 2231
	struct rfc2231_header {
		std::string value;
		std::unordered_map<std::string,
				   header_parameter_value> parameters;

		// Parses a folded header value.
		rfc2231_header(const std::string_view &);
	};

	template<typename line_iter_type>
	void parse(line_iter_type &iter);

	/*
	  Prepare to read the headers of the next mime section.

	  This is called immediately upon encountering an
	  rfc2045_message_content_type, or at the beginning of each multipart
	  sub-entity.
	*/

	template<typename line_iter_type>
	entity &start(line_iter_type &iter)
	{
		subentities.emplace_back();

		auto &subentity=subentities.back();

		// Initialize subentity positions appropriately. In case of a
		// rfc2045_message_content_type we just started its body,
		// and my startbody and endbody are the same. In case of a
		// multipart subentity, my endbody is where it starts, so
		// using endbody in either case gets this right.

		subentity.startpos=subentity.endpos=
			subentity.startbody=subentity.endbody=endbody;
		iter.entered_header();

		return subentity;
	}
};

template<bool crlf>
template <typename beg_iter_type, typename end_iter_type>
struct rfc2045::entity::line_iter<crlf>::iter : entity_parse_meta {

	beg_iter_type &b;
	end_iter_type &e;

	// Buffered contents of the current line being processed
	std::string buffer;

	// Maximum number of characters read in each line, the rest are skipped
	// (but counted).
	size_t longquotedlinesize=1020;

	size_t longunfoldedheadersize=(1024 * 10);
	// The maximum number of MIME entities that are parsed, and the
	// maximum nesting level.
	size_t mimeentityparselimit=300;
	size_t mimeentitynestedlimit=20;

	struct body_line{};
	struct header_line{};
	struct empty_header_line{};

	// What kind of a line was most recently read in.
	//
	// In the beginning, there was a header line...
	//
	// Then there was an empty header line...
	//
	// Finally, a body line followed...
	std::variant<body_line,
		     header_line,
		     empty_header_line> current_line_type{
		header_line{}
	};

	// ... until another genesis, another header for another MIME section.
	//
	// This is called from start(), upon creating a new MIME subentity,
	// so we're now reading headers.
	void entered_header()
	{
		current_line_type=header_line{};
	}

	// For convenience, entered_body() is also used in the testsuite
	void entered_body()
	{
		current_line_type=body_line{};
	}

	// Whether the current line is considered to be in the header section.
	bool in_header()
	{
		return !std::holds_alternative<body_line>(current_line_type);
	}

	iter(beg_iter_type &b,
	     end_iter_type &e)
		: b{b}, e{e}
	{
	}

	constexpr size_t eol_size() { return crlf ? 2:1; }

	// Have we reached logical or actual end of input?
	//
	// Inspects the current line to see if it matches
	// any currently open mime entity's boundary, otherwise
	// whether the underlying input's iterator reached
	// the ending iterator value.
	//
	// eof() caches the result of a search for an open multipart
	// entity with the matching boundary delimiter, we do it once.
	// The cache is cleared in consume_line().

	std::optional<entity *> cached_boundary_found;

	// Buffer for boundary searches
	std::string boundary_chk_buf;

	eof_t eof() {
		const auto &[p, q]=current_line();

		if (buffer.empty() && b == e)
			return eof_yes{};

		if (!cached_boundary_found)
		{
			entity *found_entity_boundary=nullptr;

			if (q-p > 2 &&
			    buffer[0] == '-' &&
			    buffer[1] == '-')
			{
				boundary_chk_buf.assign(p+2, q);

				tolowercase(boundary_chk_buf);

				for (auto e:parsing_entities)
				{
					auto &b=e->content_type_boundary;
					if (b.empty())
						continue;
					if (b.size() >
					    boundary_chk_buf.size())
					{
						continue;
					}

					if (b == std::string_view(
						    boundary_chk_buf.data(),
						    b.size()))
					{
						found_entity_boundary=e;
						break;
					}
				}
			}
			cached_boundary_found=found_entity_boundary;
		}

		if (*cached_boundary_found)
			return *cached_boundary_found;
		return eof_no{};
	}

	// Ensure that buffer has a complete line, including
	// EOL.
	//
	// Returns a tuple with an iterator to the beginning and an
	// iterator to the end of line character (the EOL character itself,
	// and not one past it).
	//
	// For \r\n EOL sequence this will be a pointer to \r
	//
	// Quietly prunes lines to 1024 characters. If
	// the line, including EOL, is less than 1024 chars
	// the returned ending iterator is not buffer.end(). a
	// buffer.end() return indicates a truncated line.
	//
	// The ending iterator is saved in cached_eol_iter,
	// and if this is called again it'll just return it,
	// saving all the work. consume_line() clears the
	// cached_eol_iter.

	std::optional<std::string::iterator> cached_eol_iter;

	std::tuple<std::string::iterator,
		   std::string::iterator> current_line() {

		if (cached_eol_iter)
			return { buffer.begin(), *cached_eol_iter };

		auto p=do_current_line();
		cached_eol_iter=p;

		auto b=buffer.begin();

		if (std::holds_alternative<header_line>(current_line_type) &&
		    b == p)
		{
			current_line_type=empty_header_line{};
		} else if (std::holds_alternative<empty_header_line>(
				   current_line_type
			   ))
		{
			entered_body();
		}

		return {b, p};
	}

	std::string::iterator do_current_line() {

		buffer.clear();

		char prev_ch=0;

		while (b != e)
		{
			if constexpr (!crlf)
			{
				if (buffer.size() == 1024)
					break;

				char c=*b++;

				buffer.push_back(c);
				if (c == '\n')
				{
					return --buffer.end();
				}
			}
			else
			{
				char c=*b;

				if (buffer.size() ==
				    (c == '\r' ? 1023:1024))
				{
					break;
				}

				++b;
				buffer.push_back(c);
				if (c == '\n' &&
				    prev_ch == '\r')
				{
					return buffer.end()-2;
				}
				prev_ch=c;
			}
		}

		return buffer.end();
	}

	// The current line is no longer looked at. consume_line(), then
	// update the header/body_position.

	size_t consume_line_and_update_position(cte encoding, bool &has8bit)
	{
		// Note where we are before calling consume_line.

		bool was_in_header=in_header();

		size_t c{consume_line(encoding, has8bit)};

		if (was_in_header)
			consumed_header_line(c);
		else
			consumed_body_line(c);

		return c;
	}

	// Remove the current_line() from the input.
	//
	// If the input line was truncated in current_line(),
	// quietly discards what was unread.
	//
	// Returns the number of characters in the current_line(),
	// including the EOL terminator (LF or CRLF), the
	// character count is accurate even if the next line
	// in buffer was truncated.

private:
	bool isnybble(char c)
	{
		return (c >= '0' && c <= '7') ||
			(c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F');
	}

	size_t unquoted_line_size{0};

	void check_qp(char prev_prev_ch, char prev_ch, char ch, bool &has8bit)
	{
		++unquoted_line_size;

		if (ch & 0x80)
			report_error(RFC2045_ERR8BITINQP);

		if (prev_prev_ch == '=')
		{
			unquoted_line_size -= 2;

			if (!isnybble(prev_ch) || !isnybble(ch))
			{
				report_error(RFC2045_ERRBADHEXINQP);
			}
		}

		if (prev_ch == '=' && ch > '7')
		{
			has8bit=true;
		}
	}

	void check_base64(char c)
	{
		if (c >= '0' && c <= '9')
			return;

		if (c >= 'A' && c <= 'Z')
			return;

		if (c >= 'a' && c <= 'z')
			return;

		switch (c) {
		case '+':
		case '/':
		case '=':
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			return;
		}

		report_error(RFC2045_ERRINVALIDBASE64);
	}

	void check_qp_toolong(char last_ch)
	{
		if (last_ch == '=')
		{
			// Don't count trailing =

			--unquoted_line_size;
			return;
		}

		if (unquoted_line_size > longquotedlinesize)
		{
			if (!parsing_entities.empty())
				// Sanity check
				parsing_entities.back()->haslongquotedline=true;
		}

		unquoted_line_size=0;
	}

	size_t consume_line(cte encoding, bool &has8bit)
	{
		const auto &[p,q]=current_line();

		cached_eol_iter.reset();
		cached_boundary_found.reset();

		char prev_prev_ch=0;
		char prev_ch=0;

		if (encoding == cte::qp)
		{
			for (auto i=p; i != q; ++i)
			{
				check_qp(prev_prev_ch, prev_ch, *i, has8bit);

				prev_prev_ch=prev_ch;
				prev_ch=*i;
			}
		}
		else
		{
			for (auto i=p; i != q; ++i)
			{
				if (*i & 0x80)
					has8bit=true;
				else if (encoding == cte::base64)
				{
					check_base64(*i);
				}
			}
		}

		if (q != buffer.end() ||
		    b == e)
		{
			// Not truncated.

			if (encoding == cte::qp)
			{
				check_qp_toolong(prev_ch);
			}

			size_t s=buffer.size();
			buffer.clear();
			return s;
		}

		// Truncated. Do more work, read until the
		// true EOL.

		size_t c=buffer.size();

		buffer.clear();

		while (1)
		{
			if (b == e)
			{
				if (encoding == cte::qp)
				{
					check_qp_toolong(prev_ch);
				}
				break;
			}

			char ch=*b++;
			++c;

			if (encoding == cte::qp)
			{
				check_qp(prev_prev_ch, prev_ch, ch, has8bit);
			}
			else
			{
				if (encoding == cte::base64)
				{
					check_base64(ch);
				}
				if (ch & 0x80)
					has8bit=true;
			}
			if ((!crlf || prev_ch == '\r') &&
			    ch == '\n')
			{
				if (encoding == cte::qp)
				{
					// Subtract the EOL

					unquoted_line_size -=
						(crlf ? 2:1);

					check_qp_toolong(
						crlf ? prev_prev_ch:prev_ch
					);
				}
				break;
			}
			prev_prev_ch=prev_ch;
			prev_ch=ch;
		}
		return c;

	}
public:

	// Return the current_line(), as long as we're still in the header
	// section, otherwise returns the same beginning/ending iterator,
	// simulating an empty line at the end of the header.

	std::tuple<std::string::iterator,
		   std::string::iterator> current_header_line()
	{
		if (std::holds_alternative<body_line>(current_line_type))
		{
			auto b=buffer.begin();

			return {b, b};
		}

		return current_line();
	}

	// Collect the next folded line into a std::string
	// line.
	//
	// Silently truncate the next folded line to 10kb
	//
	// Parse out the header name from the beginning of the
	// line, and convert to lowercase. Returns string_views
	// for the header name and contents.
	std::tuple<std::string_view,
		   std::string_view>
		next_folded_header_line(entity &e,
					std::string &line)
	{
		line.clear();

		if (!std::holds_alternative<eof_no>(eof()))
		{
			// We are getting a real EOF indication, or
			// a MIME boundary delimitered. Officially enter
			// the body part of the MIME entity, and call it quits.
			entered_body();
			return std::tuple("","");
		}

		auto beg_end_line=current_header_line();

		auto &[bp, ep]=beg_end_line;

		line.insert(line.end(), bp, ep);

		while (1)
		{
			// Here, we're looking either at the first physical,
			// line, read above, or at a continuation line, read
			// below, and looped back here.
			//
			// If we just read an empty line, as the first physical
			// line, we're done. However, if we already read the
			// the first physical line, and it was not empty, we
			// want to stop before accounting for the empty line,
			// so that the next time here we will return with an
			// empty line.

			if (bp == ep && !line.empty())
				break;

			consume_line_and_update_position(cte::sevenbit,
							 e.has8bitheader);

			if (!std::holds_alternative<eof_no>(
				    eof()
			    ))
			{
				// Before returning, we enter the body
				// portion, as a formality.
				entered_body();
				break; // No need to check the line, it's empty.
			}

			// Peak at the next line, is it still a header, and
			// does it begin with whitespace?
			beg_end_line=current_header_line();

			auto s=bp;

			while (bp != ep && (
				       *bp == ' ' || *bp == '\r'
				       || *bp == '\n'
				       || *bp == '\t'))
			{
				++bp;
			}

			if (s == bp)
				break;

			// Back up, the last whitespace will
			// be a space. Do the accounting
			// based on the included space.

			--bp;

			size_t l=ep-bp;

			if (l+buffer.size() > longunfoldedheadersize)
			{
				l=longunfoldedheadersize-buffer.size();
				report_error(RFC2045_ERRLONGUNFOLDEDHEADER);
			}
			// The first character is always a space

			if (l)
			{
				line.push_back(' ');
				++bp;
				--l;
			}
			line.insert(line.end(), bp, bp+l);
		}

		auto lb=line.begin(), le=line.end(),
			p=std::find(lb, le, ':');

		tolowercase(lb, p);
		auto q=std::find_if(
			p, le,
			[]
			(char &c)
			{
				return c != ':' &&
					c != ' ' &&
					c != '\t' &&
					c != '\r';
			});

		while (le > q)
		{
			switch(le[-1]) {
			case ' ':
			case '\t':
			case '\r':
				--le;
				continue;
			}
			break;
		}
		return std::tuple(
			std::string_view(line.c_str(), p-lb),
			std::string_view(line.c_str()+(
						 q-lb
					 ), le-q));
	}
};


template<typename line_iter_type>
void rfc2045::entity::parse(line_iter_type &iter)
{
	entity_parse_meta::scope scope{iter, this};

	std::string line;

	while (iter.in_header())
	{
		auto [name, header]=iter.next_folded_header_line(*this, line);

		if (name == "mime-version")
		{
			auto b=header.begin(), e=header.end();

			if (b < e && *b == '1' && ++b < e && *b == '.')
			{
				mime1=true;
			}
		}

		if (name == "content-type")
		{
			rfc2231_header ct{header};

			content_type=ct.value;

			auto p=ct.parameters.find("charset");
			if (p != ct.parameters.end())
			{
				content_type_charset=p->second.value;

				tolowercase(content_type_charset);
			}

			p=ct.parameters.find("boundary");
			if (p != ct.parameters.end())
			{
				content_type_boundary=p->second.value;
				tolowercase(content_type_boundary);
			}
		}
		if (name == "content-transfer-encoding")
		{
			content_transfer_encoding=to_cte(
				header.size() ? *header.data():0);

			if (content_transfer_encoding==cte::error)
			{
				iter.report_error(RFC2045_ERRUNKNOWNTE
						  | RFC2045_ERRFATAL);
				return;
			}
		}
	}

	startbody=endbody=endpos;

	bool is_multipart=false;

	// Bail out of the MIME complexity is unreasonable, indicates a
	// possible attack.

	if (++iter.nparsed > iter.mimeentityparselimit ||
	    iter.parsing_entities.size() > iter.mimeentitynestedlimit)
	{
		iter.report_error(RFC2045_ERR2COMPLEX |
				  RFC2045_ERRFATAL);
		return;
	}
	else if (mime1)
	{
		if (rfc2045_message_content_type(content_type.c_str()) &&
		    (content_transfer_encoding == cte::sevenbit ||
		     content_transfer_encoding == cte::eightbit))
		{
			start(iter).parse(iter);
			return;
		}

		if (multipart())
		{
			size_t l=content_type_boundary.size();
			auto lp=content_type_boundary.data();

			if (l == 0)
			{
				iter.report_error(
					RFC2045_ERRBADBOUNDARY |
					RFC2045_ERRFATAL);
				return;
			}

			// Sanity check: unique boundaries.
			is_multipart=true;

			for (auto &e:iter.parsing_entities)
			{
				if (e == this)
					continue; // Last one is us

				auto &eb=e->content_type_boundary;
				size_t m=eb.size();

				if (m == 0) // message/subtype
					continue;

				if (m > l)
					m=l;
				if (std::equal(lp, lp+m, eb.data()))
				{
					iter.report_error(
						RFC2045_ERRBADBOUNDARY |
						RFC2045_ERRFATAL
					);
					return;
				}
			}
		}
	}

	bool multipart_ongoing=false;

	if (!is_multipart)
		content_type_boundary.clear(); // Get rid of any strays...
	else
	{
		if (content_transfer_encoding == cte::base64)
		{
			iter.report_error(
				RFC2045_ERRINVALIDBASE64 |
				RFC2045_ERRFATAL
			);
		}
		multipart_ongoing=true;
		content_transfer_encoding=cte::eightbit; // Meaningless
	}

	// So, if this is a multipart entity, we start scanning for boundary
	// delimiters.

	while (multipart_ongoing)
	{
		auto status=iter.eof();

		entity **boundary_ptr;

		while ((boundary_ptr=std::get_if<entity *>(&status)) != nullptr
		       && !iter.fatal_error())
		{
			// If we see a parent entity's boundary, we've got
			// problems.

			if (*boundary_ptr != this)
			{
				iter.report_error(
					RFC2045_ERRWRONGBOUNDARY |
					RFC2045_ERRFATAL);
				return;
			}

			// Adjust endbody of last subentity to be /before/
			// the trailing crlf, according to MIME spec,
			// and recursively do the adjustment to its last
			// subentity, if there is one.

			auto adjusted_endbody=endbody - iter.eol_size();

			auto *se= &subentities;

			while (!se->empty())
			{
				auto &last_se=se->end()[-1];

				if (adjusted_endbody < last_se.endbody)
					last_se.endbody=adjusted_endbody;

				se= &last_se.subentities;
			}

			// Process the boundary line.

			auto line=iter.current_line();
			auto &[b, e]=line;

			// We know that the line starts with --, at least.

			b=std::find(b+2, e, '-');

			if (e-b >= 2 && *b == '-' && b[1] == '-')
			{
				// Terminating boundary delimiter. We'll
				// consume it, below, then swallow things up
				// until the end of this entity.

				multipart_ongoing=false;
				break;
			}

			iter.consume_line_and_update_position(
				cte::sevenbit,
				has8bitbody);

			auto &new_subentity=start(iter);

			new_subentity.mime1=true;

			if (content_type == "multipart/digest")
				new_subentity.content_type="message/rfc822";

			new_subentity.parse(iter);

			if (iter.fatal_error())
				return;
			status=iter.eof();
		}
		if (std::holds_alternative<entity_parse_meta::eof_yes>(status))
			return;

		if (iter.fatal_error())
			break;

		iter.consume_line_and_update_position(cte::sevenbit,
						      has8bitbody);
	}

	while (std::holds_alternative<entity_parse_meta::eof_no>(iter.eof()))
	{
		iter.consume_line_and_update_position(
			content_transfer_encoding,
			*(content_transfer_encoding == cte::qp ?
			  &has8bitcontentchar:&has8bitbody)
		);
	}
}

/* Push interface for the rfc2045 parser

   Push content to parse via rfc2045 one chunk at a time.

   A separate execution thread gets started, which invokes parse(), and
   the parsed chunks are fed to it.

   entity_parser<bool> parser;

   parser.parse(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>{});

   entity e=parser.parsed_entity();

   The template parameter specifies whether the MIME entity uses LF (false)
   or CRLF (true) newline sequences.

   After constructing parse() can be called repeatedly to define the MIME
   entity to parse. Each call to parse() might block to wait for the
   execution thread to finish parsing the previous block.

   After the entire MIME entity is parse()d, parsed_entity() stops the
   execution thread and returns the parsed entity.
 */

class rfc2045::entity_parser_base {

protected:
	entity entity_getting_parsed;
	std::thread parsing_thread;
private:

	std::mutex m;
	std::condition_variable c;

	bool thread_finished{false};
	bool end_of_parse{false};

	bool has_content_to_parse{false};
	std::string content_to_parse;

public:
	entity_parser_base();
	~entity_parser_base();


#ifndef RFC2045_ENTITY_PARSER_TEST
#define RFC2045_ENTITY_PARSER_TEST(a) do {} while (0)
#define RFC2045_ENTITY_PARSER_DECL(a) do {} while (0)
#endif

	template<typename beg_iter, typename end_iter>
	void parse(beg_iter &&b, end_iter &&e)
	{
		std::unique_lock lock{m};

		c.wait(lock,
		       [this]
		       {
			       return thread_finished || end_of_parse ||
				       !has_content_to_parse;
		       });

		if (thread_finished || end_of_parse)
		{
			RFC2045_ENTITY_PARSER_TEST("parser: chunk (ignored)");
			return;
		}

		content_to_parse.clear();

		std::copy(b, e, std::back_inserter(content_to_parse));
		has_content_to_parse=true;
		RFC2045_ENTITY_PARSER_TEST("parser: chunk");
		c.notify_all();
	}

	// Used internally to retrieve the next chunk to parse
	bool get_next_chunk(std::string &chunk);

	entity &&parsed_entity();
};

template<bool crlf>
class rfc2045::entity_parser : entity_parser_base {

public:
	entity_parser();
	~entity_parser();

	using entity_parser_base::parse;
	using entity_parser_base::parsed_entity;
};

extern template class rfc2045::entity_parser<false>;
extern template class rfc2045::entity_parser<true>;

#endif

#endif
