#include	"config.h"
#include	<signal.h>
#include	"alarmsleep.h"


AlarmSleep::AlarmSleep(unsigned nseconds) : flag(0)
{
	Set(nseconds);
	while (!flag)
		wait_alarm();
}

AlarmSleep::~AlarmSleep()
{
}

void AlarmSleep::handler()
{
	flag=1;
}
