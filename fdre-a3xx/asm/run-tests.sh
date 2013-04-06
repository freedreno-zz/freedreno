#!/bin/sh

for f in tests/*.asm; do
	outfile=${f%%.asm}.o3
	./fdasm $f $outfile
	if [ $? != 0 ]; then
		echo "failed at: $f"
		exit 1
	fi
done

