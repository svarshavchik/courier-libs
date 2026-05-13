#include	"config.h"
#include	"message.h"
#include	"buffer.h"
#include	"xconfig.h"
#include	"funcs.h"
#include	"varlist.h"

Message::Message(Maildrop &maildrop) : maildrop{maildrop}
{
}

Message::~Message() = default;

void Message::Init()
{
	buffer.clear();
	extra_headers.clear();
	mio=rfc822::fdstreambuf{};
	rfc2045p=rfc2045::entity{};
	setg(nullptr, nullptr, nullptr);
}

void Message::Init(int fd, const std::string &extra_headers)
{
	Init();
	rfc2045::entity_parser<false> parser;
	init_partial partial{*this, parser};

	ExtraHeaders(partial, extra_headers);

	// If the file descriptor is seekable, and the message is big
	// keep message in the file.

	off_t msgsize;

	if ( lseek(fd, 0, SEEK_END) >= 0 &&
		(msgsize=lseek(fd, 0, SEEK_CUR)) >= 0 && msgsize > SMALLMSG)
	{
		if (lseek(fd, 0, SEEK_SET) < 0)
			seekerr();

	// BUGFIX 0.55 - we take ownership of the file descriptor. When
	// ::Init() is called, it will call fd(-1) which will CLOSE this
	// descriptor we have now.
	//
	// This func is invoked from main with STDIN - 0, so we must take
	// ownership of a duplicate.

		fd=dup(fd);
		if (fd < 0)	throw "dup() failed.";

		mio=rfc822::fdstreambuf{fd};

		char buffer[BUFSIZ];

		int n;

		while ((n=mio.sgetn(buffer, sizeof(buffer))) > 0)
		{
			parser.parse(buffer, buffer+n);
		}

		partial.done();
		pubseekpos(0);
		return;
	}
	// Well, just read the message, and let Init() figure out what to
	// do.

	lseek(fd, 0, SEEK_SET);	// Just in case

#ifdef	BUFSIZ
	char	buf[BUFSIZ];
#else
	char	buf[8192];
#endif
	int	n;

	while ((n=read(fd, buf, sizeof(buf))) > 0)
	{
		partial(buf, n);
	}
	if (n < 0)
		throw "Error - read() failed reading message.";
	partial.done();
	pubseekpos(0);
}

void Message::init_partial::operator()(const char *data, size_t cnt)
{
	parser.parse(data, data+cnt);

	if (message.mio.fileno() < 0)	// Still trying to save to buffer
	{
		if (message.buffer.empty())
		{
			message.buffer.reserve(SMALLMSG);
		}

		if (SMALLMSG - message.buffer.size() >= cnt) // Can still do it
		{
			message.buffer.insert(
				message.buffer.end(),
				data,
				data+cnt);
			return;
		}

#if	SHARED_TEMPDIR

		message.mio=rfc822::fdstreambuf::tmpfile();
#else
		message.mio=rfc822::fdstreambuf::tmpfile(
			message.maildrop.tempdir.c_str()
		);
#endif

		if (message.mio.error())
			throw "Unable to create temporary file.";

		if ((size_t)message.mio.sputn(message.buffer.data(),
					      message.buffer.size())
		    != message.buffer.size())
			throw "Unable to write to temporary file - possibly out of disk space.";

		message.buffer.clear();
	}

	if ((size_t)message.mio.sputn(data, cnt) != cnt)
		throw "Unable to write to temporary file - possibly out of disk space.";
}

void Message::init_partial::done()
{
	message.rfc2045p=parser.parsed_entity();
}

void Message::ExtraHeaders(init_partial &partial, const std::string &buf)
{
	partial.parser.parse(buf.data(), buf.data()+buf.size());
	extra_headers=buf;
}

void Message::Rewind()
{
	pubseekpos(0);
}

Message::int_type Message::underflow()
{
	if (mio.fileno() < 0)
	{
		if (eback() && eback() == extra_headers.data())
		{
			setg(buffer.data(), buffer.data(),
			     buffer.data() + buffer.size());
			if (buffer.size() == 0)
				return -1;
			return (unsigned char)*gptr();
		}

		// Must've already been at buffer
		return -1;
	}

	buffer.clear();
	buffer.reserve(BUFSIZ);

	if (mio.in_avail() == 0 && mio.sgetc() == -1)
		return -1;

	size_t n=mio.in_avail();
	if (n == 0)
	{
		auto c=mio.sbumpc();

		if (c < 0)
			return -1;
		buffer.push_back(c);
	}
	else
	{
		char stackbuf[n];

		n=mio.sgetn(stackbuf, n);

		if (n == 0)
			return -1;

		buffer.insert(buffer.end(), stackbuf, stackbuf+n);
	}
	setg(buffer.data(), buffer.data(), buffer.data()+buffer.size());
	return (unsigned char)*buffer.data();
}

void Message::readerr()
{
	throw "Read error.";
}

void Message::seekerr()
{
	throw "Seek error.";
}

int Message::appendline(std::string &buf, int stripcr)
{
	int	c;
	bool	eof=true;
	int	lastc=0;

	while ((c=sbumpc()) >= 0 && c != '\n')
	{
		eof=false;
		buf.push_back(c);
		lastc=c;
	}
	if (c < 0 && eof)	return (-1);
	if (stripcr && lastc == '\r')	buf.pop_back();
						// Drop trailing CRs
	buf += "\n";
	return (0);
}
