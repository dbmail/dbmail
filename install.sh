#!/bin/bash

# $Id$
# (c) 2000 - 2001 IC&S, The Netherlands 

# check if user root is running this script

#amiroot=`id -u`

#if [ "`id -u`" != "0" ] ; then
#	echo "You need to be root to run this script"
#	exit 1
#fi

default_location_mysql_h="/usr/include/mysql/"
default_location_libmysqlclient_so="/usr/lib/"

echo "*** MYSQL SETTINGS ***"
echo -n "Where can we find mysql.h? [default: $default_location_mysql_h] "
read location_mysql_h

if [ "$location_mysql_h" == "" ] ; then 
	location_mysql_h=$default_location_mysql_h
fi
	
echo -n "Where can we find libmysqlclient.so? [default: $default_location_libmysqlclient_so] "
read location_libmysqlclient_so	

if [ "$location_libmysqlclient_so" == "" ] ; then 
	location_mysqlclient_so=$default_location_libmysqlclient_so
fi

echo -n "Ok creating Makefile out of Makefile.tmpl.. "


