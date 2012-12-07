#!/bin/sh

_dir=`dirname $0`

for f in ${_dir}/*.imap; do
	echo "$f"
	cat $f | nc -q 10 localhost 10143
	cat $f | openssl s_client -connect localhost:10143 -starttls imap -ign_eof
done


