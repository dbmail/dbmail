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
 * \file dbmail-message.h
 * 
 * \brief DbmailMessage class  
 *
 */

#ifndef _DBMAIL_MESSAGE_H
#define _DBMAIL_MESSAGE_H


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"
#include <gmime/gmime.h>
#include <assert.h>

#define MSGBUF_FORCE_UPDATE -1

struct DbmailMessage {
	gulong id;
	size_t size;
	size_t rfcsize;
	GMimeMessage *message;
	GHashTable *headers;
};

enum DBMAIL_MESSAGE_FILTER_TYPES { 
	DBMAIL_MESSAGE_FILTER_FULL,
	DBMAIL_MESSAGE_FILTER_HEAD
};

struct DbmailMessage * dbmail_message_new(void);
struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t id, int filter);
struct DbmailMessage * dbmail_message_init(struct DbmailMessage *self, const GString *message);
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream);
char * dbmail_message_get_headers_as_string(struct DbmailMessage *self);
char * dbmail_message_get_body_as_string(struct DbmailMessage *self);
size_t dbmail_message_get_rfcsize(struct DbmailMessage *self);
void dbmail_message_destroy(struct DbmailMessage *self);

#endif
