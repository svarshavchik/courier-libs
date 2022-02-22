#ifndef	storeinfo_h
#define	storeinfo_h

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"imaptoken.h"
#include	"imapscanclient.h"
#include	"numlib/numlib.h"

#include	<vector>

#ifdef __cplusplus
extern "C" {
#endif

struct storeinfo {
	int plusminus=0;
	int silent=0;
	struct imapflags flags;
	mail::keywords::list keywords;
} ;

bool storeinfo_init(struct storeinfo &);
int do_store(unsigned long, int, storeinfo *);

int do_copy_message(unsigned long, int, void *);
int do_copy_quota_calc(unsigned long, int, void *);

struct uidplus_info;

struct do_copy_info {
	const char *mailbox;
	const char *acls;

	struct uidplus_info *uidplus_list;
	struct uidplus_info **uidplus_tail;
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
