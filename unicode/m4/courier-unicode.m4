dnl Sets the COURIER_UNICODE_CXXFLAGS variable to any additional compiler
dnl flags needed to build the courier-unicode package and packages that
dnl use the courier-unicode package.

AC_DEFUN([AX_COURIER_UNICODE_CXXFLAGS],[

AC_REQUIRE([AC_PROG_CXX])

save_FLAGS="$CXXFLAGS"

AC_LANG_PUSH([C++])


AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <string>
#include <string_view>

void func(std::u32string_view, char32_t);

]], [[
     std::u32string s;
     char32_t c=0;

     func(s, c);
     ]])],
     [
     ],
     [

COURIER_UNICODE_CXXFLAGS="-std=c++17"
CXXFLAGS="$save_CFLAGS $COURIER_UNICODE_CXXFLAGS"

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <string>
#include <string_view>

void func(std::u32string_view, char32_t);

]], [[
     std::u32string s;
     char32_t c=0;

     func(s, c);
     ]])],
     [
     ],
     [
AC_MSG_ERROR([*** A compiler with C++17 Unicode support was not found])
])
])
CXXFLAGS="$save_FLAGS"

AC_LANG_POP([C++])
])
