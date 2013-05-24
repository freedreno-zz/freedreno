#!/bin/sh

dir=`dirname $0`
cffdump_args=""
pgmdump_args=""

for f in $*; do
	if [ $f = "--no-color" ]; then
		cffdump_args="$cffdump_args $f"
		continue
	fi
	if [ $f = "--summary" ]; then
		cffdump_args="$cffdump_args $f"
		continue
	fi
	if [ $f = "--dump-shaders" ]; then
		cffdump_args="$cffdump_args $f"
		continue
	fi
	if [ $f = "--allregs" ]; then
		cffdump_args="$cffdump_args $f"
		continue
	fi
	if [ $f = "--verbose" ]; then
		cffdump_args="$cffdump_args $f"
		pgmdump_args="$pgmdump_args $f"
		continue
	fi
	if [ $f = "--short" ]; then
		pgmdump_args="$pgmdump_args $f"
		continue
	fi
	if [ $f = "--expand" ]; then
		pgmdump_args="$pgmdump_args $f"
		continue
	fi

	echo "$f"
	if [ -x $dir/cffdump ]; then
		$dir/cffdump $cffdump_args $f > ${f%%.rd}-cffdump.txt
	fi
	if [ -x $dir/pgmdump ]; then
		$dir/pgmdump $pgmdump_args $f > ${f%%.rd}-pgmdump.txt
	fi
done

