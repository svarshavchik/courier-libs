dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 1998 - 2009 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT([rfc822lib],[0.13],[courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(rfc822.c)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])
AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_17
LT_INIT

if test "$GCC" = yes ; then
        CXXFLAGS="$CXXFLAGS -Wall"
        CFLAGS="$CFLAGS -Wall"
fi

CXXFLAGS="$CXXFLAGS -I.. -I$srcdir/.."
CFLAGS="$CFLAGS -I.. -I$srcdir/.."

dnl Checks for libraries.

dnl Checks for header files.
AC_CHECK_HEADERS(locale.h strings.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_SYS_LARGEFILE

dnl Checks for library functions.
AX_COURIER_UNICODE_VERSION(2.4.0)

PKG_CHECK_MODULES(LIBIDN, libidn2 >= 2.0.5, [libidn=yes], [libidn=no])

if test "$libidn" = "no"
then
	AC_MSG_ERROR(libidn2 not found)
fi

AC_CHECK_FUNCS(setlocale)

AC_CACHE_CHECK([how to calculate alternate timezone],librfc822_cv_SYS_TIMEZONE,

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include	<time.h>
time_t  t;
]], [[
t=altzone;
]])],[librfc822_cv_SYS_TIMEZONE=altzone],[])

if test "$librfc822_cv_SYS_TIMEZONE" = ""
then
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include	<time.h>

extern struct tm dummy;
long n;
]], [[
	n=dummy.tm_gmtoff;
	]])],[librfc822_cv_SYS_TIMEZONE=tm_gmtoff],[librfc822_cv_SYS_TIMEZONE=unknown])
fi
)

if test "$librfc822_cv_SYS_TIMEZONE" = ""
then
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include	<time.h>
int n;
]], [[
	n=daylight;
	]])],[librfc822_cv_SYS_TIMEZONE=daylight],[])
fi

case $librfc822_cv_SYS_TIMEZONE in
tm_gmtoff)
	AC_DEFINE_UNQUOTED(USE_TIME_GMTOFF,1,
		[ The time offset is specified in the tm_gmtoff member ])
	;;
altzone)
	AC_DEFINE_UNQUOTED(USE_TIME_ALTZONE,1,
		[ The daylight savings time offset is in the altzone member ])
	;;
daylight)
	AC_DEFINE_UNQUOTED(USE_TIME_DAYLIGHT,1,
		[ The daylight savings time offset is in the tm_isdst member ])
	;;
*)
	AC_MSG_WARN([Cannot figure out how to calculate the alternate timezone, will use GMT])
	;;
esac

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
