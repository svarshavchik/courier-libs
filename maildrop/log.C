#include	"log.h"
#include	"mio.h"
#include	"formatmbox.h"
#include	"xconfig.h"
#include	"maildrop.h"
#include	"mytime.h"


#if	CRLF_TERM
	#define EOL "\r\n"
#else
	#define	EOL "\n"
#endif

void log(const char *mailbox, int status, class FormatMbox &msg)
{
time_t	t;
Buffer	tbuf;
Buffer	szbuf;

	if (maildrop.logfile.fd() < 0)	return;	// Logfile not open

	time(&t);
	tbuf=ctime(&t);
	tbuf.pop();	// Drop trailing newline
	msg.hdrfrom.Length(72);
	msg.hdrsubject.Length(72);
	maildrop.logfile << "Date: " << tbuf << EOL;
	maildrop.logfile << "From: " << msg.hdrfrom << EOL;
	maildrop.logfile << "Subj: " << msg.hdrsubject << EOL;

	szbuf="(";
	szbuf.append( (unsigned long)msg.msgsize);
	szbuf += ")";
	tbuf=mailbox;

int	l= 72 - szbuf.Length();

	while (tbuf.Length() < l-1)
		tbuf.push(' ');
	tbuf.Length(l-1);
	tbuf.push(' ');
	tbuf += szbuf;

	maildrop.logfile << (status ? "!Err: ":"File: ") << tbuf << EOL << EOL;
	maildrop.logfile.flush();
}

void log_line(const class Buffer &buf)
{
	if (maildrop.logfile.fd() < 0)	return;	// Logfile not open
	maildrop.logfile << buf;
	maildrop.logfile.flush();
}
