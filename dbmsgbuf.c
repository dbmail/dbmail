/*
  $Id$

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

/**
 * \file dbmsgbuf.c
 *
 * implement msgbuf functions prototyped in dbmsgbuf.h
 */

#include "dbmail.h"

#define MSGBUF_WINDOWSIZE (128ull*1024ull)

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

static int _msg_fetch_inited = 0;

/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

/**
 * CONDITIONS FOR MSGBUF
 */
static u64_t rowpos = 0; /**< current position in row */
static db_pos_t zeropos; /**< absolute position (block/offset) of 
			    msgbuf_buf[0]*/
static unsigned nblocks = 0; /**< number of block  */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter)
{
	struct DbmailMessage *msg;

	int result;
	u64_t physid = 0;
	result = db_get_physmessage_id(msg_idnr, &physid);
	if (result != DM_SUCCESS)
		return NULL;
	msg = dbmail_message_new();
	return dbmail_message_retrieve(msg, physid, filter);
}
	
struct DbmailMessage * db_init_fetch_message(u64_t msg_idnr, int filter)
{

	struct DbmailMessage *msg;
	char *buf;

	msg = db_init_fetch(msg_idnr, filter);
	if (! msg)
		return NULL;
	
	/* set globals: msgbuf contains a crlf decoded version of the message at hand */
	buf = dbmail_message_to_string(msg);
	msgbuf_buf = buf;

	msgbuf_idx = 0;
	msgbuf_buflen = strlen(msgbuf_buf);
	db_store_msgbuf_result();

	/* done */
	return msg;
}


int db_update_msgbuf(int minlen UNUSED)
{
	/* use the former msgbuf_result */
	db_use_msgbuf_result();
	db_store_msgbuf_result();
	return 1;
}

void db_close_msgfetch()
{
	if (!_msg_fetch_inited)
		return;		/* nothing to be done */

	dm_free(msgbuf_buf);
	msgbuf_buf = NULL;

	nblocks = 0;
	/* make sure the right result set is freed and restore the
	 * old one after that.*/
	db_use_msgbuf_result();
	db_free_result();
	db_store_msgbuf_result();
	_msg_fetch_inited = 0;
}

void db_give_msgpos(db_pos_t * pos)
{
	if (msgbuf_idx >= ((msgbuf_buflen) - rowpos)) {
		pos->block = zeropos.block + 1;
		pos->pos = msgbuf_idx - ((msgbuf_buflen) - rowpos);
	} else {
		pos->block = zeropos.block;
		pos->pos = zeropos.pos + msgbuf_idx;
	}
}

u64_t db_give_range_size(db_pos_t * start, db_pos_t * end)
{
	unsigned i;
	u64_t size;

	if (start->block > end->block)
		return 0;	/* bad range */

	if (start->block >= nblocks || end->block >= nblocks)
		return 0;	/* bad range */

	if (start->block == end->block)
		return (start->pos >
			end->pos) ? 0 : (end->pos - start->pos + 1);

	/* use the former msgbuf result */
	db_use_msgbuf_result();

	if (start->pos > db_get_length(start->block, 0) ||
	    end->pos > db_get_length(end->block, 0)) {
		db_store_msgbuf_result();
		return 0;	/* bad range */
	}

	size = db_get_length(start->block, 0) - start->pos;

	for (i = start->block + 1; i < end->block; i++)
		size += db_get_length(i, 0);

	size += end->pos;
	size++;

	/* store the result again.. */
	db_store_msgbuf_result();
	return size;
}

