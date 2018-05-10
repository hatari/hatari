#!/bin/sh

cd $(dirname $0)/..

errors=0

for i in $(hg locate '*\.sh'); do
	echo "Checking $i ..."
	checkbashisms $i
	errors=$(($errors + $?))
done

if [ "$errors" -gt 0 ]; then
	echo "TEST FAILED"
	exit 1
fi

echo "TEST SUCCESS"
exit 0
