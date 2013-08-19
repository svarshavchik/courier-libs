#ifndef	log_h
#define	log_h


#include	"config.h"

////////////////////////////////////////////////////////////////////////
//
// Log file support is too lame to be encapsulate.  Just two function
// calls will do.
//
////////////////////////////////////////////////////////////////////////

void log(const char *mailbox, int status, class FormatMbox &);
void log_line(const class Buffer &);

#endif
