#
# Copyright 2001-2014 Double Precision, Inc.  See COPYING for
# distribution information.
#


CLEANFILES=gpg.h

noinst_SCRIPTS=webgpg

noinst_LTLIBRARIES=libgpg.la
noinst_PROGRAMS=testgpg mimegpg

libgpg_la_SOURCES=checksign.c delete.c export.c fork.c genkey.c gpg.c gpglib.h\
	import.c libgpg.c list.c mimegpgfork.c mimegpgfork.h mimegpgheader.c \
	mimegpgheader.h mimegpgstack.c mimegpgstack.h options.c rfc2045.c \
	sign.c tempname.c tempname.h

testgpg_SOURCES=testgpg.c
testgpg_DEPENDENCIES=libgpg.la ../numlib/libnumlib.la
testgpg_LDADD=$(testgpg_DEPENDENCIES) -lcourier-unicode
testgpg_LDFLAGS=-static

mimegpg_SOURCES=mimegpg.c
mimegpg_LDADD=libgpg.la ../rfc2045/librfc2045.la ../rfc822/librfc822.la \
		../rfc822/libencode.la ../numlib/libnumlib.la \
		-lcourier-unicode
mimegpg_DEPENDENCIES=libgpg.la ../rfc2045/librfc2045.la ../rfc822/librfc822.la\
		../rfc822/libencode.la ../numlib/libnumlib.la
BUILT_SOURCES=gpg.h mimegpg.html mimegpg.1
EXTRA_DIST=mimegpg.html mimegpg.1 README.html

if HAVE_SGML

mimegpg.html: mimegpg.sgml ../docbook/sgml2html
	../docbook/sgml2html mimegpg.sgml mimegpg.html

mimegpg.1: mimegpg.sgml ../docbook/sgml2man
	../docbook/sgml2man mimegpg.sgml mimegpg.1 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"

endif

gpg.h: config.status
	echo '#define GPG "@GPG@"' >gpg.h
