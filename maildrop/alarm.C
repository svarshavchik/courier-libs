#include	"config.h"
#include	<iostream>
#include	<poll.h>
#include	<sys/wait.h>

#if HAVE_UNISTD_H
#include	<unistd.h>
#else
extern "C" long alarm(long);
#endif

#include	<signal.h>
#include	"alarm.h"
#include	"maildrop.h"

static alarmlist_t alarmlist;

Alarm::~Alarm()
{
	Cancel();
}

void Alarm::Cancel()
{
	if (!expiration)
		return;
	alarmlist.erase(me);
	expiration=0;
}

static int ring()
{
	time_t now=time(NULL);
	bool called_handler=false;

	alarmlist_t::iterator first;

#if ALARM_DEBUG

	std::cout << "Time now is " << now << "\n" << std::flush;

	for (auto &a:alarmlist)
	{
		std::cout << "   Alarm: " << a.first << "\n" << std::flush;
	}
#endif

	while ((first=alarmlist.begin()) != alarmlist.end())
	{
		if (first->first > now)
		{
			if (called_handler)
				return 0;

#if ALARM_DEBUG
			std::cout << "Next alarm in "
				  << (first->first-now) << "\n" << std::flush;
#endif
			return 1000 * (
				first->first-now < 30 ? first->first-now:30
			);
		}

		auto wake_up=first->second;

#if ALARM_DEBUG
		std::cout << "Alarm went off\n" << std::flush;
#endif
		wake_up->Cancel();
		wake_up->handler();
		called_handler=true;
	}

	return -1;
}

void Alarm::Set(unsigned nseconds)
{
	Cancel();		// Just in case
	if (nseconds == 0)
	{
		handler();	// Fooey.
		return;
	}

	expiration=time(NULL)+nseconds;

	me=alarmlist.emplace(alarmlist_t::value_type{expiration, this});
}

pid_t Alarm::wait_child(int *wstatus)
{
	char buf;
	pid_t ret;
	struct pollfd pfd;

	pfd.fd=Maildrop::sigchildfd[0];
	pfd.events=POLLIN;

	while (1)
	{
		int timeout=ring();
		(void)read(Maildrop::sigchildfd[0], &buf, 1);

		ret=waitpid(-1, wstatus, WNOHANG);

		if (ret != 0)
			break;

		poll(&pfd, 1, timeout);
	}

	return ret;
}

void Alarm::wait_alarm()
{
	struct pollfd pfd;

	pfd.fd=-1;

	auto timeout=ring();

	if (timeout < 0)
		return;

	poll(&pfd, 0, timeout);
}


struct block_sigalarm::sigs {

	sigset_t ss;

	sigs() {
		sigemptyset(&ss);
		sigaddset(&ss, SIGALRM);
		sigaddset(&ss, SIGTERM);
		sigaddset(&ss, SIGHUP);
		sigaddset(&ss, SIGINT);
	}
};

block_sigalarm::block_sigalarm()
{
	sigs ss;

	sigprocmask(SIG_BLOCK, &ss.ss, NULL);
}

block_sigalarm::~block_sigalarm()
{
	sigs ss;

	sigprocmask(SIG_UNBLOCK, &ss.ss, NULL);
}
