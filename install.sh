#!/bin/bash

# $Id$
# (c) 2000 - 2001 IC&S, The Netherlands 

# check if user root is running this script

amiroot=`id -u`

if [ "`id -u`" != "0" ] ; then
	echo "You need to be root to run this script"
	exit 1
fi

echo "Setting permissions..."
/bin/chmod 700 ./dbmail-config ./dbmail-adduser ./dbmail-maintenance
/bin/chown root:root ./dbmail-config ./dbmail-adduser ./dbmail-maintenance

echo "Copying files to /usr/local/bin..."
cp ./dbmail-config ./dbmail-adduser ./dbmail-maintenance /usr/local/bin

echo "Finished"
