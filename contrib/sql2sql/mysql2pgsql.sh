#!/bin/bash

MYDB="dbmail"
PGDB="dbmail"
PGDD="../../sql/postgresql/create_tables.pgsql"

TMPDIR="/opt/tmp"

export tables="
dbmail_users
dbmail_mailboxes
dbmail_physmessage
dbmail_messages
dbmail_messageblks
dbmail_aliases
dbmail_acl
dbmail_auto_notifications
dbmail_auto_replies
dbmail_ccfield
dbmail_datefield
dbmail_envelope
dbmail_fromfield
dbmail_headername
dbmail_headervalue
dbmail_pbsp
dbmail_referencesfield
dbmail_replycache
dbmail_replytofield
dbmail_sievescripts
dbmail_subjectfield
dbmail_subscription
dbmail_tofield
dbmail_usermap
"

export_mysql()
{
	dumpfile=$TMPDIR/dbmail.mysqldata
	[ -e "$dumpfile" ] && return 1
	echo -n "export from mysql ..."
	mysqldump --skip-opt --single-transaction --hex-blob --compatible=postgresql -q -t -c $MYDB $tables > $dumpfile
	echo "done"
}


init_pgsql()
{
	dropdb dbmail >/dev/null 2>&1
	createdb dbmail >/dev/null 2>&1
	psql dbmail < ../../sql/postgresql/create_tables.pgsql >/dev/null 2>&1 || { echo "create db failed. abort."; exit 1; }
	echo "delete from dbmail_users;"|psql dbmail

}
import_pgsql()
{
	dumpfile=$TMPDIR/dbmail.mysqldata
	echo -n "import into pgsql ..."
	cat $dumpfile | psql -q dbmail
	echo "done."
	return $?
}

pgsql_sequences()
{
	qfile=`tempfile`
	cat >> $qfile << EOQ
BEGIN;
SELECT setval('dbmail_alias_idnr_seq', max(alias_idnr)) FROM dbmail_aliases;
SELECT setval('dbmail_user_idnr_seq', max(user_idnr)) FROM dbmail_users;
SELECT setval('dbmail_mailbox_idnr_seq', max(mailbox_idnr)) FROM dbmail_mailboxes;
SELECT setval('dbmail_physmessage_id_seq', max(id)) FROM dbmail_physmessage;
SELECT setval('dbmail_message_idnr_seq', max(message_idnr)) FROM dbmail_messages;
SELECT setval('dbmail_messageblk_idnr_seq', max(messageblk_idnr)) FROM dbmail_messageblks;
SELECT setval('dbmail_seq_pbsp_id', max(idnr)) FROM dbmail_pbsp;
SELECT setval('dbmail_headername_idnr_seq', max(id)) FROM dbmail_headername;
SELECT setval('dbmail_headervalue_idnr_seq', max(id)) FROM dbmail_headervalue;
SELECT setval('dbmail_subjectfield_idnr_seq', max(id)) FROM dbmail_subjectfield;
SELECT setval('dbmail_datefield_idnr_seq', max(id)) FROM dbmail_datefield;
SELECT setval('dbmail_referencesfield_idnr_seq', max(id)) FROM dbmail_referencesfield;;
SELECT setval('dbmail_fromfield_idnr_seq', max(id)) FROM dbmail_fromfield;
SELECT setval('dbmail_tofield_idnr_seq', max(id)) FROM dbmail_tofield;
SELECT setval('dbmail_replytofield_idnr_seq', max(id)) FROM dbmail_replytofield;
SELECT setval('dbmail_ccfield_idnr_seq', max(id)) FROM dbmail_ccfield;
SELECT setval('dbmail_sievescripts_idnr_seq', max(id)) FROM dbmail_sievescripts;
SELECT setval('dbmail_envelope_idnr_seq', max(id)) FROM dbmail_envelope;
END;
EOQ
	psql -q dbmail < $qfile
	rm -f $qfile
}

pgsql_owner()
{
	for t in `echo '\d'|psql dbmail|grep root|awk '{print $3}'`; do 
		echo "alter table $t owner to dbmail;"|psql -q dbmail
	done
}

main()
{
	install -d -m 7777 $TMPDIR || { echo "unable to access $TMPDIR"; exit 1; }
	#export_mysql || { echo "Export failed"; exit 1; }
	init_pgsql
	import_pgsql || { echo "Import failed"; exit 1; }
	pgsql_sequences
	pgsql_owner
}


main

