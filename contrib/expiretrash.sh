#!/bin/sh

# copyright 2007, Paul Stevens, NFG
# licence: GPLv2

export qfile=`tempfile`

DBNAME="dbmail"
EXDAYS="60"  # expire messages in Trash older then EXDAYS days
DRIVER="mysql" # select on of mysql, pgsql, sqlite

function cleanup()
{
	[ -n "$qfile" ] && rm -f $qfile
}

function abort()
{
	msg="$@"
	echo "$msg"
	cleanup
	exit 1
}

function sql_expire()
{
	ival="$1"
	case "$DRIVER" in
		mysql)
			echo "now() - interval $ival day"
		;;
		pgsql)
			echo "now() - interval '$ival day'"
		;;
		sqlite)
			echo "datetime('now','-$ival days')"
		;;
	esac

}

function query()
{
	if `test -e "$1"`; then
		q=`cat $1`
	else
		q="$@"
	fi
	case "$DRIVER" in
		mysql)
			mysql --batch --skip-column-names -e "$q" $DBNAME
		;;
		pgsql)
			psql -q -t -c "$q" $DBNAME
		;;
		sqlite)
			echo $q|sqlite3 -noheader $DBNAME
		;;
	esac
}

function get_TrashIds()
{
	cat > $qfile << EOS

SELECT mailbox_idnr FROM dbmail_mailboxes b 
 WHERE b.name IN ('Trash','INBOX/Trash') 
 OR b.name LIKE 'Trash/%'
 OR b.name LIKE 'INBOX/Trash/%'

EOS
	query $qfile|xargs -L50|sed -e 's/ /,/g'
}

function expire_Trash()
{
	ids="$1"
	[ -n "$ids" ] || return 1
	cat > $qfile << EOQ
START TRANSACTION;
UPDATE dbmail_messages m, dbmail_physmessage p 
 SET m.deleted_flag=1,m.status=2 
 WHERE m.mailbox_idnr IN ($ids)
 AND p.id=m.physmessage_id 
 AND p.internal_date < ($EXPIRE);
COMMIT;
EOQ
	query $qfile
}

function main()
{
	trap cleanup KILL INT CHLD
	export EXPIRE=`sql_expire $EXDAYS`
	for l in `get_TrashIds`; do
		expire_Trash "$l"
	done
	cleanup
}

## run some tests
realdriver="$DRIVER"
export DRIVER="mysql"
r=`sql_expire "60"` 
[ "$r" = "now() - interval 60 day" ] || abort "test failed"
export DRIVER="pgsql"
r=`sql_expire "60"`
[ "$r" = "now() - interval '60 day'" ] || abort "test failed"
export DRIVER="sqlite"
r=`sql_expire "60"`
[ "$r" = "datetime('now','-60 days')" ] || abort "test failed"

export DRIVER=$realdriver

main

