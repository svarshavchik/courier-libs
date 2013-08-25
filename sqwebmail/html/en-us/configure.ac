dnl

AC_INIT(sqwebmail, 0.11, [courier-sqwebmail@lists.sourceforge.net])

>confdefs.h  # Kill PACKAGE_ macros

AC_CONFIG_SRCDIR(empty.html)

AC_CONFIG_AUX_DIR(../../../..)
AM_INIT_AUTOMAKE([foreign no-define])

changequote(:,:)
lang="`cat ${srcdir}/LANGUAGE | sed 's/[^a-zA-z]/_/g'`"
changequote([,])

LPATH="$PATH:/usr/local/bin"
AC_PATH_PROG(SED, sed, sed, $LPATH)
if test "$SED" = "sed"
then
	AC_MSG_ERROR(sed not found.)
fi

lang=`cat ${srcdir}/LANGUAGE`
AC_SUBST(lang)

test "x$prefix" = xNONE && prefix=$ac_default_prefix
eval "prefix=$prefix"
eval "datadir=$datadir"
htmldir="$datadir/sqwebmail/html"
AC_SUBST(htmldir)


AC_SUBST(htmldir)
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_OUTPUT(Makefile)
