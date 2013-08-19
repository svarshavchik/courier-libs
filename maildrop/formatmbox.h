#ifndef	formatmbox_h
#define	formatmbox_h


#include	"config.h"
#include	"buffer.h"
#include	"mio.h"

//////////////////////////////////////////////////////////////////////////////
//
// FormatMbox class returns consecutive lines from the current message,
// formatted for delivery into a mailbox.  That means the lines are
// terminate with either <LF> or <CRLF> (defined at compilation time).
// Furthermore, the message can be optionally preceded by the From line.
//
// Mandatory calling sequence:
//
//  int HasMsg() - return 0 if there's a message to deliver, or -1 if
//                 the message is empty (none).
//  void Init(flag) - initialize.  Flag is non-zero if the message is to
//                    have a From line (and embedded From lines to be
//                    escaped).  Flag is zero if the message should not
//                    be molested by From lines (when writing to a pipe).
//  Buffer NextLine() - return consecutive lines, until a NULL pointer is
//                      returned.
//
//  When NULL pointer is returned, the hdrfrom, hdrsubject, and msgsize will
//  contain message information (suitable for logging).
//
//////////////////////////////////////////////////////////////////////////////

class FormatMbox {

	Buffer	msglinebuf;
	Buffer	tempbuf;
	int	do_escape;

	Buffer	* (FormatMbox::* next_func)(void);

	Buffer	*GetFromLine(void);
	Buffer	*GetLineBuffer(void);
	Buffer	*GetNextLineBuffer(void);

	int	inheader;
public:

	Buffer	hdrfrom, hdrsubject;
	unsigned long msgsize;

	FormatMbox()	{}
	~FormatMbox()	{}

	int	HasMsg();
	void	Init(int);
	Buffer	*NextLine()
		{
			return ( (this->*next_func)() );
		}

	int	DeliverTo(class Mio &);
} ;
#endif
