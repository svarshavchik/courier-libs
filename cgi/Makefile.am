#
# Copyright 1998 - 2007 S. Varshavchik.  See COPYING for
# distribution information.


noinst_LTLIBRARIES=libcgi.la

libcgi_la_SOURCES=cgi.h cgi.C cgiextrapath.c cgiselect.c cgicheckbox.c\
	cgiinput.c cgihttpscriptptr.c cgihttpsscriptptr.c cgirelscriptptr.c \
	cginocache.c cgiredirect.c cgitextarea.c cgiversion.c cgihasversion.c \
	cgicookie.C cgiuseragent.c cgidaemon.c cgidaemond.c \
	cgiurlencode.C cgimultiple.C

noinst_PROGRAMS=testsuite

testsuite_SOURCES=testsuite.cpp
testsuite_LDADD=libcgi.la
testsuite_DEPENDENCIES=libcgi.la

check-am:
	./testsuite
	test "`which valgrind 2>/dev/null`" != "" || exit 0; set -evx; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 \
			./testsuite
