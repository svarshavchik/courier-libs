dnl Sets the COURIER_UNICODE_CXXFLAGS variable to any additional compiler
dnl flags needed to build the courier-unicode package and packages that
dnl use the courier-unicode package.

AC_DEFUN([AX_COURIER_UNICODE_CXXFLAGS],[

save_FLAGS="$CXXFLAGS"

AC_LANG_PUSH([C++])

AC_TRY_COMPILE([
#include <string>
], [
     std::u32string s;
     char32_t c;
     ],
     [
     ],
     [

COURIER_UNICODE_CXXFLAGS="-std=c++11"
CXXFLAGS="$save_CFLAGS $COURIER_UNICODE_CXXFLAGS"

AC_TRY_COMPILE([
#include <string>
], [
     std::u32string s;
     char32_t c;
     ],
     [
     ],
     [

COURIER_UNICODE_CXXFLAGS="-std=c++0x"
CXXFLAGS="$save_CFLAGS $COURIER_UNICODE_CXXFLAGS"

AC_TRY_COMPILE([
#include <string>
], [
     std::u32string s;
     char32_t c;
     ],
     [
     ],
     [
AC_MSG_ERROR([*** A compiler with C++11 Unicode support was not found])
])
])
])
CXXFLAGS="$save_FLAGS"
AC_LANG_POP([C++])
])

AC_DEFUN([AX_COURIER_UNICODE_VERSION],[

AC_MSG_CHECKING(courier-unicode library and version)

v="$1"

if test "$v" = ""
then
	v=2.2
fi

set -- `echo "$v" | tr '.' ' '`

v=$[]1
r=$[]2
p=$[]3

if test "$p" = ""
   then p="0"
fi

AC_TRY_COMPILE([
#include <courier-unicode.h>
#ifndef COURIER_UNICODE_VERSION
#define COURIER_UNICODE_VERSION 0
#endif

#if COURIER_UNICODE_VERSION < ]$v$r$p[
#error "courier-unicode ]$1[ library is required"
#endif

],[],[],
AC_MSG_ERROR([
ERROR: The Courier Unicode Library ]$1[ header files appear not to be installed.
You may need to upgrade the library or install a separate development
subpackage in addition to the main package.])
)

AC_MSG_RESULT([ok])
])
