dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 2000 - 2006 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(ldapaddressbook, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(Makefile.am)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])
LPATH="$PATH:/usr/local/bin"
AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_LIBTOOL

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(stdio.h sys/wait.h sys/time.h)
AC_HEADER_TIME

AC_ARG_WITH(ldapaddressbook,
[  --without-ldapaddressbook   Disable LDAP address book code ],
   HAVE_OPENLDAP="$withval",
   HAVE_OPENLDAP=1)

OPENLDAP_LIBS=""

if test "$HAVE_OPENLDAP" = 1
then
	HAVE_OPENLDAP=0

AC_CHECK_HEADERS([lber.h])
AC_CHECK_HEADERS([ldap.h],
	[ HAVE_OPENLDAP=1 ],,[
#if HAVE_LBER_H
#include <lber.h>
#endif
])

if test "$HAVE_OPENLDAP" = 1
then
	save_LIBS="$LIBS"
	LIBS="$LIBS -lldap"
	AC_TRY_LINK([
#include <stdio.h>
#if HAVE_LBER_H
#include <lber.h>
#endif
#include <ldap.h>
],
[
	LDAP *p=NULL;

	ldap_search(p, "", 0, NULL, NULL, 0);
],
[
	:
],
[
	AC_MSG_ERROR([ldap.h header found, but a test compile failed -- check ldapaddressbook/config.log for more details])
])
	LIBS="$save_LIBS"
	OPENLDAP_LIBS="-lldap"
fi
fi

AC_DEFINE_UNQUOTED(HAVE_OPENLDAP, $HAVE_OPENLDAP,
		[ Whether openldap support has been detected ])

AM_CONDITIONAL(HAVE_OPENLDAP, [test $HAVE_OPENLDAP = 1])
AC_SUBST(OPENLDAP_LIBS)

AC_HEADER_SYS_WAIT
dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_SYS_LARGEFILE

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

dnl Checks for library functions.

AC_OUTPUT(Makefile)
