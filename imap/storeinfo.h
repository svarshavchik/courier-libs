#ifndef	storeinfo_h
#define	storeinfo_h

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"imapflags.h"
#include	"imapscanclient.h"
#include	"numlib/numlib.h"

#include	<vector>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {none=0, plus='+', minus='-'} plusminus_t;

struct storeinfo {
	plusminus_t plusminus{plusminus_t::none};
	bool silent{false};
	imapflags flags;
	mail::keywords::list keywords;
} ;

bool storeinfo_init(struct storeinfo &);
int do_store(unsigned long, int, storeinfo *);

int do_copy_message(unsigned long, int, void *);
int do_copy_quota_calc(unsigned long, int, void *);

struct do_copy_info {
	const char *mailbox;
	const char *acls;

	std::vector<uidplus_info> uidplus;
};

/*
** maildir quota calculation for copying messages.
*/

struct copyquotainfo {
	const char *destmailbox;
	int64_t nbytes;
	int nfiles;

	const char *acls;

	} ;
#ifdef __cplusplus
}
#endif

#endif
