The test and libwrap code link using normal gcc (on gnu/linux type
environment, I'm using armv7 ubuntu 11.10, but debian, etc, should
work just fine), against an android libc and libC2D2, etc.  The
/system directory from an android snapdragon filesystem should be
extracted under /system.  You can find what you need from, for ex,
a cyanogenmod filesystem for a snapdragon phone.

You can run the test apps with wrapper lib like:

  LD_PRELOAD=`pwd`/libwrap.so ./test-copy > test-copy.log

(run that as root or fix up permissions on /dev/kgsl-*)

The redump utility can post process a set of .rd log files to
generate a html table showing side-by-side comparisions of the
cmdstream with params and gpuaddr's highlighted:

  ./redump copy*.rd > copy.html

