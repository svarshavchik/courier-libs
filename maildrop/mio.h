#ifndef	mio_h
#define	mio_h


#include	"config.h"
#include	<sys/types.h>
#include	<string.h>

// Fault-tolerant wrappers around I/O functions - they automatically retry
// when they get an EINTR (for kernels that do not restart the calls themselves)

int mopen(const char *fname, int flags, mode_t mode);
int mread(int fd, void *buf, size_t count);
int mwrite(int fd, const void *buf, size_t count);
int mclose(int fd);
off_t mseek(int, off_t, int);

/////////////////////////////////////////////////////////////////////////////
//
// Class Mio - reinventing the stdio wheel.
//
/////////////////////////////////////////////////////////////////////////////

class Mio {
	int fd_;
	unsigned char *readptr;
	unsigned char *writeptr;
	off_t readstartpos;
	int readsize;
	int readcnt;
	int writecnt;
	int err;

	unsigned char buf[8192];
public:
	struct rfc2045 *rfc2045p;

	Mio();
	virtual ~Mio();
	int Open(const char *, int, mode_t=0666);
	void Close();
	int peek() { return (readcnt ? *readptr:
			fill() < 0 ? -1:(++readcnt,*--readptr)); }
	int get() { return (readcnt ? (--readcnt,*readptr++):fill()); }
	int put(int c) { return (writecnt ?
				(--writecnt,*writeptr++=c):flush(c)); }
	int seek(off_t, int);
	int Rewind();
	off_t tell();
	virtual int write(const void *, int);
	int fd() { return (fd_); }
	void fd(int f) { Close();
			err=0;
			readcnt=0;
			writecnt=0;
			readptr=buf;
			writeptr=buf;
			fd_=f; }

	int errflag() { return (err); }
	void my_clearerr() { err=0; }
	int flush() { return (flush(-1)); }

	void write(const char *p) { write(p, strlen(p)); }

	Mio &operator<<(const char *p) { write(p); return (*this); }
	Mio &operator<<(const class Buffer &);

private:
	int fill();
	int flush(int);
} ;

class MioStdio : public Mio {
public:
	MioStdio(int);
	~MioStdio();
	int write(const void *, int);
	void write(const char *p) { write(p, strlen(p)); }
} ;

extern MioStdio mout, merr;

#endif
