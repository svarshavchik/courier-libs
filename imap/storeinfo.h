#ifndef	storeinfo_h
#define	storeinfo_h

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"imapflags.h"
#include	"imapscanclient.h"
#include	"numlib/numlib.h"
#include	"imapd.h"
#include	<vector>

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
	acl_check_rights &acls;

	std::vector<uidplus_info> uidplus;
};

/*
** maildir quota calculation for copying messages.
*/

struct copyquotainfo {

	std::string destmailbox;
	int64_t nbytes=0;
	int nfiles=0;

	acl_check_rights &acls;

	copyquotainfo(const std::string &destmailbox,
		      acl_check_rights &acls)
		: destmailbox{destmailbox}, acls{acls} {}
} ;

#endif
