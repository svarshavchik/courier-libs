#include	"formatmbox.h"
#include	"message.h"
#include	"messageinfo.h"
#include	"mio.h"
#include	"maildrop.h"
#include	"xconfig.h"
#include	"config.h"
#if HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#include	"mytime.h"
#include	<ctype.h>


int	FormatMbox::HasMsg()
{
	maildrop.msgptr->Rewind();
	msglinebuf.reset();
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

Buffer	*FormatMbox::GetFromLine(void)
{
time_t	tm;

	time(&tm);

	tempbuf="From ";
	tempbuf += maildrop.msginfo.fromname;
	tempbuf += ' ';

const char *p=ctime(&tm);
	while (*p && *p != '\n')
	{
		tempbuf.push(*p);
		++p;
	}
#if	CRLF_TERM
	tempbuf.push('\r');
#endif
	tempbuf.push('\n');
	next_func= &FormatMbox::GetLineBuffer;
	return (&tempbuf);
}

Buffer	*FormatMbox::GetLineBuffer(void)
{
	if (!(const char *)msglinebuf)	return (0);

	if (do_escape)
	{
	const char *p=msglinebuf;
	int	l=msglinebuf.Length();

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
	if (inheader && *(const char *)msglinebuf == '\n')
		inheader=0;
	if (inheader)
	{
	const char *p=msglinebuf;
	Buffer	*bufp=0;

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
			bufp->append(p, l);
		}
	}

#if	CRLF_TERM
	msglinebuf.pop();	// Drop terminating \n
	msglinebuf.push('\r');
	msglinebuf.push('\n');
#endif
	next_func= &FormatMbox::GetNextLineBuffer;
	msgsize += msglinebuf.Length();
	return (&msglinebuf);
}

Buffer	*FormatMbox::GetNextLineBuffer(void)
{
	msglinebuf.reset();
	if (maildrop.msgptr->appendline(msglinebuf,0) == 0)
		return (GetLineBuffer());
	return (0);	// END OF FILE
}

int	FormatMbox::DeliverTo(class Mio &mio)
{
Buffer	*bufptr;

	while ((bufptr=NextLine()) != NULL)
	{
		if (mio.write((const char *)*bufptr, bufptr->Length()) < 0)
		{
write_error:
			merr << "maildrop: error writing to mailbox.\n";
			mio.Close();
			return (-1);
		}
	}

	if (do_escape && mio.write(
#if	CRLF_TERM
			"\r\n", 2
#else
			"\n", 1
#endif
			) < 0)
			goto write_error;

	if (mio.flush() < 0)
		goto write_error;

	if (fsync(mio.fd()) < 0)
	{
	struct	stat	stat_buf;

		// fsync() failure on a regular file means trouble.

		if (fstat(mio.fd(), &stat_buf) == 0 &&
			S_ISREG(stat_buf.st_mode))
			goto write_error;
	}

	mio.Close();
	if (mio.errflag())	return (-1);
	return (0);
}
