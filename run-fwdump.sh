#!/bin/sh

dir=`dirname $0`

for f in $*; do
	echo "$f"
	$dir/fwdump $f > ${f%%.fw}.asm
done

