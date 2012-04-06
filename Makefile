
TESTS = test-fill test-fill2 test-copy test-fb test-composite
UTILS = bmp.o

CFLAGS = -Iincludes
LFLAGS = -L /system/lib -lC2D2 -lgsl -llog -lOpenVG -lcutils -lstdc++ -lstlport -ldl -lc

all: libwrap.so $(UTILS) $(TESTS) redump

clean:
	rm -f *.bmp *.dat *.so *.o *.rd *.html *.log redump $(TESTS)

%.o: %.c
	gcc -g -c -fPIC $(CFLAGS) $(LFLAGS) $< -o $@

libwrap.so: wrap-util.o wrap-syscall.o wrap-c2d2.o
	ld -shared -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib -ldl -lc $^ -o $@

test-%: test-%.o $(UTILS)
	ld -nostdlib --entry=_start --dynamic-linker /system/bin/linker -rpath /system/lib $(LFLAGS) $^ -o $@

# build redump normally.. it doesn't need to link against android libs
redump: redump.c
	gcc -g $^ -o $@
