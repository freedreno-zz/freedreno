#!/bin/bash

# a test to disassemble shaders from cmdstream, then re-assemble them and
# compare the output to spot assembler mistakes or important missing bits


tmpdir="/tmp/fd$RANDOM/"
mkdir -p $tmpdir

origdir=`pwd`
cd `dirname $0`
fd=`pwd`

cd $tmpdir

for rd in $*; do
	prefix=`basename $rd`
	prefix=${prefix%%.rd}
	$fd/cffdump --dump-shaders $origdir/$rd > /dev/null
	for shader in *.vo *.fo; do
		$fd/pgmdump --raw $shader > $shader.txt
		$fd/fdre/asm/fdasm $shader.txt fd-$shader
		$fd/pgmdump --verbose --raw $shader > $prefix-$shader.txt
		$fd/pgmdump --verbose --raw fd-$shader > fd-$prefix-$shader.txt
		meld fd-$prefix-$shader.txt $prefix-$shader.txt
	done
done

cd $origdir
rm -rf $tmpdir

