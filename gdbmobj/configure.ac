dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT([gdbmobj],[0.10],[courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(gdbmobj.c)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])
AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_SEARCH_LIBS([strerror],[cposix])
AC_PROG_AWK
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_17
LT_INIT

dnl Checks for libraries.

saveLIBS="$LIBS"
AC_CHECK_LIB(gdbm, gdbm_open, [ LIBGDBM=-lgdbm ; LIBS="-lgdbm $LIBS" ])

AC_CHECK_FUNC(gdbm_open, FOUND_GDBM=1, FOUND_GDBM=0)

if test "$FOUND_GDBM" = 1
then
	AC_CHECK_FUNCS(gdbm_fdesc)
fi
AC_CHECK_HEADERS(unistd.h fcntl.h)

LIBS="$saveLIBS"

AC_SUBST(LIBGDBM)

AM_CONDITIONAL(FOUND_GDBM, test "$FOUND_GDBM" = 1)

dnl Checks for header files.

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE

dnl Checks for library functions.

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
