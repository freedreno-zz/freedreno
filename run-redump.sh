#!/bin/sh

found=""

for f in *.rd; do
	test=${f%%-[0-9]*};
	echo $found | grep $test > /dev/null
	if [ $? = 1 ]; then
		echo "found: $test";
		./redump $test-*.rd > $test.html
		found="$found $test"
	fi
done

