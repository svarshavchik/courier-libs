#ifndef	maildirfilter_h
#define	maildirfilter_h

/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/


#include	"config.h"
#include	<string_view>
#include	<string>
#include	<vector>

enum maildirfiltertype {
	startswith,
	endswith,
	contains,
	hasrecipient,
	mimemultipart,
	textplain,
	islargerthan,		/* Use negation for the opposite! */
	anymessage
	} ;

struct maildirfilterrule {
	std::string rulename_utf8;
	enum maildirfiltertype type;
	int flags;

#define	MFR_DOESNOT	1	/* Negates pretty much every condition */
#define	MFR_BODY	2	/* startswith/endswith/contains applied
				** to body.
				*/
#define	MFR_CONTINUE	4	/* Continue filtering (cc instead of to) */
#define MFR_PLAINSTRING	8	/* Pattern is a plain string, not a regex */

	std::string fieldname_utf8;	/* Match this header */
	std::string fieldvalue_utf8;	/* Match/search value */
	std::string tofolder;		/* Destination folder, fwd address, err msg */
	std::string fromhdr;		/* From: header on autoreplies. */
	} ;

typedef std::vector<struct maildirfilterrule> maildirfilter;

/****************************************************************************/
/*             Low-level filter access functions                            */
/****************************************************************************/

/*
** A maildirfilter structure is initialized simply by nulling it out, then: */

maildirfilterrule *maildir_filter_appendrule(
	maildirfilter &r,
	std::string_view name,
	enum maildirfiltertype type,
	int flags,
	std::string_view header,
	std::string_view value,
	std::string_view folder,
	std::string_view fromhdr,
	std::string rulecharset,
	int &errcode
);	/* Append a new rule */


/* Update an existing rule */

bool maildir_filter_ruleupdate(
	maildirfilter &r,
	maildirfilterrule &p,
	std::string_view name,
	enum maildirfiltertype,
	int flags,
	std::string_view header,
	std::string_view value,
	std::string_view folder,
	std::string_view fromhdr,
	std::string rulecharset,
	int &errcode
);

/*
** maildir_filter_appendrule and maildir_filter_ruleupdate set err_code to the following upon an error
** exit
*/

#define	MF_ERR_BADRULENAME	1
#define	MF_ERR_BADRULETYPE	2
#define	MF_ERR_BADRULEHEADER	3
#define	MF_ERR_BADRULEVALUE	4
#define	MF_ERR_BADRULEFOLDER	5
#define MF_ERR_BADFROMHDR	6
#define MF_ERR_EXISTS		7
#define MF_ERR_INTERNAL		100

/* Save/Load rules from the given file */

bool maildir_filter_saverules(const maildirfilter &,
		 const std::string &,		/* Filename */
		 std::string_view,		/* Path to maildir from mailfilter */
		 std::string_view);		/* The return address */

int maildir_filter_loadrules(maildirfilter &,
		 const std::string &);		/* Filename */

#define	MF_LOADOK	0
#define	MF_LOADNOTFOUND	1
#define	MF_LOADFOREIGN	2
#define	MF_LOADERROR	3

/****************************************************************************/
/*             High-level filter access functions                            */
/****************************************************************************/

namespace maildir {
	namespace filter {
#if 0
	}
}
#endif

bool import(std::string_view); /* Get the maildir filter */
bool load(maildirfilter &, std::string_view);
bool save(const maildirfilter &, std::string_view, std::string_view);
bool commit(std::string_view); /* Commit the maildir filter */
bool has(std::string_view); /* Is maildir filter defined? */

void cancel(std::string_view); /* Remove the temp file */


#if 0
{
	{
#endif
	}
}

	/*
	** A convenient structure to parse autoresponder parameters.
	*/

struct maildir_filter_autoresp_info {
	std::string name;
	int mode=0;
	unsigned days=0;
} ;

#define MAILDIR_FILTER_AUTORESP_MODE_DSN	1
#define MAILDIR_FILTER_AUTORESP_MODE_NOQUOTE	2

bool maildir_filter_autoresp_info_init_str(maildir_filter_autoresp_info &,
	std::string_view);
bool maildir_filter_autoresp_info_init(maildir_filter_autoresp_info &,
	const char *);
std::string maildir_filter_autoresp_info_asstr(maildir_filter_autoresp_info &);

#endif
