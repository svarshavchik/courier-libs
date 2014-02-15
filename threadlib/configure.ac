dnl Process this file with autoconf to produce a configure script.
AC_INIT(threadlib, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(pthread.c)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])

AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_CC

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h pthread.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

dnl Checks for library functions.

THREADLIB=""
save_LIBS="$LIBS"

AC_CHECK_LIB(pthread, pthread_cond_wait, [
	THREADLIB="-lpthread" ; LIBS="-lpthread $LIBS" ], [

LIBS="-pthread $save_LIBS"
AC_TRY_LINK([
void pthread_cond_wait();
],[
	pthread_cond_wait();
],
	THREADLIB="-pthread"
)
]
)

LIBS="$THREADLIB $save_LIBS"

have_pthreads=no

AC_CHECK_HEADER(pthread.h, [
	AC_CHECK_FUNC(pthread_cond_wait, have_pthreads=yes)
]
)

LIBS="$save_LIBS"

AC_ARG_WITH(pthreads, [--without-pthreads        - do not use Posix threads ],
	if test "$withval" = "no"
	then
		have_pthreads=no
	fi
	)

if test "$have_pthreads" = "no"
then
	THREADLIB=""
else
	AC_DEFINE_UNQUOTED(HAVE_PTHREADS,1,
		[ Whether pthreads are available ])
fi

AM_CONDITIONAL(HAVE_PTHREADS, test "$have_pthreads" != "no")

AC_SUBST(THREADLIB)

AC_CHECK_LIB(m, sqrt)

if test "$GCC" = "yes"
then
	CFLAGS="-Wall $CFLAGS"
fi
AC_OUTPUT(Makefile)
