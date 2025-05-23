dnl Process this file with autoconf to produce a configure script.
dnl
dnl
dnl Copyright 1998 - 2025 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT([courier-imap],[5.2.11],[courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(imapd.c)
AC_CONFIG_AUX_DIR(../..)
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([foreign no-define])
LPATH="$PATH:/usr/local/bin"

AC_CONFIG_HEADERS(config.h)
dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX_17
LT_INIT
AC_PATH_PROGS(PERL, perl5 perl, perl, $LPATH)

if test "$PERL" = "perl"
then
	AC_MSG_ERROR(Perl is required)
fi

AC_PATH_PROGS(COURIERAUTHCONFIG, courierauthconfig)

if test "$COURIERAUTHCONFIG" = ""
then
	AC_MSG_ERROR(courierauthconfig not found)
fi

CPPAUTH="`$COURIERAUTHCONFIG --cppflags`"
LDAUTH="`$COURIERAUTHCONFIG --ldflags`"
AC_SUBST(CPPAUTH)
AC_SUBST(LDAUTH)

MAKECHECKBROKEN=N

test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'

eval "exec_prefix=$exec_prefix"
eval "bindir=$bindir"

AC_ARG_WITH(mailer,
[  --with-mailer=prog  Your mail submission program],
   SENDMAIL="$withval",

[
        if test -d ${srcdir}/../../courier
        then
                SENDMAIL="$bindir/sendmail"
	else
		SENDMAIL_PATH=$PATH:/etc:/sbin:/usr/sbin:/usr/local/bin:/var/qmail/bin
                AC_PATH_PROG(sendmail, sendmail, /usr/bin/sendmail, $SENDMAIL_PATH)
                SENDMAIL="$sendmail"
	fi
]
)
AC_SUBST(SENDMAIL)

AC_ARG_ENABLE(root-check, [  --disable-root-check    Allow build as root (make check will fail)],
	rootcheck="$enableval",
	rootcheck="yes")
AC_SUBST(rootcheck)

OPENSSL_PATH=$PATH:/sbin:/usr/sbin:/usr/local/sbin:/usr/local/bin

AC_PATH_PROGS(OPENSSL, openssl, , $OPENSSL_PATH)

if test "$OPENSSL" = ""
then
	OPENSSL=/usr/local/bin/openssl
fi
AC_SUBST(OPENSSL)

AC_PATH_PROGS(CERTTOOL, certtool, , $OPENSSL_PATH)

if test "$CERTTOOL" = ""
then
	CERTTOOL=/usr/local/bin/certtool
fi

AC_ARG_WITH(random, [ --with-random=/dev/urandom - location of the system random file generator
--without-random            - there is no system random file generator ],
	RANDOMV="$withval",
RANDOMV="/dev/random"
if test -r /dev/urandom
then
        RANDOMV="/dev/urandom"
fi
	)

AC_SUBST(RANDOMV)

AC_ARG_WITH(mailuser, [], mailuser="$withval",
        AC_MSG_ERROR(--with-mailuser missing))
AC_SUBST(mailuser)
AC_DEFINE_UNQUOTED(MAILUSER, ["$mailuser"], [ Mail system user ])

dnl Checks for libraries.

AC_ARG_WITH(db, [], db="$withval", db="")


AC_ARG_WITH(piddir, [  --with-piddir         Directory where imapd.pid is created ], piddir="$withval", piddir=/var/run)

AC_SUBST(piddir)

dnl Checks for header files.

AC_CHECK_HEADERS(locale.h unistd.h sys/stat.h sys/wait.h time.h sys/time.h sys/utsname.h utime.h strings.h)
AC_CHECK_HEADERS_ONCE([sys/time.h])

AC_HEADER_DIRENT
AC_HEADER_SYS_WAIT

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_SYS_LARGEFILE

AC_CACHE_CHECK([for socklen_t],
	sox_cv_hassocklen_t,

AC_COMPILE_IFELSE([
AC_LANG_SOURCE( [
#include <sys/types.h>
#include <sys/socket.h>

socklen_t sl_t;
],[
	accept(0, 0, &sl_t);
])],
	sox_cv_hassocklen_t=yes,
	sox_cv_hassocklen_t=no)
)

socklen_t="int"

if test $sox_cv_hassocklen_t = yes
then
	:
else
	AC_DEFINE_UNQUOTED(socklen_t, int, [ Default definition for socklen_t ])
fi

dnl Checks for library functions.

AC_CHECK_FUNCS(strerror utime utimes setlocale poll getaddrinfo)

AC_DEFINE_UNQUOTED(SOCKET_TIMEOUT,60,
	[ Read/write timeout ])

AC_ARG_WITH(db, [ ], db="$withval")

AC_DEFINE_UNQUOTED(IMAPDB, "courierimapuiddb",
			   [ Filename of the UID cache file ])
AC_DEFINE_UNQUOTED(IMAPDBVERSION, 1,
			  [ UID cache file format version ])

AC_DEFINE_UNQUOTED(SNAPSHOTDIR, "courierimapsnapshots",
				[ Directory where folder state is saved ])
AC_DEFINE_UNQUOTED(SNAPSHOTVERSION, 1,
				    [ snapshot file format version ])

AC_DEFINE_UNQUOTED(TRASH,"Trash", [ Name of the trash folder ])
AC_DEFINE_UNQUOTED(DRAFTS,"Drafts", [ Name of the drafts folder ])

# Unless you're the maintainer, clear DEBUGLIB just in case.

#case `hostname` in
#*.email-scan.com)
#	;;
#*)
	DEBUGLIB=""
#	;;
#esac
AC_SUBST(DEBUGLIB)

if test x$GXX = xyes
then
	CFLAGS="-Wall $CFLAGS"
fi

if test x$GXX = xyes
then
	CXXFLAGS="-Wall $CXXFLAGS"
fi

CFLAGS="-I.. -I$srcdir/.. $CFLAGS"
CXXFLAGS="-I.. -I$srcdir/.. $CXXFLAGS"

AC_ARG_ENABLE(workarounds-for-imap-client-bugs, [  --enable-workarounds-for-imap-client-bugs
              - compile fixes for various bugs in several IMAP clients ],
	IMAP_CLIENT_BUGS="$enableval",
	IMAP_CLIENT_BUGS="no")

case "$IMAP_CLIENT_BUGS" in
y*|Y*)
	MAKECHECKBROKEN=Y
	AC_DEFINE_UNQUOTED(IMAP_CLIENT_BUGS, 1,
	[ Whether to suppress untagged replies that confuse some clients ])
	;;
esac

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

LIBS="$saveLIBS $NETLIBS"
AC_SUBST(NETLIBS)

AC_ARG_WITH(dirsync, [ --with-dirsync           Manually sync queue file directory],
	dirsync="$withval", dirsync="N")

case "$dirsync" in
y*|Y*|1*)
	AC_DEFINE_UNQUOTED(EXPLICITDIRSYNC,1,
	[ Whether to sync the parent directory after delivering to a maildir ])
	;;
esac

AC_ARG_WITH(smap, [ --without-smap           Do not compile SMAP support],
		  smap="$withval", smap=yes)

case "$smap" in
y*|Y*)
	AC_DEFINE_UNQUOTED(SMAP, 1, [ Whether SMAP support is compiled in ])
	smap="yes"
	;;
esac
AC_SUBST(smap)

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
eval "prefix=$prefix"
eval "exec_prefix=$exec_prefix"
eval "libexecdir=$libexecdir"
eval "bindir=$bindir"
eval "sbindir=$sbindir"
eval "datarootdir=$datarootdir"
eval "datadir=$datadir"
eval "sysconfdir=$sysconfdir"
eval "localstatedir=$localstatedir"

AC_ARG_WITH(certsdir, [ --with-certsdir Directory where certs are created ],
certsdir="$withval", certsdir="$datadir")

AC_SUBST(certsdir)


mydatadir="$datadir"
AC_SUBST(mydatadir)   # Avoid useless autoconf warning

#
# Check for PAM configuration flavor

rm -f imapd.pam pop3d.pam

cp -f $srcdir/imapd.authpam imapd.pam
cp -f $srcdir/pop3d.authpam pop3d.pam

if test -f /etc/pam.d/system-auth
then
	if ls /lib*/security/pam_stack.so 2>/dev/null >/dev/null
	then
		cp -f $srcdir/system-auth.authpam imapd.pam
		cp -f $srcdir/system-auth.authpam pop3d.pam
	else
		cp -f $srcdir/system-auth2.authpam imapd.pam
		cp -f $srcdir/system-auth2.authpam pop3d.pam
	fi
fi

if test -f /etc/pam.d/common-auth
then
	cp $srcdir/common.authpam imapd.pam
	cp $srcdir/common.authpam pop3d.pam
fi

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

# This gets built by the make rule, and is not in the git repo

for f in imapd.dist.in imapd-ssl.dist.in pop3d.dist.in pop3d-ssl.dist.in
do
    if test ! -f "$srcdir/$f"
    then
	cp -p "$srcdir/$f.git" "$srcdir/$f"
	touch "$srcdir/$f.git"
    fi
done

AC_SUBST(target_cpu)
AC_SUBST(target_vendor)
AC_SUBST(target_os)

AC_ARG_WITH(package, [], package="$withval", [package='courier-imap'])
AC_ARG_WITH(version, [], version="$withval", [version=$VERSION])

case "$package" in
courier)
	package="Courier $version (Courier-IMAP $VERSION)"
	;;
*)
	package="Courier-IMAP $version"
	;;
esac

SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-`date +%s`}"
date=`date -u -d "@$SOURCE_DATE_EPOCH" "+%F %T" 2>/dev/null || date -u -r "$SOURCE_DATE_EPOCH" "+%F %T" 2>/dev/null || date -u "+%F %T"`
AC_DEFINE_UNQUOTED(PROGRAMVERSION, "$package/${target_cpu}-${target_vendor}-${target_os}/$date",
		       [ Source code version ])

AC_ARG_WITH(trashquota, [ --with-trashquota       Count deleted messages as part of the quota],
	trashquota="$withval",
	trashquota="no")

if test "$trashquota" = "yes"
then
	MAKECHECKBROKEN=Y
fi

AC_SUBST(MAKECHECKBROKEN)

. ../tcpd/couriertls.config
if test "$ssllib" = ""
then
	ssllib="gnutls"
fi
AC_SUBST(ssllib)

AC_COMPILE_IFELSE([AC_LANG_SOURCE([

#include <stdio.h>

int main()
{
   setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
}
])],
	AC_DEFINE_UNQUOTED(HAVE_SETVBUF_IOLBF,1,[Whether setvbuf(..._IOLBF) works])
)

. ../rootcerts/rootcertsdir.cnf
AC_SUBST(cacerts)

AC_CONFIG_FILES([Makefile imapd.dist imapd-ssl.dist pop3d.dist pop3d-ssl.dist
	testsuitefix.pl mkimapdcert mkpop3dcert mkdhparams
	imapd.cnf.gnutls pop3d.cnf.gnutls
	imapd.cnf.openssl pop3d.cnf.openssl])
AC_OUTPUT
