/*
** Copyright 2025 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"rfc822.h"
#include	<unistd.h>
#include	<cstring>
#include	<cstdio>

#ifndef BUFSIZ
#define BUFSIZ 8192
#endif

rfc822::fdstreambuf::~fdstreambuf()
{
	(void)sync();
	if (defaultbuf)
		delete[] defaultbuf;
	if (fd >= 0)
		close(fd);
}

rfc822::fdstreambuf rfc822::fdstreambuf::tmpfile()
{
	FILE *fp=::tmpfile();

	rfc822::fdstreambuf tmpfilebuf{dup(::fileno(fp))};

	fclose(fp);
	return tmpfilebuf;
}

rfc822::fdstreambuf &rfc822::fdstreambuf::operator=(fdstreambuf &&o) noexcept
{
	std::swap(fd, o.fd);
	std::swap(defaultbuf, o.defaultbuf);

	{
		auto ebackptr=eback();
		auto gptrptr=gptr();
		auto egptrptr=egptr();
		setg(o.eback(), o.gptr(), o.egptr());

		o.setg(ebackptr, gptrptr, egptrptr);
	}

	auto pbaseme=pbase();
	auto pptrme=pptr();
	auto epptrme=epptr();

	auto pbaseo=o.pbase();
	auto pptro=o.pptr();
	auto epptro=o.epptr();

	setp(pbaseo, epptro);

	if (pbaseo)
		pbump(pptro-pbaseo);

	o.setp(pbaseme, epptrme);
	if (pbaseme)
		o.pbump(pptrme-pbaseme);
	return *this;
}

rfc822::fdstreambuf *rfc822::fdstreambuf::setbuf(
	char *p, std::streamsize s
)
{
	(void)sync();
	if (defaultbuf)
	{
		delete[] defaultbuf;
		defaultbuf=nullptr;
	}

	// Use for both the get and the put area.
	setg(p, p+s, p+s);
	setp(p, p+s);

	return this;
}

int rfc822::fdstreambuf::sync()
{
	if (fd < 0)
		return -1;

	// Flush the put area
	auto b=pbase();
	auto e=pptr();

	int error = 0;

	while (b && e && e>b)
	{
		auto n=write(fd, b, e-b);

		if (n <= 0)
		{
			close(fd);
			fd= -1;
			error= -1;
			break;
		}
		b += n;
	}

	setp(pbase(), epptr());

	if (eback())
	{
		b=gptr();
		e=egptr();

		setg(eback(), e, e);

		// If there was unread get area, seek back to the logical
		// file position

		if (fd >= 0 && b<e)
		{
			if (seekoff(-static_cast<off_t>(e-b),
				std::ios_base::cur,
				std::ios_base::in | std::ios_base::out
			    ) == static_cast<pos_type>(
				    static_cast<off_type>(-1)
			    ))
				error= -1;
		}
	}

	return error;
}

rfc822::fdstreambuf::pos_type rfc822::fdstreambuf::seekoff(
	off_type off, std::ios_base::seekdir dir,
	std::ios_base::openmode which)
{
	// Flush if there is unwritten put area.
	//
	// If there's a get area and the offset is relative, subtract the
	// unread data from the offset.

	auto b=pbase();
	auto p=pptr();

	pos_type ps{static_cast<pos_type>(static_cast<off_type>(-1))};

	if (b && p && p > b)
	{
		if (sync() < 0)
			return ps;
	}

	if (dir == std::ios_base::cur && eback())
		off -= egptr()-gptr();

	setg(eback(), egptr(), egptr()); // Nothing is unread any more.

	if (fd >= 0)
	{
		auto r=lseek(fd, off, dir == std::ios_base::end ? SEEK_END
			     : dir == std::ios_base::cur ? SEEK_CUR:SEEK_SET);

		if (r >= 0)
		{
			ps=static_cast<pos_type>(r);
		}
		else
		{
			close(fd);
			fd= -1;
		}
	}
	return ps;
}

rfc822::fdstreambuf::pos_type rfc822::fdstreambuf::seekpos(
	pos_type pos,
	std::ios_base::openmode which
)
{
	// Flush if there is unwritten put area.
	auto b=pbase();
	auto p=pptr();

	pos_type ps{static_cast<pos_type>(static_cast<off_type>(-1))};

	if (b && p > b)
	{
		if (sync() < 0)
			return ps;
	}

	setg(eback(), egptr(), egptr()); // Nothing is unread any more.

	if (fd >= 0)
	{
		auto r=lseek(fd, pos, SEEK_SET);

		if (r >= 0)
		{
			ps=static_cast<pos_type>(r);
		}
		else
		{
			close(fd);
			fd= -1;
		}
	}
	return ps;
}

rfc822::fdstreambuf::int_type rfc822::fdstreambuf::underflow()
{
	// If there was unwritten put area, flush it.
	auto b=pbase();
	auto p=pptr();

	if (b && p && p > b)
	{
		if (sync() < 0)
			return traits_type::eof();
	}

	b=eback();
	auto e=egptr();

	// If there's no buffer, or there is no get area, set up our own buffer.

	auto g=gptr();
	if (!b || g >= e)
	{
		if (!defaultbuf)
			defaultbuf=new char[BUFSIZ];

		b=defaultbuf;
		e=defaultbuf+BUFSIZ;
		setg(b, e, e);
	}
	else
	{
		// There's unread get area.

		return static_cast<int_type>(
			static_cast<unsigned char>(*g)
		);
	}

	if (fd >= 0)
	{
		auto n=read(fd, b, e-b);

		if (n > 0)
		{
			if (n < e-b)
				memmove(e-n, b, n);
			setg(b, e-n, e);

			return static_cast<int_type>(
				static_cast<unsigned char>(
					*(e-n)
				)
			);
		}
		else if (n < 0)
		{
			close(fd);
			fd= -1;
		}
	}

	return traits_type::eof();
}

std::streamsize rfc822::fdstreambuf::xsgetn(char* s, std::streamsize count )
{
	std::streamsize c{0};

	// If there was unwritten put area, flush it.
	auto b=pbase();
	auto p=pptr();

	if (b && p && p > b)
	{
		if (sync() < 0)
			return 0;
	}
	b=gptr();
	p=egptr();

	// Use anything that's in the unread buffer.
	while (count && eback() && b && b < p)
	{
		*s++ = *b;
		--count;
		++c;
		++b;
		gbump(1);
	}

	// And now, take care of the rest in one fell swoop.

	if (count && fd >= 0)
	{
		auto ret=read(fd, s, count);

		if (ret >= 0)
		{
			c += ret;
		}
	}

	return c;
}

rfc822::fdstreambuf::int_type rfc822::fdstreambuf::overflow(int_type ch)
{
	if (sync() < 0)
		return traits_type::eof();

	auto b=pbase();
	auto e=epptr();

	if (!b || epptr() == b)
	{
		if (!defaultbuf)
			defaultbuf=new char[BUFSIZ];

		b=defaultbuf;
		e=defaultbuf+BUFSIZ;
	}

	setp(b, e);

	if (!traits_type::eq_int_type(ch, traits_type::eof()))
	{
		*b=static_cast<char>(ch);

		pbump(1);
	}

	return ch;
}

std::streamsize rfc822::fdstreambuf::xsputn(const char *s,
					    std::streamsize count )
{
	std::streamsize c{0};

	auto p=gptr();
	auto e=egptr();

	if (p && e && p < e) // buffered input
	{
		if (sync() < 0)
			return 0;
	}

	if (fd < 0)
		return 0;
	// If we started to buffer previously written output, fill the output
	// buffer, and flush, first.

	auto b=pbase();
	p=pptr();
	e=epptr();

	if (b && p>b)
	{
		while (pptr() < e && count)
		{
			*pptr()=*s++;
			--count;
			++c;
			pbump(1);
		}

		if (count == 0)
			return c;

		if (sync() < 0)
			return 0;
	}

	auto r=write(fd, s, count);

	if (r >= 0)
		c += r;
	else
	{
		close(fd);
		fd= -1;
	}
	return c;
}

rfc822::fdstreambuf::int_type rfc822::fdstreambuf::pbackfail(int_type c)
{
	auto e=eback();
	auto g=gptr();

	if (g && e && g > e)
	{
		setg(e, --g, egptr());
		if (c != traits_type::eof())
		{
			*g=c;
			return c;
		}
	}

	return traits_type::eof();
}
