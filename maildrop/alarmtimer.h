#ifndef	alarmtimer_h
#define	alarmtimer_h


#include	"config.h"
#include	"alarm.h"

///////////////////////////////////////////////////////////////////////////
//
//  This is mainly used by DotLock to implement a dotlock timeout.
//
///////////////////////////////////////////////////////////////////////////

class AlarmTimer: public Alarm {

	void	handler() override;
	int	flag;
public:
	AlarmTimer();
	void	Set(unsigned);
	~AlarmTimer();
	int	Expired() { return (flag); }
} ;

#endif
