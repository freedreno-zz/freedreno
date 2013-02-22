#!/bin/sh

opts="-cl-fast-relaxed-math -cl-unsafe-math-optimizations -cl-mad-enable -cl-finite-math-only -cl-single-precision-constant -cl-denorms-are-zero"

for f in kernels/*.cl; do
	./cltool --dump-shaders --opts "$opts" $f > ${f%%\.cl}.txt
done

