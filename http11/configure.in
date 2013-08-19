dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 1998 - 2005 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(http11, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(http11.h.in)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])


dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_LIBTOOL

if test "$GCC" = yes ; then
        CXXFLAGS="$CXXFLAGS -Wall"
        CFLAGS="$CFLAGS -Wall"
fi

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE

dnl Checks for library functions.

AC_ARG_WITH(defaultlang,
	[  --with-defaultlang=lang        Default Content-Language:],
	DEFAULTLANG="$withval", DEFAULTLANG="en")
AC_SUBST(DEFAULTLANG)

AC_OUTPUT(Makefile  http11.h)
