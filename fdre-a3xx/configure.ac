#
#  Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  the rights to use, copy, modify, merge, publish, distribute, sublicense,
#  and/or sell copies of the Software, and to permit persons to whom the
#  Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
#  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.
#

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libfreedreno], [0.0.1], [https://gitorious.org/freedreno/], [libfreedreno])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Enable quiet compiles on automake 1.11.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Initialize lex/yacc:
AM_PROG_LEX
AC_PROG_YACC

# Initialize libtool
AC_PROG_LIBTOOL

# Obtain compiler/linker options for depedencies
PKG_CHECK_MODULES(DRM, libdrm libdrm_freedreno)

# Check for X11/libdri2
PKG_CHECK_MODULES(X11, x11 dri2, [HAVE_X11=yes], [HAVE_X11=no])
if test "x$HAVE_X11" = "xyes"; then
	AC_DEFINE(HAVE_X11, 1, [Have X11 support])
else
	AC_MSG_WARN([Building without X11 support])
fi
AM_CONDITIONAL(ENABLE_X11, [test "x$HAVE_X11" = xyes])

dnl ===========================================================================
dnl check compiler flags
AC_DEFUN([LIBDRM_CC_TRY_FLAG], [
	AC_MSG_CHECKING([whether $CC supports $1])

	libdrm_save_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS $1"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[]])],
						[libdrm_cc_flag=yes],
						[libdrm_cc_flag=no])
	CFLAGS="$libdrm_save_CFLAGS"
	
	if test "x$libdrm_cc_flag" = "xyes"; then
		ifelse([$2], , :, [$2])
	else
		ifelse([$3], , :, [$3])
	fi
	AC_MSG_RESULT([$libdrm_cc_flag])
])

MAYBE_WARN="-Wall -Wextra -Wno-packed-bitfield-compat \
-Wsign-compare -Werror-implicit-function-declaration \
-Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
-Wnested-externs \
-Wpacked -Wswitch -Wmissing-format-attribute \
-Wstrict-aliasing=2 -Winit-self -Wunsafe-loop-optimizations \
-Wdeclaration-after-statement -Wold-style-definition \
-Wno-missing-field-initializers -Wno-unused-parameter \
-Wno-attributes -Wno-long-long -Winline"

AC_MSG_CHECKING([which warning flags were supported])
for W in $MAYBE_WARN; do
	LIBDRM_CC_TRY_FLAG([$W], [WARN_CFLAGS="$WARN_CFLAGS $W"])
done

AC_SUBST(WARN_CFLAGS)

AC_CONFIG_FILES([
	Makefile
	asm/Makefile
	tests/Makefile])
AC_OUTPUT
