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

/* 
 * preliminary goal is to provide an interface between dbmail and gmime.
 *
 * ambition is facilitation of language-bindings.
 *
 * very much unfinished, work in progress, subject to change, etc...
 *
 */

enum DBMAIL_MESSAGE_CLASS {
	DBMAIL_MESSAGE,
	DBMAIL_MESSAGE_PART
};

enum DBMAIL_MESSAGE_FILTER_TYPES { 
	DBMAIL_MESSAGE_FILTER_FULL,
	DBMAIL_MESSAGE_FILTER_HEAD
};


struct DbmailMessage {
	gulong id;
	enum DBMAIL_MESSAGE_CLASS klass;
	size_t size;
	size_t rfcsize;
	GMimeObject *content;
	GRelation *headers;
};

struct DbmailMessage * dbmail_message_new(void);
void dbmail_message_set_class(struct DbmailMessage *self, int klass);
struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t id, int filter);
struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content);
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream);
gchar * dbmail_message_get_headers_as_string(struct DbmailMessage *self);
gchar * dbmail_message_get_body_as_string(struct DbmailMessage *self);
size_t dbmail_message_get_rfcsize(struct DbmailMessage *self);
void dbmail_message_destroy(struct DbmailMessage *self);

/*
 *
 * OLD Stuff. we will assimilate you.
 *
 */


/**
 * split the whole message into header and body
 * \param[in] whole_message the whole message, including header
 * \param[in] whole_message_size size of whole_message.
 * \param[out] header will hold header 
 * \param[out] header_size size of header
 * \param[out] header_rfcsize rfc size of header
 * \param[out] body will hold body
 * \param[out] body_size size of body
 * \param[out] body_rfcsize rfc size of body
 */
int split_message(const char *whole_message, 
		  char **header, u64_t *header_size,
		  const char **body, u64_t *body_size,
		  u64_t *body_rfcsize);


#endif
