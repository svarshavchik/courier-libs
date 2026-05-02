#ifndef pcpretr_h
#define pcpretr_h

/*
** Copyright 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include "config.h"

#include <stdio.h>
#include <time.h>
#include <vector>
#include <string>

struct xretr_time_list {
	time_t from;
	time_t to;
} ;

struct xretrinfo {
	FILE *tmpfile;
	int status;
	std::vector<std::string> participant_list;
	std::vector<xretr_time_list> time_list;
} ;

#endif
