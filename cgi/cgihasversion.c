/*
** Copyright 1998 - 1999 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/
#include	"cgi.h"

int cgihasversion(unsigned major, unsigned minor)
{
unsigned vmajor, vminor;

	cgiversion(&vmajor, &vminor);
	return (vmajor > major || (vmajor == major && vminor >= minor));
}
