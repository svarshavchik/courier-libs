#include "config.h"
/*
*/
#include	<string.h>
#include	<time.h>
#include	"sqwebmail.h"
#include	<courier-unicode.h>

extern const char *sqwebmail_system_charset;
extern const char *sqwebmail_content_charset;

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
#if	USE_LIBCHARSET || HAVE_LANGINFO_CODESET

size_t strftime_unicode(char *s, size_t max, const char *fmt,
			const struct tm *tm)
{
	char sbuf[128] = "\0";
	char *buf;

	if (sqwebmail_system_charset && *sqwebmail_system_charset
	    && sqwebmail_content_charset && *sqwebmail_content_charset
	    && strcasecmp(sqwebmail_system_charset, "ASCII"))
	{
		int err;
		char *sfmt=unicode_convert_tobuf(fmt,
						   sqwebmail_content_charset,
						   sqwebmail_system_charset,
						   &err);

		if (sfmt && err)
		{
			free(sfmt);
			sfmt=0;
		}

		if (sfmt)
		{
			strftime(sbuf, sizeof(sbuf), sfmt, tm);
			sbuf[sizeof(sbuf)-1] = 0;
			free(sfmt);

			buf=unicode_convert_tobuf(sbuf,
						    sqwebmail_system_charset,
						    sqwebmail_content_charset,
						    &err);

			if (buf && err)
			{
				free(buf);
				buf=0;
			}

			if (buf)
			{
				strncpy(s, buf, max);
				free(buf);
			}
			else
			{
				strncpy(s, sbuf, max);
			}
			return strlen(s);
		}
	}

	return strftime(s, max, fmt, tm);
}

#endif
#endif
#endif

