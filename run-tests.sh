#!/bin/sh

#LD_PRELOAD=`pwd`/libwrap.so ./test-quad-flat > test-quad-flat.log

for f in test-*; do
	if [ -x $f ]; then
		echo "Running: $f"
		LD_PRELOAD=`pwd`/libwrap.so ./$f > $f.log
	fi
done

