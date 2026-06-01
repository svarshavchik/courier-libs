#
# Copyright 1998 - 2009 S. Varshavchik.  See COPYING for
# distribution information.


noinst_LTLIBRARIES=librfc822.la

librfc822_la_SOURCES=rfc822.c rfc822.h rfc822hdr.c rfc822hdrcpp.C rfc822hdr.h \
		  rfc822_2047.h rfc822cpp.C \
		  rfc822_getaddr.c rfc822_getaddrs.c \
		  rfc822_fdstreambuf.C \
                  rfc822_mkdate.c rfc822_parsedt.C rfc2047u.c \
                  rfc2047.c rfc2047.h imapsubj.C imaprefs.h imaprefs.C
librfc822_la_LIBADD = $(LIBIDN_LIBS)

DISTCLEANFILES=rfc822.config

AM_CFLAGS = $(LIBIDN_CFLAGS)

BUILT_SOURCES=rfc822.3 rfc822.html
noinst_DATA=$(BUILT_SOURCES)

noinst_PROGRAMS=testsuite testsuitecpp testsuite2 reftest
nodist_EXTRA_testsuite_SOURCES=cpplink.C
testsuite_SOURCES=testsuite.c
testsuite_DEPENDENCIES=librfc822.la
testsuite_LDADD=librfc822.la -lcourier-unicode
testsuite_LDFLAGS=-static

testsuitecpp_SOURCES=testsuitecpp.C
testsuitecpp_DEPENDENCIES=librfc822.la
testsuitecpp_LDADD=librfc822.la -lcourier-unicode
testsuitecpp_LDFLAGS=-static

testsuite2_SOURCES=testsuite2.C
testsuite2_DEPENDENCIES=librfc822.la
testsuite2_LDADD=librfc822.la -lcourier-unicode

reftest_SOURCES=reftest.C imaprefs.h
reftest_DEPENDENCIES=librfc822.la
reftest_LDADD=librfc822.la -lcourier-unicode
reftest_LDFLAGS=-static

EXTRA_DIST=testsuite.txt reftest.txt $(BUILT_SOURCES)

if HAVE_SGML
rfc822.html: rfc822.sgml ../docbook/sgml2html
	../docbook/sgml2html rfc822.sgml rfc822.html

rfc822.3: rfc822.sgml ../docbook/sgml2html
	../docbook/sgml2man rfc822.sgml rfc822.3 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
endif

check-am:
	./testsuite | diff -U 5 $(srcdir)/testsuite.txt -
	./testsuitecpp | diff -U 5 $(srcdir)/testsuite.txt -
	./testsuite2
	./reftest | diff -U 3  $(srcdir)/reftest.txt -
	test "`which valgrind 2>/dev/null`" != "" || exit 0; set -vx; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testsuite >/dev/null || exit 1; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testsuitecpp >/dev/null || exit 1; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testsuite2 >/dev/null || exit 1; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./reftest >/dev/null || exit 1;
clean-local:
	rm -rf $(DEPDIR)
