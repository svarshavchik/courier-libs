/*
** Copyright 2006, Double Precision Inc.
**
*/

#ifndef ldapsearch_h
#define ldapsearch_h

#ifdef  __cplusplus
extern "C" {
#endif

/*
** Encapsulate interface for search address book in LDAP
*/

#include "config.h"
#if HAVE_LBER_H
#include <lber.h>
#endif
#include <ldap.h>
#include <stdio.h>

struct ldapsearch {

	LDAP *handle;

	char *base;   /* Saved base of the search */
};

/*
** Allocate and deallocate the ldapsearch struct.
*/

struct ldapsearch *l_search_alloc(const char *host,
				  int port,
				  const char *userid,
				  const char *password,
				  const char *base);
/* base - the starting point of the search in the LDAP tree */

void l_search_free(struct ldapsearch *s);

/*
** Search the address book, and invoke the callback function for each
** match found.  The callback function receives the name & address of the
** found entry.  'exact_match' is nonzero if the lookupkey matched on of the
** LDAP attributes.
**
** The callback function must return zero.   A non-zero return stops the
** search.
*/

int l_search_do(struct ldapsearch *s,
		const char *lookupkey,

		int (*callback_func)(const char *utf8_name,
				     const char *address,
				     void *callback_arg),
		void *callback_arg);

/*
** Ping the LDAP server (makes sure that host/port were valid, because
** any connection attempt is deferred).
*/

int l_search_ping(struct ldapsearch *s);

#ifdef  __cplusplus
}
#endif

#endif
