/*
*/
#ifndef	html_h
#define	html_h

#include	"unicode/unicode.h"

/*
** Copyright 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/

struct htmlfilter_info;

extern struct htmlfilter_info
*htmlfilter_alloc(void (*)(const unicode_char *, size_t, void *), void *);
extern void htmlfilter_free(struct htmlfilter_info *);

extern void htmlfilter(struct htmlfilter_info *,
		       const unicode_char *, size_t);

extern void htmlfilter_set_contentbase(struct htmlfilter_info *,
				   const char *);

extern void htmlfilter_set_http_prefix(struct htmlfilter_info *,
				       const char *);
extern void htmlfilter_set_mailto_prefix(struct htmlfilter_info *,
				      const char *);
extern void htmlfilter_set_convertcid(struct htmlfilter_info *,
				      char *(*)(const char *, void *), void *);

#endif
