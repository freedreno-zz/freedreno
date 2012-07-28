#!/bin/sh

dir=`dirname $0`

for f in $*; do
	echo "$f"
	$dir/cffdump $f > ${f%%.rd}-cffdump.txt
	$dir/pgmdump --verbose --short $f > ${f%%.rd}-pgmdump.txt
done

