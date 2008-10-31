#!/bin/bash

base=`dirname $0`

echo "LHLO host"
i=0

msg1=`cat $base/lmtp2.txt`
msg2=`cat $base/lmtp3.txt`
while [ $i -lt 1000 ]; do
	echo "RSET"
	echo "$msg1"
	echo "RSET"
	echo "$msg2"
	let i=$i+1
done

echo "QUIT"

