/*
  $Id: dbmsgbuf.c 1891 2005-10-03 10:01:21Z paul $

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

//#define MSGBUF_WINDOWSIZE (128ull*1024ull)

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

/**
 * CONDITIONS FOR MSGBUF
 */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter)
{
	struct DbmailMessage *msg;

	int result;
	u64_t physid = 0;
	if ((result = db_get_physmessage_id(msg_idnr, &physid)) != DM_SUCCESS)
		return NULL;
	msg = dbmail_message_new();
	if (! (msg = dbmail_message_retrieve(msg, physid, filter)))
		return NULL;

	db_store_msgbuf_result();

	return msg;
}
	
int db_update_msgbuf(void)
{
	/* use the former msgbuf_result */
	db_use_msgbuf_result();
	db_store_msgbuf_result();
	return 1;
}
