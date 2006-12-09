#!/bin/sh


MAILBOX='Spam'
EXPIRES=30

function getids()
{
	tmpfile=`tempfile`

	cat >> $tmpfile << EOQ

select message_idnr from dbmail_messages m 
 join dbmail_mailboxes b on m.mailbox_idnr=b.mailbox_idnr 
 join dbmail_physmessage p on m.physmessage_id=p.id 
where 
 b.name="$MAILBOX"
and 
 m.status<2
and
 to_days(p.internal_date) < to_days(now())-$EXPIRES

EOQ
	mysql -r -B --skip-column-names dbmail < $tmpfile 
}

count=`getids|wc -l`
if [ $count -gt 0 ]; then
	echo expiring $count messages
	getids | xargs -l100 echo|sed 's/ /,/g'|\
	xargs -l1 printf "update dbmail_messages set status=2 where message_idnr in (%s);\n" |\
	mysql dbmail
else
	echo nothing to do
fi

