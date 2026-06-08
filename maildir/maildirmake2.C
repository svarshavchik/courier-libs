#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<fcntl.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<errno.h>

#include	"maildirmisc.h"

#include	<filesystem>
#include	<optional>

bool maildir::make(
	std::string_view maildir,
	int perm,
	int subdirperm,
	bool folder)
{
	std::error_code ec;
	std::optional<std::filesystem::path> p;

	try {
		p=std::filesystem::path(maildir);
	} catch (const std::exception &e) {
		return false;
	}

	auto &q=*p;
	int fd= -1;

	if (mkdir(q.c_str(), perm) < 0 ||
		mkdir( (q / "tmp").c_str(), subdirperm) < 0 ||
		mkdir( (q / "new").c_str(), subdirperm) < 0 ||
		mkdir( (q / "cur").c_str(), subdirperm) < 0 ||
		(folder && (fd=open( (q / "maildirfolder").c_str(),
					O_CREAT|O_WRONLY, 0600)) < 0))
	{
		return false;
	}
	if (fd >= 0)
		close(fd);
	return true;
}

bool maildir::del(std::string_view maildir)
{
	std:: error_code ec;

	std::filesystem::remove_all(maildir, ec);

	return !ec;
}
