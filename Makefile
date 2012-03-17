
TESTS = test-fill
UTILS = bmp.o

CFLAGS = 
LFLAGS = -L /system/lib -lC2D2 -lgsl -llog -lOpenVG -lcutils -lstdc++ -lstlport -ldl -lc

all: libwrap.so $(UTILS) $(TESTS)

clean:
	rm -f *.bmp *.so *.o $(TESTS)

%.o: %.c
	gcc -g -c -fPIC $(LFLAGS) $< -o $@

lib%.so: %.o
	ld -shared -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib -ldl -lc $< -o $@

test-%: test-%.o $(UTILS)
	ld -nostdlib --entry=_start --dynamic-linker /system/bin/linker -rpath /system/lib $(LFLAGS) $^ -o $@

