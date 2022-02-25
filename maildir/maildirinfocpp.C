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

#include <iostream>
namespace maildir {
#if 0
}
#endif

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

info info_imap_find(const std::string &path,
		    const std::string &myId)
{
	info ret;

	const char *p;
	imap_find_shared ifs;
	std::string indexfile;
	struct maildir_shindex_cache *curcache;
	const char *subhierarchy;

	if (path.find('/') != path.npos)
	{
		errno=EINVAL;
		return ret;
	}

	char prev_char=0;

	for (auto p:path)
	{
		if (p == '.' && prev_char == '.')
		{
			errno=EINVAL;
			return ret;
		}

		prev_char=p;
	}

	if (strncmp(path.c_str(), SHARED, sizeof(SHARED)-1) == 0)
	{
		ret.homedir=".";

		ret.mailbox_type=MAILBOXTYPE_OLDSHARED;
		ret.owner="anonymous";

		/* We need to specialcase "shared" and "shared.name".
		** maildir_shareddir will return NULL for these cases, because
		** it will insist on "name.folder", but we need to return a
		** non NULL value to indicate that this is a valid hierarchy
		** name.  We return a special value of an empty string, which
		** is checked for in situations where a valid folder is
		** required.
		*/

		switch (path.c_str()[sizeof(SHARED)-1]) {
		case '\0':
		case '.':
			break;
		default:
			errno=EINVAL;
			ret={};
			return ret;
		}

		return ret;
	}

	if (strncasecmp(path.c_str(), INBOX, sizeof(INBOX)-1) == 0)
	{
		switch (path[sizeof(INBOX)-1]) {
		case 0:
		case '.':
			break;
		default:
			errno=EINVAL;
			return ret;
		}

		ret.homedir=".";

		ret.maildir=path;

		ret.mailbox_type=MAILBOXTYPE_INBOX;
		ret.owner=std::string{"user="}+myId;

		return ret;
	}

	if (strncmp(path.c_str(), NEWSHARED,
		    sizeof(NEWSHARED)-1) != 0)
	{
		errno=EINVAL;
		return ret;
	}

	ifs.path=path.c_str()+sizeof(NEWSHARED)-1;

	ret.mailbox_type=MAILBOXTYPE_NEWSHARED;
	ret.homedir.clear();
	ret.owner="vendor=courier.internal";

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

		ret.homedir=maildir::location(ifs.homedir, ifs.maildir);

		if (!subhierarchy || !*subhierarchy)
		{
			ret.owner="vendor=courier.internal";
		}
		else
		{
			char *owner_utf8;

			ret.owner=std::string{"user="}+subhierarchy;

			/*
			** The folder path is in modified-UTF7.  The owner is
			** obtained from shared hierarchy, but in ACL2 the
			** identifiers are in UTF8.
			*/

			owner_utf8=
				unicode_convert_tobuf(ret.owner.c_str(),
						      unicode_x_imap_modutf7,
						      "utf-8", NULL);

			if (!owner_utf8)
			{
				ret.homedir.clear();
				return (ret);
			}

			ret.owner=owner_utf8;
			free(owner_utf8);
		}

		ifs.path += ifs.path_l;

		ret.maildir=std::string{INBOX}+ifs.path;

		if (maildir_info_suppress(ret.homedir.c_str()))
		{
			ret.homedir.clear();
			ret.maildir.clear();
			ret.mailbox_type=MAILBOXTYPE_IGNORE;
			ret.owner="vendor=courier.internal";
		}

		return ret;
	}

	return ret;
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

static std::string smap_to_utf8(const smap_words_t &words)
{
	std::string f;

	for (auto &n:words)
	{
		if (!*n)
			break;

		if (!f.empty())
			f += ".";

		f += unicode::iconvert::convert(n,
						"utf-8",
						unicode_x_smap_modutf8);
	}

	return f;
}

static std::string smap_path(
	const std::string &homedir,
	const smap_words_t &words)  /* words[0] better be INBOX! */
{
	struct stat stat_buf;

	std::string n=smap_to_utf8(words);

	if (n.empty())
		return n;

	auto p=maildir::name2dir(homedir, n);

	if (p.empty())
	{
		n.clear();
		return n;
	}

	if (stat(p.c_str(), &stat_buf) == 0)
	{
		return n;
	}

	std::string out_pathname;

	maildir::list(
		homedir,
		[&]
		(const std::string &s)
		{
			if (!out_pathname.empty())
				return;

			auto fn=maildir_smapfn_fromutf8(s.c_str());

			if (!fn)
			{
				perror(s.c_str());
				return;
			}

			size_t i;

			for (i=0; i<words.size(); i++)
			{
				if (fn[i] == NULL ||
				    words[i] != fn[i])
				{
					maildir_smapfn_free(fn);
					return;
				}
			}

			maildir_smapfn_free(fn);

			size_t j;
			for (j=0; i && j<s.size(); j++)
				if (s[j] == '.' &&
				    j+1 < s.size() &&
				    s[j+1] != '.')
				{
					--i;
					if (i == 0)
						break;
				}

			out_pathname=s.substr(0, j);
		});

	if (!out_pathname.empty())
	{
		return out_pathname;
	}

	return n;
}

info info_smap_find(char **folder, const std::string &myId)
{
	smap_words_t folderv;

	for (size_t i=0; folder[i]; ++i)
		folderv.push_back(folder[i]);
	return info_smap_find(folderv, myId);
}

info info_smap_find(const smap_words_t &folder,
		    const std::string &myId)
{

	info ret;

	size_t n;
	const char *indexfile;
	struct maildir_shindex_cache *curcache;
	const char *subhierarchy;
	struct imap_find_shared ifs;
	int rc, eof;
	std::string indexfile_cpy;

	ret.mailbox_type=MAILBOXTYPE_IGNORE;

	if (folder.size() == 0)
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

		ret.maildir=smap_path(".", folder);
		if (ret.maildir.empty())
		{
			ret={};
			return ret;
		}
		ret.homedir=".";

		ret.mailbox_type=MAILBOXTYPE_INBOX;

		ret.owner=std::string{"user="}+myId;

		return ret;
	}

	indexfile=NULL;
	curcache=NULL;
	subhierarchy=NULL;
	n=1;

	while (n < folder.size())
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

		ret.homedir=maildir::location(ifs.homedir, ifs.maildir);
		ret.maildir.clear();

		if (maildir_info_suppress(ret.homedir.c_str()))
		{
			ret.homedir.clear();
			ret.maildir.clear();
			ret.mailbox_type=MAILBOXTYPE_IGNORE;
			ret.owner="vendor=courier.internal";

			return ret;
		}


		if (!subhierarchy || !*subhierarchy)
		{
			ret.owner="vendor=courier.internal";
		}
		else
		{
			ret.owner=std::string{"user="}+subhierarchy;
		}

		smap_words_t new_path;

		new_path.reserve(folder.size()-n);
		new_path.push_back(INBOX);

		for (auto p=folder.begin()+n+1; p != folder.end(); ++p)
			new_path.push_back(*p);

		ret.maildir=smap_path(ret.homedir, new_path);

		if (ret.maildir.empty())
		{
			ret={};
			return ret;
		}

		ret.mailbox_type=MAILBOXTYPE_NEWSHARED;
		return ret;
	}

	if (n >= folder.size())
	{
		ret.mailbox_type=MAILBOXTYPE_NEWSHARED;
		ret.owner="vendor=courier.internal";

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
