dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 1998 - 2005 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(cgi, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(cgi.c)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])

AC_CONFIG_HEADERS(cgi_config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_LIBTOOL

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h sys/time.h sys/wait.h sys/select.h sys/uio.h unistd.h)
AC_HEADER_TIME

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

AC_CACHE_CHECK([for socklen_t],
        cgi_cv_hassocklen_t,
 
AC_COMPILE_IFELSE([
AC_LANG_SOURCE( [
#include <sys/types.h>
#include <sys/socket.h>
 
socklen_t sl_t;
],[
	accept(0, 0, &sl_t);
])],
	cgi_cv_hassocklen_t=yes,
	cgi_cv_hassocklen_t=no)
)
 
socklen_t="int"
 
if test $cgi_cv_hassocklen_t = yes
then
	:
else
	AC_DEFINE_UNQUOTED(socklen_t, int, [ Default definition for socklen_t ])
fi


dnl Checks for library functions.
AC_CHECK_FUNCS(strdup strncasecmp)

dnl Other checks

AC_ARG_WITH(formdata,
	[  --with-formdata                Compile support for multipart/formdata],
	AC_DEFINE_UNQUOTED(CGIFORMDATA, 1,
		[ Whether to generate code to handle multipart/formdata ]))

AC_ARG_WITH(maxargsize,
	[  --with-maxargsize=nbytes       Limit maximum size of CGI args],
	CFLAGS="$CFLAGS -DCGIMAXARG=$withval")

AC_ARG_WITH(maxformargsize,
	[  --with-maxformargsize=nbytes   Maximum size of multipart/formdata uploads],
	CFLAGS="$CFLAGS -DCGIMAXFORMDATAARG=$withval")

AC_TRY_RUN( [

#include	<stdio.h>
#include	<stdlib.h>
#include	<limits.h>

int main()
{
FILE	*fp=fopen("conftestval", "w");

	if (!fp)	exit(1);
	fprintf(fp, "-%lu\n", ULONG_MAX);
	fclose(fp);
	return (0);
}
]
	, [ MAXLONGSIZE=`wc -c conftestval | awk ' { print $1 } '  ` ],
	[
		AC_MSG_ERROR(Unable to run test program.)
	] ,
	[
		MAXLONGSIZE=60
		AC_MSG_WARN([Cross compiling, setting MAXLONGSIZE to $MAXLONGSIZE])
	]
		)

AC_CACHE_CHECK([how to pass file descriptors],
		    ac_cv_sqwebmail_passfd,

AC_TRY_COMPILE( [
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

int fd; struct msghdr msg; ],
[
	msg.msg_accrights=(caddr_t)fd;
	msg.msg_accrightslen=sizeof(fd);
],
	ac_cv_sqwebmail_passfd=msg_accrights,

	AC_TRY_COMPILE( [
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

struct msghdr msg; struct cmsghdr cmsg; char buf;
],
[
              msg.msg_control = &buf;
              msg.msg_controllen = 1;
],
	ac_cv_sqwebmail_passfd=msg_control,
	ac_cv_sqwebmail_passfd=none)))

if test "$ac_cv_sqwebmail_passfd" = "msg_accrights"
then
	AC_DEFINE_UNQUOTED(CGI_PASSFD_MSGACCRIGHTS,1,
		[Pass file descriptors in msg_accrights])
fi

if test "$ac_cv_sqwebmail_passfd" = "msg_control"
then
	AC_DEFINE_UNQUOTED(CGI_PASSFD_MSGCONTROL,1,
		[Pass file descriptors in msg_control])
fi


if test "$GCC" = yes ; then
        CFLAGS="$CFLAGS -Wall"
fi

CFLAGS="$CFLAGS -I.. -I$srcdir/.."
AC_SYS_LARGEFILE

AC_DEFINE_UNQUOTED(MAXLONGSIZE, $MAXLONGSIZE, [ Calculate max size of long ])
AC_OUTPUT(Makefile)
