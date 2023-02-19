#include "alarmtimer.h"
#include "alarmsleep.h"
#include <unistd.h>
#include <iostream>

extern "C" {

	void rfc2045_error(const char *p)
	{
		fprintf(stderr, "%s\n", p);
		fflush(stderr);
		exit(1);
	}
}

int main()
{
	alarm(30);

	{
		AlarmTimer timer;

		timer.Set(20);

		AlarmSleep(1);

		if (timer.Expired())
		{
			std::cerr << "Timer shouldn't expire\n";
			exit(1);
		}
	}

	AlarmTimer timer;

	timer.Set(1);

	while (!timer.Expired())
	{
		AlarmSleep(1);
	}

	return 0;
}
