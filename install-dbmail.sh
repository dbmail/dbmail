#!/bin/bash

# $Id$
# (c) 2000 - 2001 IC&S, The Netherlands 

# check if user root is running this script

amiroot=`id -u`

if [ "$1" != "" ]; then
    targetexec=$1
else
    targetexec=/usr/local/sbin/
fi

targetman=/usr/local/man/man1/

if [ "`id -u`" != "0" ] ; then
	echo "You need to be root to run this script"
	exit 1
fi

cat << EOF

This script will install dbmail on your system. 
Before executing this script be sure to have read the INSTALL file. 
Although dbmail is very easy to install you'll need to know a few little
things before you can start using it.

If you have any problems, man files will also be installed so you can 
always check the manpage of a program.

Next i'll be asking you as what user and group you want to be running DBMAIL. 
Best thing is to create a user called dbmail with a dbmail group. 
Don't forget to edit these users in the dbmail.conf file and run dbmail-config
afterwards. The pop3 daemon and the imapd daemon have capabilities to
drop their privileges! Use that capability!

EOF

echo -n "As what user are the dbmail daemons going to run? [default: dbmail] " 
read user_dbmail

if [ "$user_dbmail" == "" ] ; then 
	user_dbmail="dbmail"
fi
	
echo -n "As what group are the dbmail daemons going to run? [default: dbmail] " 
read group_dbmail

if [ "$group_dbmail" == "" ] ; then 
	group_dbmail="dbmail"
fi

echo "Ok installing dbmail executables as $user_dbmail:$group_dbmail.."
for file in dbmail-smtp dbmail-pop3d dbmail-imapd dbmail-maintenance dbmail-adduser
do
	/bin/chown $user_dbmail:$group_dbmail $file
	/bin/chmod 770 $file
	/bin/cp -fp $file $targetexec
done

echo "Ok installing manfiles in $targetman.."
/bin/cp -f man/* $targetman

echo "Done"
