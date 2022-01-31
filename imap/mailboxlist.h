#ifndef	mailboxlist_h
#define	mailboxlist_h


#include "config.h"

#include <functional>
#include <string>

/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#define MAILBOX_MARKED		0x0001
#define MAILBOX_UNMARKED	0x0002
#define MAILBOX_NOCHILDREN	0x0004
#define MAILBOX_NOINFERIORS	0x0008
#define MAILBOX_CHILDREN	0x0010
#define MAILBOX_NOSELECT	0x0020

#define LIST_SUBSCRIBED		0x0100
#define LIST_ACL		0x0200
#define LIST_MYRIGHTS		0x0400
#define LIST_POSTADDRESS	0x0800
#define LIST_CHECK1FOLDER	0x1000

struct mailbox_scan_info {
	std::string hiersep;
	std::string mailbox;
	int flags;
};

typedef std::function<bool (const mailbox_scan_info &)> mailbox_scan_cb_t;

bool mailbox_scan(const char *reference, const char *name,
		  int list_options,
		  const mailbox_scan_cb_t &callback_func);

#endif
