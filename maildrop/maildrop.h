#include	"config.h"
#include	"message.h"
#include	"messageinfo.h"

#ifndef	maildrop_h
#define	maildrop_h


////////////////////////////////////////////////////////////////////////////
//
// Maildrop global variables

#include	"buffer.h"
#include	"mio.h"

class Maildrop {
public:
	int verbose_level;	// Current verbose level
	int isdelivery;		// Running in delivery mode
static	int sigfpe;		// Floating point exception trapped.
	int includelevel;	// Catch include loops
	int embedded_mode;	// Running in embedded mode
	int authlib_essential;	// Whether authlib is essential

	Message *msgptr, *savemsgptr;	// msgptr is the current message.
					// savemsgptr points to a spare message
					// structure (m1 and m2).
	MessageInfo msginfo;

#if	SHARED_TEMPDIR

#else
	std::string	tempdir;	// Directory for temporary files
#endif

	std::string  init_home;	// Initial HOME
	std::string	init_logname;	// Initial LOGNAME
	std::string	init_shell;	// Initial SHELL
	std::string	init_default;	// Initial DEFAULT
	std::string	init_quota;	// Initial MAILDIRQUOTA

	Mio	logfile;	// Log file.
	Maildrop();

	static void cleanup();
	static void bye(int);
	static int trap(int (*)(int, char *[]), int, char *[]);
	static void reset_vars();

	static int sigchildfd[2];
} ;

extern class Maildrop maildrop;

#endif
