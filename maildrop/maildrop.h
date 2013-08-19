#include	"config.h"
#include	"message.h"
#include	"messageinfo.h"

#ifndef	maildrop_h
#define	maildrop_h


////////////////////////////////////////////////////////////////////////////
//
// Maildrop global variables

#include	"buffer.h"
#include	"globaltimer.h"
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
	Buffer	tempdir;	// Directory for temporary files
#endif

	Buffer  init_home;	// Initial HOME
	Buffer	init_logname;	// Initial LOGNAME
	Buffer	init_shell;	// Initial SHELL
	Buffer	init_default;	// Initial DEFAULT
	Buffer	init_quota;	// Initial MAILDIRQUOTA

	Mio	logfile;	// Log file.
	GlobalTimer global_timer;	// Watchdog timeout.
	Maildrop();

static void cleanup();
static RETSIGTYPE bye(int);
static int trap(int (*)(int, char *[]), int, char *[]);
static void reset_vars();
} ;

extern class Maildrop maildrop;

#endif
