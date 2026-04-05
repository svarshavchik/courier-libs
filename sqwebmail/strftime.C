#include "config.h"
/*
*/
#include	<string.h>
#include	<time.h>
#include	"sqwebmail.h"
#include	<courier-unicode.h>

#if	HAVE_LOCALE_H
#if	HAVE_SETLOCALE
#if	USE_LIBCHARSET || HAVE_LANGINFO_CODESET

size_t strftime_unicode(char *s, size_t max, const char *fmt,
			const struct tm *tm)
{
	char sbuf[128] = "\0";

	if (!sqwebmail_system_charset.empty()
	    && !sqwebmail_content_charset.empty()
	    && strcasecmp(sqwebmail_system_charset.c_str(), "ASCII"))
	{
		bool errflag;
		auto sfmt=unicode::iconvert::convert(
			fmt,
			sqwebmail_content_charset,
			sqwebmail_system_charset,
			errflag);

		if (!errflag)
		{
			strftime(sbuf, sizeof(sbuf), sfmt.c_str(), tm);
			sbuf[sizeof(sbuf)-1] = 0;

			auto buf=unicode::iconvert::convert(
						    sbuf,
						    sqwebmail_system_charset,
						    sqwebmail_content_charset,
						    errflag);

			if (!errflag)
			{
				strncpy(s, buf.c_str(), max-1);
				s[max-1]=0;
				return strlen(s);
			}
		}
	}

	return strftime(s, max, fmt, tm);
}
#endif
#endif
#endif

