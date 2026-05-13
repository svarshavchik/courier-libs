#ifndef	maildir_h
#define	maildir_h


///////////////////////////////////////////////////////////////////////
//
//  Message delivery to maildir directories.
//
///////////////////////////////////////////////////////////////////////

#include	"rfc822/rfc822.h"
#include	"buffer.h"
#include	<sys/types.h>

class Maildir {
	int	is_open;
	int	is_afs;
	std::string	maildirRoot;
public:
	std::string	tmpname;
	std::string	newname;

	Maildir();
	virtual ~Maildir();

static	int	IsMaildir(const char *);	// Is this a Maildir directory?
	int	MaildirOpen(const char *, rfc822::fdstreambuf &, off_t);
	void	MaildirSave();
	void	MaildirAbort();
} ;
#endif
