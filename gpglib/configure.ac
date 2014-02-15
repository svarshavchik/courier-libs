dnl Process this file with autoconf to produce a configure script.
dnl
dnl Copyright 2001-2010 Double Precision, Inc.  See COPYING for
dnl distribution information.
dnl

AC_INIT(webpgp, 0.10, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(webgpg.in)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])
LPATH="$PATH:/usr/local/bin"

AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_CC
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL
AC_PROG_LN_S
AC_PATH_PROGS(GPG, gpg gpg2, /usr/bin/gpg, $LPATH)
AC_PATH_PROGS(PERL, perl5 perl, perl, $LPATH)

if test "$PERL" = "perl"
then
        AC_MSG_ERROR(Perl is required)
fi

AC_CACHE_CHECK( [for gpg charset], ac_cv_gpg_charset,

rm -rf conftestdir
mkdir conftestdir

GNUPGHOME=`pwd`/conftestdir
export GNUPGHOME

if $GPG --charset utf-8 --list-keys >/dev/null 2>&1
then
	ac_cv_gpg_charset=utf-8
else
	ac_cv_gpg_charset=iso-8859-1
fi
rm -rf conftestdir
unset GNUPGHOME

)

AC_DEFINE_UNQUOTED(GPG_CHARSET,"$ac_cv_gpg_charset",
	[ Default gpg output character set ])

VERSION="`$GPG --version | sed '2,$d;s/.* //'`"

if test "$VERSION" = ""
then
	AC_MSG_WARN(Unable to determine gpg version)
else
	has_cert_check_level=1

	case $VERSION in
	1.0.4)
		has_cert_check_level=0
		;;
	1.0.5)
		has_cert_check_level=0
		;;
	1.0.6)
		has_cert_check_level=0
		;;
	esac

fi

if test "$has_cert_check_level" = 1
then
	AC_DEFINE_UNQUOTED(GPG_HAS_CERT_CHECK_LEVEL,1,
	[ Whether gpg --sign-key asks for certificate trust level ])
fi

rm -rf conftempdir
mkdir conftempdir

if $GPG --homedir conftempdir --list-keys --allow-secret-key-import >/dev/null
then
	AC_DEFINE_UNQUOTED(GPG_HAS_ALLOW_SECRET_KEY_IMPORT,1,
	[ Whether gpg has the --allow-secret-key-import option ])
fi

rm -rf conftempdir

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h sys/wait.h sys/time.h unistd.h fcntl.h)
AC_HEADER_SYS_WAIT
AC_HEADER_TIME

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE

dnl Checks for library functions.
AC_TYPE_SIGNAL
AC_CHECK_FUNCS(gettimeofday)

if test "$GCC" = yes ; then
        CFLAGS="$CFLAGS -Wall"
fi

CFLAGS="-I.. -I$srcdir/.. $CFLAGS"

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

AC_OUTPUT(Makefile webgpg)
