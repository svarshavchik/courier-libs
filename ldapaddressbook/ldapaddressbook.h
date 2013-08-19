#ifndef	ldapaddressbook_h
#define	ldapaddressbook_h


#include <stdio.h>

/*
** Copyright 2000-2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/

#ifdef  __cplusplus
extern "C" {
#endif

/*
** This module implements an abstraction of an interface to an LDAP address
** book.  There's no reason to reinvent the wheel, so we simply run ldapsearch
** as a child process, and read its output.  ldapsearch is run indirectly, via
** a stub shell script that can be customized on a given system.  The template
** for the stub shell script is provided.
**
** There's a small library here that can be used to maintain a configuration
** file listing available address books that can be contacted.  The format
** of each line in this library is simply:
**
** name<tab>host<tab>port<tab>suffix<tab>binddn<tab>bindpw
**
** Functions are provided to add and remove names from this configuration
** file easily.  The above is parsed into the following structure:
*/

struct ldapabook {
	struct ldapabook *next;

	char	*name;
	char	*host;
	char	*port;
	char	*suffix;
	char	*binddn;
	char	*bindpw;

	struct	ldapabook_opts *opts;

	} ;

struct ldapabook_opts {
	struct ldapabook_opts *next;
	char	*options;	/* First char - name, rest - value */
} ;

	/* Potential first chars: */

#define SASL_SECURITY_PROPERTIES	'O'
#define SASL_AUTHENTICATION_ID		'U'
#define	SASL_AUTHENTICATION_RID		'X'  /* u:username, or dn:dn */
#define	SASL_AUTHENTICATION_MECHANISM	'Y'
#define SASL_STARTTLS			'Z'

/* Read a configuration file, and create a link list of ldapabook structs */

struct ldapabook *ldapabook_read(const char *);		/* filename */

/* Free memory allocated by ldapabook_init */

void ldapabook_free(struct ldapabook *);

/* Find a certain address book */

const struct ldapabook *ldapabook_find(const struct ldapabook *,
					const char *);

/* Add a new entry to the address book */

int ldapabook_add(const char *,			/* filename */
		const struct ldapabook *);	/* new entry */

/* Delete an entry from the address book */

int ldapabook_del(const char *,		/* filename */
		const char *,		/* temporary filename on same filesys */
		const char *);		/* name to delete */

/* Run ldapsearch in the background, return a file descriptor containing
** ldapsearch's output.
*/

int ldapabook_search(const struct ldapabook *b,	/* Search this address book */
		     const char *script,
		     const char *search,
		     int (*callback_func)(const char *utf8_name,
					  const char *address,
					  void *callback_arg),
		     void (*callback_err)(const char *errmsg,
					  void *callback_arg),
		     void *callback_arg);

/*
** Internal function:
*/

void ldapabook_writerec(const struct ldapabook *, FILE *);

#ifdef  __cplusplus
}
#endif

#endif
