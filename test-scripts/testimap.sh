#!/bin/sh

for f in *.imap; do
	cat $f | nc localhost 10143
done

