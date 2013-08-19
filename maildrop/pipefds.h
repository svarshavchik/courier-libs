#ifndef	pipefds_h
#define	pipefds_h


/////////////////////////////////////////////////////////////////////////
//
//  Convenience class - automatically destroy pair of pipe handles.
//
/////////////////////////////////////////////////////////////////////////

#include	"config.h"
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

class PipeFds {
public:
	int fds[2];

	PipeFds() { fds[0]= -1; fds[1]= -1; }
	int Pipe();
	void close0()
		{
			if (fds[0] >= 0)	close(fds[0]);
			fds[0]= -1;
		}
	void close1()
		{
			if (fds[1] >= 0)	close(fds[1]);
			fds[1]= -1;
		}
	~PipeFds();
} ;
#endif
