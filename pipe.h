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

/* $Id$ 
 */

#ifndef _PIPE_H
#define _PIPE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

/**
 * \brief inserts a message in the database. The header of the message is 
 * supposed to be given. The rest of the message will be read from instream
 * \param instream is a FILE stream where the rest of the message is
 * \param header header of the message
 * \param headersize size of the header
 * \param headerrfcsize rfc size of the header (newlines counted twice)
 * \param users list of users to sent the message to
 * \param errusers list of users who didn't work for some reason
 * \param returnpath From: addresses. Used for bouncing messages.
 * \param users_are_usernames if 0, the users list holds user_idnr, if 1 it
 * holds usernames
 * \param deliver_to_mailbox mailbox to deliver to
 * \param headerfields list of header fields
 * \return 0
 */
int insert_messages(FILE * instream,
		    char *header, u64_t headersize, u64_t headerrfcsize,
		    struct list *headerfields, struct list *dsnusers,
		    struct list *returnpath);

/**
 * \brief discards all input coming from instream
 * \param instream FILE stream holding input from a client
 */
void discard_client_input(FILE * instream);

#endif
