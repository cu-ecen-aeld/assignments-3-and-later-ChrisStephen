#!/bin/sh

if [ $# -ne 2 ]; then
	echo "Error --- usage: $0 <filesdir> <searchstr>"
	exit 1
fi

if [ ! -e $1 ]; then
	echo "Error --- $1 does not exist"
	exit 1
fi

echo "The number of files are $(find $1 -type f | wc -l) and the number of matching lines are $(grep -r $2 $1 | wc -l)"

exit 0

