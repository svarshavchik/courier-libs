/*
** Copyright 2021 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif

#include "maildirinfo.h"
#include "maildirmisc.h"
#include "maildirnewshared.h"

#include <cstring>
#include <courier-unicode.h>

namespace maildir {
#if 0
}
#endif

struct info {
	int mailbox_type=MAILBOXTYPE_ERROR;
	std::string homedir;
	std::string maildir;
	std::string owner;

	operator bool() const
	{
		return mailbox_type != MAILBOXTYPE_ERROR;
	}
};

struct imap_find_shared {
	const char *path;
	size_t path_l;

	std::string homedir;
	std::string maildir;
};

static int imap_find_cb(struct maildir_newshared_enum_cb *cb)
{
	imap_find_shared *ifs=
		reinterpret_cast<imap_find_shared *>(cb->cb_arg);

	if (cb->homedir)
	{
		ifs->homedir=cb->homedir;
	}

	if (cb->maildir)
	{
		ifs->maildir=cb->maildir;
	}

	return 0;
}

info info_imap_find(const char *path,
		    const char *myId)
{
	info ret;
	info *info=&ret;

	const char *p;
	imap_find_shared ifs;
	std::string indexfile;
	struct maildir_shindex_cache *curcache;
	const char *subhierarchy;

	if (strchr(path, '/'))
	{
		errno=EINVAL;
		return ret;
	}

	for (p=path; *p; p++)
		if (*p == '.' && p[1] == '.')
		{
			errno=EINVAL;
			return ret;
		}

	if (strncmp(path, SHARED, sizeof(SHARED)-1) == 0)
	{
		path += sizeof(SHARED)-1;

		info->homedir=".";

		info->mailbox_type=MAILBOXTYPE_OLDSHARED;
		info->owner="anonymous";

		/* We need to specialcase "shared" and "shared.name".
		** maildir_shareddir will return NULL for these cases, because
		** it will insist on "name.folder", but we need to return a
		** non NULL value to indicate that this is a valid hierarchy
		** name.  We return a special value of an empty string, which
		** is checked for in situations where a valid folder is
		** required.
		*/

		if (*path && *path != '.')
		{
			errno=EINVAL;
			ret={};
			return ret;
		}

		return ret;
	}

	if (strncasecmp(path, INBOX, sizeof(INBOX)-1) == 0)
	{
		switch (path[sizeof(INBOX)-1]) {
		case 0:
		case '.':
			break;
		default:
			errno=EINVAL;
			return ret;
		}

		info->homedir=".";

		info->maildir=path;

		info->mailbox_type=MAILBOXTYPE_INBOX;
		info->owner=std::string{"user="}+myId;

		return ret;
	}

	if (strncmp(path, NEWSHARED,
		    sizeof(NEWSHARED)-1) != 0)
	{
		errno=EINVAL;
		return ret;
	}

	ifs.path=path+sizeof(NEWSHARED)-1;

	info->mailbox_type=MAILBOXTYPE_NEWSHARED;
	info->homedir.clear();
	info->owner="vendor=courier.internal";

	curcache=NULL;
	subhierarchy=NULL;

	while (*ifs.path)
	{
		int rc, eof;
		size_t i;

		curcache=maildir_shared_cache_read(curcache, indexfile.c_str(),
						   subhierarchy);

		if (!curcache)
			break;

		p=strchr(ifs.path, '.');

		if (p)
			ifs.path_l=p-ifs.path;
		else
			ifs.path_l=strlen(ifs.path);

		ifs.homedir.clear();
		ifs.maildir.clear();

		for (i=0; i < curcache->nrecords; i++)
		{
			char *n=maildir_info_imapmunge(curcache->
						       records[i].name);

			if (n == NULL)
			{
				i=curcache->nrecords;
				break;
			}

			if (strlen(n) == ifs.path_l &&
			    strncmp(n, ifs.path, ifs.path_l) == 0)
			{
				free(n);
				break;
			}
			free(n);
		}

		if (i >= curcache->nrecords)
			break;

		curcache->indexfile.startingpos=
			curcache->records[i].offset;
		rc=maildir_newshared_nextAt(&curcache->indexfile,
					    &eof,
					    imap_find_cb, &ifs);

		indexfile.clear();

		if (rc || eof)
		{
			fprintf(stderr, "ERR: Internal error -"
				" maildir_newshared_nextAt: %s\n",
				strerror(errno));
			fflush(stderr);
			break;
		}

		if (ifs.homedir.empty() && ifs.maildir.empty())
			break;

		if (ifs.homedir.empty())
		{
			indexfile=ifs.maildir;
			subhierarchy=curcache->records[i].name;

			ifs.path += ifs.path_l;
			if (*ifs.path)
				++ifs.path;
			continue;
		}

		{
			auto location=maildir_location(ifs.homedir.c_str(),
						       ifs.maildir.c_str());

			if (!location)
			{
				ret={};
				return ret;
			}

			info->homedir=location;
			free(location);
		}

		if (!subhierarchy || !*subhierarchy)
		{
			info->owner="vendor=courier.internal";
		}
		else
		{
			char *owner_utf8;

			info->owner=std::string{"user="}+subhierarchy;

			/*
			** The folder path is in modified-UTF7.  The owner is
			** obtained from shared hierarchy, but in ACL2 the
			** identifiers are in UTF8.
			*/

			owner_utf8=
				unicode_convert_tobuf(info->owner.c_str(),
							unicode_x_imap_modutf7,
							"utf-8", NULL);

			if (!owner_utf8)
			{
				info->homedir.clear();
				return (ret);
			}

			info->owner=owner_utf8;
			free(owner_utf8);
		}

		ifs.path += ifs.path_l;

		info->maildir=std::string{INBOX}+ifs.path;

		if (maildir_info_suppress(info->homedir.c_str()))
		{
			info->homedir.clear();
			info->maildir.clear();
			info->mailbox_type=MAILBOXTYPE_IGNORE;
			info->owner="vendor=courier.internal";
		}

		return ret;
	}

	return ret;
}

struct get_existing_folder_info {
	char **fn;

	std::string pathname;
};

static void get_existing_callback(const char *f, void *vp)
{
	char **fn;

	get_existing_folder_info *gefi=
		reinterpret_cast<get_existing_folder_info *>(vp);
	size_t i;
	size_t j;

	if (!gefi->pathname.empty())
		return;

	fn=maildir_smapfn_fromutf8(f);
	if (!fn)
	{
		perror(f);
		return;
	}

	for (i=0; gefi->fn[i]; i++)
		if (fn[i] == NULL || strcmp(fn[i], gefi->fn[i]))
		{
			maildir_smapfn_free(fn);
			return;
		}

	maildir_smapfn_free(fn);

	for (j=0; i && f[j]; j++)
		if (f[j] == '.' && f[j+1] && f[j+1] != '.')
		{
			--i;
			if (i == 0)
				break;
		}

	gefi->pathname=std::string{f, f+j};
}
/*
** Maildir folders are named in IMAP-compatible modified-UTF8 encoding,
** with periods as hierarchy delimiters.  One exception: ".", "/", "~", and
** ":" are also encoded using modified-UTF8, making folder names that contain
** those characters incompatible with IMAP.
**
** smap_to_utf8 crates a modified-UTF8-encoded folder name from a vector
** of UTF-8 words.
**
** input:  "INBOX" "a" "b"
** output: "INBOX.a.b"
**
*/

static std::string smap_to_utf8(char **ptr)
{
	std::string f;
	char *n;

	while ((n=*ptr++) != NULL && *n)
	{
		char *p=unicode_convert_tobuf(n, "utf-8",
					      unicode_x_smap_modutf8,
					      NULL);

		if (!p)
		{
			f.clear();
			return f;
		}

		if (!f.empty())
			f += ".";
		f += p;
		free(p);
	}

	return f;
}

static std::string smap_path(const char *homedir,
			     char **words)  /* words[0] better be INBOX! */
{
	struct get_existing_folder_info gefi;
	char *p;
	struct stat stat_buf;

	std::string n=smap_to_utf8(words);

	if (n.empty())
		return n;

	p=maildir_name2dir(homedir, n.c_str());

	if (!p)
	{
		n.clear();
		return n;
	}

	if (stat(p, &stat_buf) == 0)
	{
		free(p);
		return n;
	}

	gefi.fn=words;

	maildir_list(homedir ? homedir:".",
		     &get_existing_callback, &gefi);

	if (!gefi.pathname.empty())
	{
		free(p);

		return gefi.pathname.c_str();
	}

	free(p);
	return n;
}

info info_smap_find(char **folder,
		    const char *myId)
{
	info ret;
	info *info=&ret;

	char *p;
	size_t n;
	const char *indexfile;
	struct maildir_shindex_cache *curcache;
	const char *subhierarchy;
	struct imap_find_shared ifs;
	int rc, eof;
	std::string indexfile_cpy;

	info->mailbox_type=MAILBOXTYPE_IGNORE;

	if (folder[0] == NULL)
	{
		errno=EINVAL;
		ret={};
		return ret;
	}

	if (strcmp(folder[0], PUBLIC))
	{
		if (strcmp(folder[0], INBOX))
		{
			errno=EINVAL;
			ret={};
			return ret;
		}

		info->maildir=smap_path(NULL, folder);
		if (info->maildir.empty())
		{
			ret={};
			return ret;
		}
		info->homedir=".";

		info->mailbox_type=MAILBOXTYPE_INBOX;

		info->owner=std::string{"user="}+myId;

		return ret;
	}

	indexfile=NULL;
	curcache=NULL;
	subhierarchy=NULL;
	n=1;

	while (folder[n])
	{
		size_t i;

		curcache=maildir_shared_cache_read(curcache, indexfile,
						   subhierarchy);

		if (!curcache)
			break;

		for (i=0; i<curcache->nrecords; i++)
			if (strcmp(curcache->records[i].name,
				   folder[n]) == 0)
				break;

		if (i >= curcache->nrecords)
			break;
		curcache->indexfile.startingpos=
			curcache->records[i].offset;

		ifs.homedir.clear();
		ifs.maildir.clear();

		rc=maildir_newshared_nextAt(&curcache->indexfile,
					    &eof,
					    imap_find_cb, &ifs);

		if (rc || eof)
		{
			fprintf(stderr, "ERR: Internal error -"
				" maildir_newshared_nextAt: %s\n",
				strerror(errno));
			fflush(stderr);
			break;
		}

		if (ifs.homedir.empty() && ifs.maildir.empty())
			break;

		if (ifs.homedir.empty())
		{
			indexfile_cpy=ifs.maildir;
			indexfile=indexfile_cpy.c_str();
			subhierarchy=curcache->records[i].name;
			++n;
			continue;
		}


		{
			auto location=maildir_location(ifs.homedir.c_str(),
							ifs.maildir.c_str());
			if (!location)
			{
				ret={};
				return ret;
			}
			info->homedir=location;
			free(location);
		}

		info->maildir.clear();

		if (maildir_info_suppress(info->homedir.c_str()))
		{
			info->homedir.clear();
			info->maildir.clear();
			info->mailbox_type=MAILBOXTYPE_IGNORE;
			info->owner="vendor=courier.internal";

			return ret;
		}


		if (!subhierarchy || !*subhierarchy)
		{
			info->owner="vendor=courier.internal";
		}
		else
		{
			info->owner=std::string{"user="}+subhierarchy;
		}

		p=folder[n];

		static char inbox_s[]="INBOX";
		folder[n]=inbox_s;

		info->maildir=smap_path(info->homedir.c_str(), folder+n);

		folder[n]=p;

		if (info->maildir.empty())
		{
			ret={};
			return ret;
		}

		info->mailbox_type=MAILBOXTYPE_NEWSHARED;
		return ret;
	}

	if (folder[n] == 0)
	{
		info->mailbox_type=MAILBOXTYPE_NEWSHARED;
		info->owner="vendor=courier.internal";

		/* Intermediate shared namespce */
		return ret;
	}

	ret={};
	return ret;
}

#if 0
{
#endif
}

extern "C"
int maildir_info_imap_find(struct maildir_info *info, const char *path,
			   const char *myid)
{
	memset(info, 0, sizeof(*info));

	maildir::info n=maildir::info_imap_find(path, myid);

	info->mailbox_type=n.mailbox_type;

	if (!n
	    || (!n.owner.empty() &&
		!(info->owner=strdup(n.owner.c_str())))
	    || (!n.maildir.empty() &&
		!(info->maildir=strdup(n.maildir.c_str())))
	    || (!n.homedir.empty() &&
		!(info->homedir=strdup(n.homedir.c_str()))))
	{
		maildir_info_destroy(info);
		return -1;
	}

	return 0;
}

extern "C"
int maildir_info_smap_find(struct maildir_info *info, char **folder,
			   const char *myid)
{
	memset(info, 0, sizeof(*info));

	maildir::info n=maildir::info_smap_find(folder, myid);

	info->mailbox_type=n.mailbox_type;

	if (!n
	    || (!n.owner.empty() &&
		!(info->owner=strdup(n.owner.c_str())))
	    || (!n.maildir.empty() &&
		!(info->maildir=strdup(n.maildir.c_str())))
	    || (!n.homedir.empty() &&
		!(info->homedir=strdup(n.homedir.c_str()))))
	{
		maildir_info_destroy(info);
		return -1;
	}
	return 0;
}
