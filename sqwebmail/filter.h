/*
*/
#ifndef	filter_h
#define	filter_h

/*
** Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
	The filter set of function is used to format message text for
	display. The message text gets converted from unicode to the
	display character set, and excessively long lines get truncated to
	some reasonable value.

	Special characters: <, >, &, and " get replaced by their HTML
	entities.


	filter(ptr, cnt) - repeated calls to this function are used to
	supply text being filtered.

	filter_end() - is called when the end of the text being filtered
	is reached.

*/

#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<stdlib.h>

#include	<courier-unicode.h>

struct filter_info {
	unicode_convert_handle_t handle;

	int conversion_error;

	char32_t prevchar;

	size_t u_w;

	void (*handler_func)(const char *, size_t, void *);
	void *func_arg;

	size_t linesize;
};

void	filter_start(struct filter_info *, const char *,
		     void (*)(const char *, size_t, void *), void *);
void	filter(struct filter_info *,
	       const char32_t *, size_t);
void	filter_passthru(struct filter_info *info,
			const char32_t *ptr, size_t cnt);
void	filter_end(struct filter_info *info);

#endif
