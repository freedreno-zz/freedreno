#!/bin/sh

dir=`dirname $0`

for f in $*; do
	echo "$f"
	$dir/zdump $f > ${f%%.rd}.z
done

