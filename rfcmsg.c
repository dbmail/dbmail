/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

/*
 * $Id: rfcmsg.c 1891 2005-10-03 10:01:21Z paul $
 * function implementations for parsing an RFC822/MIME 
 * compliant mail message
 */


#include "dbmail.h"

/* 
 * initialize all the memory associated with a msg
 */
mime_message_t * db_new_msg(void)
{
	mime_message_t *m = g_new0(mime_message_t,1);

	dm_list_init(&m->children);
	dm_list_init(&m->rfcheader);
	dm_list_init(&m->mimeheader);

	return m;

}

/* 
 * frees all the memory associated with a msg
 */
void db_free_msg(mime_message_t * msg)
{
	struct element *tmp;

	if (!msg)
		return;

	/* free the children msg's */
	tmp = dm_list_getstart(&msg->children);

	while (tmp) {
		db_free_msg((mime_message_t *) tmp->data);
		tmp = tmp->nextnode;
	}

	tmp = dm_list_getstart(&msg->children);
	dm_list_free(&tmp);

	tmp = dm_list_getstart(&msg->mimeheader);
	dm_list_free(&tmp);

	tmp = dm_list_getstart(&msg->rfcheader);
	dm_list_free(&tmp);

	memset(msg, 0, sizeof(*msg));
}


/* 
 * reverses the children lists of a msg
 */
static void db_reverse_msg(mime_message_t * msg)
{
	struct element *tmp;

	if (!msg)
		return;

	/* reverse the children msg's */
	tmp = dm_list_getstart(&msg->children);

	while (tmp) {
		db_reverse_msg((mime_message_t *) tmp->data);
		tmp = tmp->nextnode;
	}

	/* reverse this list */
	msg->children.start = dm_list_reverse(msg->children.start);

	/* reverse header items */
	msg->mimeheader.start = dm_list_reverse(msg->mimeheader.start);
	msg->rfcheader.start = dm_list_reverse(msg->rfcheader.start);
}

/*
 * db_fetch_headers()
 *
 * builds up an array containing message headers
 *
 * creates a linked-list of headers found
 *
 * returns:
 * -3 memory error
 * -2 dbase error
 * -1 parse error but msg is retrieved as plaintext
 *  0 success
 */
int db_fetch_headers(u64_t msguid, mime_message_t * msg)
{
	struct DbmailMessage *message;
	int hdrlines;

	if (!(message = db_init_fetch_message(msguid, DBMAIL_MESSAGE_FILTER_FULL))) 
		return -2;

	if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
		return -2;

	if ((hdrlines = mime_readheader(message, &msgbuf_idx, &msg->rfcheader, &msg->rfcheadersize)) < 0)
		return hdrlines;

	msg->rfcheaderlines = hdrlines;

	db_reverse_msg(msg);
	db_close_msgfetch();
	return 0;
}

