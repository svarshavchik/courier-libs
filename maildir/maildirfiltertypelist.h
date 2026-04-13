#ifndef	maildirfiltertypelist_h
#define	maildirfiltertypelist_h

/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/


#include	"config.h"

static struct {
	enum maildirfiltertype type;
	const char *name;
	} typelist[] = {
		{startswith, "startswith"},
		{endswith, "endswith"},
		{contains, "contains"},
		{hasrecipient, "hasrecipient"},
		{mimemultipart, "mimemultipart"},
		{textplain, "textplain"},
		{islargerthan, "islargerthan"},
		{anymessage, "anymessage"},
		{ (enum maildirfiltertype)0, 0}
	};
#endif
