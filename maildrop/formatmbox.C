#include	"formatmbox.h"
#include	"message.h"
#include	"messageinfo.h"
#include	"rfc822/rfc822.h"
#include	"maildrop.h"
#include	"xconfig.h"
#include	"config.h"
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	"mytime.h"
#include	<ctype.h>
#include	<iostream>

int	FormatMbox::HasMsg()
{
	maildrop.msgptr->Rewind();
	msglinebuf.clear();
	if (maildrop.msgptr->appendline(msglinebuf,0) < 0)	return (-1);
					// Empty message, do not deliver.

	return (0);
}

void	FormatMbox::Init(int flag)
{
	hdrfrom="";
	hdrsubject="";
	msgsize=0;
	inheader=1;

	do_escape=flag;
	next_func= &FormatMbox::GetLineBuffer;
	if (do_escape)
		next_func= &FormatMbox::GetFromLine;
}

std::string	*FormatMbox::GetFromLine(void)
{
time_t	tm;

	time(&tm);

	tempbuf="From ";
	tempbuf += maildrop.msginfo.fromname;
	tempbuf += " ";

const char *p=ctime(&tm);
	while (*p && *p != '\n')
	{
		tempbuf.push_back(*p);
		++p;
	}
	tempbuf += "\n";
	next_func= &FormatMbox::GetLineBuffer;
	return (&tempbuf);
}

std::string	*FormatMbox::GetLineBuffer(void)
{
	if (!msglinebuf.c_str())	return (0);

	if (do_escape)
	{
	const char *p=msglinebuf.c_str();
	auto	l=msglinebuf.size();

		while (l && *p == '>')	p++, l--;
		if (l >= 5 &&
			*p == 'F' && p[1] == 'r' && p[2] == 'o'
				&& p[3] == 'm' && p[4] == ' ')
		{
			tempbuf=">";
			tempbuf += msglinebuf;
			msglinebuf=tempbuf;
		}
	}
	if (inheader && *msglinebuf.c_str() == '\n')
		inheader=0;
	if (inheader)
	{
	const char *p=msglinebuf.c_str();
	std::string	*bufp=0;

		if ( tolower(*p) == 'f' && tolower(p[1]) == 'r' &&
			tolower(p[2]) == 'o' && tolower(p[3]) == 'm' &&
			p[4] == ':')
		{
			p += 5;
			bufp= &hdrfrom;
		}
		else if ( tolower(*p) == 's' && tolower(p[1]) == 'u' &&
			tolower(p[2]) == 'b' && tolower(p[3]) == 'j' &&
			tolower(p[4]) == 'e' && tolower(p[5]) == 'c' &&
			tolower(p[6]) == 't' && p[7] == ':')
		{
			p += 8;
			bufp= &hdrsubject;
		}
		if (bufp)
		{
		int	l;

			while (*p != '\n' && isspace(*p))	p++;
			for (l=0; p[l] != '\n'; l++)
				;
			bufp->append(p, p+l);
		}
	}

	next_func= &FormatMbox::GetNextLineBuffer;
	msgsize += msglinebuf.size();
	return (&msglinebuf);
}

std::string	*FormatMbox::GetNextLineBuffer(void)
{
	msglinebuf.clear();
	if (maildrop.msgptr->appendline(msglinebuf,0) == 0)
		return (GetLineBuffer());
	return (0);	// END OF FILE
}

int	FormatMbox::DeliverTo(rfc822::fdstreambuf &mio)
{
std::string	*bufptr;

	while ((bufptr=NextLine()) != NULL)
	{
		if ((size_t)mio.sputn(bufptr->c_str(), bufptr->size())
		    != bufptr->size())
		{
write_error:
			std::cerr << "maildrop: error writing to mailbox.\n"
				  << std::flush;
			mio=rfc822::fdstreambuf{};
			return (-1);
		}
	}

	if (do_escape && mio.sputn("\n", 1) != 1)
		goto write_error;

	if (mio.pubsync() < 0)
		goto write_error;

	if (fsync(mio.fileno()) < 0)
	{
	struct	stat	stat_buf;

		// fsync() failure on a regular file means trouble.

		if (fstat(mio.fileno(), &stat_buf) == 0 &&
			S_ISREG(stat_buf.st_mode))
			goto write_error;
	}

	bool error=mio.error();
	mio=rfc822::fdstreambuf{};
	if (error)	return (-1);
	return (0);
}
