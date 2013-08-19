/*
** Copyright 2003-2007 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"config.h"
#include	"sqwebmail.h"
#include	"htmllibdir.h"
#include	"cgi/cgi.h"
#include	<string.h>

int main(int argc, char **argv)
{
	int	pass_fd;


	pass_fd=0;

#if	CGI_PASSFD

	/*
	** If invoked as sqwebmailn, don't pass file descripts.
	** Fallback to proxy mode.
	*/

	{
		const char *p=strrchr(argv[0], '/');

		if (p == NULL) p=argv[0];
		else ++p;

		if (strcmp(p, "sqwebmailn"))
			pass_fd=1;
	}
#endif

	cgi_connectdaemon(SOCKFILENAME, pass_fd);
	return (0);
}
