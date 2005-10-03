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

/* $Id: pipe.h 1891 2005-10-03 10:01:21Z paul $ 
 */

#ifndef _PIPE_H
#define _PIPE_H

#include "dbmail.h"

/**
 * \brief inserts a message in the database. The header of the message is 
 * supposed to be given. The rest of the message will be read from instream
 * \return 0
 */
int insert_messages(struct DbmailMessage *message,
		struct dm_list *headerfields, 
		struct dm_list *dsnusers,
		struct dm_list *returnpath);

/**
 * \brief discards all input coming from instream
 * \param instream FILE stream holding input from a client
 * \return 
 *      - -1 on error
 *      -  0 on success
 */
int discard_client_input(FILE * instream);

/**
 * store a messagebody (without headers in one or more blocks in the database
 * \param message the message
 * \param message_size size of message
 * \param msgidnr idnr of message
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int store_message_in_blocks(const char* message,
				   u64_t message_size,
				   u64_t msgidnr);


#endif
