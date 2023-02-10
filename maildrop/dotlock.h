#ifndef	dotlock_h
#define	dotlock_h


#include	"tempfile.h"
#include	"dotlockrefresh.h"
#include	<string>

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

	static unsigned GetLockSleep();
private:
	static unsigned GetLockTimeout();
	static unsigned GetLockRefresh();
	static std::string GetLockExt();
} ;
#endif
