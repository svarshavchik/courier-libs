dnl Process this file with autoconf to produce a configure script.
#
# Copyright 2001-2003 Double Precision, Inc.  See COPYING for
# distribution information.

AC_INIT(pcp, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(pcp.h)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])
AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_AWK
AC_PROG_INSTALL
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL
AC_PATH_PROGS(PERL, perl5 perl, perl)

CFLAGS="-I.. -I${srcdir}/.. -I$srcdir/../.. -I../.. $CFLAGS"
ALL_LINGUAS=""
AM_GNU_GETTEXT

AC_PATH_PROGS(COURIERAUTHCONFIG, courierauthconfig)

if test "$COURIERAUTHCONFIG" = ""
then
	AC_MSG_ERROR(COURIERAUTHCONFIG not found)
fi

CPPAUTH="`$COURIERAUTHCONFIG --cppflags`"
LDAUTH="`$COURIERAUTHCONFIG --ldflags`"
AC_SUBST(CPPAUTH)
AC_SUBST(LDAUTH)

dnl Checks for libraries.

USENSL=no
saveLIBS="$LIBS"
AC_CHECK_LIB(socket,socket,result=yes,result=no)
if test $result = yes; then
        NETLIBS="-lsocket"
else
        AC_CHECK_LIB(socket,socket,result=yes,result=no,-lnsl)
        if test $result = yes; then
                NETLIBS = "-lsocket -lnsl"
                USENSL=yes
        else
                AC_CHECK_LIB(socket,connect,result=yes,result=no)
                if test $result = yes; then
                        NETLIBS="-lsocket"
                else
                        AC_CHECK_LIB(socket,connect,result=yes,result=no,-lnsl)
                        if test $result = yes; then
                                NETLIBS="-lsocket -lnsl"
                                USENSL=yes
                        fi
                fi
        fi
fi

if test $USENSL != yes; then
	LIBS="$LIBS $NETLIBS"
	AC_TRY_LINK_FUNC(inet_addr, [ : ],
	[
	        AC_CHECK_LIB(nsl,inet_addr,result=yes,result=no)
	        if test $result = yes; then
	                NETLIBS="$NETLIBS -lnsl"
	        fi
	])
fi

LIBS="$saveLIBS"

echo $NETLIBS >pcp.libs
AC_SUBST(NETLIBS)

scriptdir="$datadir/sqwebmail"
SENDIT="$scriptdir/sendit.sh"
AC_SUBST(SENDIT)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h sys/stat.h sys/time.h termios.h unistd.h)
AC_HEADER_DIRENT
AC_HEADER_TIME

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_PID_T
AC_TYPE_SIGNAL
AC_STRUCT_TM
AC_SYS_LARGEFILE

AC_CACHE_CHECK([for socklen_t],
	pcpd_cv_hassocklen_t,
 
AC_COMPILE_IFELSE([
AC_LANG_SOURCE( [
#include <sys/types.h>
#include <sys/socket.h>
 
socklen_t sl_t;
],[
	accept(0, 0, &sl_t);
])],
	pcpd_cv_hassocklen_t=yes,
	pcpd_cv_hassocklen_t=no)
)
 
socklen_t="int"
 
if test $pcpd_cv_hassocklen_t = yes
then
	:
else
	AC_DEFINE_UNQUOTED(socklen_t, int, [ Default definition for socklen_t ])
fi

dnl Checks for library functions.
AC_CHECK_FUNCS(gethostname strdup tcgetattr)

AC_ARG_WITH(calendardir,
[  --with-calendardir=path    - use path instead of prefix/var/spool/calendar],
	calendardir="$withval",
	calendardir="$localstatedir/calendar")
AC_SUBST(calendardir)

localpcpdcachedir="$calendardir/localcache"
AC_SUBST(localpcpdcachedir)

AC_ARG_WITH(calendarpurge,
[  --with-calendarpurge=N     - purge past events over N days old (default: 30)],
	calendarpurge="$withval",
	calendarpurge=30)

AC_DEFINE_UNQUOTED(CALENDARPURGE,$calendarpurge,
	[ Purge calendar entries after this many days ])

# TODO

AC_ARG_ENABLE(hardtimeout,
	[  --enable-hardtimeout=n     Timeout after n seconds (min 1800)],
	TIMEOUT=$enableval, TIMEOUT=1800)
if test $TIMEOUT -lt 1800
then
	TIMEOUT=1800
fi
AC_SUBST(TIMEOUT)

AC_ARG_WITH(cacheowner,
	[  --with-cacheowner=user     Owner of the cache directory (default: bin)],
        localcacheowner="$withval", localcacheowner="bin")

AC_SUBST(localcacheowner)
AC_DEFINE_UNQUOTED(LOCALCACHEOWNER,"$localcacheowner",
	[ Owner of the login cache directory ])

AC_ARG_WITH(mailgroup,
	[],
	mailgroup="$withval",

	for mailgroup in mail sys bin
	do
		if $PERL -e ' getgrnam("'$mailgroup'") && exit 0; exit 1;'
		then
			break;
		fi
	done
)
AC_SUBST(mailgroup)
AC_DEFINE_UNQUOTED(MAILGROUP,"$mailgroup",
	[ Mail system group ])

AC_TRY_COMPILE( [
#include <signal.h>
#include <string.h>
#include <stdio.h>
],[
struct sigaction sa;

memset(&sa, 0, sizeof(sa));
sa.sa_handler=SIG_IGN;
sa.sa_flags=SA_NOCLDWAIT;
sigaction(SIGCHLD, &sa, NULL);
], AC_DEFINE_UNQUOTED(USE_NOCLDWAIT,1, [ Whether to use SA_NOCLDWAIT ]))

if test -f $srcdir/.debug
then
	EFENCE="-lefence"
fi
AC_SUBST(EFENCE)

if test x$GCC = xyes
then
	CFLAGS="-Wall $CFLAGS"
fi

CPPFLAGS="-Iintl/ -I${srcdir}/intl $CPPFLAGS"
MKINSTALLDIRS="$mkdir_p"
AC_OUTPUT(Makefile po/Makefile.in   )
