/*
*/
#ifndef	newmsg_h
#define	newmsg_h

#if	HAVE_CONFIG_H
#undef	PACKAGE
#undef	VERSION
#include	"config.h"
#endif
#include	<courier-unicode.h>
/*
** Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include	<stdlib.h>

extern void newmsg_init(const char *, const char *);
extern void newmsg_do(const char *);

extern char *newmsg_createdraft_do(const char *, const char *, int);
#define NEWMSG_SQISPELL	1
#define NEWMSG_PCP	2


struct wrap_info {
	const char *output_chset;
	void (*output_func)(const char *p, size_t l, void *arg);
	void *arg;

	const char32_t *uc;
	size_t ucsize;

	size_t cur_index;
	size_t word_start;
	size_t word_width;

	size_t line_start;
	size_t line_width;
};

void wrap_text_init(struct wrap_info *uw,
		    const char *output_chset,
		    void (*output_func)(const char *p, size_t l, void *arg),
		    void *arg);

void wrap_text(struct wrap_info *uw,
	       const char *newmsg,
	       size_t newmsg_size);


/*
** Format flowed text format-encoded message for editing.
*/

struct show_textarea_info {

	size_t (*handler)(struct show_textarea_info *, const char *, size_t);
	int seen_sig;
	size_t sig_index;

	int stop_at_sig;
};

void show_textarea_init(struct show_textarea_info *info,
			int stop_at_sig);

void show_textarea(struct show_textarea_info *info,
		   const char *ptr, size_t cnt);

#endif
