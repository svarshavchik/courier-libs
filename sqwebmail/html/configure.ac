dnl
dnl Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
dnl distribution information.

AC_INIT(sqwebmail, 0.23, [courier-sqwebmail@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(mksoftlinks)
AC_CONFIG_AUX_DIR(../../..)
AM_INIT_AUTOMAKE([foreign no-define])

AC_PROG_INSTALL
AC_PROG_MAKE_SET

LANGUAGES="`cat ${srcdir}/*/LANGUAGE_PREF | sort | awk ' { print $2 } ' | uniq`"
LANGUAGES=`echo $LANGUAGES`

test "x$prefix" = xNONE && prefix=$ac_default_prefix
eval "prefix=$prefix"
eval "datadir=$datadir"
htmldir="$datadir/sqwebmail/html"

AC_SUBST(htmldir)
AC_CONFIG_SUBDIRS($LANGUAGES)
AC_SUBST(LANGUAGES)
AC_OUTPUT(Makefile README_LANG.html)

# Set up soft links

for f in $LANGUAGES
do
	${srcdir}/mksoftlinks . $f
done
