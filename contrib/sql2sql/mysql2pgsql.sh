#!/bin/bash

# this script can be used to migrate from MySQL to PostgreSQL
# 
# check postgresql.conf:
#
# bytea_output = 'escape'
# escape_string_warning = off
# standard_conforming_strings = off
#
# this script requires py-mysql2pgsql which utilizes the yaml script in
# this directory. Please edit the yaml file to match your setup.
#

set -e

MYDB="dbmail"
PGDB="dbmail"
PGDD="../../sql/postgresql/create_tables.pgsql"


export tables="
dbmail_users
dbmail_mailboxes
dbmail_physmessage
dbmail_messages
dbmail_aliases
dbmail_acl
dbmail_authlog
dbmail_auto_notifications
dbmail_auto_replies
dbmail_filters
dbmail_keywords
dbmail_replycache
dbmail_sievescripts
dbmail_subscription
dbmail_usermap
"


init_pgsql()
{
	echo "clean out old DB ..."
	dropdb $PGDB >/dev/null 2>&1
	echo "create fresh DB ..."
	createdb -E UTF-8 -O dbmail $PGDB >/dev/null 2>&1
	echo "create schema ..."
	psql $PGDB < $PGDD >/dev/null 2>&1 || { echo "create db failed. abort."; exit 1; }
	echo "delete from dbmail_users;"|psql $PGDB >/dev/null 2>&1
	pgsql_owner
}

import_pgsql()
{
	for table in `echo $tables`; do
		echo -n "  migrate table: $table ..."
		mysqldump --compatible=postgresql -t --compact -c dbmail $table|psql dbmail >/dev/null 2>&1
		echo "done."
	done
	echo "  migrate tables: mimeparts, partlists ..."
	py-mysql2pgsql -v 2>/dev/null
	pgsql_sequences
	return $?
}

pgsql_sequences()
{
	echo -e "reset sequences ..."
	qfile=`tempfile`
	cat >> $qfile << EOQ
BEGIN;
SELECT setval('dbmail_alias_idnr_seq', max(alias_idnr)+1) FROM dbmail_aliases;
SELECT setval('dbmail_user_idnr_seq', max(user_idnr)+1) FROM dbmail_users;
SELECT setval('dbmail_mailbox_idnr_seq', max(mailbox_idnr)+1) FROM dbmail_mailboxes;
SELECT setval('dbmail_physmessage_id_seq', max(id)+1) FROM dbmail_physmessage;
SELECT setval('dbmail_message_idnr_seq', max(message_idnr)+1) FROM dbmail_messages;
SELECT setval('dbmail_seq_pbsp_id', max(idnr)+1) FROM dbmail_pbsp;
SELECT setval('dbmail_headername_id_seq', max(id)+1) FROM dbmail_headername;
SELECT setval('dbmail_headervalue_id_seq', max(id)+1) FROM dbmail_headervalue;
SELECT setval('dbmail_referencesfield_idnr_seq', max(id)+1) FROM dbmail_referencesfield;;
SELECT setval('dbmail_sievescripts_idnr_seq', max(id)+1) FROM dbmail_sievescripts;
SELECT setval('dbmail_envelope_idnr_seq', max(id)+1) FROM dbmail_envelope;
SELECT setval('dbmail_authlog_id_seq', max(id)+1) FROM dbmail_authlog;
SELECT setval('dbmail_filters_id_seq', max(id)+1) FROM dbmail_filters;
SELECT setval('dbmail_mimeparts_id_seq', max(id)+1) FROM dbmail_mimeparts;
END;
EOQ
	psql -q $PGDB < $qfile >/dev/null 2>&1
	echo "done."
	rm -f $qfile
}

pgsql_owner()
{
	echo -n "fix ownership ..."
	for t in `echo '\d'|psql $PGDB|grep public |awk '{print $3}'`; do 
		echo "alter table $t owner to dbmail;"|psql -q $PGDB >/dev/null 2>&1
	done
	echo "done."
}

main()
{
	init_pgsql
	import_pgsql || { echo "Import failed"; exit 1; }
	echo "Data migration complete. Now rebuild all caching tables using dbmail-util -by."
}


main

