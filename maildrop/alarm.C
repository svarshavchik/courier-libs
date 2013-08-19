#include	"config.h"
#include	<iostream>


#if HAVE_UNISTD_H
#include	<unistd.h>
#else
extern "C" long alarm(long);
#endif

#include	<signal.h>
#include	"alarm.h"

Alarm *Alarm::first=0;
Alarm *Alarm::last=0;

Alarm::~Alarm()
{
	Cancel();
}

void Alarm::Unlink()
{
	set_interval=0;
	if (prev)	prev->next=next;
	else		first=next;
	if (next)	next->prev=prev;
	else		last=prev;
}

void Alarm::cancel_sig(unsigned seconds_left)
{
Alarm	*p;
Alarm	*alarm_chain=0;

	while ((p=first) != 0 && p->set_interval <= seconds_left)
			// Marginal case
	{
		p->Unlink();
		p->next=alarm_chain;
		alarm_chain=p;
	}

	for (p=first; p; p=p->next)
		p->set_interval -= seconds_left;

	while ((p=alarm_chain) != 0)
	{
		alarm_chain=p->next;
		p->handler();
	}
}

void Alarm::set_sig()
{
	if (!first)	return;
	signal(SIGALRM, &Alarm::alarm_func);
	alarm(first->set_interval);
}

RETSIGTYPE Alarm::alarm_func(int)
{
	if (first)	cancel_sig(first->set_interval);
	set_sig();

#if RETSIGTYPE != void
	return (0);
#endif
}

unsigned Alarm::sig_left()
{
	if (!first)	return (0);

unsigned n=alarm(0);

	return (n ? n <= first->set_interval ? first->set_interval - n:0:0);
}

void Alarm::Set(unsigned nseconds)
{
	Cancel();		// Just in case
	if (nseconds == 0)
	{
		handler();	// Fooey.
		return;
	}

	cancel_sig(sig_left());

Alarm	*p;

	for (p=first; p; p=p->next)
		if (p->set_interval > nseconds)
			break;

	if (!p)
	{
		next=0;
		if ((prev=last) != 0)
			prev->next=this;
		else
			first=this;
		last=this;
	}
	else
	{
		if ((prev=p->prev) != 0)
			prev->next=this;
		else
			first=this;
		next=p;
		p->prev=this;
	}
	set_interval=nseconds;
	set_sig();
}

void Alarm::Cancel()
{
	cancel_sig(sig_left());
	if (set_interval) Unlink();
	set_sig();
}
