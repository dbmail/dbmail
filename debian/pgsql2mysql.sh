#!/bin/sh

# example script to convert from postgresql to mysql
#
# This file is not a part of the original dbmail package.
#
# It's slow, but it works.
#
# $Id: pgsql2mysql.sh 1510 2004-12-10 19:34:04Z paul $

mysqladmin -f drop dbmail
mysqladmin create dbmail
zcat /usr/share/doc/dbmail-mysql/examples/create_tables_innoDB.mysql.gz | mysql dbmail
pg_dump --file=/var/tmp/dbmail-dump.pgdata --format=p --data-only --inserts --no-owner --no-reconnect --no-privileges dbmail
mysql -f dbmail < /var/tmp/dbmail-dump.pgdata
