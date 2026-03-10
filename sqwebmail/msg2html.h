#ifndef msg2html_h
#define msg2html_h

#include "config.h"
#include "rfc822/rfc822.h"
#include "rfc2045/rfc2045.h"

#include <string>
#include <string_view>
#include <functional>
#include <tuple>
#include <vector>
#include <unordered_set>

// Tuple:
//           ":->"
//           "<img src=\"...\">"
typedef std::tuple<std::string, std::string> smiley;

typedef std::unordered_set<char> smiley_index_t;

struct msg2html_info {
	const char *output_character_set=nullptr;
	/* Required - generate output in this character set */

	void *arg=nullptr; /* Passthrough parameter to callback functions */

	const char *mimegpgfilename=nullptr;
	/*
	** If not NULL, msg2html() receives GPG-decoded message read from
	** mimegpgfilename.  The contents of mimegpgfilename are blindly
	** appended to href links to multipart/related content.
	*/

	const char *gpgdir=nullptr;
	/*
	** If not NULL -- points to the .gpg configuration directory.
	*/

	int fullheaders=0; /* Flag: show all headers */

	int noflowedtext=0; /* Flag: do not show flowed text */

	int showhtml=0; /* Flag: show HTML content */

	int is_gpg_enabled=0;
	/* True: check for decrypted content, and format it accordingly */

	int is_preview_mode=0;
	/* True: sqwebmail is showing a draft message in preview mode */

	std::function<std::string (const char *mimeid)> get_url_to_mime_part;
	/*
	** Return a malloced buffer with a URL that would point to the
	** message's indicated MIME part.
	*/

	void (*charset_warning)(std::string_view, void *)=nullptr;
	/* If not NULL - content in this character set could not be converted */

	void (*html_content_follows)()=nullptr;
	/* If not NULL - HTML content follows */

	std::string wash_http_prefix;
	/* Prepended to http: URLs, which get encoded */

	std::string wash_mailto_prefix;
	/* Prepended to mailto: URLs */

	void (*message_rfc822_action)(std::string_view idptr)=nullptr;
	/*
	** idpart references a message/rfc822 attachment.  Emit HTML
	** for the usual actions (reply, forward, etc...)
	*/

	void (*inline_image_action)(std::string_view idptr,
				    std::string_view content_type,
				    void *arg)=nullptr;
	/* Inline image attachment */

	void (*application_pgp_keys_action)(
		std::string_view idptr,
		std::string_view content_description
	)=nullptr;
	/* Attached PGP keys */

	void (*unknown_attachment_action)(std::string_view idptr,
					  std::string_view content_type,
					  std::string_view content_name,
					  off_t size,
					  void *arg)=nullptr;

	void (*gpg_message_action)()=nullptr;
	/*
	** This message contains MIME/PGP content.  Post the appropriate
	** notice.
	*/

	/* Mark the beginning and end of an E-mail address in mail headers: */
	void (*email_address_start)(const char *name, const char *addr)=nullptr;
	void (*email_address_end)()=nullptr;

	/*
	** Format a mail header. (*format_header) should invoke
	** (*cb_func) with a pointer to whatever should be displayed, which
	** may be just hdrname (which is the default behavior.
	*/

	void (*email_header)(std::string_view,
			     void (*cb_func)(std::string_view))=nullptr;

	/*
	** Return strftime() format string for dates. 'def' is the
	** proposed default.  The default implementation simply returns
	** the default string.
	*/

	const char *(*email_header_date_fmt)(const char *def)=nullptr;

	/*
	** Return HTML code for rendering a URL, if the URL scheme is
	** recognized.  The HTML code is returned in an malloc-ed buffer:
	*/

	std::function<std::string (std::string_view url,
				   std::string_view disp_url)> get_textlink;

	std::vector<smiley> smileys;
	smiley_index_t smiley_index;
};

struct msg2html_info *msg2html_alloc(const char *charset);
void msg2html_add_smiley(struct msg2html_info *i,
			 const char *txt, const char *imgurl);

void msg2html_free(struct msg2html_info *);

void msg2html(std::streambuf &,
	      const rfc2045::entity &, struct msg2html_info *info);

void msg2html_download(std::streambuf &fd,
		       const char *mimeid, int dodownload,
		       const char *system_charset);

void msg2html_showmimeid(std::string_view idptr, const char *p);

/*
** INTERNAL
*/

struct msg2html_textplain_info;

struct msg2html_textplain_info *
msg2html_textplain_start(const char *message_charset,
			 const char *output_character_set,
			 bool isflowed,
			 bool isdelsp,
			 const std::function<
			 std::string (std::string_view url,
				      std::string_view disp_url)
			 > &get_textlink,

			 const smiley_index_t *smiley_index,
			 const std::vector<smiley> *smileys,
			 int wikifmt,

			 void (*output_func)(const char *p,
					     size_t n, void *arg),
			 void *arg);

void msg2html_textplain(struct msg2html_textplain_info *info,
			const char *ptr,
			size_t cnt);

int msg2html_textplain_end(struct msg2html_textplain_info *info);

#endif
