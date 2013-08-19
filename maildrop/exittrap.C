#include	"exittrap.h"
#include	"config.h"
#include	<stdlib.h>


#if HAVE_UNISTD_H
#include	<unistd.h>
#else
extern "C" long alarm(long);
#endif

static ExitTrap *first=0, *last=0;

ExitTrap::ExitTrap() : next(0), prev(last), callcleanup(0)
{
	if (prev)	prev->next=this;
	else		first=this;
	last=this;
	callcleanup=1;
}

ExitTrap::~ExitTrap()
{
	callcleanup=0;
	if (prev)	prev->next=next;
	else		first=next;

	if (next)	next->prev=prev;
	else		last=prev;
}

void ExitTrap::onexit()
{
ExitTrap	*p;

	alarm(0);
	for (p=first; p; p=p->next)
		if (p->callcleanup)
			p->cleanup();
}

void ExitTrap::onfork()
{
ExitTrap	*p;

	alarm(0);
	for (p=first; p; p=p->next)
		if (p->callcleanup)
			p->forked();
}
