/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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

/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _PIPE_H
#define _PIPE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

/**
 * \brief reads incoming pipe until header is found 
 * \param blksize this will hold the size of the header when the function
 * returns successfully
 * \return 
 *      - pointer to header if successful
 *      - NULL otherwise 
 */
char *read_header(u64_t *blksize);

/**
 * \brief inserts a message in the database. The header of the message is 
 * supposed to be given. The rest of the message will be received using stdin
 * \param header header of the message
 * \param headersize size of the header
 * \param users list of users to sent the message to
 * \param returnpath From: addresses. Used for bouncing messages.
 * \param users_are_usernames if 0, the users list holds user_idnr, if 1 it
 * holds usernames
 * \param deliver_to_mailbox mailbox to deliver to
 * \param headerfields of header fields
 * \return 0
 */
int insert_messages(char *header, u64_t headersize,struct list *users, 
		    struct list *returnpath, int users_are_usernames, 
		    char *deliver_to_mailbox, struct list *headerfields);

#endif
