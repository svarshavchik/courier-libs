/*
** Copyright 2003-2022 S. Varshavchik.
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

static int chk_admin(const std::function<int (const char *)> &cb_func,
		     const char *identifier)
{
	if (strcmp(identifier, "administrators") == 0 ||
	    strcmp(identifier, "group=administrators") == 0)
	{
		int rc=cb_func("administrators");

		if (rc == 0)
			rc=cb_func("group=administrators");

		return rc;
	}

	return cb_func(identifier);
}

static int do_maildir_acl_compute_chkowner(
	maildir::aclt &aclt,
	const maildir::aclt_list &aclt_list,
	const std::function<int (const char *)> &cb_func,
	int chkowner);

static int check_adminrights(maildir::aclt &list)
{
	if (list.find(ACL_LOOKUP[0]) == list.npos ||
	    list.find(ACL_ADMINISTER[0]) == list.npos)
	{
		return -1;
	}

	return 0;
}

static int do_maildir_acl_write(const maildir::aclt_list &aclt_list,
				const char *maildir,
				const char *path,

				const char *owner,
				const char **err_failedrights)
{
	int trycreate;
	struct maildir_tmpcreate_info tci;
	FILE *fp;
	const char *dummy_string;

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

	// Sanity check: compute ACLs for "owner", and verify that "owner"
	// had admin rights.

	maildir::aclt chkacls{""};

	if (do_maildir_acl_compute_chkowner(
		    chkacls, aclt_list,
		    []
		    (const char *identifier)
		    {
			    return strcmp(identifier, "owner") == 0 ? 1:0;
		    },
		    0))
	{
		errno=EINVAL;
		return -1;
	}

	if (check_adminrights(chkacls))
	{
		*err_failedrights="owner";
		errno=EINVAL;
		return -1;
	}

	// Sanity check: if an owner identifier gets passed in, we also
	// compute the ACLs for the specified owner and "owner".

	if (owner)
	{
		if (do_maildir_acl_compute_chkowner(
			    chkacls, aclt_list,
			    [owner]
			    (const char *identifier)
			    {
				    return strcmp(identifier, "owner") == 0
					    || strcmp(identifier, owner) == 0
					    ? 1:0;
			    },
			    0))
		{
			errno=EINVAL;
			return -1;
		}
		if (check_adminrights(chkacls))
		{
			*err_failedrights=owner;
			errno=EINVAL;
			return -1;
		}
	}


	/* We don't need to check for group=administrators, see chk_admin() */

	if (aclt_list.compute(
		    chkacls,
		    []
		    (const char *identifier)
		    {
			    return strcmp(identifier, "administrators") == 0;
		    }))
	{
		errno=EINVAL;
		return -1;
	}

	// Administrators should have all ACLs.

	const char *all=ACL_ALL;

	while (*all)
	{
		if (chkacls.find(*all) == chkacls.npos)
		{
			errno=EINVAL;
			return -1;
		}
		++all;
	}

	std::string p;

	p.reserve(strlen(maildir) + strlen(path)+sizeof(ACLHIERDIR)+10);

	p=maildir;
	p += "/";
	p += path;

	maildir_tmpcreate_init(&tci);

	tci.maildir=p.c_str();
	tci.uniq="acl";
	tci.doordie=1;

	fp=maildir_tmpcreate_fp(&tci);

	std::string newname, tmpname;

	if (tci.newname)
		newname=tci.newname;

	if (tci.tmpname)
		tmpname=tci.tmpname;

	maildir_tmpcreate_free(&tci);

	trycreate=0;

	if (fp)
	{
		newname=p;
		newname += "/" ACLFILE;
	}
	else
	{
		// This maildir does not exist, but inferior maildirs may
		// exist, in this case we record the maildir in the
		// ACLHIERDIR.

		newname=maildir;
		newname +=  "/" ACLHIERDIR "/";
		newname += path+1;

		tci.maildir=maildir;
		tci.uniq="acl";
		tci.doordie=1;

		fp=maildir_tmpcreate_fp(&tci);

		tmpname=tci.tmpname;
		maildir_tmpcreate_free(&tci);
		trycreate=1;
	}

	for (const auto &node:aclt_list)
	{
		fprintf(fp, "%s %s\n", node.identifier.c_str(),
			node.acl.c_str());
	}

	if (ferror(fp) || fflush(fp) < 0)
	{
		fclose(fp);
		unlink(tmpname.c_str());
		return -1;
	}
	fclose(fp);

	if (rename(tmpname.c_str(), newname.c_str()) < 0)
	{
		/* Perhaps ACLHIERDIR needs to be created? */

		if (!trycreate)
		{
			unlink(tmpname.c_str());
			return -1;
		}

		size_t n=newname.rfind('/');

		mkdir(newname.substr(0, n).c_str(), 0755);

		if (rename(tmpname.c_str(), newname.c_str()) < 0)
		{
			unlink(tmpname.c_str());
			return -1;
		}
	}
	return 0;
}

int maildir_acl_write(maildir_aclt_list *aclt_list,
		      const char *maildir,
		      const char *path,

		      const char *owner,
		      const char **err_failedrights)
{
	maildir::aclt_list default_list;

	return do_maildir_acl_write(
		*aclt_list ? (*aclt_list)->list:default_list,
		maildir,
		path,
		owner,
		err_failedrights);
}

int maildir::aclt_list::write(const std::string &maildir,
			      const std::string &path,
			      const std::string &owner) const
{
	std::string ignore;

	return write(maildir, path, owner, ignore);
}

int maildir::aclt_list::write(const std::string &maildir,
			      const std::string &path,
			      const std::string &owner,
			      std::string &failed_rights) const
{
	const char *err_failedrights;

	int rc=do_maildir_acl_write(*this, maildir.c_str(), path.c_str(),
				    owner.c_str(),
				    &err_failedrights);

	failed_rights.clear();
	if (err_failedrights)
		failed_rights=err_failedrights;

	return rc;
}

int maildir::aclt_list::compute(
	maildir::aclt &ret,
	const std::function<int (const char *)> &cb
) const
{
	ret={""};

	int rc=do_maildir_acl_compute_chkowner(ret, *this, cb, 1);

	if (rc)
		ret={""};

	return rc;
}

int maildir_acl_compute(maildir_aclt *aclt, maildir_aclt_list *aclt_list,
			int (*cb_func)(const char *isme,
				       void *void_arg), void *void_arg)
{
	if (!(*aclt=new maildir_aclt_impl{""}))
		return -1;

	maildir::aclt_list default_list;

	int rc=(*aclt_list ? (*aclt_list)->list:default_list).compute(
		(*aclt)->impl,
		[cb_func, void_arg]
		(const char *identifier)
		{
			return cb_func(identifier, void_arg);
		});

	if (rc)
	{
		delete *aclt;
		*aclt=NULL;
	}

	return rc;
}

#define ISIDENT(s)	\
	(MAILDIR_ACL_ANYONE(s) ? 1: chk_admin(cb_func, (s)))

static int do_maildir_acl_compute_chkowner(
	maildir::aclt &aclt,
	const maildir::aclt_list &aclt_list,
	const std::function<int (const char *)> &cb_func,
	int chkowner)
{
	for (const auto &node:aclt_list)
	{
		auto identifier=node.identifier.c_str();

		if (*identifier == '-')
			continue;

		int rc=ISIDENT(identifier);

		if (rc < 0)
			return rc;

		if (rc > 0)
			aclt += node.acl;
	}

	for (const auto &node:aclt_list)
	{
		auto identifier=node.identifier.c_str();

		if (*identifier != '-')
			continue;

		++identifier;

		int rc=ISIDENT(identifier);

		if (rc < 0)
			return rc;

		if (rc > 0)
			aclt -= node.acl;
	}

	/*
	** In our scheme, the owner always gets admin rights.
	*/

	int rc=chkowner ? cb_func("owner"):0;

	if (maildir_acl_disabled)
		rc=0;	/* Except when ACLs are disabled */

	if (rc < 0)
	{
		return rc;
	}

	if (rc)
	{
		aclt += ACL_ADMINISTER;
	}
	return 0;
}
