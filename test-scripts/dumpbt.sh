#!/bin/bash

# generate a backtrace for busy worker threads

# only look for thread with more that MAXCPU cpu load
MAXCPU=20

# get the imap PID
IMAPPID=`pidof dbmail-imapd`

# find busy threads
BUSY=`top -H -b -n1 -p $IMAPPID|grep 'pool'|awk '{if ($9 > $MAXCPU) print $1}'`

[ -n "$BUSY" ] || exit 0

# generate a gdb commandfile
tmpfile=`mktemp`
echo 'bt' > $tmpfile
for TID in $BUSY; do
	# get a backtrace
	gdb --batch -x $tmpfile -p $TID > \
		/tmp/debug-dbmail-backtrace.$TID
        echo "backtrace in " /tmp/debug-dbmail-backtrace.$TID
done
rm -f $tmpfile

