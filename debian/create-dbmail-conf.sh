#!/bin/sh
#
# File: create-systemd.sh
# Date: 2022-Oct-26
# By  : Kevin L. Esteb
#
# Create the service unit files for dbmail
#
dbdir=/var/lib/dbmail
rundir=/run
#
cp ../dbmail.conf .
sed -i "s|/var/tmp|$dbdir|g" dbmail.conf
sed -i "s|/var/run|$rundir|g" dbmail.conf
sed -i "s|= nobody|= dbmail|g" dbmail.conf
sed -i "s|= nogroup|= dbmail|g" dbmail.conf
#
