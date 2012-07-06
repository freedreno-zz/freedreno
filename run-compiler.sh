#!/bin/sh -v

cd `dirname $0`
dir=`pwd`

mkdir -p tmp

TARGET="touchpad"

scp shaders/*.fs shaders/*.vs $TARGET:$dir/shaders
# note: the android linked binaries give bogus return values, so ignore the result of
# running test-compiler
ssh $TARGET "(cd $dir ; LD_PRELOAD=$dir/libwrap.so ./test-compiler ; ./run-cffdump.sh compiler*.rd)"
scp $TARGET:$dir/compiler-\*-pgmdump.txt ./tmp

