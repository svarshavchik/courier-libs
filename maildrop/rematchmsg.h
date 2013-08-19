#ifndef	rematchmsg_h
#define	rematchmsg_h


class Message;

#include	"config.h"
#include	<sys/types.h>
// #include	<unistd.h>
#include	"rematch.h"

/////////////////////////////////////////////////////////////////////////////
//
// ReMatchMsg - match regular expression against the message directly.
//
// This class is derived from ReMatch, and is used when a regular expression
// should be matched against the message directly.
// It defines the virtual functions from ReMatch to go against the Message
// class itself.
//
// The constructor takes two flags:  headeronly, and mergelines.
// "headeronly" forces and end-of-file condition when a blank line is found
// in the message.  "mergelines" causes a newline character to be silently
// eaten, when it is immediately followed by a space.  The flags must be
// set as follows, in the following situations:
//
// A)  Match header and body:  mergelines is true, headeronly is false
// B)  Match header only:      mergelines is true, headeronly is true
// C)  Match body only:        mergelines is false, headeronly is false,
//                             also the message must be seeked to the
//                             start of the message contents.
//
/////////////////////////////////////////////////////////////////////////////

class ReMatchMsg : public ReMatch {
	Message *msg;

	int header_only, mergelines;
	int eof;
	int lastc;

	off_t end_headers;
	off_t start;
public:
	ReMatchMsg(Message *m, int flag, int flag2);
	virtual ~ReMatchMsg();

	int NextChar();
	int CurrentChar();
	off_t GetCurrentPos();
	void SetCurrentPos(off_t);
} ;
#endif
