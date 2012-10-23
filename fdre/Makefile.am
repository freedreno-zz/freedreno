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

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = asm . tests

libfreedreno_la_LTLIBRARIES  = libfreedreno.la
libfreedreno_ladir           = $(libdir)
libfreedreno_la_LDFLAGS      = -no-undefined
libfreedreno_la_LIBADD       = asm/libasm.la $(DRM_LIBS)
libfreedreno_la_CFLAGS       = \
	-O0 -g \
	$(WARN_CFLAGS) \
	$(DRM_CFLAGS) \
	-I$(top_srcdir)/../includes \
	-I$(top_srcdir)/asm \
	-I$(top_srcdir)

libfreedreno_la_SOURCES      = \
	bmp.c \
	program.c \
	ws-fbdev.c \
	freedreno.c

if ENABLE_X11
libfreedreno_la_SOURCES += ws-dri2.c
libfreedreno_la_CFLAGS += $(X11_CFLAGS)
libfreedreno_la_LIBADD += $(X11_LIBS)
endif
