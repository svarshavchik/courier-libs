/*
** Copyright 1998 - 1999 S. Varshavchik.
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
