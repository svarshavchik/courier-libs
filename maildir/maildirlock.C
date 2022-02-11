/*
** Copyright 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#include "config.h"
#include "maildirwatch.h"
#include "maildircreate.h"
#include "liblock/config.h"
#include "liblock/liblock.h"
#include "liblock/mail.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdexcept>
#include <string>

#ifndef LOCK_TIMEOUT
#define LOCK_TIMEOUT 120
#endif

/*
** Courier-IMAP compatible maildir lock.
*/

static std::string do_lock(const std::string &dir,
			   maildir::watch *w)
{
	maildir::tmpcreate_info createInfo;

	createInfo.maildir=dir;
	createInfo.uniq="courierlock";

	const char *p=getenv("HOSTNAME");

	if (p)
		createInfo.hostname=p;

	int fd=createInfo.fd();

	if (fd < 0)
		return "";

	close(fd);

	/* HACK: newname now contains: ".../new/filename" */
	size_t l=createInfo.newname.rfind('/');

	createInfo.newname=createInfo.newname.substr(0, l-3);

	createInfo.newname += WATCHDOTLOCK;

	while (ll_dotlock(createInfo.newname.c_str(),
			  createInfo.tmpname.c_str(), LOCK_TIMEOUT) < 0)
	{
		if (errno == EEXIST)
		{
			if (w == NULL || !w->unlock(LOCK_TIMEOUT))
				sleep(1);
			continue;
		}

		if (errno == EAGAIN)
		{
			sleep(5);
			continue;
		}

		createInfo.newname.clear();
		break;
	}

	return createInfo.newname;
}

maildir::watch::lock::lock(watch &&w)
	: lock{ static_cast<watch &>(w)}
{
}

maildir::watch::lock::lock(watch &w)
	: lockname{do_lock(w.maildir, &w)}
{
	if (lockname.empty())
		throw std::runtime_error("invalid maildir for a lock");
}

maildir::watch::lock::~lock()
{
	unlink(lockname.c_str());
}

char *maildir_lock(const char *dir, struct maildirwatch *w,
		   int *tryAnyway)
{
	if (tryAnyway)
		*tryAnyway=0;

	auto s=do_lock(dir, w ? static_cast<maildir::watch *>(w):nullptr);

	if (s.empty())
		return nullptr;

	return strdup(s.c_str());
}
