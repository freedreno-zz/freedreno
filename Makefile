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
	test-quad-flat \
	test-quad-flat2 \
	test-triangle-quad

TESTS = $(TESTS_2D) $(TESTS_3D)
UTILS = bmp.o

CFLAGS = -Iincludes -Iutil
# Note: setup symlinks in /system/lib to the vendor specific .so's in
# /system/lib/egl because android's dynamic linker can't seem to cope
# with multiple -rpath's..
# Possibly we don't need to link directly against gpu specific libs
# but I was getting eglCreateContext() failing otherwise.
LFLAGS_3D = -lEGL_adreno200 -lGLESv2_adreno200
LFLAGS_2D = -lC2D2 -lOpenVG
LFLAGS = -L /system/lib  $(LFLAGS_2D) $(LFLAGS_3D) -lgsl -llog -lcutils -lstdc++ -lstlport -ldl -lc

all: libwrap.so $(UTILS) $(TESTS) redump cffdump

clean:
	rm -f *.bmp *.dat *.so *.o *.rd *.html *-cffdump.txt *.log redump cffdump $(TESTS)

%.o: %.c
	gcc -g -c -fPIC $(CFLAGS) $(LFLAGS) $< -o $@

libwrap.so: wrap-util.o wrap-syscall.o wrap-c2d2.o
	ld -shared -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib -ldl -lc $^ -o $@

test-%: test-%.o $(UTILS)
	ld -nostdlib --entry=_start --dynamic-linker /system/bin/linker -rpath /system/lib $(LFLAGS) $^ -o $@

# build redump normally.. it doesn't need to link against android libs
redump: redump.c
	gcc -g $^ -o $@

cffdump: cffdump.c
	gcc -g $^ -o $@

