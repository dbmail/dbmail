#!/bin/sh

# example script to convert from postgresql to mysql
#
# It's slow, but it works.
#
# $Id: pgsql2mysql.sh 1612 2005-02-23 19:16:43Z paul $

PGDB="dbmail"
MYDB="dbmail"

TMPDIR="/var/tmp"

mysqladmin drop $MYDB
mysqladmin create $MYDB
zcat /usr/share/doc/dbmail-mysql/examples/create_tables_innoDB.mysql.gz | mysql $MYDB
pg_dump --file=$TMPDIR/dbmail-dump.pgdata \
	--format=p --data-only --inserts \
	--no-owner --no-reconnect --no-privileges $PGDB
	
mysql -f $MYDB < $TMPDIR/dbmail-dump.pgdata

