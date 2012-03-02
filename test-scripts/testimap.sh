#!/bin/sh

_dir=`dirname $0`

for f in ${_dir}/*.imap; do
	cat $f | nc localhost 10143
done

