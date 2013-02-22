CFLAGS = -I../includes

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
LDFLAGS_CL = -lOpenCL
LDFLAGS_MISC = -lgsl -llog -lcutils -lstdc++ -lstlport
CFLAGS += -DBIONIC
CC = gcc -L /system/lib -mfloat-abi=soft
LD = ld -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib lib/crtbegin_dynamic.o lib/crtend_android.o
else ifeq ($(strip $(BUILD)),glibc)
LDFLAGS_CL = -lOpenCL
LDFLAGS_MISC = -lX11 -lm
CC = gcc -L /usr/lib
LD = gcc -L /usr/lib
else
error "Invalid build type"
endif

LFLAGS = $(LDFLAGS_CL) $(LDFLAGS_MISC) -ldl -lc -lm

all: cltool

clean:
	rm -f *.so *.o cltool kernels/*.txt kernels/*.co3

%.o: %.c
	$(CC) -fPIC -g -O0 -c $(CFLAGS) $(LFLAGS) $< -o $@

cltool: cltool.o
	$(LD) $^ -lllvm-a3xx -lc -o $@

