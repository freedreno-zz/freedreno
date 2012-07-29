# Note: this VPATH bit is just a hack to avoid mucking with the build
# system after splitting the directory tree into various subdirs..
# really need to clean this up but I've got better things to work on
# right now:
VPATH = tests-2d:tests-3d:util:wrap

TESTS_2D = \
	test-fill \
	test-fill2 \
	test-copy \
	test-fb \
	test-composite \
	test-composite2 \
	test-multi

TESTS_3D = \
	test-compiler \
	test-enable-disable \
	test-quad-flat \
	test-quad-flat2 \
	test-strip-smoothed \
	test-cube \
	test-vertex \
	test-triangle-smoothed \
	test-triangle-quad

TESTS = $(TESTS_2D) $(TESTS_3D)
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
LDFLAGS_MISC = -lgsl -llog -lcutils -lstdc++ -lstlport
CFLAGS += -DBIONIC
CC = gcc -L /system/lib
LD = ld --entry=_start -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib
else ifeq ($(strip $(BUILD)),glibc)
LFLAGS_3D = -lEGL -lGLESv2
LFLAGS_2D =
LDFLAGS_MISC =
CC = gcc -L /usr/lib
LD = gcc -L /usr/lib
else
error "Invalid build type"
endif

LFLAGS = $(LFLAGS_2D) $(LFLAGS_3D) $(LDFLAGS_MISC) -ldl -lc

all: tests-3d tests-2d

utils: libwrap.so $(UTILS) redump cffdump pgmdump

tests-2d: $(TESTS_2D) utils

tests-3d: $(TESTS_3D) utils

clean:
	rm -f *.bmp *.dat *.so *.o *.rd *.html *-cffdump.txt *-pgmdump.txt *.log redump cffdump pgmdump $(TESTS)

%.o: %.c
	$(CC) -fPIC -g -c $(CFLAGS) $(LFLAGS) $< -o $@

libwrap.so: wrap-util.o wrap-syscall.o wrap-c2d2.o
	$(LD) -shared -ldl -lc $^ -o $@

test-%: test-%.o $(UTILS)
	$(LD) $^ $(LFLAGS) -o $@

# build redump normally.. it doesn't need to link against android libs
redump: redump.c
	gcc -g $^ -o $@

cffdump: cffdump.c disasm.c
	gcc -g $(CFLAGS) $^ -o $@

pgmdump: pgmdump.c disasm.c
	gcc -g $(CFLAGS) $^ -o $@

