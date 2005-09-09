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
#include "dsn.h"
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

typedef enum DBMAIL_MESSAGE_FILTER_TYPES { 
	DBMAIL_MESSAGE_FILTER_FULL,
	DBMAIL_MESSAGE_FILTER_HEAD,
	DBMAIL_MESSAGE_FILTER_BODY
} message_filter_t;

enum DBMAIL_STREAM_TYPE {
	DBMAIL_STREAM_PIPE = 1,
	DBMAIL_STREAM_LMTP
};

struct DbmailMessage {
	u64_t id;
	u64_t physid;
	GString *internal_date;
	enum DBMAIL_MESSAGE_CLASS klass;
	GMimeObject *content;
	GRelation *headers;
	GHashTable *header_dict;
};

/*
 * initializers
 */

struct DbmailMessage * dbmail_message_new(void);
struct DbmailMessage * dbmail_message_new_from_stream(FILE *instream, int streamtype);
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, int type);
struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content);

/*
 * database facilities
 */

int dbmail_message_store(struct DbmailMessage *message);
int dbmail_message_headers_cache(struct DbmailMessage *message);

dsn_class_t sort_and_deliver(struct DbmailMessage *self, u64_t useridnr, const char *mailbox);

struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter);

/*
 * attribute accessors
 */
void dbmail_message_set_physid(struct DbmailMessage *self, u64_t physid);
u64_t dbmail_message_get_physid(struct DbmailMessage *self);

void dbmail_message_set_internal_date(struct DbmailMessage *self, char *internal_date);
gchar * dbmail_message_get_internal_date(struct DbmailMessage *self);
	
int dbmail_message_set_class(struct DbmailMessage *self, int klass);
int dbmail_message_get_class(struct DbmailMessage *self);

gchar * dbmail_message_to_string(struct DbmailMessage *self, gboolean crlf);
gchar * dbmail_message_hdrs_to_string(struct DbmailMessage *self, gboolean crlf);
gchar * dbmail_message_body_to_string(struct DbmailMessage *self, gboolean crlf);

size_t dbmail_message_get_size(struct DbmailMessage *self, gboolean crlf);

#define dbmail_message_get_rfcsize(x) dbmail_message_get_size(x, TRUE)

size_t dbmail_message_get_hdrs_size(struct DbmailMessage *self, gboolean crlf);
size_t dbmail_message_get_body_size(struct DbmailMessage *self, gboolean crlf);

/*
 * manipulate the actual message content
 */

void dbmail_message_set_header(struct DbmailMessage *self, const char *header, const char *value);
gchar * dbmail_message_get_header(struct DbmailMessage *self, const char *header);

void dbmail_message_cache_tofield(struct DbmailMessage *self);
void dbmail_message_cache_ccfield(struct DbmailMessage *self);
void dbmail_message_cache_fromfield(struct DbmailMessage *self);
void dbmail_message_cache_replytofield(struct DbmailMessage *self);
void dbmail_message_cache_datefield(struct DbmailMessage *self);
void dbmail_message_cache_subjectfield(struct DbmailMessage *self);
void dbmail_message_cache_referencesfield(struct DbmailMessage *self);

/*
 * destructor
 */

void dbmail_message_free(struct DbmailMessage *self);


/* move these elsewhere: */

#endif
