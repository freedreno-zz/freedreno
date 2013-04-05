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

AM_CFLAGS = \
	-O0 -g \
	$(WARN_CFLAGS) \
	$(DRM_CFLAGS) \
	-I$(top_srcdir)/../util \
	-I$(top_srcdir)/../includes \
	-I$(top_srcdir)

AM_LDFLAGS = \
	-static-libtool-libs

LDADD = \
	-lm \
	$(top_builddir)/libfreedreno.la

TESTS = \
	regdump \
	cube-textured \
	cube \
	lolscat \
	stencil \
	fan-smoothed \
	strip-smoothed \
	triangle-smoothed \
	triangle-quad \
	quad-flat

noinst_PROGRAMS = $(TESTS)

quad_flat_SOURCES         = quad-flat.c
triangle_quad_SOURCES     = triangle-quad.c
triangle_smoothed_SOURCES = triangle-smoothed.c
strip_smoothed_SOURCES    = strip-smoothed.c
fan_smoothed_SOURCES      = fan-smoothed.c
stencil_SOURCES           = stencil.c
lolscat_SOURCES           = cat.c esTransform.c cat-model.c lolstex1.c lolstex2.c
cube_SOURCES              = cube.c esTransform.c
cube_textured_SOURCES     = cube-textured.c esTransform.c cubetex.c
regdump_SOURCES           = regdump.c

