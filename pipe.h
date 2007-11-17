/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

#ifndef _PIPE_H
#define _PIPE_H

#include "dbmail.h"

/**
 * \brief Inserts a message in the database.
 * \return 0
 */
int insert_messages(struct DbmailMessage *message, GList *dsnusers);

/**
 * \brief Store a messagebody into the database,
 *        breaking it into blocks as needed.
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

int send_vacation(struct DbmailMessage *message,
		const char *to, const char *from,
		const char *subject, const char *body, const char *handle);
int send_redirect(struct DbmailMessage *message,
		const char *to, const char *from);
int send_forward_list(struct DbmailMessage *message, GList *targets, const char *from);
int send_alert(u64_t user_idnr, char *subject, char *body);

#endif
