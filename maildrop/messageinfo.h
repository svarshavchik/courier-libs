#ifndef	messageinfo_h
#define	messageinfo_h


#include	"config.h"
#include	<sys/types.h>
#include	"buffer.h"

class	Message;

///////////////////////////////////////////////////////////////////////////
//
//  The MessageInfo class collects information about a message - namely
//  it calculates where the message headers actually start in the Message
//  class.  We ignore blank lines and "From " lines at the beginning of
//  the message
//
///////////////////////////////////////////////////////////////////////////

class	MessageInfo {
public:
	off_t msgoffset;	// Skip leading blank lines and From header
	Buffer fromname;	// Envelope sender

	MessageInfo() : msgoffset(0)	{}
	~MessageInfo()			{}

	void	info(Message &);
	void	filtered() { msgoffset=0; }
} ;
#endif
