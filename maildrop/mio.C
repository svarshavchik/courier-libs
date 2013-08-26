#include	"config.h"
#include	<sys/types.h>
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	<errno.h>

#include	"mio.h"
#include	"funcs.h"
#include	"buffer.h"
#include	"rfc2045/rfc2045.h"

int mopen(const char *fname, int flags, mode_t mode)
{
int	rc;

	while ((rc=open(fname, flags, mode)) < 0 && errno == EINTR)
		;
	return (rc);
}

int mread(int fd, void *buf, size_t count)
{
int	n;

	while ((n=read(fd, buf, count)) < 0 && errno == EINTR)
		;
	return (n);
}

int mwrite(int fd, const void *buf, size_t count)
{
int	n;

	while ((n=write(fd, buf, count)) < 0 && errno == EINTR)
		;
	return (n);
}

off_t mseek(int fd, off_t off, int whence)
{
off_t	n;

	while ((n=lseek(fd, off, whence)) < 0 && errno == EINTR)
		;
	return (n);
}

int mclose(int fd)
{
int	n;

	while ((n=close(fd)) < 0 && errno == EINTR)
		;
	return (n);
}

//-------------------------------------------------------------------------

Mio::Mio() : fd_(-1), readcnt(0), writecnt(0), err(0), rfc2045p(0)
{
}

Mio::~Mio() { Close(); }

void Mio::Close()
{
	if (fd_ < 0)	return;
	if (writeptr > buf)	flush(-1);
	mclose(fd_);
	fd_= -1;
}

int Mio::Open(const char *filename, int flags, mode_t m)
{
	fd(mopen(filename, flags, m));
	return (fd_);
}

int Mio::fill()
{
	readcnt=0;
	if (fd_ < 0 || err)	return (-1);
	if (writeptr > buf && flush(-1) < 0)	return (-1);

	readstartpos=tell();
	readcnt=mread(fd_, buf, sizeof(buf));
	readsize=readcnt;

	if (readcnt <= 0)
	{
		readcnt=0;
		readsize=0;
		err= -1;
		return (-1);
	}
	if (rfc2045p)
		rfc2045_parse(rfc2045p, (const char *)buf, readcnt);

	--readcnt;
	return (*(readptr=buf)++);
}

int Mio::flush(int c)
{
	if (fd_ < 0 || err)	return (-1);
	if (readcnt && mseek(fd_, -readcnt, SEEK_CUR) < 0)	return (-1);
	readcnt=0;
	readptr=buf;

	while (writeptr > buf)
	{
	int	n;

		if ((n=mwrite(fd_, buf, writeptr-buf)) <= 0)
		{
			err= -1;
			return (-1);
		}
		memorycopy(buf, buf+n, writeptr-buf-n);
		writeptr -= n;
	}

	writecnt=sizeof(buf);
	if (c >= 0)
	{
		--writecnt;
		*writeptr++ = c;
	}
	return (0);
}

int Mio::Rewind()
{
	readstartpos= -1;
	return ( seek (0L, SEEK_SET) );
}

int Mio::seek(off_t off, int whence)
{
	if (fd_ < 0)	return (-1);
	if (writeptr > buf && flush(-1) < 0)	return (-1);
	if (whence == SEEK_CUR && err == 0)	off -= readcnt;

	// Optimize - seek can be satisfied within the read buffer.

	if (whence == SEEK_SET && readstartpos != -1 &&
		off >= readstartpos && off <= readstartpos + readsize)
	{
		readptr = buf + (off - readstartpos);
		readcnt = readsize - (off - readstartpos);
		return (0);
	}

	if (mseek(fd_, off, whence) < 0)
	{
		err= -1;
		return (-1);
	}
	err=0;
	readcnt=0;
	readptr=buf;
	return (0);
}

int Mio::write(const void *p, int cnt)
{
const unsigned char *cp=(const unsigned char *)p;
int	done=0;

	if (fd_ < 0 || err)	return (-1);

	while (cnt)
	{
	int	l;

		if (writecnt == 0 && flush() < 0)
		{
			if (done == 0)	done= -1;
			break;
		}

		if (writeptr > buf || cnt <= writecnt)
		{
			l=writecnt;

			if (l > cnt)	l=cnt;
			memcpy(writeptr, cp, l);
			writecnt -= l;
			writeptr += l;
		}
		else
		{
			l=mwrite(fd_, cp, cnt);
			if (l <= 0)
			{
				if (done == 0)	done= -1;
				break;
			}
		}

		cp += l;
		done += l;
		cnt -= l;
	}
	return (done);
}

off_t Mio::tell()
{
	if (fd_ < 0)	return (0);

off_t	p=mseek(fd_, 0L, SEEK_CUR);

	if (p == -1)	return (-1);
	return (p + (writeptr - buf) - readcnt);
}

Mio &Mio::operator<<(const class Buffer &b)
{
	write( (const char *)b, b.Length() );
	return (*this);
}

MioStdio mout(1), merr(2);

MioStdio::MioStdio(int f)
{
	fd(f);
}

MioStdio::~MioStdio()
{
	fd(-1);
}

int MioStdio::write(const void *p, int cnt)
{
int n;

	n=Mio::write(p, cnt);
	if (n > 0)	Mio::flush();
	return (n);
}

extern MioStdio mout, merr;
