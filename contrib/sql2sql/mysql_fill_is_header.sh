#!/bin/sh

mysql --skip-column-names -B -e "select dbmail_messageblk_idnr from messageblks group by physmessage_id" dbmail |\
	awk 'BEGIN { printf("\nupdate dbmail_messageblks set is_header=1 where messageblk_idnr in ("); }   { if(NR % 200 == 0) { printf("\nupdate dbmail_messageblks set is_header=1 where messageblk_idnr in ("); i=0; } else { printf("%s,",$1); }}' |\
	sed 's/,$/);/'

