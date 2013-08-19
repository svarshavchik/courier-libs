#
# Copyright 2004-2007 Double Precision, Inc.  See COPYING for
# distribution information.

sbin_PROGRAMS=courierlogger

BUILT_SOURCES=courierlogger.html courierlogger.1
man_MANS=courierlogger.1

EXTRA_DIST=$(BUILT_SOURCES)

courierlogger_SOURCES=logger.c
courierlogger_DEPENDENCIES=../liblock/liblock.la ../numlib/libnumlib.la
courierlogger_LDADD=../liblock/liblock.la ../numlib/libnumlib.la
courierlogger_LDFLAGS=-static

if HAVE_SGML
courierlogger.html: courierlogger.sgml ../docbook/sgml2html
	../docbook/sgml2html courierlogger.sgml courierlogger.html

courierlogger.1: courierlogger.sgml ../docbook/sgml2html
	../docbook/sgml2man courierlogger.sgml courierlogger.1
endif
