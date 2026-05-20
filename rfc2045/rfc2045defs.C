/*
** Copyright 2025 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/
#include	"rfc2045.h"
#include	<courier-unicode.h>
#include	<algorithm>

static bool ciequal(std::string_view &a, std::string_view lbl)
{
	return std::equal(a.data(), a.data()+a.size(),
			  lbl.data(), lbl.data()+lbl.size(),
			  []
			  (unsigned char a, unsigned char b)
			  {
				  return unicode_lc(a) == unicode_lc(b);
			  });
}
/*
** Whether this MIME content type is a nested MIME message.
*/

bool rfc2045::message_content_type(std::string_view content_type)
{
	return ciequal(content_type, RFC2045_MIME_MESSAGE_RFC822) ||
		ciequal(content_type, RFC2045_MIME_MESSAGE_GLOBAL);
}

/*
** Whether this MIME content type is a delivery status notification.
*/

bool rfc2045::delivery_status_content_type(std::string_view content_type)
{
	return ciequal(content_type,
		       RFC2045_MIME_MESSAGE_DELIVERY_STATUS) ||
		ciequal(content_type,
			RFC2045_MIME_MESSAGE_GLOBAL_DELIVERY_STATUS);
}

bool rfc2045::message_headers_content_type(std::string_view content_type)
{
	return ciequal(content_type,
		       RFC2045_MIME_MESSAGE_HEADERS) ||
		ciequal(content_type,
			RFC2045_MIME_MESSAGE_GLOBAL_HEADERS);
}
