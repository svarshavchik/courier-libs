#ifndef	maildir_h
#define	maildir_h


///////////////////////////////////////////////////////////////////////
//
//  Message delivery to maildir directories.
//
///////////////////////////////////////////////////////////////////////

#include	"buffer.h"
#include	<sys/types.h>

class	Mio;

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
	int	MaildirOpen(const char *, Mio &, off_t);
	void	MaildirSave();
	void	MaildirAbort();
} ;
#endif

