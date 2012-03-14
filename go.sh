gcc -g -c wrap.c -fPIC
ld -shared -o wrap.so wrap.o -nostdlib --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib -ldl -lc
gcc -g -c test.c
ld -o test test.o -nostdlib --entry=_start --dynamic-linker /system/bin/linker -rpath /system/lib -L /system/lib -lC2D2 -lgsl -llog -lOpenVG -lcutils -lstdc++ -lstlport -ldl -lc

#export LD_LIBRARY_PATH=/system/lib
LD_PRELOAD=`pwd`/wrap.so ./test

