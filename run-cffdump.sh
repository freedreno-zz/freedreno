#!/bin/sh

dir=`dirname $0`
nocolor="--no-color"

for f in $*; do
	echo "$f"
	if [ -x $dir/cffdump ]; then
		$dir/cffdump --verbose $nocolor $f > ${f%%.rd}-cffdump.txt
	fi
	if [ -x $dir/pgmdump ]; then
		$dir/pgmdump --verbose --short $f > ${f%%.rd}-pgmdump.txt
	fi
done

