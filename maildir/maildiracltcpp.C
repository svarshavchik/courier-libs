/*
** Copyright 2003-2012 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"maildiraclt.h"
#include	"maildirmisc.h"
#include	"maildircreate.h"
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#include	<string.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<assert.h>

#include	<algorithm>

maildir::aclt::aclt(const char *p) : std::string{p}
{
	fixup();
}

maildir::aclt::~aclt()=default;

maildir::aclt &maildir::aclt::operator+=(const std::string &s)
{
	std::string::operator+=(s);
	fixup();

	return *this;
}

maildir::aclt &maildir::aclt::operator-=(const std::string &s)
{
	erase(std::remove_if(begin(), end(),
			     [&]
			     (char c)
			     {
				     return s.find(c) != s.npos;
			     }), end());
	return *this;
}

void maildir::aclt::fixup()
{
	erase(std::remove_if(begin(), end(),
			     [&]
			     (unsigned char c)
			     {
				     return c < ' ';
			     }), end());
	std::sort(begin(), end());
	erase(std::unique(begin(), end()), end());
}

struct maildir_aclt_impl {
	maildir::aclt impl;
};

int maildir_aclt_init(maildir_aclt *aclt,
		      const char *initvalue_cstr,
		      const maildir_aclt *initvalue_cpy)
{
	if (initvalue_cpy)
		initvalue_cstr= (*initvalue_cpy)->impl.c_str();

	*aclt=NULL;

	if (!initvalue_cstr || !*initvalue_cstr)
		return 0;

	if (!(*aclt=new maildir_aclt_impl{initvalue_cstr}))
	{
		*aclt=NULL;
		return -1;
	}

	return 0;
}

/* Destroy an aclt after it is no longer used. */

void maildir_aclt_destroy(maildir_aclt *aclt)
{
	if (!*aclt)
		return;

	delete *aclt;
	*aclt=NULL;
}


/* Add or remove access chars. */

int maildir_aclt_add(maildir_aclt *aclt,
		     const char *add_strs,
		     const maildir_aclt *add_aclt)
{
	if (add_aclt && *add_aclt)
		add_strs= (*add_aclt)->impl.c_str();

	if (!add_strs || !*add_strs)
		return 0;

	if (*aclt)
	{
		(*aclt)->impl += add_strs;
	}
	else
	{
		return maildir_aclt_init(aclt, add_strs, NULL);
	}

	return 0;
}

int maildir_aclt_del(maildir_aclt *aclt,
		     const char *del_strs,
		     const maildir_aclt *del_aclt)
{
	if (del_aclt && *del_aclt)
		del_strs= (*del_aclt)->impl.c_str();

	if (!del_strs)
		return 0;

	if (!*aclt)
		return 0;

	(*aclt)->impl -= del_strs;

	if ((*aclt)->impl.empty())
	{
		maildir_aclt_destroy(aclt);
	}
	return 0;
}

const char *maildir_aclt_ascstr(const maildir_aclt *aclt)
{
	if (!aclt || !*aclt)
		return "";

	return (*aclt)->impl.c_str();
}

struct maildir_aclt_node {
	struct maildir_aclt_node *prev;
	struct maildir_aclt_node *next;
	char *identifier;
	maildir_aclt acl;
};

struct maildir_aclt_list_impl {
	struct maildir_aclt_node *head;
	struct maildir_aclt_node *tail;
};

/* -------------------------------------------------------------------- */


void maildir_aclt_list_init(maildir_aclt_list *aclt_list)
{
	*aclt_list=NULL;
}

static struct maildir_aclt_list_impl *get_impl(maildir_aclt_list *aclt_list)
{
	if (*aclt_list)
		return *aclt_list;

	if (!(*aclt_list=(struct maildir_aclt_list_impl *)
	      malloc(sizeof(struct maildir_aclt_list_impl))))
		return NULL;

	(*aclt_list)->head=NULL;
	(*aclt_list)->tail=NULL;

	return *aclt_list;
}

void maildir_aclt_list_destroy(maildir_aclt_list *aclt_list)
{
	struct maildir_aclt_node *p;

	if (!*aclt_list)
		return;

	for (p=(*aclt_list)->head; p; )
	{
		struct maildir_aclt_node *q=p->next;

		free(p->identifier);
		maildir_aclt_destroy(&p->acl);
		free(p);
		p=q;
	}

	free(*aclt_list);
	*aclt_list=NULL;
}


/* Add an <identifier,acl> pair.  Returns 0 on success, -1 on failure */

int maildir_aclt_list_add(maildir_aclt_list *aclt_list,
			  const char *identifier,
			  const char *aclt_str,
			  maildir_aclt *aclt_cpy)
{
	struct maildir_aclt_node *p;
	const char *q;
	struct maildir_aclt_list_impl *impl=get_impl(aclt_list);

	if (!impl)
		return -1;

	/* Check for valid identifiers */

	for (q=identifier; *q; q++)
		if ( (int)(unsigned char)*q <= ' ')
		{
			errno=EINVAL;
			return -1;
		}

	if (*identifier == 0)
	{
		errno=EINVAL;
		return -1;
	}

	if (aclt_cpy)
	{
		const char *str=maildir_aclt_ascstr(aclt_cpy);

		if (*str)
			aclt_str=str;
	}

	for (p=impl->head; p; p=p->next)
	{
		if (strcmp(p->identifier, identifier) == 0)
		{
			maildir_aclt_destroy(&p->acl);
			return maildir_aclt_init(&p->acl, aclt_str, NULL);
		}
	}

	if ((p=(maildir_aclt_node *)malloc(sizeof(*p))) == NULL ||
	    (p->identifier=strdup(identifier)) == NULL)
	{
		if (p) free(p);
		return -1;
	}

	if (maildir_aclt_init(&p->acl, aclt_str, NULL) < 0)
	{
		free(p->identifier);
		free(p);
		return -1;
	}

	p->next=NULL;
	if ((p->prev=impl->tail) != NULL)
		p->prev->next=p;
	else
		(*aclt_list)->head=p;
	(*aclt_list)->tail=p;
	return 0;
}

/*
** Remove 'identifier' from the ACL list.
*/

int maildir_aclt_list_del(maildir_aclt_list *aclt_list,
			  const char *identifier)
{
	struct maildir_aclt_node *p;
	struct maildir_aclt_list_impl *impl=get_impl(aclt_list);

	if (!impl)
		return -1;

	for (p=impl->head; p; p=p->next)
	{
		if (strcmp(p->identifier, identifier) == 0)
		{
			if (p->prev)
				p->prev->next=p->next;
			else impl->head=p->next;

			if (p->next)
				p->next->prev=p->prev;
			else impl->tail=p->prev;

			maildir_aclt_destroy(&p->acl);
			free(p->identifier);
			free(p);
			return 0;
		}
	}
	return 0;
}

/*
** Generic enumeration.
*/

int maildir_aclt_list_enum(maildir_aclt_list *aclt_list,
			   int (*cb_func)(const char *identifier,
					  const maildir_aclt *acl,
					  void *cb_arg),
			   void *cb_arg)
{
	struct maildir_aclt_node *p;
	int rc;

	if (!*aclt_list)
		return 0;

	for (p=(*aclt_list)->head; p; p=p->next)
	{
		rc= (*cb_func)(p->identifier, &p->acl, cb_arg);

		if (rc)
			return rc;
	}
	return 0;
}

const maildir_aclt *maildir_aclt_list_find(maildir_aclt_list *aclt_list,
					   const char *identifier)
{
	struct maildir_aclt_node *p;

	if (!*aclt_list)
		return NULL;

	for (p=(*aclt_list)->head; p; p=p->next)
	{
		if (strcmp(p->identifier, identifier) == 0)
			return &p->acl;
	}
	return NULL;
}
