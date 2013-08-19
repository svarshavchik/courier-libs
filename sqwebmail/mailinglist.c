#include "config.h"

/*
*/

#include	"sqwebmail.h"
#include	"mailinglist.h"
#include	"rfc822/rfc822.h"
#include	<stdio.h>
#include	<string.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<sys/stat.h>

#define	MAILINGLISTS	"sqwebmail-mailinglists"
#define	MAILINGLISTSTMP	"sqwebmail-mailinglists.tmp"

char *getmailinglists()
{
	FILE *fp=fopen(MAILINGLISTS, "r");
	struct stat stat_buf;
	char *buf;
	int l;

	if (!fp)
		return (0);

	if (fstat(fileno(fp), &stat_buf) != 0 ||
	    (buf=malloc(stat_buf.st_size+1)) == NULL)
	{
		fclose(fp);
		return (0);
	}

	l=fread(buf, 1, stat_buf.st_size, fp);
	fclose(fp);

	if (l < 0)
		l=0;
	buf[l]=0;
	return (buf);
}

void savemailinglists(const char *p)
{
	FILE *fp;
	int lastc;

	if ((fp=fopen(MAILINGLISTSTMP, "w")) == NULL)
		return;

	for (lastc='\n'; *p; p++)
	{
		if (isspace((int)(unsigned char)*p) && *p != '\n')
			continue;

		if (*p == '\n' && lastc == '\n')
			continue;

		putc(*p, fp);
		lastc=*p;
	}
	
	fprintf(fp, "%s", p);
	fflush(fp);
	if (ferror(fp))
	{
		fclose(fp);
		unlink(MAILINGLISTSTMP);
		return;
	}
	fclose(fp);
	rename (MAILINGLISTSTMP, MAILINGLISTS);
}

