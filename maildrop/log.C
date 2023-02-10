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
std::string	tbuf;
std::string	szbuf;

	if (maildrop.logfile.fd() < 0)	return;	// Logfile not open

	time(&t);
	tbuf=ctime(&t);
	tbuf.pop_back();	// Drop trailing newline

	if (msg.hdrfrom.size() > 72)
		msg.hdrfrom.resize(72);
	if (msg.hdrsubject.size() > 72)
		msg.hdrsubject.resize(72);
	maildrop.logfile << "Date: " << tbuf << EOL;
	maildrop.logfile << "From: " << msg.hdrfrom << EOL;
	maildrop.logfile << "Subj: " << msg.hdrsubject << EOL;

	szbuf="(";
	add_integer(szbuf, msg.msgsize);
	szbuf += ")";
	tbuf=mailbox;

size_t	l= 72 - szbuf.size();

	while (tbuf.size() < l-1)
		tbuf.push_back(' ');
	tbuf.resize(l-1);
	tbuf.push_back(' ');
	tbuf += szbuf;

	maildrop.logfile << (status ? "!Err: ":"File: ") << tbuf << EOL << EOL;
	maildrop.logfile.flush();
}

void log_line(const std::string &buf)
{
	if (maildrop.logfile.fd() < 0)	return;	// Logfile not open
	maildrop.logfile << buf;
	maildrop.logfile.flush();
}
