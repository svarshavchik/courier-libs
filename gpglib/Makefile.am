#
# Copyright 2001-2026 S. Varshavchik.  See COPYING for
# distribution information.
#


CLEANFILES=gpg.h

noinst_SCRIPTS=webgpg

noinst_LTLIBRARIES=libgpg.la
noinst_PROGRAMS=testgpg mimegpg

libgpg_la_SOURCES=checksign.c delete.c export.c fork.c genkey.c gpg.C gpglib.h\
	import.c libgpg.c list.c mimegpgfork.c mimegpgfork.h mimegpgheader.c \
	mimegpgheader.h mimegpgstack.c mimegpgstack.h options.c rfc2045.C \
	sign.c tempname.c tempname.h

testgpg_SOURCES=testgpg.c
testgpg_DEPENDENCIES=libgpg.la ../numlib/libnumlib.la ../rfc822/librfc822.la \
	../rfc2045/librfc2045.la ../rfc2045/libencode.la
testgpg_LDADD=$(testgpg_DEPENDENCIES) -lcourier-unicode -lpthread
testgpg_LDFLAGS=-static
nodist_EXTRA_testgpg_SOURCES=testgpg_linkcpp.cpp

mimegpg_SOURCES=mimegpg.c
mimegpg_LDADD=libgpg.la ../rfc2045/librfc2045.la ../rfc822/librfc822.la \
		../rfc2045/libencode.la ../numlib/libnumlib.la \
		-lcourier-unicode -lpthread
mimegpg_DEPENDENCIES=libgpg.la ../rfc2045/librfc2045.la ../rfc822/librfc822.la\
		../rfc2045/libencode.la ../numlib/libnumlib.la
nodist_EXTRA_mimegpg_SOURCES=mimegpg_linkcpp.cpp
BUILT_SOURCES=gpg.h mimegpg.html mimegpg.1
EXTRA_DIST=mimegpg.html mimegpg.1 README.html testsuite

if HAVE_SGML

mimegpg.html: mimegpg.sgml ../docbook/sgml2html
	../docbook/sgml2html mimegpg.sgml mimegpg.html

mimegpg.1: mimegpg.sgml ../docbook/sgml2man
	../docbook/sgml2man mimegpg.sgml mimegpg.1 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"

endif

gpg.h: config.status
	echo '#define GPG "@GPG@"' >gpg.h

check-am:
	$(srcdir)/testsuite
