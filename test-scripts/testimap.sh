#!/bin/sh

_dir=`dirname $0`

for f in ${_dir}/*.imap; do
	echo "$f"
	cat $f | nc -q 10 localhost 10143
done

