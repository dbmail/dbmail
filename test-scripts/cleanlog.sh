#!/bin/sh


grep -E 'COMMAND|RESPONSE' - | \
	cut -f5,8- -d' ' | \
	sed -e 's/COMMAND/C/' -e 's/RESPONSE/S/' -e 's,dbmail/imap4d,,'
