#!/bin/sh

dir=`dirname $0`

for f in $*; do
	$dir/cffdump $f > ${f%%.rd}-cffdump.txt
	$dir/pgmdump $f > ${f%%.rd}-pgmdump.txt
done

