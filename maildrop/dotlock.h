#ifndef	dotlock_h
#define	dotlock_h


#include	"tempfile.h"
#include	"dotlockrefresh.h"

/////////////////////////////////////////////////////////////////////////////
//
// Well, here's my attempt to do dot locking.
//
/////////////////////////////////////////////////////////////////////////////

class DotLock : public TempFile {

	int attemptlock(const char *, const char *);
	void	Refreshlock();
	DotLockRefresh refresh;
	void	dorefresh();

	unsigned	refresh_interval;
public:
	DotLock();
	~DotLock();

	void	Lock(const char *);
	void	LockMailbox(const char *);
	void	Unlock();
		//
		// Sounds simple, right?  <sigh>

	friend	class DotLockRefresh;

private:
static unsigned GetLockSleep();
static unsigned GetLockTimeout();
static unsigned GetLockRefresh();
static const	char *GetLockExt();

} ;
#endif
