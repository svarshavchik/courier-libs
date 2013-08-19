#ifndef mimegpgheader_h
#define mimegpgheader_h
/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct header {
	struct header *next;
	char *header;
} ;

struct read_header_context {
	int continue_header;
	int header_len;
	struct header *first, *last;
} ;

void libmail_readheader_init(struct read_header_context *);
int libmail_readheader(struct read_header_context *, const char *);
struct header *libmail_readheader_finish(struct read_header_context *);
#define READ_START_OF_LINE(cts) ((cts).continue_header == 0)

void libmail_header_free(struct header *p);
struct header *libmail_header_find(struct header *p, const char *n);
const char *libmail_header_find_txt(struct header *p, const char *n);

struct mime_header {
	char *header_name;
	struct mime_attribute *attr_list;
} ;

struct mime_attribute {
	struct mime_attribute *next;
	char *name, *value;
} ;

void libmail_mimeheader_free(struct mime_header *);
struct mime_header *libmail_mimeheader_parse(const char *);
const char *libmail_mimeheader_getattr(struct mime_header *, const char *);

#ifdef  __cplusplus
} ;
#endif

#endif
