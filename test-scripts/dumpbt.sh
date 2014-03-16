#!/bin/bash

# generate a backtrace for busy worker threads
PNAME=${1:-dbmail-imapd}

# only look for thread with more that MAXCPU cpu load
MAXCPU=${2:-20}

# get the imap PID
PID=`pidof $PNAME`

[ -n "$PID" ] || { echo "Error: Unable to determine main process ID for $PNAME"; exit 1; }

echo "generate backtrace for $PNAME($PID) with CPU > $MAXCPU"

# find busy threads
BUSY=`top -H -b -n1 -p $PID|awk '{if ($9 > $MAXCPU) print $1}'`

[ -n "$BUSY" ] || exit 0

# generate a gdb commandfile
tmpfile=`mktemp`
echo 'bt' > $tmpfile
for TID in $BUSY; do
	# get a backtrace
	gdb --batch -x $tmpfile -p $TID > \
		/tmp/debug-dbmail-backtrace.$TID
        echo "backtrace for $PNAME in " /tmp/debug-dbmail-backtrace.$TID
done
rm -f $tmpfile

