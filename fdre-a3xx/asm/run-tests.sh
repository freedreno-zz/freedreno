#!/bin/sh

cd `dirname $0`

for f in tests/*.asm; do
	o3file=${f%%.asm}.co3
	disfile=${f%%.asm}.dasm
	./fdasm $f $o3file
	if [ $? != 0 ]; then
		echo "assembler failed at: $f"
		exit 1
	fi
	../../pgmdump $o3file | grep "\[" | sed 's/[0-9]*\[[0-9a-f]*x_[0-9a-f]*x\] //' > $disfile
	diff $f $disfile > /dev/null || meld $f $disfile
done

