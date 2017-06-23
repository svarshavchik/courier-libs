dnl
dnl Copyright 1998 - 2015 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(sqwebmail, 5.9.0, [courier-sqwebmail@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(sqwebmail.c)
AC_CONFIG_AUX_DIR(../..)
AM_INIT_AUTOMAKE([foreign no-define])

LPATH="$PATH:/usr/local/bin"
AC_CONFIG_HEADERS(config.h)
AC_PROG_MAKE_SET

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL

if test "$GCC" = yes ; then
        CXXFLAGS="$CXXFLAGS -Wall"
        CFLAGS="$CFLAGS -Wall"
fi

AC_PATH_PROGS(SETENV, env)
AC_PATH_PROGS(PERL,perl5 perl, perl, $LPATH)
AC_PATH_PROGS(GZIP,gzip,gzip,$LPATH)

if test "$PERL" = "perl"
then
	AC_MSG_ERROR(Perl not found)
fi

AC_PATH_PROGS(COURIERAUTHCONFIG, courierauthconfig)

if test "$COURIERAUTHCONFIG" = ""
then
	AC_MSG_ERROR(COURIERAUTHCONFIG not found)
fi

CPPAUTH="`$COURIERAUTHCONFIG --cppflags`"
LDAUTH="`$COURIERAUTHCONFIG --ldflags`"
AC_SUBST(CPPAUTH)
AC_SUBST(LDAUTH)

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'

eval "prefix=$prefix"
eval "exec_prefix=$exec_prefix"
eval "datadir=$datadir"
eval "bindir=$bindir"
eval "localstatedir=$localstatedir"

htmldir="$datadir/sqwebmail/html"
scriptdir="$datadir/sqwebmail"

AC_SUBST(htmldir)
AC_SUBST(scriptdir)

AC_ARG_WITH(mailer,
[  --with-mailer=prog  Your mail submission program],
	mailer="$withval",

[
	if test -d ${srcdir}/../../courier
	then
		mailer="$bindir/sendmail"
	else
		SENDMAIL_PATH=$PATH:/etc:/sbin:/usr/sbin:/usr/local/bin:/var/qmail/bin
		AC_PATH_PROG(sendmail, sendmail, /usr/bin/sendmail, $SENDMAIL_PATH)
		mailer="$sendmail -oi -t"
	fi
]
)

AC_SUBST(mailer)

if test -d ${srcdir}/../../courier
then
	pkglibexecdir='$(libexecdir)/courier'
else
	pkglibexecdir='$(libexecdir)/sqwebmail'
fi
AC_SUBST(pkglibexecdir)

AC_ARG_WITH(ispell,
[  --without-ispell      Do not compile ispell support
  --with-ispell=prog    Pathname to ispell (if not in default path)],
	ispell="$withval", [ AC_PATH_PROG(ispell, ispell, N, $LPATH) ] )

case $ispell in
"/"*)
	AC_DEFINE_UNQUOTED(ISPELL, "$ispell",
	[ Path to ispell or aspell ])
	;;
*)
	ispell=""
	;;
esac

AM_CONDITIONAL(ISPELL, test "$ispell" != "")

AC_ARG_ENABLE(autorenamesent, [
  --enable-autorenamesent   Enable Autorename Sent folder periodically (default)
  --disable-autorenamesent  Disable Autorename Sent folder periodically
], , enable_autorenamesent=yes)
if test ! "$enable_autorenamesent" = no; then
        AC_DEFINE(AUTORENAMESENT, "yes",
	[ Whether the Sent directory is automatically renamed monthly ])
else
        AC_DEFINE(AUTORENAMESENT, "no",
	[ Whether the Sent directory is automatically renamed monthly ])
fi

dnl Checks for libraries.

AC_CHECK_LIB(sun,getpwnam)
AC_CHECK_LIB(crypt, crypt)

dnl Check whether we should use gdbm, or db.

needs_withdb=0

. ../../dbobj.config
dblibrary="../../$dblibrary"
AC_SUBST(dblibrary)

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

AC_SUBST(NETLIBS)

AC_ARG_WITH(piddir, [  --with-piddir         Directory where imapd.pid is created ], piddir="$withval", piddir=$localstatedir/run)

AC_SUBST(piddir)

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h sys/wait.h sys/select.h sys/uio.h sys/stat.h sys/time.h utime.h unistd.h locale.h crypt.h wchar.h)

AC_HEADER_SYS_WAIT
AC_HEADER_TIME

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_STRUCT_TM
AC_TYPE_PID_T
AC_TYPE_UID_T
AC_TYPE_SIGNAL
AC_SYS_LARGEFILE

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

dnl Checks for library functions.
AC_FUNC_STRFTIME
AC_CHECK_FUNCS(crypt utime utimes setreuid setlocale)
AC_REPLACE_FUNCS(strdup strcasecmp strncasecmp)

AC_CACHE_CHECK([for crypt() prototype],sqwebmail_cv_NEED_CRYPT_PROTOTYPE,

AC_TRY_COMPILE( [
       #if HAVE_CRYPT_H
       #include <crypt.h>
       #endif
       #if HAVE_UNISTD_H
       #include <unistd.h>
       #endif
       int crypt(int, int);

        ], [], sqwebmail_cv_NEED_CRYPT_PROTOTYPE=1,
                sqwebmail_cv_NEED_CRYPT_PROTOTYPE=0 )

)

AC_DEFINE_UNQUOTED(NEED_CRYPT_PROTOTYPE,$sqwebmail_cv_NEED_CRYPT_PROTOTYPE,
	[ Whether crypt() must be prototyped ])

AC_MSG_CHECKING([whether libcharset is used])
AC_ARG_WITH(libcharset,
	[  --with-libcharset     Use the libcharset library],
	sqwebmail_with_libcharset=$withval,
	sqwebmail_with_libcharset=no)
AC_MSG_RESULT($sqwebmail_with_libcharset)
if test "$sqwebmail_with_libcharset" = "yes"; then
  AC_CHECK_LIB(charset,locale_charset)
  if test "$ac_cv_lib_charset_locale_charset" = "yes"; then
    AC_CHECK_HEADERS(localcharset.h libcharset.h)
  else
    AC_MSG_ERROR([libcharset was not found.  Install libcharset package.])
  fi
  AC_DEFINE_UNQUOTED(USE_LIBCHARSET, 1,
	[ Set this to 1 to use libcharset library. ])
else
  AM_LANGINFO_CODESET
fi

dnl Other

AC_ARG_ENABLE(https, [  --enable-https          Generate https:// URLs for all accesses
  --enable-https=login    Use https:// URLs for password transmission only
  --enable-https=auto     Use relative URLs (up to user which to use)
  --disable-https         Do not use https:// URLs no matter what.
                          (auto is the default)],
	https="$enableval", https="auto")

case "$https" in
1*|y*|Y*)
	AC_DEFINE_UNQUOTED(USE_HTTPS, 1, [ Always use https:// URLs ])
	AC_DEFINE_UNQUOTED(USE_HTTPS_LOGIN, 1,
		[ Use https:// for logins ])
	;;
login)
	AC_DEFINE_UNQUOTED(USE_HTTPS_LOGIN, 1,
		[ Use https:// for logins ])
	;;
auto)
	AC_DEFINE_UNQUOTED(USE_RELATIVE_URL, 1,
		[ Autodetect whether to use http or https URLs ])
	;;
esac


AC_ARG_ENABLE(hardtimeout, [  --enable-hardtimeout=7200  Hard session timeout (2hrs default)],
	TIMEOUTHARD=$enableval, TIMEOUTHARD=7200)
AC_DEFINE_UNQUOTED(TIMEOUTHARD,$TIMEOUTHARD,
	[ Hard login timeout ])
AC_SUBST(TIMEOUTHARD)

AC_ARG_ENABLE(softtimeout, [  --enable-softtimeout=1200  Inactivity timeout (20 min default)],
	TIMEOUTSOFT=$enableval, TIMEOUTSOFT=1200)
AC_DEFINE_UNQUOTED(TIMEOUTSOFT,$TIMEOUTSOFT,
	[ Soft login timeout ])

AC_ARG_ENABLE(autopurge, [  --enable-autopurge=7       Automatically purge deleted messages (days).],
	AUTOPURGE=$enableval, AUTOPURGE=7)
AC_DEFINE_UNQUOTED(AUTOPURGE,$AUTOPURGE,
	[ Automatically purge Trash after this many days ])

AC_ARG_ENABLE(maxpurge,  [  --enable-maxpurge=90       Maximum value for autopurge (days).],
	MAXPURGE=$enableval, MAXPURGE=90)
AC_DEFINE_UNQUOTED(MAXPURGE,$MAXPURGE,
	[ Maximum number of days mail is kept in Trash, before deleting ])

AC_ARG_WITH(maxmsgsize,
[  --with-maxmsgsize=nbytes       Limit max size of a created message, in bytes
                                 (default: 2 megabytes)],
	MAXMSGSIZE=$withval,
	MAXMSGSIZE=2097152)

AC_SUBST(MAXMSGSIZE)

AC_ARG_ENABLE(lang, [  --with-defaultlang={lang}       Default language to use (only en, so far)],
	lang="$enableval",lang=en; ac_configure_args="$ac_configure_args --with-defaultlang=$lang")

AC_ARG_ENABLE(mimecharset,
	[  --enable-mimecharset=charset   Default MIME charset to set on new messages])

mimetypes=""

for f in /usr/lib /usr/local/lib /usr/lib/pine /usr/local/lib/pine /etc \
         /var/lib/httpd/conf /home/httpd/conf /usr/local/etc/apache \
         /usr/local/apache/conf /var/lib/apache/etc /etc/httpd \
         /usr/local/apache2/conf
do
	if test -f $f/mime.types
	then
		if test "$mimetypes" != ""
		then
			mimetypes="$mimetypes:"
		fi
		mimetypes="$mimetypes$f/mime.types"
	fi
done

AC_ARG_ENABLE(mimetypes, [  --enable-mimetypes={dir}   Your mime.types file.],
	mimetypes="$enableval",
	[
		if test "$mimetypes" = ""
		then
			AC_MSG_ERROR([Unable to find your mime.types file, specify --enable-mimetypes.])
		fi
	] )
AC_DEFINE_UNQUOTED(MIMETYPES, "$mimetypes",
			      [ Your mime.types files ])

AC_DEFINE_UNQUOTED(MAXLONGSIZE, $MAXLONGSIZE,
				[ Maximum size of a long ])

AC_ARG_ENABLE(bannerprog,
	[  --enable-bannerprog=prog   Program which will generate a banner.],
	[
		AC_DEFINE_UNQUOTED(BANNERPROG, "$enableval",
		[ External banner generating program ])
		 ])


CFLAGS="$CFLAGS -I.. -I$srcdir/.. -I../.. -I$srcdir/../.."

AC_ARG_WITH(maxargsize,
	[  --with-maxargsize=nbytes       Limit maximum size of CGI args])
AC_ARG_WITH(maxformargsize,
	[  --with-maxformargsize=nbytes   Maximum size of multipart/formdata uploads])

cachedir=$localstatedir/webmail-logincache

AC_ARG_WITH(cachedir,
	[  --with-cachedir=dir        Specify location of the cache directory],
	cachedir="$withval")
AC_SUBST(cachedir)
AC_DEFINE_UNQUOTED(CACHEDIR, "$cachedir",
			     [ Login cache directory location ])

AC_ARG_WITH(cacheowner,
	[  --with-cacheowner=user     Owner of the cache directory (default: bin)],
	cacheowner="$withval", cacheowner="bin")

AC_DEFINE_UNQUOTED(CACHEOWNER, "$cacheowner",
			       [ Login cache directory owner ])
AC_SUBST(cacheowner)

dogzip="no"
if test "$GZIP" != "gzip"
then
	dogzip="yes"
fi

AC_ARG_WITH(gzip,
	[  --without-gzip                 Do not use gzip to compress messages],
	dogzip="$withval")

if test "$dogzip" = "no"
then
	GZIP=""
fi

if test "$GZIP" != ""
then
	AC_DEFINE_UNQUOTED(GZIP,"$GZIP",
	[ Path to gzip ])
fi

AC_ARG_WITH(efence,
	[  --with-efence                  Link with efence.a debugger ],
	efence=$withval, efence="N")

case $efence in
1*|y*|Y*)
	LIBS="$LIBS -lefence"
	;;
*)
	;;
esac

imageurl="/webmail"

AC_ARG_ENABLE(imageurl, [  --enable-imageurl={url}   URL to <DocumentRoot> directory for images.],
	imageurl="$enableval")

AC_SUBST(imageurl)
AC_MSG_CHECKING(URL to access images)
AC_MSG_RESULT($imageurl)

AC_CONFIG_SUBDIRS(images html)

INSTALL_SCRIPT='${INSTALL} -m 0755'

cp -f $srcdir/sqwebmail.pamconf webmail.authpam

if test -f /etc/pam.d/system-auth
then
	if ls /lib*/security/pam_stack.so 2>/dev/null >/dev/null
	then
		cp -f $srcdir/sqwebmail-system-auth.pamconf webmail.authpam
	else
		cp -f $srcdir/sqwebmail-system-auth2.pamconf webmail.authpam
	fi
fi

AC_ARG_WITH(mailuser, [], mailuser="$withval",
	AC_MSG_ERROR(--with-mailuser missing))
AC_ARG_WITH(mailgroup, [], mailgroup="$withval",
	AC_MSG_ERROR(--with-mailgroup missing))

AC_SUBST(mailuser)
AC_SUBST(mailgroup)

AC_ARG_WITH(minpasslen,
[  --with-minpasslen=ncharacters        Minimum length of a password, in characters
                                 (default: 1 character)],
	MINPASSLEN=$withval,
	MINPASSLEN=1)

AC_SUBST(MINPASSLEN)
AC_DEFINE_UNQUOTED(MINPASSLEN, $MINPASSLEN,
			     [ Minimum length of a password ])


AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

# This gets built by the make rule, and is not in the git repo

for f in sqwebmaild.dist.in
do
    if test ! -f "$srcdir/$f"
    then
	cp -p "$srcdir/$f.git" "$srcdir/$f"
	touch "$srcdir/$f.git"
    fi
done

AC_OUTPUT(Makefile sendit.sh cleancache.pl sqwebmaild.dist)

if test ! -d html/$lang
then
	AC_MSG_ERROR(Unknown language: $lang)
fi
