#!/usr/bin/env bash

BIN=$(cd $(dirname $0); pwd)/../bin

if [ -z $1 ]; then
	echo "Usage: $0 [testnum]"
	echo "Example: $0 1"
else
	echo "file1"
	cat ex$1_1
	echo "file2"
	cat ex$1_2
	echo "diff test"
	diff ex$1_1 ex$1_2
	echo "self test"
	$BIN/fileMerger ex$1_1 ex$1_2 ex$1_3 ex$1_4
	cat ex$1_4
fi

