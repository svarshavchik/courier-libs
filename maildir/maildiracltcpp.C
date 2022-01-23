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

/* -------------------------------------------------------------------- */

void maildir::aclt_list::add(const std::string &identifier, const aclt &acl)
{
	if (std::find_if(identifier.begin(),
			 identifier.end(),
			 []
			 (unsigned char c)
			 {
				 return c < ' ';
			 }) != identifier.end())
		return;

	auto iter=std::find_if(begin(), end(),
			       [&]
			       (aclt_node &n)
			       {
				       return n.identifier == identifier;
			       });

	if (iter != end())
	{
		iter->acl=acl;
		return;
	}

	push_back({identifier, acl});
}

void maildir::aclt_list::del(const std::string &identifier)
{
	auto iter=std::find_if(begin(), end(),
			       [&]
			       (aclt_node &n)
			       {
				       return n.identifier == identifier;
			       });

	if (iter != end())
		erase(iter);
}

maildir::aclt_list::aclt_list()=default;

maildir::aclt_list::~aclt_list()=default;

struct maildir_aclt_list_impl {
	maildir::aclt_list list;
};


void maildir_aclt_list_init(maildir_aclt_list *aclt_list)
{
	*aclt_list=NULL;
}

static struct maildir_aclt_list_impl *get_impl(maildir_aclt_list *aclt_list)
{
	if (*aclt_list)
		return *aclt_list;

	if (!(*aclt_list=new maildir_aclt_list_impl))
		return NULL;

	return *aclt_list;
}

void maildir_aclt_list_destroy(maildir_aclt_list *aclt_list)
{
	if (!*aclt_list)
		return;

	delete *aclt_list;
	*aclt_list=NULL;
}


/* Add an <identifier,acl> pair.  Returns 0 on success, -1 on failure */

int maildir_aclt_list_add(maildir_aclt_list *aclt_list,
			  const char *identifier,
			  const char *aclt_str,
			  maildir_aclt *aclt_cpy)
{
	struct maildir_aclt_list_impl *impl=get_impl(aclt_list);

	if (!impl)
		return -1;

	if (aclt_cpy)
	{
		const char *str=maildir_aclt_ascstr(aclt_cpy);

		if (*str)
			aclt_str=str;
	}

	if (!aclt_str)
		aclt_str="";

	impl->list.add(identifier, {aclt_str});
	return 0;
}

/*
** Remove 'identifier' from the ACL list.
*/

int maildir_aclt_list_del(maildir_aclt_list *aclt_list,
			  const char *identifier)
{
	struct maildir_aclt_list_impl *impl=get_impl(aclt_list);

	if (!impl)
		return -1;

	impl->list.del(identifier);
	return 0;
}

/*
** Generic enumeration.
*/

int maildir_aclt_list_enum(maildir_aclt_list *aclt_list,
			   int (*cb_func)(const char *identifier,
					  const char *acl,
					  void *cb_arg),
			   void *cb_arg)
{
	int rc;

	if (!*aclt_list)
		return 0;

	for (auto &node:(*aclt_list)->list)
	{
		rc= (*cb_func)(node.identifier.c_str(),
			       node.acl.c_str(), cb_arg);

		if (rc)
			return rc;
	}
	return 0;
}

const char *maildir_aclt_list_lookup(maildir_aclt_list *aclt_list,
				     const char *identifier)
{
	if (!*aclt_list)
		return NULL;

	auto iter=std::find_if((*aclt_list)->list.begin(),
			       (*aclt_list)->list.end(),
			       [&]
			       (const maildir::aclt_node &n)
			       {
				       return n.identifier == identifier;
			       });

	if (iter == (*aclt_list)->list.end())
		return NULL;

	return iter->acl.c_str();
}



/*
** An ACL entry for "administrators" or "group=administrators" will match
** either one.
*/

static int chk_admin(int (*cb_func)(const char *isme,
				    void *void_arg),
		     const char *identifier,
		     void *void_arg)
{
	if (strcmp(identifier, "administrators") == 0 ||
	    strcmp(identifier, "group=administrators") == 0)
	{
		int rc=(*cb_func)("administrators", void_arg);

		if (rc == 0)
			rc=(*cb_func)("group=administrators", void_arg);
		return rc;
	}

	return (*cb_func)(identifier, void_arg);
}

#define ISIDENT(s) \
	(MAILDIR_ACL_ANYONE(s) ? 1: chk_admin(info->cb_func, (s),	\
					      info->void_arg))

struct maildir_acl_compute_info {
	maildir_aclt *aclt;
	int (*cb_func)(const char *isme, void *void_arg);
	void *void_arg;
};

static int compute_cb_add(const char *identifier,
			  const char *acl,
			  void *cb_arg)
{
	struct maildir_acl_compute_info *info=
		(struct maildir_acl_compute_info *)cb_arg;
	int rc;

	if (identifier[0] == '-')
		return 0;

	rc= ISIDENT(identifier);

	if (rc <= 0)
	{
		return rc;
	}

	if (maildir_aclt_add(info->aclt, acl, NULL) < 0)
	{
		return -1;
	}
	return 0;
}

static int compute_cb_del(const char *identifier,
			  const char *acl,
			  void *cb_arg)
{
	struct maildir_acl_compute_info *info=
		(struct maildir_acl_compute_info *)cb_arg;
	int rc;

	if (identifier[0] != '-')
		return 0;

	rc= ISIDENT(identifier+1);

	if (rc <= 0)
	{
		return rc;
	}

	if (maildir_aclt_del(info->aclt, acl, NULL) < 0)
	{
		return -1;
	}

	return 0;
}

static int do_maildir_acl_compute_chkowner(maildir_aclt *aclt,
					   maildir_aclt_list *aclt_list,
					   int (*cb_func)(const char *isme,
							  void *void_arg),
					   void *void_arg,
					   int chkowner)
{
	int rc;
	struct maildir_acl_compute_info info;

	info.aclt=aclt;
	info.cb_func=cb_func;
	info.void_arg=void_arg;

	if ((rc=maildir_aclt_list_enum(aclt_list, compute_cb_add, &info)) ||
	    (rc=maildir_aclt_list_enum(aclt_list, compute_cb_del, &info)))
	{
		return -1;
	}

	/*
	** In our scheme, the owner always gets admin rights.
	*/

	rc=chkowner ? (*cb_func)("owner", void_arg):0;

	if (maildir_acl_disabled)
		rc=0;	/* Except when ACLs are disabled */

	if (rc < 0)
	{
		return rc;
	}

	if (rc)
	{
		if (maildir_aclt_add(aclt, ACL_ADMINISTER, NULL) < 0)
		{
			return rc;
		}
	}
	return 0;
}

static int maildir_acl_compute_chkowner(maildir_aclt *aclt,
					maildir_aclt_list *aclt_list,
					int (*cb_func)(const char *isme,
						       void *void_arg),
					void *void_arg,
					int chkowner)
{
	int rc;

	if (maildir_aclt_init(aclt, "", NULL) < 0)
		return -1;

	rc=do_maildir_acl_compute_chkowner(aclt, aclt_list,
					   cb_func,
					   void_arg,
					   chkowner);

	if (rc)
		maildir_aclt_destroy(aclt);
	return rc;
}

static int save_acl(const char *identifier, const char *acl,
		    void *cb_arg)
{
	if (fprintf((FILE *)cb_arg, "%s %s\n",
		    identifier,
		    acl) < 0)
		return -1;
	return 0;
}


static int is_owner(const char *isme, void *void_arg)
{
	if (void_arg && strcmp(isme, (const char *)void_arg) == 0)
		return 1;

	return strcmp(isme, "owner") == 0;
}

static int is_admin(const char *isme, void *void_arg)
{
	return strcmp(isme, "administrators") == 0;

	/* We don't need to check for group=administrators, see chk_admin() */
}

static int check_adminrights(maildir_aclt *list)
{
	if (strchr(maildir_aclt_ascstr(list), ACL_LOOKUP[0]) == NULL ||
	    strchr(maildir_aclt_ascstr(list), ACL_ADMINISTER[0]) == NULL)
	{
		maildir_aclt_destroy(list);
		return -1;
	}

	maildir_aclt_destroy(list);
	return 0;
}

static int check_allrights(maildir_aclt *list)
{
	const char *all=ACL_ALL;

	while (*all)
	{
		if (strchr(maildir_aclt_ascstr(list), *all) == NULL)
		{
			maildir_aclt_destroy(list);
			return -1;
		}
		++all;
	}

	maildir_aclt_destroy(list);
	return 0;
}

int maildir_acl_write(maildir_aclt_list *aclt_list,
		      const char *maildir,
		      const char *path,

		      const char *owner,
		      const char **err_failedrights)
{
	int trycreate;
	struct maildir_tmpcreate_info tci;
	FILE *fp;
	char *p, *q;
	const char *dummy_string;
	maildir_aclt chkacls;

	if (!err_failedrights)
		err_failedrights= &dummy_string;

	if (!maildir || !*maildir)
		maildir=".";
	if (!path || !*path)
		path=".";

	if (strchr(path, '/') || *path != '.')
	{
		errno=EINVAL;
		return -1;
	}

	if (strcmp(path, ".")) /* Sanity check */
		for (dummy_string=path; *dummy_string; dummy_string++)
			if (*dummy_string == '.' &&
			    (dummy_string[1] == '.' ||
			     dummy_string[1] == 0))
			{
				errno=EINVAL;
				return -1;
			}


	if (maildir_acl_compute_chkowner(&chkacls, aclt_list, is_owner, NULL,
					 0))
	{
		errno=EINVAL;
		return -1;
	}

	if (check_adminrights(&chkacls))
	{
		*err_failedrights="owner";
		errno=EINVAL;
		return -1;
	}

	if (owner)
	{
		maildir_aclt_destroy(&chkacls);

		if (maildir_acl_compute_chkowner(&chkacls, aclt_list, is_owner,
						 (void *)owner, 0))
		{
			errno=EINVAL;
			return -1;
		}
		if (check_adminrights(&chkacls))
		{
			maildir_aclt_destroy(&chkacls);

			*err_failedrights=owner;
			errno=EINVAL;
			return -1;
		}
	}

	if (maildir_acl_compute(&chkacls, aclt_list, is_admin, NULL))
	{
		maildir_aclt_destroy(&chkacls);
		errno=EINVAL;
		return -1;
	}
	if (check_allrights(&chkacls))
	{
		errno=EINVAL;
		return -1;
	}

	p=(char *)malloc(strlen(maildir)+strlen(path)+2);

	if (!p)
		return -1;

	strcat(strcat(strcpy(p, maildir), "/"), path);

	maildir_tmpcreate_init(&tci);

	tci.maildir=p;
	tci.uniq="acl";
	tci.doordie=1;

	fp=maildir_tmpcreate_fp(&tci);

	trycreate=0;

	if (fp)
	{
		q=(char *)malloc(strlen(p) + sizeof("/" ACLFILE));
		if (!q)
		{
			fclose(fp);
			unlink(tci.tmpname);
			maildir_tmpcreate_free(&tci);
			free(p);
			return -1;
		}
		strcat(strcpy(q, p), "/" ACLFILE);
		free(tci.newname);
		tci.newname=q;
		free(p);
	}
	else
	{
		free(p);

		q=(char *)malloc(strlen(maildir)+sizeof("/" ACLHIERDIR "/") +
			 strlen(path));
		if (!q)
		{
			maildir_tmpcreate_free(&tci);
			return -1;
		}
		strcat(strcat(strcpy(q, maildir), "/" ACLHIERDIR "/"), path+1);

		tci.maildir=maildir;
		tci.uniq="acl";
		tci.doordie=1;

		fp=maildir_tmpcreate_fp(&tci);

		if (!fp)
		{
			free(q);
			maildir_tmpcreate_free(&tci);
			return -1;
		}
		free(tci.newname);
		tci.newname=q;
		trycreate=1;
	}

	if (maildir_aclt_list_enum(aclt_list, save_acl, fp) < 0 ||
	    ferror(fp) || fflush(fp) < 0)
	{
		fclose(fp);
		unlink(tci.tmpname);
		maildir_tmpcreate_free(&tci);
		return -1;
	}
	fclose(fp);

	if (rename(tci.tmpname, tci.newname) < 0)
	{
		/* Perhaps ACLHIERDIR needs to be created? */

		if (!trycreate)
		{
			unlink(tci.tmpname);
			maildir_tmpcreate_free(&tci);
			return -1;
		}

		p=strrchr(tci.newname, '/');
		*p=0;
		mkdir(tci.newname, 0755);
		*p='/';

		if (rename(tci.tmpname, tci.newname) < 0)
		{
			unlink(tci.tmpname);
			maildir_tmpcreate_free(&tci);
			return -1;
		}
	}
	maildir_tmpcreate_free(&tci);
	return 0;
}

int maildir_acl_compute(maildir_aclt *aclt, maildir_aclt_list *aclt_list,
			int (*cb_func)(const char *isme,
				       void *void_arg), void *void_arg)
{
	return maildir_acl_compute_chkowner(aclt, aclt_list, cb_func, void_arg,
					    1);
}
