#!/bin/sh

sed 's/\r//'|grep -E 'COMMAND|RESPONSE' -|cut -b17-|cut -d' ' -f2,5-|sed -e 's/COMMAND/C/' -e 's/RESPONSE/S/' -e 's,dbmail/imap4d,,'
