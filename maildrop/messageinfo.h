#ifndef	messageinfo_h
#define	messageinfo_h


#include	"config.h"
#include	<sys/types.h>
#include	"buffer.h"

class	Message;

///////////////////////////////////////////////////////////////////////////
//
//  The MessageInfo class collects information about a message - namely
//  it extract the email address from the Return-Path: header, if present.
//
///////////////////////////////////////////////////////////////////////////

class	MessageInfo {
public:
	Buffer fromname;	// Envelope sender

	MessageInfo() {}
	~MessageInfo()			{}

	void	info(Message &);
	void	filtered() {}
} ;
#endif
