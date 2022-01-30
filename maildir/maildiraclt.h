#ifndef	maildiraclt_h
#define	maildiraclt_h


/*
** Copyright 2003-2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#ifdef  __cplusplus

#include	<string>
#include	<vector>
#include	<functional>
#include	<utility>

namespace maildir {
#if 0
}
#endif

/*
** A basic ACL entity.  Be generic, it's just a character string.
** However, we do keep it in collating order.
*/

struct aclt : public std::string {

	template<typename ...Args>
	aclt(Args && ...args)
		: std::string{std::forward<Args>(args)...}
	{
		fixup();
	}

	aclt(const aclt &)=default;

	aclt(aclt &&)=default;

	aclt &operator=(const aclt &)=default;

	aclt &operator=(aclt &&)=default;

	~aclt();

	// Add or remove access chars.
	//
	// Duplicates are automatically removed.

	aclt &operator+=(const std::string &);
	aclt &operator-=(const std::string &);

 private:
	void fixup();
};

/*
** A node in a list of ACLs. An identifiers, and its ACLs.
*/

struct aclt_node {
	std::string    identifier;
	aclt           acl;
};

/*
** The entire ACL is just a vector.
*/

struct aclt_list : std::vector<aclt_node> {

	// Add an <identifier,acl> pair.
	//
	// Sanity check: identifiers with control characters are quietly
	// ignored.

	void add(const std::string &identifier, const aclt &acl);

	// Remove an identifier

	void del(const std::string &identifier);

	// Find an entry for the identifier
	iterator lookup(const std::string &identifier);

	aclt_list();
	~aclt_list();

	/*
	** Compute my access rights.  Initializes 'aclt'.
	**
	** The closure should return >0 if identifier refers to the entity
	** whose access rights are to be computed; 0 if it does not, <0 if
	** an error occured.
	**
	** As a special case,compute() handles "anonymous" and "anyone"
	** identifiers on its own.
	**
	** As a special case, if the closure function returns >0 for the
	** identifier "owner", the computed access rights will always include
	** the ADMIN right.
	**
	** compute() uses ACL2=UNION; the computed access rights
	** consist of the union of all rights granted to all identifiers
	** that include the entity, minus the union of all reights revoked
	** from all identifiers that include the entity.
	*/

	int compute( aclt &,
		     const std::function<int (const char *)> &) const;

	/*
	** A wrapper for maildir_acl_compute.
	**
	** Computes 'rights' - my rights on the mailbox.
	**
	** me: my login identifier.
	**
	** folder_owner: the owner of the mailbox folder whose rights are computed,
	** should also take the form of "user=<n>".
	**
	** options: This is parsed as a comma-separated string. All strings
	** of the from "group=" are also added to the ACL lookup. This
	** typically comes from the "OPTIONS" environment variable.
	**
	** Returns 0 upon success, after placing the computed access rights in
	** 'rights'.
	*/

	int computerights(aclt &rights,
			  const std::string &me,
			  const std::string &owner,
			  const std::string &options);

	/*
	** Write ACLs for maildir maildir.path.
	**
	** Returns 0 for success, <0 for failure.
	**
	** Additional parameters:
	**
	** owner: the owner entity of the folder represented by 'path'.
	*/

	int write(const std::string &maildir,
		  const std::string &path,
		  const std::string &owner) const;

	/*
	** failedrights will be initialized to a non-null identifier string if
	** write() fails because aclt_list
	** illegally revokes minimum rights from this identifier (admin/lookup).
	*/

	int write(const std::string &maildir,
		  const std::string &path,
		  const std::string &owner,
		  std::string &failed_rights) const;

	/*
	** Read ACLs for maildir maildir.path.
	**
	** maildir: Path to the main maildir.
	**
	** path: ".folder.subfolder".
	**
	** aclt_list is an uninitialized maildir_aclt_list
	**
	** Returns 0 for success, <0 for failure.
	*/

	int read(const std::string &maildir,
		 const std::string &path);
};

/* Remove stale ACL entries */
void acl_reset(const std::string &maildir);

/* Remove a particular ACL entry */
bool acl_delete(const std::string &maildir, const std::string &path);

#if 0
{
#endif
}
extern "C" {
#endif
#if 0
}
#endif

/*
** C wrappers for maildir::aclt
**
** These functions return 0 on success, <0 on error.
*/

typedef struct maildir_aclt_impl *maildir_aclt;


/*
** C wrapper for a maildir::aclt
**
** Initialize the aclt.  The second or third args specify its initial value.
** Both may be NULL.  Only one can be non-NULL.
*/

int maildir_aclt_init(maildir_aclt *aclt,
		      const char *initvalue_cstr,
		      const maildir_aclt *initvalue_cpy);

/* Destroy an aclt after it is no longer used. */

void maildir_aclt_destroy(maildir_aclt *aclt);


/* See aclt's += and -= operators */

void maildir_aclt_add(maildir_aclt *aclt,
		      const char *add_strs,
		      const maildir_aclt *add_aclt);

void maildir_aclt_del(maildir_aclt *aclt,
		      const char *del_strs,
		      const maildir_aclt *del_aclt);

/* return a const char * that contains the acl */

const char *maildir_aclt_ascstr(const maildir_aclt *aclt);

/* Next level up, a list of <identifier,acl>s */

struct maildir_aclt_list_impl;

typedef struct maildir_aclt_list_impl *maildir_aclt_list;

/*
** C wrappers for a maildir::aclt_list
*/

void maildir_aclt_list_init(maildir_aclt_list *aclt_list);
void maildir_aclt_list_destroy(maildir_aclt_list *aclt_list);

/* A wrapper for aclt_list::add */

void maildir_aclt_list_add(maildir_aclt_list *aclt_list,
			   const char *identifier,
			   const char *aclt_str,
			   maildir_aclt *aclt_cpy);

/* Wrapper for aclt_list::del */

void maildir_aclt_list_del(maildir_aclt_list *aclt_list,
			  const char *identifier);

/*
** Enumerate the ACL list vector.  The callback function, cb_func, gets
** invoked for each ACL list entry.  The callback function receives:
** identifier+rights pair; as well as the transparent pass-through
** argument.  A nonzero return from the callback function terminates
** the enumeration, and maildir_aclt_list_enum itself returns
** non-zero.  A zero return continues the enumeration.  After the
** entire list is enumerated maildir_aclt_list_enum returns 0.
*/

int maildir_aclt_list_enum(maildir_aclt_list *aclt_list,
			   int (*cb_func)(const char *identifier,
					  const char *acl,
					  void *cb_arg),
			   void *cb_arg);

/* Searches the aclt_list vector.
** Find an identifier, returns NULL if not found, else its ACL gets returned.
*/

const char *maildir_aclt_list_lookup(maildir_aclt_list *aclt_list,
				     const char *identifier);

/* maildir-level acl ops */

#define ACL_LOOKUP "l"
#define ACL_READ "r"
#define ACL_SEEN "s"
#define ACL_WRITE "w"
#define ACL_INSERT "i"
#define ACL_POST "p"
#define ACL_CREATE "c"
#define ACL_DELETEFOLDER "x"
#define ACL_DELETEMSGS "t"
#define ACL_EXPUNGE "e"
#define ACL_ADMINISTER "a"

#define ACL_ALL \
	ACL_ADMINISTER \
	ACL_CREATE \
	ACL_EXPUNGE \
	ACL_INSERT \
	ACL_LOOKUP \
	ACL_READ \
	ACL_SEEN \
	ACL_DELETEMSGS \
	ACL_WRITE \
	ACL_DELETEFOLDER

#define ACL_DELETE_SPECIAL "d"

#define ACLFILE "courierimapacl"
#define ACLHIERDIR "courierimaphieracl"


#define MAILDIR_ACL_ANYONE(s) \
	(strcmp( (s), "anonymous") == 0 || \
	 strcmp( (s), "anyone") == 0)


/*
** Set maildir_acl_disabled to 1 to effectively disable ACL support, and its
** overhead.
**
** If maildir_acl_disabled is set, maildir_acl_read never goes to disk to
** read the ACL file, instead it returns a fixed ACL list which only contains
** an entry for "owner", and gives "owner" all ACL rights, except the
** ADMINISTER right, relying on higher level code to refuse to set new
** ACLs unless the existing ACL gives administer right.
**
** Additionally, maildir_acl_disabled turns off the hook in maildir_acl_compute
** that grants ADMINISTER to "owner" irrespective of what the ACLs actually
** say.
*/

extern int maildir_acl_disabled;

/*
** Creates a new maildir::aclt_list and calls read().
*/

int maildir_acl_read(maildir_aclt_list *aclt_list,
		     const char *maildir,
		     const char *path);

/*
** Wrapper for aclt_list::write, with a failed_rights returned value.
** Passing in a null pointer is equivalent to calling the overload without
** a failed_rights parameter.
*/

int maildir_acl_write(maildir_aclt_list *aclt_list,
		      const char *maildir,
		      const char *path,
		      const char *owner,
		      const char **err_failedrights);

/*
** C wrapper for maildir::acl_reset()
*/
void maildir_acl_reset(const char *maildir);

/*
** C wrapper for maildir::acl_delete().
*/

int maildir_acl_delete(const char *maildir,
		       const char *path);   /* .folder.subfolder */

/*
** C wrapper for aclt_list::compute().
**
** The closure calls that passed-in function parameter, with the additional
** void pointer parameter, that gets passed through unchanged.
*/

int maildir_acl_compute(maildir_aclt *aclt, maildir_aclt_list *aclt_list,
			int (*cb_func)(const char *identifier,
				       void *void_arg), void *void_arg);

/*
** A wrapper for maildir_acl_compute that compares against a
** const char * array.
*/

int maildir_acl_compute_array(maildir_aclt *aclt,
			      maildir_aclt_list *aclt_list,
			      const char * const *identifiers);

/*
** A C wrapper for maildir::aclt_list::computerights().
*/

int maildir_acl_computerights(maildir_aclt *rights,
			      maildir_aclt_list *acl_list,
			      const char *me,
			      const char *folder_owner,
			      const char *options);

/*
** Convenience functions:
**
** maildir_acl_canlistrights: return true if the given rights indicate that
** the rights themselves can be viewed (one of the following must be present:
** ACL_LOOKUP, ACL_READ, ACL_INSERT[0], ACL_CREATE[0], ACL_DELETEFOLDER,
** ACL_EXPUNGE[0], or ACL_ADMINISTER).
*/

int maildir_acl_canlistrights(const char *myrights);

#if 0
{
#endif
#ifdef  __cplusplus
}
#endif

#endif
