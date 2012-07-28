#!/bin/sh

# a test to disassemble shaders from cmdstream, then re-assemble them and
# compare the output to spot assembler mistakes or important missing bits


for rd in $*; do
	rm -f *.vo *.fo
	./cffdump --dump-shaders $rd > /dev/null
	for shader in *.vo *.fo; do
		./pgmdump --raw $shader > $shader.txt
		./fdre/asm/fdasm $shader.txt fd-$shader
		./pgmdump --verbose --raw $shader > $shader.txt
		./pgmdump --verbose --raw fd-$shader > fd-$shader.txt
		meld fd-$shader.txt $shader.txt
		rm fd-$shader.txt $shader.txt
	done
	rm -f *.vo *.fo
done
