#include	"log.h"
#include	"rfc822/rfc822.h"
#include	"formatmbox.h"
#include	"xconfig.h"
#include	"maildrop.h"
#include	"mytime.h"
#include	<iostream>

#define	EOL "\n"

void log(const char *mailbox, int status, class FormatMbox &msg)
{
time_t	t;
std::string	tbuf;
std::string	szbuf;

	if (maildrop.logfile.fileno() < 0)	return;	// Logfile not open

	time(&t);
	tbuf=ctime(&t);
	tbuf.pop_back();	// Drop trailing newline

	if (msg.hdrfrom.size() > 72)
		msg.hdrfrom.resize(72);
	if (msg.hdrsubject.size() > 72)
		msg.hdrsubject.resize(72);
	std::ostream{&maildrop.logfile} << "Date: " << tbuf
					<< "\nFrom: " << msg.hdrfrom
					<< "\nSubj: " << msg.hdrsubject << "\n";

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

	std::ostream{&maildrop.logfile}
		<< (status ? "!Err: ":"File: ") << tbuf << "\n\n";
	maildrop.logfile.pubsync();
}

void log_line(const std::string &buf)
{
	if (maildrop.logfile.fileno() < 0)	return;	// Logfile not open
	maildrop.logfile.sputn(buf.data(), buf.size());
	maildrop.logfile.pubsync();
}
