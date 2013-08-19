#
# Copyright 2000-2005 Double Precision, Inc.  See COPYING for
# distribution information.


EXTRA_DIST=ldapaddressbook.dist
noinst_LTLIBRARIES=libaddressbook.la
noinst_PROGRAMS=ldapsearch

libaddressbook_la_SOURCES=abookadd.c abookdel.c abookfind.c abookread.c \
	abooksearch.c ldapaddressbook.h

if HAVE_OPENLDAP
ldapsearch_SOURCES=ldapsearch.c
ldapsearch_LDADD=libldapsearch.la
noinst_LTLIBRARIES += libldapsearch.la

libldapsearch_la_SOURCES=libldapsearch.h libldapsearch.c
libldapsearch_la_LIBADD=@OPENLDAP_LIBS@

else

ldapsearch_SOURCES=noldapsearch.c

endif
