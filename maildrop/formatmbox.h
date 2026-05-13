#ifndef	formatmbox_h
#define	formatmbox_h


#include	"config.h"
#include	"buffer.h"
#include	"rfc822/rfc822.h"

//////////////////////////////////////////////////////////////////////////////
//
// FormatMbox class returns consecutive lines from the current message,
// formatted for delivery into a mailbox.  That means the lines are
// terminated with <LF> and the message can be optionally preceded by the
// From line.
//
// Mandatory calling sequence:
//
//  int HasMsg() - return 0 if there's a message to deliver, or -1 if
//                 the message is empty (none).
//  void Init(flag) - initialize.  Flag is non-zero if the message is to
//                    have a From line (and embedded From lines to be
//                    escaped).  Flag is zero if the message should not
//                    be molested by From lines (when writing to a pipe).
//  std::string NextLine() - return consecutive lines, until a NULL pointer is
//                      returned.
//
//  When NULL pointer is returned, the hdrfrom, hdrsubject, and msgsize will
//  contain message information (suitable for logging).
//
//////////////////////////////////////////////////////////////////////////////

class FormatMbox {

	std::string	msglinebuf;
	std::string	tempbuf;
	int	do_escape;

	std::string	* (FormatMbox::* next_func)(void);

	std::string	*GetFromLine(void);
	std::string	*GetLineBuffer(void);
	std::string	*GetNextLineBuffer(void);

	int	inheader;
public:

	std::string	hdrfrom, hdrsubject;
	unsigned long msgsize;

	FormatMbox()	{}
	~FormatMbox()	{}

	int	HasMsg();
	void	Init(int);
	std::string	*NextLine()
		{
			return ( (this->*next_func)() );
		}

	int	DeliverTo(rfc822::fdstreambuf &);
} ;
#endif
