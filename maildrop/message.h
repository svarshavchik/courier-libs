#ifndef	message_h
#define	message_h

#include "rfc2045/rfc2045.h"
#include "rfc822/rfc822.h"
#include <string>

//////////////////////////////////////////////////////////////////////////////
//
// A Message object represents the message being filtered.  It is expected
// that the message is going to be scanned, repetitively, from start to
// finish, in one direction.  Therefore, access to the message contents is
// sequential.
//
// A Message object can be initiated in two ways.  It can be initiated from
// a file.  The Message object will check if the file is seekable.  If so,
// the Message object will simply use the rfc822::fdstreambuf class to access
// the file.
//
// If the file is not seekable, the message will be saved in a temporary
// file, and the rfc822::fdstreambuf class will be used to access the
// temporary file.
//
// However, if the message size turns out to be smaller the SMALLMSG bytes,
// the message is saved in a memory buffer, and accessed from there, for
// speed.  We presume that most messages are less than SMALLMSG bytes long.
//
// The second way is for the messages contents to be manually specified.
// For this initialization method, first the message is saved into the
// memory buffer.  If it proves to be too small, the message gets written
// out into a temporary file.
//
//////////////////////////////////////////////////////////////////////////////

#include	"config.h"
#include	"tempfile.h"
#include	<sys/types.h>
#include	<errno.h>
#include	<string.h>

#if HAVE_UNISTD_H
#include	<unistd.h>
#else

#ifndef	SEEK_SET
#define	SEEK_SET	0
#endif

#endif

#include	<vector>

class Maildrop;

class Message : public std::streambuf {
public:
	Maildrop &maildrop;
private:
	rfc822::fdstreambuf mio;
	std::vector<char> buffer;
	std::string extra_headers;

	[[noreturn]] static void readerr();
	[[noreturn]] static void seekerr();
public:
	Message(Maildrop &maildrop);
	~Message();
	void Init(int, const std::string &extra_headers);
	// Initialize from file descriptor

	void Init();		// Begin initializing externally

	// Incremental initialization

	struct init_partial {
		Message &message;
		rfc2045::entity_parser<false> &parser;
		void operator()(const char *, size_t);

		void done();
	};
private:
	void ExtraHeaders(init_partial &, const std::string &);
public:
	void Rewind();		// Start reading the message
	int appendline(std::string &, int=1);	// Read newline-terminated line.
	std::streampos seekpos(std::streampos,
			       std::ios_base::openmode=
			       std::ios_base::in | std::ios_base::out) override;
	std::streampos seekoff(std::streamoff,
			       std::ios_base::seekdir,
			       std::ios_base::openmode=
			       std::ios_base::in | std::ios_base::out) override;
	std::streampos tellg();

	int_type underflow() override;
	void setmsgsize();

	// API translator for rfc2045 functions

	rfc2045::entity rfc2045p;
} ;

#include	"funcs.h"
#include	"maildrop.h"


inline std::streampos Message::tellg()
{
	std::streampos	pos;

	if (eback() && eback() == extra_headers.data())
		return gptr()-eback();
	if (mio.error()) // Cached in memry
	{
		if (!eback())
			pos=0;
		else
			pos=gptr()-eback();
	}
	else
	{
		pos=mio.tell();

		if (eback())
			pos -= egptr()-gptr();
	}

	pos += extra_headers.size();
	return pos;
}

inline std::streampos Message::seekoff(std::streamoff off,
				       std::ios_base::seekdir d,
				       std::ios_base::openmode mode)
{
	switch (d) {
	case std::ios_base::beg:
		if (off >= 0)
			return seekpos(off);
		break;
	case std::ios_base::end:
		return seekoff( rfc2045p.endbody + off,
			       std::ios_base::beg);
	case std::ios_base::cur:
		return seekoff(tellg()+off, std::ios_base::beg);
	}
	seekerr();
}

inline std::streampos Message::seekpos(std::streampos n,
				       std::ios_base::openmode)
{
	size_t	l=0;

	if ((size_t)n < (l=extra_headers.size()))
	{
		setg(extra_headers.data(),
		     extra_headers.data()+n,
		     extra_headers.data()+l);

		if (mio.fileno() < 0)
			return n;

		if (mio.pubseekpos(0) == (std::streampos)-1)	seekerr();

		return n;
	}

	size_t p=n;

	p -= extra_headers.size();

	if (mio.fileno() < 0)
	{
		if (p > buffer.size())
			p=buffer.size();

		setg(buffer.data(), buffer.data()+p,
		     buffer.data()+buffer.size());
		return n;
	}

	setg(nullptr, nullptr, nullptr);
	l=extra_headers.size();

	if (mio.pubseekpos(p) == (std::streampos)-1)	seekerr();

	return n;
}

#endif
