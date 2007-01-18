/*
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

/* 
 * \file dbmail-message.h
 * 
 * \brief DbmailMessage class  
 *
 */

#ifndef _DBMAIL_MESSAGE_H
#define _DBMAIL_MESSAGE_H


#include "dbmail.h"

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
	DBMAIL_MESSAGE_FILTER_FULL = 1,
	DBMAIL_MESSAGE_FILTER_HEAD,
	DBMAIL_MESSAGE_FILTER_BODY
} message_filter_t;

typedef enum DBMAIL_STREAM_TYPE {
	DBMAIL_STREAM_PIPE = 1,
	DBMAIL_STREAM_LMTP,
	DBMAIL_STREAM_RAW
} dbmail_stream_t;

struct DbmailMessage {
	u64_t id;
	u64_t physid;
	time_t internal_date;
	int *internal_date_gmtoff;
	GString *envelope_recipient;
	enum DBMAIL_MESSAGE_CLASS klass;
	GByteArray *raw;
	GMimeObject *content;
	GRelation *headers;
	GHashTable *header_dict;
	GTree *header_name;
	GTree *header_value;
};

/*
 * initializers
 */

struct DbmailMessage * dbmail_message_new(void);
struct DbmailMessage * dbmail_message_new_from_stream(FILE *instream, int streamtype);
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type);
struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content);
struct DbmailMessage * dbmail_message_construct(struct DbmailMessage *self, 
		const gchar *sender, const gchar *recipient, 
		const gchar *subject, const gchar *body);

/*
 * database facilities
 */

int dbmail_message_store(struct DbmailMessage *message);
int dbmail_message_cache_headers(const struct DbmailMessage *message);

struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter);

/*
 * attribute accessors
 */
void dbmail_message_set_physid(struct DbmailMessage *self, u64_t physid);
u64_t dbmail_message_get_physid(const struct DbmailMessage *self);

void dbmail_message_set_envelope_recipient(struct DbmailMessage *self, const char *envelope);
gchar * dbmail_message_get_envelope_recipient(const struct DbmailMessage *self);
	
void dbmail_message_set_internal_date(struct DbmailMessage *self, char *internal_date);
gchar * dbmail_message_get_internal_date(const struct DbmailMessage *self, int thisyear);
	
int dbmail_message_set_class(struct DbmailMessage *self, int klass);
int dbmail_message_get_class(const struct DbmailMessage *self);

gchar * dbmail_message_to_string(const struct DbmailMessage *self);
gchar * dbmail_message_hdrs_to_string(const struct DbmailMessage *self);
gchar * dbmail_message_body_to_string(const struct DbmailMessage *self);

size_t dbmail_message_get_size(const struct DbmailMessage *self, gboolean crlf);

#define dbmail_message_get_rfcsize(x) dbmail_message_get_size(x, TRUE)

size_t dbmail_message_get_hdrs_size(const struct DbmailMessage *self, gboolean crlf);
size_t dbmail_message_get_body_size(const struct DbmailMessage *self, gboolean crlf);

GList * dbmail_message_get_header_addresses(struct DbmailMessage *message, const char *field);

#define get_crlf_encoded(string) get_crlf_encoded_opt(string, 0)
#define get_crlf_encoded_dots(string) get_crlf_encoded_opt(string, 1)
gchar * get_crlf_encoded_opt(const gchar *string, int dots);

/*
 * manipulate the actual message content
 */

void dbmail_message_set_header(struct DbmailMessage *self, const char *header, const char *value);
const gchar * dbmail_message_get_header(const struct DbmailMessage *self, const char *header);

/* Get all instances of a header. */
GTuples * dbmail_message_get_header_repeated(const struct DbmailMessage *self, const char *header);

void dbmail_message_cache_tofield(const struct DbmailMessage *self);
void dbmail_message_cache_ccfield(const struct DbmailMessage *self);
void dbmail_message_cache_fromfield(const struct DbmailMessage *self);
void dbmail_message_cache_replytofield(const struct DbmailMessage *self);
void dbmail_message_cache_datefield(const struct DbmailMessage *self);
void dbmail_message_cache_subjectfield(const struct DbmailMessage *self);
void dbmail_message_cache_referencesfield(const struct DbmailMessage *self);
void dbmail_message_cache_envelope(const struct DbmailMessage *self);

/*
 * destructor
 */

void dbmail_message_free(struct DbmailMessage *self);


/* move these elsewhere: */

char * g_mime_object_get_body(const GMimeObject *object);

/* moved here from dbmsgbuf.h */

/**
 * \brief initialises a message headers fetch
 * \param msg_idnr 
 * \return 
 *     - -1 on error
 *     -  0 if already inited (sic) before
 *     -  1 on success
 */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter);

#define db_init_fetch_headers(x) db_init_fetch(x,DBMAIL_MESSAGE_FILTER_HEAD)
#define db_init_fetch_message(x) db_init_fetch(x,DBMAIL_MESSAGE_FILTER_FULL)

#endif
