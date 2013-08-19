#include	"filelock.h"
#include	"mio.h"
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#if HAVE_SYS_STAT_H
#include        <sys/stat.h>
#endif
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if HAVE_SYS_FILE_H
#include	<sys/file.h>
#endif
#include	<errno.h>

#include	"liblock/config.h"
#include	"liblock/liblock.h"


void FileLock::cleanup()
{
	UnLock();
}

void FileLock::forked()
{
	UnLock();
}

FileLock::FileLock() : fd(-1)
{
}

FileLock::~FileLock()
{
	UnLock();
}

void FileLock::Lock(const char *filename)
{
	UnLock();

	if ((fd=mopen(filename, O_CREAT | O_WRONLY, 0600)) < 0)
		throw "Unable to create flock file.";

	do_filelock(fd);
}


void FileLock::do_filelock(int fd)
{
int	flockrc;

	while ((flockrc=ll_lock_ex(fd)) < 0 && errno == EINTR)
		;

	if (flockrc < 0)
	{
	struct stat stat_buf;

		// FreeBSD has problems locking /dev/null.  Presume that if
		// you're writing to a device file, you know what you're doing.

		if (fstat(fd, &stat_buf) >= 0 && (
			S_ISREG(stat_buf.st_mode) || S_ISDIR(stat_buf.st_mode)))
		{
			return;
		}

		throw "flock() failed.";
	}
}

void FileLock::UnLock()
{
	if (fd >= 0)
	{
		mclose(fd);
		fd= -1;
	}
}
