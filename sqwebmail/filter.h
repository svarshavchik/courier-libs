/*
*/
#ifndef	filter_h
#define	filter_h

/*
** Copyright 1998 - 2011 S. Varshavchik.  See COPYING for
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

#include	<vector>

#include	<courier-unicode.h>

class filter_info {

	static int converted_text(const char *, size_t, void *);

	unicode_convert_handle_t handle;

	unicode_lb_info_t lb_handle{nullptr};
	std::vector<char32_t> pending_text;
	size_t pending_lb_cnt{0};

	static int lb_callback(int, void *);
	char32_t prevchar{'\n'};

	void (*handler_func)(const char *, size_t, void *);
	void *func_arg;

	size_t linesize;

	void flush_word();
	void do_flush_word(size_t);
	void do_flush_word_chunk(size_t, size_t);
public:
	bool conversion_error=false;

	filter_info(const char *,
		    void (*)(const char *, size_t, void *), void *);

	~filter_info();

	void operator()(const char32_t *, size_t);
	void	passthru(const char32_t *ptr, size_t cnt);
	void flush();

};

#endif
