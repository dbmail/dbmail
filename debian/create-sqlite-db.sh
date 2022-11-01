#!/bin/sh
#
# File: create-sqlite-db.sh
# Date: 2022-Oct-27
# By  : Kevin L. Esteb
#
# create a sqlite database and load schema.
#
rm -f dbmail.db
#
sqlite3 dbmail.db < ../sql/sqlite/create_tables.sqlite
sqlite3 dbmail.db < ../sql/sqlite/upgrades/upgrade.sqlite
#
if ! grep -q "dbmail.db" install
then
    sed -i "a debian/dbmail.db var/lib/dbmail" install
fi
#
