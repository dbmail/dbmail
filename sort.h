
/* $Id$ 
 
 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Headers for sort.c */

#ifndef SORT_H
#define SORT_H

#define MAX_SIEVE_SCRIPTNAME 100

dsn_class_t sort_and_deliver(struct DbmailMessage *self,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source,
		const char *fromaddr);

dsn_class_t sort_deliver_to_mailbox(struct DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source);

int sort_process(u64_t user_idnr, struct DbmailMessage *message);
int sort_validate(u64_t user_idnr, char *scriptname, char **errmsg);

int sort_connect(void);
int sort_disconnect(void);

int sort_get_cancelkeep(void);
const char * sort_get_mailbox(void);

#endif
