#!/bin/sh

for f in $*; do
	./cffdump $f > ${f%%.rd}-cffdump.txt
done

