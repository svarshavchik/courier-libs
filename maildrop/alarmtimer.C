#include "config.h"
#include	"alarmtimer.h"



AlarmTimer::AlarmTimer() : flag(0)
{
}

AlarmTimer::~AlarmTimer()
{
}

void AlarmTimer::handler()
{
	flag=1;
}

void AlarmTimer::Set(unsigned nseconds)
{
	Cancel();
	flag=0;
	Alarm::Set(nseconds);
}
