#ifndef	globaltimer_h
#define	globaltimer_h


#include	"config.h"
#include	"alarm.h"

///////////////////////////////////////////////////////////////////////////
//
//  This is the global timer used to terminate maildrop
//
///////////////////////////////////////////////////////////////////////////

class GlobalTimer: public Alarm {

	void	handler();
public:
	GlobalTimer();
	~GlobalTimer();
} ;

#endif
