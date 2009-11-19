#!/bin/sh

HOST="localhost:41380"
URLS="/users /users/testuser1 /users/testuser1/mailboxes /mailboxes/2/messages /messages/62820/view /messages/62820/headers/from,to,subject,date"

for u in $URLS; do
	cmd="curl --dump - --user admin:secret http://$HOST$u"
	echo $cmd
	eval "$cmd"
done


