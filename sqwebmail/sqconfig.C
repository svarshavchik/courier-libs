/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"sqconfig.h"
#include	<fcntl.h>
#include	<string.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"maildir/maildircreate.h"
#include "rfc822/rfc822.h"
#include <string>
#include <fstream>

std::optional<std::string> read_sqconfig(const char *dir, const char *configfile, time_t *mtime)
{
	std::string p;
	p.reserve(strlen(dir) + strlen(configfile) + 1);
	p.append(dir);
	p.append("/");
	p.append(configfile);

	struct stat stat_buf;

	rfc822::fdstreambuf fsb{
		open(p.c_str(), O_RDONLY)
	};
	if (fsb.error())
		return {};
	std::istream f{&fsb};

	std::string linebuf;
	if (fstat(fsb.fileno(), &stat_buf) != 0 ||
		!std::getline(f, linebuf))
	{
		return {};
	}
	if (mtime)	*mtime=stat_buf.st_mtime;

	return linebuf;
}

void write_sqconfig(const char *dir, const char *configfile, const char *val)
{
	std::string p;
	p.reserve(strlen(dir) + strlen(configfile) + 1);
	p.append(dir);
	p.append("/");
	p.append(configfile);

	maildir::tmpcreate_info createInfo;
	FILE *fp;

	if (!val)
	{
		unlink(p.c_str());
		return;
	}

	createInfo.maildir=dir;
	createInfo.uniq="config";
	createInfo.doordie=true;

	fp=createInfo.fp();

	if (!fp)
		enomem();

	createInfo.newname=p;
	fprintf(fp, "%s\n", val);
	fflush(fp);
	if (ferror(fp))	eio("Error after write:",p.c_str());
	fclose(fp);

	/* Note - umask should already turn off the 077 bits, but
	** just in case someone screwed up previously, I'll fix it
	** myself */

	chmod(createInfo.tmpname.c_str(), 0600);
	rename(createInfo.tmpname.c_str(), createInfo.newname.c_str());
}
