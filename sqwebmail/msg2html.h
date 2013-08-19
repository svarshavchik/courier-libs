#ifndef msg2html_h
#define msg2html_h

#include "config.h"
#include "rfc822/rfc822.h"
#include "rfc2045/rfc2045.h"

#include <stdio.h>
#include <string.h>

struct msg2html_smiley_list {
	struct msg2html_smiley_list *next;
	char *code;
	char *url;
};


struct msg2html_info {
	const char *output_character_set;
	/* Required - generate output in this character set */

	void *arg; /* Passthrough parameter to callback functions */

	const char *mimegpgfilename;
	/*
	** If not NULL, msg2html() receives GPG-decoded message read from
	** mimegpgfilename.  The contents of mimegpgfilename are blindly
	** appended to href links to multipart/related content.
	*/

	const char *gpgdir;
	/*
	** If not NULL -- points to the .gpg configuration directory.
	*/

	int fullheaders; /* Flag: show all headers */

	int noflowedtext; /* Flag: do not show flowed text */

	int showhtml; /* Flag: show HTML content */

	int is_gpg_enabled;
	/* True: check for decrypted content, and format it accordingly */

	int is_preview_mode;
	/* True: sqwebmail is showing a draft message in preview mode */

	char *(*get_url_to_mime_part)(const char *mimeid,
				      void *arg);
	/*
	** Return a malloced buffer with a URL that would point to the
	** message's indicated MIME part.
	*/

	void (*charset_warning)(const char *, void *);
	/* If not NULL - content in this character set could not be converted */

	void (*html_content_follows)();
	/* If not NULL - HTML content follows */

	const char *wash_http_prefix;
	/* Prepended to http: URLs, which get encoded */

	const char *wash_mailto_prefix;
	/* Prepended to mailto: URLs */

	void (*message_rfc822_action)(struct rfc2045id *idptr);
	/*
	** idpart references a message/rfc822 attachment.  Emit HTML
	** for the usual actions (reply, forward, etc...)
	*/

	void (*inline_image_action)(struct rfc2045id *idptr,
				    const char *content_type,
				    void *arg);
	/* Inline image attachment */

	void (*application_pgp_keys_action)(struct rfc2045id *id);
	/* Attached PGP keys */

	void (*unknown_attachment_action)(struct rfc2045id *idptr,
					  const char *content_type,
					  const char *content_name,
					  off_t size,
					  void *arg);

	void (*gpg_message_action)();
	/*
	** This message contains MIME/PGP content.  Post the appropriate
	** notice.
	*/

	/* Mark the beginning and end of an E-mail address in mail headers: */
	void (*email_address_start)(const char *name, const char *addr);
	void (*email_address_end)();

	/*
	** Format a mail header. (*format_header) should invoke
	** (*cb_func) with a pointer to whatever should be displayed, which
	** may be just hdrname (which is the default behavior.
	*/

	void (*email_header)(const char *hdrname,
			     void (*cb_func)(const char *));

	/*
	** Return strftime() format string for dates. 'def' is the
	** proposed default.  The default implementation simply returns
	** the default string.
	*/

	const char *(*email_header_date_fmt)(const char *def);

	/*
	** Return HTML code for rendering a URL, if the URL scheme is
	** recognized.  The HTML code is returned in an malloc-ed buffer:
	*/
	char *(*get_textlink)(const char *url, void *arg);

	struct msg2html_smiley_list *smileys;
	char smiley_index[50];
};

struct msg2html_info *msg2html_alloc(const char *charset);
void msg2html_add_smiley(struct msg2html_info *i,
			 const char *txt, const char *imgurl);

void msg2html_free(struct msg2html_info *);

void msg2html(FILE *fp, struct rfc2045 *rfc, struct msg2html_info *info);

void msg2html_download(FILE *fp, const char *mimeid, int dodownload,
		       const char *system_charset);

void msg2html_showmimeid(struct rfc2045id *idptr, const char *p);

/*
** INTERNAL
*/

struct msg2html_textplain_info;

struct msg2html_textplain_info *
msg2html_textplain_start(const char *message_charset,
			 const char *output_character_set,
			 int isflowed,
			 int isdelsp,
			 int isdraft,
			 char *(*get_textlink)(const char *url, void *arg),
			 void *get_textlink_arg,

			 const char *smiley_index,
			 struct msg2html_smiley_list *smileys,
			 int wikifmt,

			 void (*output_func)(const char *p,
					     size_t n, void *arg),
			 void *arg);

void msg2html_textplain(struct msg2html_textplain_info *info,
			const char *ptr,
			size_t cnt);

int msg2html_textplain_end(struct msg2html_textplain_info *info);

#endif

