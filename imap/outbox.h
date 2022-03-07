#ifndef	outbox_h
#define	outbox_h

#include <string>
#include <functional>

/*
** Copyright 2002 S. Varshavchik.
** See COPYING for distribution information.
*/

int check_outbox(const char *message, const char *mailbox);
int is_outbox(const char *mailbox);
int imapd_sendmsg(const char *message, char **argv,
		  const std::function<void (const std::string &)> &errfunc);

std::string defaultSendFrom();

#endif
