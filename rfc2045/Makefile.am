#
# Copyright 1998 - 2025 Double Precision, Inc.  See COPYING for
# distribution information.

noinst_LTLIBRARIES=librfc2045.la libencode.la
noinst_PROGRAMS=reformime makemime headercheck testrfc3676parser \
	testsuitecpp testsuitecpp2 \
	testrfc6533parser

AM_CXXFLAGS=@COURIER_UNICODE_CXXFLAGS@

BUILT_SOURCES=reformime.html reformime.1 makemime.html makemime.1 \
	rfc2045.html rfc2045.3

EXTRA_DIST=$(BUILT_SOURCES) testsuite testsuite.txt.idn \
	testsuitemm testsuitemm.txt \
	testrfc3676parsersuite testrfc3676parsersuite.txt

noinst_DATA=$(BUILT_SOURCES)

librfc2045_la_SOURCES=rfc2045.c rfc2045.h rfc2045cpp.C rfc2045cpp2.C \
		     rfc2045defcharset.c \
		     rfc2045defs.c rfc2045src.h \
		     rfc2045acchk.c rfc2045acchkcpp.C rfc2045acprep.c \
		     rfc2045appendurl.c rfc2045cdecode.c rfc2045decode.c \
		     rfc2045enomem.c \
		     rfc2045new_boundary.C \
		     rfc2045_fromfd.c \
                     rfc2045find.c rfc2045mkboundary.c rfc2045rewrite.c \
                     rfc2045tryboundary.c rfc2045xdump.c \
		     rfc2045searchcontenttype.c rfc2045decodemimesection.c \
		     rfc2045decodemimesectionu.c rfc2045header.c \
		     rfc2045reply.c \
		     rfc2045decodemsgtoutf8.c \
		     rfc2231.c rfc2231encode.C \
		     rfc3676parser.h rfc3676parser.c rfc3676parsercpp.C \
                     rfc6533.c \
		     base64.c base64.h

libencode_la_SOURCES=encode.C encode.h encodeautodetect.C

reformime_SOURCES=reformime.C
reformime_DEPENDENCIES =  librfc2045.la libencode.la ../rfc822/librfc822.la \
		../numlib/libnumlib.la
reformime_LDADD = $(reformime_DEPENDENCIES) -lcourier-unicode
reformime_LDFLAGS=-static

makemime_SOURCES=makemime.C
makemime_DEPENDENCIES= libencode.la librfc2045.la ../rfc822/librfc822.la ../numlib/libnumlib.la
makemime_LDADD = $(makemime_DEPENDENCIES) -lcourier-unicode
makemime_LDFLAGS=-static
nodist_EXTRA_makemime_SOURCES=forcecpp.cc

headercheck_SOURCES=headercheck.c
headercheck_DEPENDENCIES=librfc2045.la ../rfc822/librfc822.la ../numlib/libnumlib.la
headercheck_LDADD=$(headercheck_DEPENDENCIES) -lcourier-unicode
headercheck_LDFLAGS=-static

testrfc3676parser_SOURCES=testrfc3676parser.c
testrfc3676parser_DEPENDENCIES=librfc2045.la
testrfc3676parser_LDADD=$(testrfc3676parser_DEPENDENCIES) -lcourier-unicode

testrfc6533parser_SOURCES=testrfc6533parser.c
testrfc6533parser_DEPENDENCIES=librfc2045.la
testrfc6533parser_LDADD=$(testrfc6533parser_DEPENDENCIES) -lcourier-unicode

testsuitecpp_SOURCES=testsuitecpp.C testsuitecpp.H
testsuitecpp_DEPENDENCIES=librfc2045.la libencode.la ../rfc822/librfc822.la
testsuitecpp_LDADD=$(testsuitecpp_DEPENDENCIES) -lcourier-unicode

testsuitecpp2_SOURCES=testsuitecpp2.C testsuitecpp.H
testsuitecpp2_DEPENDENCIES=librfc2045.la ../rfc822/librfc822.la
testsuitecpp2_LDADD=$(testsuitecpp2_DEPENDENCIES) -lcourier-unicode -lpthread

if HAVE_SGML
reformime.html: reformime.sgml ../docbook/sgml2html
	../docbook/sgml2html reformime.sgml reformime.html

reformime.1: reformime.sgml ../docbook/sgml2html
	../docbook/sgml2man reformime.sgml reformime.1 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/maildrop/"

makemime.html: makemime.sgml ../docbook/sgml2html
	../docbook/sgml2html makemime.sgml makemime.html

makemime.1: makemime.sgml ../docbook/sgml2html
	../docbook/sgml2man makemime.sgml makemime.1 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/maildrop/"

rfc2045.html: rfc2045.sgml ../docbook/sgml2html
	../docbook/sgml2html rfc2045.sgml rfc2045.html

rfc2045.3: rfc2045.sgml ../docbook/sgml2html
	../docbook/sgml2man rfc2045.sgml rfc2045.3 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/maildrop/"
endif

check-am:
	@SHELL@ $(srcdir)/testsuite | diff -U 3 $(srcdir)/testsuite.txt.idn -
	@SHELL@ $(srcdir)/testsuitemm | diff -U 3 $(srcdir)/testsuitemm.txt -
	@SHELL@ $(srcdir)/testrfc3676parsersuite | diff -U 3 $(srcdir)/testrfc3676parsersuite.txt -
	./testrfc6533parser
	./testsuitecpp
	./testsuitecpp2
	test "`which valgrind 2>/dev/null`" != "" || exit 0; set -evx; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testrfc6533parser ; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testsuitecpp ; \
		valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testsuitecpp2
