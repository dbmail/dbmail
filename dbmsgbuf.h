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

/** 
 * \file dbmsgbuf.h
 * 
 * \brief datatypes & function prototypes for the msgbuf system
 * using a mysql database
 *
 * \date August 20, 2003 changed to doxygen
 */

#ifndef _DBMSGBUF_H
#define _DBMSGBUF_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"
#include "memblock.h"

#define MSGBUF_FORCE_UPDATE -1

char *msgbuf_buf;/**< the message buffer */
u64_t msgbuf_idx;/**< index within msgbuf, 0 <= msgidx < buflen */
u64_t msgbuf_buflen;/**< current buffer length: msgbuf[buflen] == '\\0' */

/**
 * \brief initialises a message fetch
 * \param msg_idnr 
 * \return 
 *     - -1 on error
 *     -  0 if already inited (sic) before
 *     -  1 on success
 */
int db_init_fetch_message(u64_t msg_idnr);

/**
 * \brief initialises a message headers fetch
 * \param msg_idnr 
 * \return 
 *     - -1 on error
 *     -  0 if already inited (sic) before
 *     -  1 on success
 */
int db_init_fetch_headers(u64_t msg_idnr);


/**
 * \brief update msgbuf
 * \param minlen if < 0, update is forced, otherwise only if there are
 *        less than minlen characters left in msgbuf
 * \return
 *      - -1 on error
 *      -  0 if no more chars in rows
 *      -  1 on success
 */
int db_update_msgbuf(int minlen);

/**
 * \brief finishes a message fetch
 */
void db_close_msgfetch(void);

/**
 * \brief get position in message
 * \param pos pointer to db_pos_t which will hold the position
 */
void db_give_msgpos(db_pos_t * pos);

/**
 * \brief determines number of bytes between start and end position
 * \param start start position
 * \param end end position
 * \return number of bytes between positions
 * \pre _msg_result must contain a valid result set for return value
 *      to be valid
 */
u64_t db_give_range_size(db_pos_t * start, db_pos_t * end);

/**
 * \brief dump a range specified by start,end for the message with
 *        message_idnr msg_idnr
 * \param outmem memory to write to
 * \param start start position
 * \param end end position
 * \param msg_idnr message idnr
 * \return
 *    - -1 on error
 *    - number of bytes written to outmem otherwise
 */
long db_dump_range(MEM * outmem, db_pos_t start, db_pos_t end,
		   u64_t msg_idnr);

#endif
