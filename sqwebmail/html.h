/*
*/
#ifndef	html_h
#define	html_h

#include	<courier-unicode.h>
#include <string>

/*
** Copyright 2011-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

struct htmlfilter_info;

extern struct htmlfilter_info
*htmlfilter_alloc(void (*)(const char32_t *, size_t, void *), void *);
extern void htmlfilter_free(struct htmlfilter_info *);

extern void htmlfilter(struct htmlfilter_info *,
		       const char32_t *, size_t);

extern void htmlfilter_set_contentbase(struct htmlfilter_info *,
				   const char *);

extern void htmlfilter_set_http_prefix(struct htmlfilter_info *,
				       const char *);
extern void htmlfilter_set_mailto_prefix(struct htmlfilter_info *,
				      const char *);
extern void htmlfilter_set_convertcid(struct htmlfilter_info *,
				      std::function<std::string (const char *)>
				);
#endif
