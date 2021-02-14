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
