#!/bin/sh

# $Id$
#
# fast script for deleting unconnected physmessage rows
#
# TODO: this should be integrated into dbmail-util, I know....

DB=dbmail

QUERY="SELECT id FROM dbmail_physmessage LEFT JOIN dbmail_messages 
	ON dbmail_physmessage.id = dbmail_messages.physmessage_id 
	WHERE dbmail_messages.physmessage_id IS NULL";
	
UPDATE="DELETE FROM dbmail_physmessage WHERE id IN ("
	
mysql --skip-column-names -B -e "$QUERY" $DB |\
	eval "awk 'BEGIN {printf(\"\n"$UPDATE"\");}{ if(NR % 200 == 0) {printf(\"\n"$UPDATE"\"); i=0; } else { printf(\"%s,\",\$1); }}'" |\
	sed 's/,$/);/' |\
	mysql $DB


