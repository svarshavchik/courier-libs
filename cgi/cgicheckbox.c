/*
** Copyright 2007 Double Precision, Inc.
** See COPYING for distribution information.
*/

/*
*/

#include	"cgi.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>

char *cgi_checkbox(const char *name,
		   const char *value,
		   const char *flags)
{
	char *buf;

	if (!value)
		value="";

	buf=malloc(strlen(name)+strlen(flags)+200);

	if (!buf)
		return NULL;

	strcpy(buf, "<input type='checkbox' name='");
	strcat(buf, name);
	strcat(buf, "' value='");
	strcat(buf, value);
	strcat(buf, "'");

	if (strchr(flags, '*'))
		strcat(buf, " checked='checked'");
	if (strchr(flags, 'd'))
		strcat(buf, " disabled='disabled'");
	strcat(buf, " />");
	return buf;
}
