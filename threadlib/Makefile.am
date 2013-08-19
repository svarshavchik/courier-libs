#
# Copyright 2000-2002 Double Precision, Inc.
# See COPYING for distribution information.
#


noinst_LIBRARIES=libthreadlib.a
noinst_DATA=libthread.dep
noinst_PROGRAMS=threadtest

CLEANFILES=libthread.dep

if HAVE_PTHREADS
threadc=pthread.c
else
threadc=nopthread.c
endif

libthreadlib_a_SOURCES=$(threadc) threadlib.h havepthread.h nopthread.h
EXTRA_DIST=pthread.c nopthread.c testsuite testsuite.txt

libthread.dep: config.status
	echo @THREADLIB@ >libthread.dep

threadtest_SOURCES=test.c
threadtest_DEPENDENCIES=libthreadlib.a libthread.dep
threadtest_LDADD=libthreadlib.a `cat libthread.dep`

check-am: threadtest
	@echo '------------- Testing threadlib ---------------'
	@sh $(srcdir)/testsuite | tee t
	@cmp -s t $(srcdir)/testsuite.txt
	@rm -f t
