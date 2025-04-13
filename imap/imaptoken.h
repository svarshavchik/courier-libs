#ifndef	imaptoken_h
#define	imaptoken_h

/*
** Copyright 1998 - 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<time.h>

#include	<string>
#include	"imapflags.h"

struct imaptoken_buf {
	short	tokentype;
	unsigned long tokennum;
	std::string tokenbuf;
	} ;

typedef imaptoken_buf *imaptoken;

#define	IT_ATOM			0
#define	IT_NUMBER		1
#define	IT_QUOTED_STRING	2
#define	IT_LITERAL_STRING_START	3
#define	IT_LPAREN		4
#define	IT_RPAREN		5
#define	IT_NIL			6
#define	IT_ERROR		7
#define	IT_EOL			8
#define	IT_LBRACKET		9
#define	IT_RBRACKET		10
#define	IT_LITERAL8_STRING_START 11

imaptoken nexttoken(void);
imaptoken currenttoken(void);
imaptoken nexttoken_nouc(void);
imaptoken nexttoken_noparseliteral(void);
imaptoken nexttoken_okbracket(void);
imaptoken nexttoken_nouc_okbracket(void);
void convert_literal_tokens(imaptoken tok);

int ismsgset(imaptoken);
	/* See if this token is a syntactically valid message set */
int ismsgset_str(const std::string &);
	/* Ditto */

void read_timeout(time_t);
void read_eol();
void unread(int);

extern size_t doread(char *buf, size_t bufsiz);
extern char *imap_readptr;
extern size_t imap_readptrleft;
extern void readfill();
extern void readflush();

#define	READ()	( imap_readptrleft ? \
	(--imap_readptrleft, (int)(unsigned char)*imap_readptr++): \
	(readfill(), --imap_readptrleft, (int)(unsigned char)*imap_readptr++))

void read_string(char **, unsigned long *, unsigned long);


struct imapkeywords {
	struct imapflags flags;

	struct imapkeyword *first_keyword, *last_keyword;
};

/* ATOMS have the following maximum length */

#define	IT_MAX_ATOM_SIZE	16384

char *my_strdup(const char *s);


/* SMAP */

void smap_readline(char *buffer, size_t bufsize);

#endif
