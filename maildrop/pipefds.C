#include "config.h"
#include	"pipefds.h"


int PipeFds::Pipe()
{
	close0();
	close1();
	if (pipe(fds) < 0)
	{
		fds[0]= -1;
		fds[1]= -1;	// Just in case
		return (-1);
	}
	return (0);
}

PipeFds::~PipeFds()
{
	if (fds[0] >= 0)	close(fds[0]);
	if (fds[1] >= 0)	close(fds[1]);
}
