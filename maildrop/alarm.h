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
#include	<unistd.h>
#include	<time.h>
#include	<map>
#include	<time.h>

class Alarm;

typedef std::multimap<time_t, Alarm *> alarmlist_t;

class Alarm {

	time_t expiration=0; // When this alarm expires.
	alarmlist_t::iterator me; // Our entry in alarmlist.

public:
	Alarm() {}
	virtual ~Alarm();

	virtual void handler()=0;

	void	Set(unsigned);
	void	Cancel();

	static pid_t wait_child(int *wstatus);
	static void wait_alarm();
} ;

/*
** The constructor blocks sigalarm. The destructor unblocks it.
*/

struct block_sigalarm {

	struct sigs;

	block_sigalarm();
	~block_sigalarm();

	block_sigalarm(const block_sigalarm &)=delete;

	block_sigalarm &operator=(const block_sigalarm &)=delete;
};

#endif
