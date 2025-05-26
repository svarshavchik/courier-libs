/*
** Copyright 2025 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/
#if    HAVE_CONFIG_H
#include       "rfc2045_config.h"
#endif
#include       <stdlib.h>
#include       <string.h>
#if    HAVE_STRINGS_H
#include       <strings.h>
#endif
#include	"rfc2045.h"

/*
** Whether this MIME content type is a nested MIME message.
*/

int rfc2045_message_content_type(const char *content_type)
{
	return strcasecmp(content_type, RFC2045_MIME_MESSAGE_RFC822) == 0 ||
		strcasecmp(content_type, RFC2045_MIME_MESSAGE_GLOBAL) == 0;
}

/*
** Whether this MIME content type is a delivery status notification.
*/

int rfc2045_delivery_status_content_type(const char *content_type)
{
	return strcasecmp(content_type,
		      RFC2045_MIME_MESSAGE_DELIVERY_STATUS) == 0 ||
		strcasecmp(content_type,
		       RFC2045_MIME_MESSAGE_GLOBAL_DELIVERY_STATUS) == 0;
}

int rfc2045_message_headers_content_type(const char *content_type)
{
	return strcasecmp(content_type,
			  RFC2045_MIME_MESSAGE_HEADERS) == 0 ||
		strcasecmp(content_type,
			   RFC2045_MIME_MESSAGE_GLOBAL_HEADERS) == 0;
}
