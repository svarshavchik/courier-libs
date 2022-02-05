/*
** Copyright 2003 S. Varshavchik.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	"maildirkeywords.h"
#include	"maildircreate.h"
#include	"maildirwatch.h"
#include	"numlib/numlib.h"

bool mail::keywords::save_keywords_from(
	const std::string &maildir,
	const std::string &filename,
	const std::function<void (FILE *)> &saver,
	std::string &tmpname,
	std::string &newname,
	bool try_atomic)
{
	maildir::tmpcreate_info create_info;

	newname.reserve(maildir.size()+filename.size()+10+
			sizeof(KEYWORDDIR));

	newname=maildir;

	newname += "/" KEYWORDDIR "/";

	auto p=newname.size();

	newname += filename;

	p=newname.find(MDIRSEP[0], p);

	if (p != newname.npos)
		newname.resize(p);

	create_info.maildir=maildir;
	create_info.hostname=getenv("HOSTNAME");

	auto fp=create_info.fp();

	if (!fp)
		return false;

	saver(fp);

	errno=EIO;

	if (fflush(fp) < 0 || ferror(fp))
	{
		fclose(fp);
		return false;
	}

	fclose(fp);

	tmpname=create_info.tmpname;

	if (try_atomic)
	{
		char timeBuf[NUMBUFSIZE];

		std::string n;

		n.reserve(tmpname.size()
			  + sizeof(KEYWORDDIR) + NUMBUFSIZE+10);

		n=maildir;

		n += "/" KEYWORDDIR "/.tmp.";

		n += libmail_str_time_t(time(NULL), timeBuf);

		n += tmpname.substr(tmpname.rfind('/')+1);

		if (rename( tmpname.c_str(), n.c_str()) < 0)
			return false;

		tmpname=n;
	}
	return true;
}

/***************/

static int maildir_kwSaveCommon(const char *maildir,
				const char *filename,
				struct libmail_kwMessage *newKeyword,
				const char **newKeywordArray,
				char **tmpname,
				char **newname,
				int tryAtomic);

int maildir_kwSave(const char *maildir,
		   const char *filename,
		   struct libmail_kwMessage *newKeyword,
		   char **tmpname,
		   char **newname,
		   int tryAtomic)
{
	return maildir_kwSaveCommon(maildir, filename, newKeyword, NULL,
				    tmpname, newname, tryAtomic);
}

int maildir_kwSaveArray(const char *maildir,
			const char *filename,
			const char **flags,
			char **tmpname,
			char **newname,
			int tryAtomic)
{
	return maildir_kwSaveCommon(maildir, filename, NULL, flags,
				    tmpname, newname, tryAtomic);
}

static int maildir_kwSaveCommon(const char *maildir,
				const char *filename,
				struct libmail_kwMessage *newKeyword,
				const char **newKeywordArray,
				char **tmpname,
				char **newname,
				int tryAtomic)
{
	std::string tmpnames;
	std::string newnames;

	if (!mail::keywords::save_keywords_from(
		    maildir,
		    filename,
		    [&]
		    (FILE *fp)
		    {
			    if (newKeywordArray)
			    {
				    size_t i;

				    for (i=0; newKeywordArray[i]; i++)
					    fprintf(fp, "%s\n",
						    newKeywordArray[i]);
				    return;
			    }
			    for (auto kme=newKeyword ?
					 newKeyword->firstEntry:NULL;
				 kme; kme=kme->next)
				    fprintf(fp, "%s\n",
					    keywordName(
						    kme->libmail_keywordEntryPtr
					    ));
		    },
		    tmpnames,
		    newnames,
		    tryAtomic))
		return -1;

	if (!(*tmpname=strdup(tmpnames.c_str())) ||
	    !(*newname=strdup(newnames.c_str())))
		abort();

	return 0;
}
