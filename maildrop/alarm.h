#ifndef	alarm_h
#define	alarm_h


//////////////////////////////////////////////////////////////////////////////
//
// This is an asynchronous timer function that is driven by the alarm()
// system call.  I attempt to implement here a half-baked multiple timer
// feature.
//
// Note - you can't really do a lot in a signal handler, stick to setting
// global flags, and making system calls ( **NOT** standard library calls).
//
//////////////////////////////////////////////////////////////////////////////

#include	"config.h"

class Alarm;

class Alarm {

static Alarm *first, *last;

	Alarm *next, *prev;	// List sorted by expiration interval.
	unsigned set_interval;	// For how many seconds we're set.

	void	Unlink();

static	void cancel_sig(unsigned);
static	void set_sig();
static	RETSIGTYPE alarm_func(int);
static	unsigned sig_left();
public:
	Alarm() : next(0), prev(0), set_interval(0)	{}
	virtual ~Alarm();

	virtual void handler()=0;

	void	Set(unsigned);
	void	Cancel();
} ;
#endif
