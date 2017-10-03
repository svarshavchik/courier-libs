dnl
dnl Copyright 1998 - 2017 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(maildrop, 2.9.2, [courier-users@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(alarm.C)
AC_CONFIG_AUX_DIR(../..)
LPATH="$PATH:/usr/local/bin"
AM_INIT_AUTOMAKE([foreign no-define])

AC_PROG_MAKE_SET
AC_CONFIG_HEADERS(config.h)

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_CXX
AM_PROG_LIBTOOL

AC_PATH_PROGS(PERL, perl5 perl, perl, $LPATH)

AC_ARG_ENABLE(DEBUG,[  --enable-DEBUG               leave debug info in executables],STRIP=true)

AC_CACHE_CHECK(for -fhandle-exceptions flag to g++,maildrop_cv_handle_exceptions,

maildrop_cv_handle_exceptions=no

if test "$GXX" = yes ; then

AC_LANG_CPLUSPLUS

AC_TRY_COMPILE( [
],[
throw;
],,maildrop_cv_handle_exceptions=yes
)

AC_LANG_C
fi

)

if test "$maildrop_cv_handle_exceptions" = yes ; then
   CXXFLAGS="$CXXFLAGS -fhandle-exceptions"
   CXXFLAGS=`echo "$CXXFLAGS" | sed 's/-O2//'`
fi

AC_CACHE_CHECK([ability to handle both const and non-const exceptions],
		maildrop_cv_const_exceptions,

AC_LANG_CPLUSPLUS

AC_TRY_COMPILE([

extern void foo();
extern void bar(const char *);

],[

       try
       {
             foo();
       }
       catch (const char *p)
       {
             bar(p);
       }
       catch (char *p)
       {
             bar(p);
       }
       catch (...)
       {
             bar(0);
       }

]
, maildrop_cv_const_exceptions="yes",maildrop_cv_const_exceptions="no")
)

if test "$maildrop_cv_const_exceptions" = "yes"
then
	AC_DEFINE_UNQUOTED(NEED_NONCONST_EXCEPTIONS,1,
	[ Define this if we need both catch(const foo) and catch(foo) ])
fi

AC_LANG_C

dnl Checks for libraries.

AC_CHECK_LIB(sun,getpwnam)

AC_ARG_WITH(package, [], package="$withval",
	[
		package="$PACKAGE"
		ac_configure_args="$ac_configure_args --with-package=$PACKAGE"
	])

AC_ARG_WITH(version, [], version="$withval",
	[
		version="$VERSION"
		ac_configure_args="$ac_configure_args --with-version=$VERSION"
	])

AC_PATH_PROGS(COURIERAUTHCONFIG, courierauthconfig)

# When we're a part of the Courier build, we let Courier do the authlib stuff

if test -d ${srcdir}/../../courier
then
	COURIERAUTHCONFIG=""
fi

AC_ARG_ENABLE(authlib, [], [
if test "$enableval" = "no"
then
	COURIERAUTHCONFIG=""
fi
])

AUTHINCLUDE=""
AUTHLDADD=""
AUTHLIB=0

if test "$COURIERAUTHCONFIG" != ""
then
	AUTHINCLUDE=`$COURIERAUTHCONFIG --cppflags`
	AUTHLDADD="`$COURIERAUTHCONFIG --ldflags` -lcourierauth"
	AUTHLIB=1
fi

AC_SUBST(AUTHINCLUDE)
AC_SUBST(AUTHLDADD)
AC_DEFINE_UNQUOTED(AUTHLIB, $AUTHLIB, [ Whether the Courier authentication library was detected ])

dnl Determine whether to use GDBM, or libdb.

. ../../dbobj.config

if test "$dblibrary" != ""
then
	dblibrary="../../$dblibrary"
fi
AC_SUBST(dblibrary)

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_HEADER_DIRENT
AC_CHECK_HEADERS(fcntl.h memory.h sys/file.h sys/time.h sys/stat.h unistd.h strings.h locale.h pcre.h pcre/pcre.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_PID_T
AC_TYPE_UID_T
AC_STRUCT_TM
AC_TYPE_GETGROUPS
AC_SYS_LARGEFILE

AC_TRY_RUN( [

#include        <stdio.h>
#include        <stdlib.h>
#include        <limits.h>

int main()
{
FILE    *fp=fopen("conftestval", "w");

        if (!fp)        exit(1);
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
                AC_MSG_WARN([Cross compiling, setting MAXLONGSIZE to $MAXLONGSIZ
E])
        ]
                )

AC_DEFINE_UNQUOTED(MAXLONGSIZE, $MAXLONGSIZE,
				[ Maximum character size of a long ])

dnl Checks for library functions.

AC_FUNC_CLOSEDIR_VOID
AC_TYPE_SIGNAL
AC_CHECK_FUNCS(setgroups setlocale)

AC_CHECK_FUNC(getpgrp, HAS_GETPGRP=1, HAS_GETPGRP=0)
AC_DEFINE_UNQUOTED(HAS_GETPGRP,$HAS_GETPGRP,
	[ Whether getpgrp() is available ])

AC_CHECK_FUNC(getpgid, HAS_GETPGID=1, HAS_GETPGID=0)
AC_DEFINE_UNQUOTED(HAS_GETPGID,$HAS_GETPGID,
	[ Whether getpgid() is available ])

if test $HAS_GETPGRP
then
	AC_FUNC_GETPGRP
fi

AC_CACHE_CHECK(setpgrp(),maildrop_cv_HAS_SETPGRP,

if test $HAS_GETPGRP -o $HAS_GETPGID
then
    AC_TRY_LINK([
#include	<stdio.h>
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
],[
	(void)setpgrp();
],maildrop_cv_HAS_SETPGRP=1,maildrop_cv_HAS_SETPGRP=0)

else
	maildrop_cv_HAS_SETPGRP=0
fi

)
AC_DEFINE_UNQUOTED(HAS_SETPGRP,$maildrop_cv_HAS_SETPGRP,
	[ Whether setpgrp() is available ])

AC_CACHE_CHECK(setpgid(),maildrop_cv_HAS_SETPGID,

if test $HAS_GETPGRP -o $HAS_GETPGID
then

    AC_TRY_LINK([
#include	<stdio.h>
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif
],[
	(void)setpgid(0,0);
],maildrop_cv_HAS_SETPGID=1,maildrop_cv_HAS_SETPGID=0)
else
	maildrop_cv_HAS_SETPGID=0
fi
)
AC_DEFINE_UNQUOTED(HAS_SETPGID,$maildrop_cv_HAS_SETPGID,
	[ Whether setpgid() is available ])

AC_CACHE_CHECK([for missing gethostname prototype],maildir_cv_SYS_GETHOSTNAME,

AC_TRY_COMPILE([
#if HAVE_UNISTD_H
#include	<unistd.h>
#endif

extern "C" int gethostname(int,int);
],[
],maildir_cv_SYS_GETHOSTNAME=yes,maildir_cv_SYS_GETHOSTNAME=no
)

)

if test $maildir_cv_SYS_GETHOSTNAME = "no"
then
	AC_DEFINE_UNQUOTED(HAS_GETHOSTNAME,1,
	[ Whether gethostname() needs to be prototyped ])
fi

dnl Post processing

AC_CACHE_CHECK(default PATH for commands,maildrop_cv_SYS_DEFAULT_PATH,
maildrop_cv_SYS_DEFAULT_PATH="/bin:/usr/bin"
if test -d /usr/local/bin
then
	maildrop_cv_SYS_DEFAULT_PATH="$maildrop_cv_SYS_DEFAULT_PATH":/usr/local/bin
fi
)

AC_DEFINE_UNQUOTED(DEFAULT_PATH,"$maildrop_cv_SYS_DEFAULT_PATH",
	[ Default value of the PATH variable ])

if test -x /var/qmail/bin/qmail-inject
then
	QMAIL="/var/qmail/bin/qmail-inject"
else
	AC_CHECK_PROG(QMAIL,qmail-inject,qmail-inject,)
fi

dnl Try to find sendmail.

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'

if test -d $srcdir/../../courier
then
#
# This version of maildrop is integrated into Courier mail server

	AC_DEFINE_UNQUOTED(HAVE_COURIER,1,
	[ Whether this version of maildrop is part of Courier ])
fi

AC_ARG_ENABLE(sendmail,[  --enable-sendmail=prog       set sendmail location],
	[ MTA="$enableval -oi"],
	[ MTA=""
#
# When maildrop is configured as part of courier, MTA is fixed to use Courier.
#
	if test -d $srcdir/../../courier
	then
		eval "prefix=$prefix"
		eval "exec_prefix=$exec_prefix"
		eval "bindir=$bindir"
		MTA=$bindir/sendmail
	fi

	])

test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'


eval "prefix=$prefix"
eval "exec_prefix=$exec_prefix"
eval "sysconfdir=$sysconfdir"

AC_CACHE_CHECK(mail transport agent,maildrop_cv_SYS_MTA,

if test "$MTA" != ""
then
	maildrop_cv_SYS_MTA="$MTA"
else
	maildrop_cv_SYS_MTA="/bin/sendmail -oi"

	for f in /var/qmail/bin/sendmail /usr/bin/sendmail /usr/sbin/sendmail /lib/sendmail /usr/lib/sendmail /etc/sendmail /sbin/sendmail
	do
		if test -x $f
		then
			maildrop_cv_SYS_MTA="$f -oi"
			break
		fi
	done
fi

)

if test "$MTA" != ""
then
	maildrop_cv_SYS_MTA="$MTA"
fi

AC_DEFINE_UNQUOTED(SENDMAIL_DEF,"$maildrop_cv_SYS_MTA",
	[ Default value for the SENDMAIL variable ])

dnl###########################################################################
dnl#
dnl# Try to locate any mail spool directory suffix, whether it is just
dnl# $SPOOL/userid, or $SPOOL/u/userid, or $SPOOL/u/us/userid, or
dnl# $SPOOL/u/us/use/userid
dnl#
dnl###########################################################################

test_spooldir() {
spooldir="$1"
WHOAMI=`whoami`

if test "$WHOAMI" = ""
then
	AC_MSG_ERROR(Unable to run whoami)
	exit 1
fi

a=`echo $WHOAMI"..." | cut -c1`
b=`echo $WHOAMI"..." | cut -c1-2`
c=`echo $WHOAMI"..." | cut -c1-3`

DIRA=$spooldir/$a
DIRB=$spooldir/$a/$b
DIRC=$spooldir/$a/$b/$c

DIR=""
TDIR=""

if test -d $spooldir
then
	DIR=$spooldir
	TDIR="$spooldir"
fi
if test -d $DIRA
then
	DIR=$spooldir'/='
	TDIR="$DIRA"
fi

if test -d $DIRB
then
	DIR=$spooldir'/=/=='
	TDIR="$DIRB"
fi

if test -d $DIRC
then
	DIR=$spooldir'/=/==/==='
	TDIR="$DIRC"
fi

if test "`ls -ld $TDIR/. | cut -c10`" = "t"
then
	MBOX_RESET_GID=1
else
	MBOX_RESET_GID=0
fi

SPOOLDIR="$DIR"
}

dnl#########################################################################
dnl#
dnl# Check for possible $SPOOL locations
dnl#
dnl#########################################################################

get_spooldir() {

if test -d $srcdir/../../courier
then
	MBOX_RESET_GID=1
	SPOOLDIR="./Maildir"
	return 0
fi

for f in /var/mail /usr/spool/mail /var/spool/mail /usr/mail
do
	test_spooldir "$f"
	if test "$SPOOLDIR" != ""
	then
		return 0
	fi
done

# Not found, possibly qmail $HOME/Mailbox

if test "$QMAIL" != ""
then
	SPOOLDIR="./Mailbox"
else
	AC_MSG_ERROR(Cannot determine default mailbox)
fi
}

check_spooldir() {
  if test "$CHECKED_SPOOLDIR" != 1
  then
     get_spooldir
     MBOX_DIR="$SPOOLDIR"
     CHECKED_SPOOLDIR=1
  fi
}

AC_CACHE_CHECK(location of system mailboxes,maildrop_cv_SYS_INSTALL_MBOXDIR,
check_spooldir
maildrop_cv_SYS_INSTALL_MBOXDIR="$MBOX_DIR"
)

AC_CACHE_CHECK(whether maildrop should reset its group ID,maildrop_cv_SYS_INSTALL_RESET_GID,
check_spooldir
maildrop_cv_SYS_INSTALL_RESET_GID="$MBOX_RESET_GID"
)
AC_ARG_WITH(default-maildrop, [  --with-default-maildrop=file   specify DEFAULT mailbox location],
	maildrop_cv_SYS_INSTALL_MBOXDIR="$withval",

# Courier defaults to ./Maildir

	if test -d $srcdir/../../courier
	then
		maildrop_cv_SYS_INSTALL_MBOXDIR=./Maildir
	fi
	)

AC_DEFINE_UNQUOTED(DEFAULT_DEF,"${maildrop_cv_SYS_INSTALL_MBOXDIR}",
	[ Default mail delivery instruction ])
AC_DEFINE_UNQUOTED(RESET_GID,${maildrop_cv_SYS_INSTALL_RESET_GID},
	[ Whether maildrop should reset its gid ])


AC_ARG_ENABLE(trusted-users, [  --enable-trusted-users='...' specify users allowed to use the -d option],
    [TRUSTED_USERS=$enableval], [TRUSTED_USERS=""])

AC_CACHE_CHECK(privileged users,maildrop_cv_SYS_INSTALL_PRIVUSERS,

if test "$TRUSTED_USERS" = ""
then
	maildrop_cv_SYS_INSTALL_PRIVUSERS="root"
	for USERID in mail daemon postmaster qmaild mmdf
	do
		if test "`eval echo ~$USERID`" != ~$USERID
		then
			maildrop_cv_SYS_INSTALL_PRIVUSERS="$maildrop_cv_SYS_INSTALL_PRIVUSERS $USERID"
		fi
	done
else
	maildrop_cv_SYS_INSTALL_PRIVUSERS="$TRUSTED_USERS"
fi

)
AC_DEFINE_UNQUOTED(TRUSTED_USERS,"${maildrop_cv_SYS_INSTALL_PRIVUSERS}",
	[ List of usernames who are allowed to use the -d option ])

AC_ARG_ENABLE(trusted-groups, [  --enable-trusted-groups='...' specify groups allowed to use the -d option],
    [TRUSTED_GROUPS=$enableval], [TRUSTED_GROUPS=""])
AC_DEFINE_UNQUOTED(TRUSTED_GROUPS,"$TRUSTED_GROUPS",
	[ List of groupnames who are allowed to use the -d option ])

AC_MSG_CHECKING(for UID of maildrop)
AC_ARG_ENABLE(maildrop-uid, [  --enable-maildrop-uid=root   UID of maildrop],
    [MAILDROPUID=$enableval], [MAILDROPUID=root])
AC_DEFINE_UNQUOTED(MAILDROPUID, "$MAILDROPUID",
				[ The installation uid ])
AC_MSG_RESULT($MAILDROPUID)
AC_SUBST(MAILDROPUID)

AC_MSG_CHECKING(for GID of maildrop)
AC_ARG_ENABLE(maildrop-gid, [  --enable-maildrop-gid=mail   GID of maildrop],
    [MAILDROPGID=$enableval], [MAILDROPGID=mail])
AC_DEFINE_UNQUOTED(MAILDROPGID, "$MAILDROPGID",
				[ The installation gid ])
AC_MSG_RESULT($MAILDROPGID)
AC_SUBST(MAILDROPGID)

echo "uid=$MAILDROPUID" >uidgid
echo "gid=$MAILDROPGID" >>uidgid

dnl     *** options in config.h ***

AC_MSG_CHECKING(for USE_FLOCK)
AC_ARG_ENABLE(use-flock, [  --enable-use-flock=(1|0)     enable flock()/lockf() locking],
    [USE_FLOCK=$enableval], [USE_FLOCK=1])
AC_MSG_RESULT($USE_FLOCK)
AC_SUBST(USE_FLOCK)

AC_MSG_CHECKING(for USE_DOTLOCK)
AC_ARG_ENABLE(use-dotlock, [  --enable-use-dotlock=(1|0)   enable dotlock locking],
    [USE_DOTLOCK=$enableval], [

#
# If we are using FLOCK, and mailbox directory has no sticky bit, don't
# use dotlocks.
#

USE_DOTLOCK=1

if test $USE_FLOCK != 0
then
	if test $maildrop_cv_SYS_INSTALL_RESET_GID = 0
	then
		USE_DOTLOCK=0
	fi
fi

])
AC_MSG_RESULT($USE_DOTLOCK)
AC_SUBST(USE_DOTLOCK)

AC_MSG_CHECKING(for LOCKEXT_DEF)
AC_ARG_ENABLE(lockext-def, [  --enable-lockext-def=.lock   set extension for dotlock files],
    [LOCKEXT_DEF=$enableval], [LOCKEXT_DEF=".lock"])
AC_MSG_RESULT($LOCKEXT_DEF)
AC_SUBST(LOCKEXT_DEF)

AC_MSG_CHECKING(for LOCKSLEEP_DEF)
AC_ARG_ENABLE(locksleep-def, [  --enable-locksleep-def=5     set sleep interval dotlock files (seconds)],
    [LOCKSLEEP_DEF=$enableval], [LOCKSLEEP_DEF="5"])
AC_MSG_RESULT($LOCKSLEEP_DEF)
AC_SUBST(LOCKSLEEP_DEF)

AC_MSG_CHECKING(for LOCKTIMEOUT_DEF)
AC_ARG_ENABLE(locktimeout-def, [  --enable-locktimeout-def=60  set timeout for dotlock files (seconds)],
    [LOCKTIMEOUT_DEF=$enableval], [LOCKTIMEOUT_DEF="60"])
AC_MSG_RESULT($LOCKTIMEOUT_DEF)
AC_SUBST(LOCKTIMEOUT_DEF)

AC_MSG_CHECKING(for LOCKREFRESH_DEF)
AC_ARG_ENABLE(lockrefresh-def, [  --enable-lockrefresh-def=15  set timeout for dotlock files (seconds)],
    [LOCKREFRESH_DEF=$enableval], [LOCKREFRESH_DEF="15"])
AC_MSG_RESULT($LOCKREFRESH_DEF)
AC_SUBST(LOCKREFRESH_DEF)

AC_MSG_CHECKING(for TEMPDIR)
AC_ARG_ENABLE(tempdir,
[  --enable-tempdir=.tmp        directory for temp files (in user's home dir)
  --disable-tempdir            disable temp files in home directory (use
                               the tmpfile function)],
    [TEMPDIR=$enableval], [TEMPDIR="no"])

if test "$TEMPDIR" = "no"
then
	TEMPDIR='use tmpfile()'
	AC_DEFINE_UNQUOTED(SHARED_TEMPDIR,1,
	[ Use tmpfile() ])
fi
AC_MSG_RESULT($TEMPDIR)
AC_SUBST(TEMPDIR)

AC_MSG_CHECKING(for SMALLMSG)
AC_ARG_ENABLE(smallmsg, [  --enable-smallmsg=8192       size of buffer for small messages],
    [SMALLMSG=$enableval], [SMALLMSG=8192])
AC_MSG_RESULT($SMALLMSG)
AC_SUBST(SMALLMSG)

AC_MSG_CHECKING(for GLOBAL_TIMEOUT)
AC_ARG_ENABLE(global-timeout, [  --enable-global-timeout=300  global watchdog timer (seconds)],
    [GLOBAL_TIMEOUT=$enableval], [GLOBAL_TIMEOUT=300])
AC_MSG_RESULT($GLOBAL_TIMEOUT)
AC_SUBST(GLOBAL_TIMEOUT)

AC_MSG_CHECKING(for CRLF_TERM)
AC_ARG_ENABLE(crlf-term, [  --enable-crlf-term=0         enable <CR><LF> line termination],
    [CRLF_TERM=$enableval], [CRLF_TERM=0])
case "$CRLF_TERM" in
1*|y*|Y*)
	CRLF_TERM=1
	;;
*)
	CRLF_TERM=0
	;;
esac
AC_MSG_RESULT($CRLF_TERM)
AC_SUBST(CRLF_TERM)

AC_MSG_CHECKING(for RESTRICT_TRUSTED)
AC_ARG_ENABLE(restrict-trusted, [  --enable-restrict-trusted=1  restrict -d for trusted users only],
    [RESTRICT_TRUSTED=$enableval], [RESTRICT_TRUSTED=1])
case "$RESTRICT_TRUSTED" in
1*|y*|Y*)
	RESTRICT_TRUSTED=1
	;;
*)
	RESTRICT_TRUSTED=0
	;;
esac
AC_MSG_RESULT($RESTRICT_TRUSTED)
AC_SUBST(RESTRICT_TRUSTED)

AC_MSG_CHECKING(for KEEP_FROMLINE)
AC_ARG_ENABLE(keep-fromline, [  --enable-keep-fromline=(1|0) preserve the original From_ line in messages],
    [KEEP_FROMLINE=$enableval], [ KEEP_FROMLINE=$RESTRICT_TRUSTED ])
case "$KEEP_FROMLINE" in
1*|y*|Y*)
	KEEP_FROMLINE=1
	;;
*)
	KEEP_FROMLINE=0
	;;
esac
AC_MSG_RESULT($KEEP_FROMLINE)
AC_SUBST(KEEP_FROMLINE)

AC_MSG_CHECKING(for syslog support)
AC_ARG_ENABLE(syslog,    [  --enable-syslog=1            log fatal errors to syslog],
	[ SYSLOG_LOGGING="$enableval"], [ SYSLOG_LOGGING=0 ])
AC_MSG_RESULT($SYSLOG_LOGGING)
AC_DEFINE_UNQUOTED(SYSLOG_LOGGING,$SYSLOG_LOGGING,
	[ Whether to log fatal errors to syslog ])

AC_ARG_WITH(dirsync, [ --with-dirsync           Manually sync queue file directory],
	dirsync="$withval", dirsync="N")

case "$dirsync" in
y*|Y*|1*)
	AC_DEFINE_UNQUOTED(EXPLICITDIRSYNC,1,
	[ Whether to sync the parent directory after delivering to a maildir ])
	;;
esac



AC_SUBST(VERSION)

CFLAGS="$CFLAGS -I$srcdir/../rfc822 -I$srcdir/../rfc2045 -I.. -I$srcdir/.. -I ../.. -I$srcdir/../.."
CXXFLAGS="$CXXFLAGS -I$srcdir/../rfc822 -I$srcdir/../rfc2045 -I.. -I$srcdir/.. -I../.. -I$srcdir/../.."

AC_ARG_WITH(etcdir,    [  --with-etcdir=dir            Instead of /etc, use this. ],
	[ withetcdir=$withval],
	[ withetcdir="/etc"

	if test -d $srcdir/../../courier
	then
		eval "sysconfdir=$sysconfdir"
		withetcdir=$sysconfdir
	fi

	])

AC_SUBST(withetcdir)
WITHETCDIR="$withetcdir"
AC_SUBST(WITHETCDIR)

AC_DEFINE_UNQUOTED(ETCDIR,"$withetcdir",
	[ Configuration directory location ])

if test "$GXX" = yes ; then
	CXXFLAGS="$CXXFLAGS -Wall"
	CFLAGS="$CFLAGS -Wall"
fi

AC_MSG_CHECKING(whether to treat authlib errors as tempreject)
AC_ARG_ENABLE(authlib-tempreject,
	[  --enable-authlib-tempreject=(1|0) authlib errors are temprejects ],
	[ AUTHLIB_TEMPREJECT="$enableval"], [ AUTHLIB_TEMPREJECT=1 ])
AC_MSG_RESULT($AUTHLIB_TEMPREJECT)
AC_ARG_ENABLE(dovecotauth,[],[
	if test "$enableval" = "yes"
	then
		AC_DEFINE_UNQUOTED(DOVECOTAUTH,1,[ Dovecot authentication enabled ])
	fi
])
AC_DEFINE_UNQUOTED(AUTHLIB_TEMPREJECT,$AUTHLIB_TEMPREJECT,
	[ Whether authlib errors cause temporary rejects ])

AC_ARG_WITH(db, [], db="$withval", db="")

USE_GDBM=0
USE_DB=0;

case "$db" in
     db)
     USE_DB=1
     ;;
     gdbm)
     USE_GDBM=1
     ;;
esac

AC_SUBST(USE_GDBM)
AC_SUBST(USE_DB)

AM_CONDITIONAL(HAVE_SGML, test -d ${srcdir}/../docbook)

. ../rfc822/rfc822.config

AM_CONDITIONAL(HAVE_LIBIDN, test "$libidn" = "yes")

AC_OUTPUT(xconfig.h Makefile README.html testsuite)
