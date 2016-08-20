#!/bin/sh

#LD_PRELOAD=`pwd`/libwrap.so ./test-quad-flat > test-quad-flat.log

for f in test-*; do
	if [ -x $f ]; then
		echo "Running: $f"
		i=0
		while `true`; do
			echo "Running: $f ($i)"
			TESTNAME=${f#test-} TESTNUM=$i LD_PRELOAD=`pwd`/libwrap.so ./$f > $f.$i.log
			if [ "$?" = "42" ]; then
				break;
			fi
			sync
			i=$((i+1))
		done
	fi
done

