#ifndef	dotlockrefresh_h
#define	dotlockrefresh_h


#include	"config.h"
#include	"alarm.h"

///////////////////////////////////////////////////////////////////////////
//
// This is a member object of DotLock that is derived from Alarm.  DotLock
// sets a timeout to have this dotlock refreshed, in order to keep it
// from being removed as a stale dotlock, by some other process.
// The main code to do that is part of the DotLock class, this is just
// a stub to redirect the alarm call to the DotLock class.
//
///////////////////////////////////////////////////////////////////////////

class DotLock;

class DotLockRefresh : public Alarm {

	void	handler();
	DotLock	*dotlock;
public:
	DotLockRefresh(DotLock *);
	~DotLockRefresh();
} ;
#endif
