# Note: this VPATH bit is just a hack to avoid mucking with the build
# system after splitting the directory tree into various subdirs..
# really need to clean this up but I've got better things to work on
# right now:
VPATH = tests-2d:tests-3d:tests-cl:util:wrap

TESTS_2D = \
	test-fill \
	test-fill2 \
	test-replay \
	test-copy \
	test-fb \
	test-composite \
	test-composite2 \
	test-multi

TESTS_3D = \
	test-varyings \
	test-quad-attributeless \
	test-float-int \
	test-occquery \
	test-draw \
	test-clear \
	test-es2gears \
	test-piglit-bad \
	test-piglit-good \
	test-caps \
	test-stencil \
	test-mipmap \
	test-cubemap \
	test-compiler \
	test-enable-disable \
	test-quad-flat \
	test-quad-flat2 \
	test-quad-textured \
	test-quad-textured-3d \
	test-quad-flat-fbo \
	test-strip-smoothed \
	test-cat \
	test-cube \
	test-cube-textured \
	test-tex \
	test-vertex \
	test-triangle-smoothed \
	test-triangle-quad \
	test-instanced \
	test-tf

TESTS_CL = \
	test-simple \
	test-image

TESTS = $(TESTS_2D) $(TESTS_3D) $(TESTS_CL)
UTILS = bmp.o

CFLAGS = -Iincludes -Iutil

# Build Mode:
#  bionic -  build for gnu/linux style filesystem, linking
#            against android libc and libs in /system/lib
#  glibc  -  build for gnu/linux glibc, linking against
#            normal gnu dynamic loader
BUILD ?= bionic

ifeq ($(strip $(BUILD)),bionic)
# Note: setup symlinks in /system/lib to the vendor specific .so's in
# /system/lib/egl because android's dynamic linker can't seem to cope
# with multiple -rpath's..
# Possibly we don't need to link directly against gpu specific libs
# but I was getting eglCreateContext() failing otherwise.
LFLAGS_3D = -lEGL_adreno200 -lGLESv2_adreno200
LFLAGS_2D = -lC2D2 -lOpenVG
LFLAGS_CL = -lOpenCL
LDFLAGS_MISC = -lgsl -llog -lcutils -lstdc++ -lstlport -lm
CFLAGS += -DBIONIC
CC = gcc -L /system/lib -mfloat-abi=soft
LD = ld --entry=_start -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib
# only build c2d2 bits for android, otherwise we don't have the right
# headers/libs:
WRAP_C2D2 = wrap-c2d2.o
else ifeq ($(strip $(BUILD)),glibc)
LFLAGS_3D = -lEGL -lGLESv2
LFLAGS_2D =
#LFLAGS_CL = -lOpenCL
LDFLAGS_MISC = -lX11 -lm
CFLAGS += -DSUPPORT_X11
CC = gcc -L /usr/lib
LD = gcc -L /usr/lib
WRAP_C2D2 =
else
error "Invalid build type"
endif

LFLAGS = $(LFLAGS_2D) $(LFLAGS_3D) $(LFLAGS_CL) $(LDFLAGS_MISC) -ldl -lc

all: tests-3d tests-2d tests-cl

utils: libwrap.so $(UTILS) redump cffdump pgmdump zdump

tests-2d: $(TESTS_2D) utils

tests-3d: $(TESTS_3D) utils

tests-cl: $(TESTS_CL) utils

clean:
	rm -f *.bmp *.dat *.so *.o *.rd *.html *-cffdump.txt *-pgmdump.txt *.log redump cffdump pgmdump $(TESTS)

%.o: %.c
	$(CC) -fPIC -g -c $(CFLAGS) $(LFLAGS) $< -o $@

libwrap.so: wrap-util.o wrap-syscall.o $(WRAP_C2D2)
	$(LD) -shared -ldl -lc $^ -o $@

test-%: test-%.o $(UTILS)
	$(LD) $^ $(LFLAGS) -o $@

# build redump normally.. it doesn't need to link against android libs
redump: redump.c
	gcc -g $^ -o $@

envytools/Makefile:
	(cd envytools; cmake .)

envytools/rnn/librnn.a:
envytools/util/libenvyutil.a:
	(cd envytools; make rnn)

RNN = envytools/rnn/librnn.a envytools/util/libenvyutil.a
cffdump: cffdump.c disasm-a2xx.c disasm-a3xx.c script.c io.c rnnutil.c $(RNN)
	gcc -g $(CFLAGS) -Wall -Wno-packed-bitfield-compat -I. -Ienvytools/include $^ -lxml2 -llua -larchive -o $@

pgmdump: pgmdump.c disasm-a2xx.c disasm-a3xx.c io.c
	gcc -g $(CFLAGS) -Wno-packed-bitfield-compat -I. $^ -larchive -o $@
zdump: zdump.c
	gcc -g $(CFLAGS) -Wall -Wno-packed-bitfield-compat -I. $^ -o $@

