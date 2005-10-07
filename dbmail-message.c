/*
  $Id$

  Copyright (C) 2004-2005 NFG Net Facilities Group BV, info@nfg.nl

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
 * \file dbmail-message.c
 *
 * implements DbmailMessage object
 */

#include "dbmail.h"


extern db_param_t _db_params;
#define DBPFX _db_params.pfx

#define MESSAGE_MAX_LINE_SIZE 1024
/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

static int dm_errno = 0;

#define DBMAIL_TEMPMBOX "INBOX"
/*
 * _register_header
 *
 * register a message header in a ghashtable dictionary
 *
 */
static void _register_header(const char *header, const char *value, gpointer user_data);
static void _header_cache(const char *header, const char *value, gpointer user_data);

static struct DbmailMessage * _retrieve(struct DbmailMessage *self, char *query_template);
static void _map_headers(struct DbmailMessage *self);
static void _set_content(struct DbmailMessage *self, const GString *content);
static void _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, int type);
static int _message_insert(struct DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id); 


/* general mime utils (missing from gmime?) */

gchar * g_mime_object_get_body(const GMimeObject *object)
{
	gchar *s = NULL;
        size_t i;
	GString *t;
	
        s = g_mime_object_get_headers(GMIME_OBJECT(object));
        i = strlen(s);
        g_free(s);
	
	s = g_mime_object_to_string(GMIME_OBJECT(object));
	t = g_string_new(s);
	
	if (t->len > i && s[i] == '\n')
		i++;

	g_free(s);
	
	t = g_string_erase(t,0,i);
	
	s=t->str;
	g_string_free(t,FALSE);
	
	return s;
}

gchar * get_crlf_encoded(gchar *string)
{
	GMimeStream *ostream, *fstream;
	GMimeFilter *filter;
	gchar *encoded, *buf;
	GString *raw;
	
	ostream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	
	g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
	g_mime_stream_write_string(fstream,string);
	
	g_object_unref(filter);
	g_object_unref(fstream);
	
	g_mime_stream_reset(ostream);

	raw = g_string_new("");
	buf = g_new0(char,256);
	while ((g_mime_stream_read(ostream, buf, 255)) > 0) {
		raw = g_string_append(raw, buf);
		memset(buf,'\0', 256);
	}
	
	g_object_unref(ostream);
	
	encoded = raw->str;
	g_string_free(raw,FALSE);
	g_free(buf);
	
	return encoded;

}




/*  \brief create a new empty DbmailMessage struct
 *  \return the DbmailMessage
 */
struct DbmailMessage * dbmail_message_new(void)
{
	struct DbmailMessage *self = g_new0(struct DbmailMessage,1);
	if (! self) {
		trace(TRACE_ERROR, "%s,%s: memory error", __FILE__, __func__);
		return NULL;
	}
	
	self->internal_date = g_string_new("");
	
	self->header_dict = g_hash_table_new_full((GHashFunc)g_str_hash, (GEqualFunc)g_str_equal, (GDestroyNotify)g_free, NULL);
	
	dbmail_message_set_class(self, DBMAIL_MESSAGE);
	return self;
}

void dbmail_message_free(struct DbmailMessage *self)
{
	if (self->headers)
		g_relation_destroy(self->headers);
	if (self->content)
		g_object_unref(self->content);
	
	self->headers=NULL;
	self->content=NULL;
	
	g_string_free(self->internal_date,TRUE);
	g_hash_table_destroy(self->header_dict);
	
	self->id=0;
	dm_free(self);
}


/* \brief create and initialize a new DbmailMessage
 * \param FILE *instream from which to read
 * \param int streamtype is DBMAIL_STREAM_PIPE or DBMAIL_STREAM_LMTP
 * \return the new DbmailMessage
 */
struct DbmailMessage * dbmail_message_new_from_stream(FILE *instream, int streamtype) 
{
	
	GMimeStream *stream;
	struct DbmailMessage *message;
	
	assert(instream);
	message = dbmail_message_new();
	stream = g_mime_stream_fs_new(dup(fileno(instream)));
	message = dbmail_message_init_with_stream(message, stream, streamtype);
	g_object_unref(stream);
	return message;
}

/* \brief set the type flag for this DbmailMessage
 * \param the DbmailMessage on which to set the flag
 * \param type flag is either DBMAIL_MESSAGE or DBMAIL_MESSAGE_PART
 * \return non-zero in case of error
 */
int dbmail_message_set_class(struct DbmailMessage *self, int klass)
{
	switch (klass) {
		case DBMAIL_MESSAGE:
		case DBMAIL_MESSAGE_PART:
			self->klass = klass;
			break;
		default:
			return 1;
			break;
	}		
	return 0;
			
}

/* \brief accessor for the type flag
 * \return the flag
 */
int dbmail_message_get_class(struct DbmailMessage *self)
{
	return self->klass;
}

/* \brief initialize a previously created DbmailMessage using a GString
 * \param the empty DbmailMessage
 * \param GString *content contains the raw message
 * \return the filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content)
{
	_set_content(self,content);
	/* If there's no From header and no Subject assume it's a message-part and re-init */
	if (dbmail_message_get_class(self) == DBMAIL_MESSAGE) {
		if ((! g_mime_message_get_header(GMIME_MESSAGE(self->content),"From")) && 
				(! g_mime_message_get_header(GMIME_MESSAGE(self->content),"Subject"))) {
			dbmail_message_set_class(self, DBMAIL_MESSAGE_PART);
			g_object_unref(self->content);
			self->content=NULL;
			_set_content(self, content);
		}
	}
	_map_headers(self);
	
	return self;
}

/* \brief initialize a previously created DbmailMessage using a GMimeStream
 * \param empty DbmailMessage
 * \param stream from which to read
 * \param type which indicates either pipe/network style streaming
 * \return the filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, int type)
{
	_set_content_from_stream(self,stream,type);
	_map_headers(self);
	return self;
}

static void _set_content(struct DbmailMessage *self, const GString *content)
{
	GMimeStream *stream = g_mime_stream_mem_new_with_buffer(content->str, content->len);
	_set_content_from_stream(self, stream, DBMAIL_STREAM_PIPE);
	g_object_unref(stream);
}
static void _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, int type)
{
	/* 
	 * We convert all messages to crlf->lf for internal usage and
	 * db-insertion
	 */
	
	GMimeStream *ostream, *fstream, *bstream, *mstream;
	GMimeFilter *filter;
	GMimeParser *parser;
	char *buf = g_new0(char, MESSAGE_MAX_LINE_SIZE);

	/*
	 * buildup the memory stream buffer
	 * we will read from stream until either EOF or <dot><crlf> is encountered
	 * depending on the streamtype
	 */
        bstream = g_mime_stream_buffer_new(stream,GMIME_STREAM_BUFFER_CACHE_READ);
	mstream = g_mime_stream_mem_new();
        while (g_mime_stream_buffer_gets(bstream, buf, MESSAGE_MAX_LINE_SIZE)) {
                if ((type==DBMAIL_STREAM_LMTP) && (strncmp(buf,".\r\n",3)==0))
			break;
		g_mime_stream_write_string(mstream, buf);
	}
	g_mime_stream_reset(mstream);
	g_free(buf);
	
	/* 
	 * filter mstream by decoding crlf and dot lines
	 * ostream will hold the decoded data
	 */
	ostream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
	g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
	g_mime_stream_write_to_stream(mstream,fstream);
	g_mime_stream_reset(ostream);
	
	/*
	 * finally construct a message by parsing ostream
	 */
	parser = g_mime_parser_new_with_stream(ostream);

	switch (dbmail_message_get_class(self)) {
		case DBMAIL_MESSAGE:
			self->content = GMIME_OBJECT(g_mime_parser_construct_message(parser));
			break;
		case DBMAIL_MESSAGE_PART:
			self->content = GMIME_OBJECT(g_mime_parser_construct_part(parser));
			break;
	}
			
	
	g_object_unref(filter);
	g_object_unref(fstream);
	g_object_unref(ostream);
	g_object_unref(bstream);
	g_object_unref(mstream);
	g_object_unref(parser);
}

static void _map_headers(struct DbmailMessage *self) 
{
	GRelation *rel = g_relation_new(2);
	assert(self->content);
	g_relation_index(rel, 0, g_str_hash, g_str_equal);
	g_mime_header_foreach(GMIME_OBJECT(self->content)->headers, _register_header, rel);
	self->headers = rel;
}

static void _register_header(const char *header, const char *value, gpointer user_data)
{
	g_relation_insert((GRelation *)user_data, (gpointer)header, (gpointer)value);
}

void dbmail_message_set_physid(struct DbmailMessage *self, u64_t physid)
{
	self->physid = physid;
}
u64_t dbmail_message_get_physid(struct DbmailMessage *self)
{
	return self->physid;
}

void dbmail_message_set_internal_date(struct DbmailMessage *self, char *internal_date)
{
	if (internal_date)
		g_string_printf(self->internal_date,"%s", internal_date);
}

gchar * dbmail_message_get_internal_date(struct DbmailMessage *self)
{
	if (self->internal_date->len > 0)
		return self->internal_date->str;
	return NULL;
}


void dbmail_message_set_header(struct DbmailMessage *self, const char *header, const char *value)
{
	g_mime_message_set_header(GMIME_MESSAGE(self->content), header, value);
}

gchar * dbmail_message_get_header(struct DbmailMessage *self, const char *header)
{
	return (gchar *)g_mime_object_get_header(GMIME_OBJECT(self->content), header);
}

/* dump message(parts) to char ptrs */
gchar * dbmail_message_to_string(struct DbmailMessage *self) 
{
	return g_mime_object_to_string(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_hdrs_to_string(struct DbmailMessage *self)
{
	gchar *h;
	GString *hs;

	h = g_mime_object_get_headers(GMIME_OBJECT(self->content));
	hs = g_string_new(h);
	g_free(h);

	/* append newline */
	hs = g_string_append_c(hs, '\n');
	h = hs->str;
	g_string_free(hs,FALSE);
	return h;
	
}
gchar * dbmail_message_body_to_string(struct DbmailMessage *self)
{
	return g_mime_object_get_body(GMIME_OBJECT(self->content));
}

/* 
 * Some dynamic accessors.
 * 
 * Don't cache these values to allow changes in message content!!
 * 
 */
size_t dbmail_message_get_size(struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_to_string(self);

        if (crlf) {
		t = get_crlf_encoded(s);
		r = strlen(t);
		g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}
size_t dbmail_message_get_hdrs_size(struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_hdrs_to_string(self);

	if (crlf) {
	        t = get_crlf_encoded(s);
		r = strlen(t);
        	g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}
size_t dbmail_message_get_body_size(struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_body_to_string(self);

	if (crlf) {
		t = get_crlf_encoded(s);
		r = strlen(t);
        	g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}


static struct DbmailMessage * _retrieve(struct DbmailMessage *self, char *query_template)
{
	
	int row = 0, rows = 0;
	GString *message = g_string_new("");
	
	assert(dbmail_message_get_physid(self));
	
	snprintf(query, DEF_QUERYSIZE, query_template, DBPFX, dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: sql error", __FILE__, __func__);
		return NULL;
	}
	
	rows = db_num_rows();
	
	if (rows < 1) {
		trace(TRACE_ERROR, "%s,%s: blk error [%d]", __FILE__, __func__, rows);
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	for (row=0; row < rows; row++)
		message = g_string_append(message, db_get_result(row, 0));

	db_free_result();
	
	return dbmail_message_init_with_string(self,message);
}

/*
 *
 * retrieve the header messageblk
 *
 * TODO: this call is yet unused in the code, but here for
 * forward compatibility's sake.
 *
 */
static struct DbmailMessage * _fetch_head(struct DbmailMessage *self)
{
	char *query_template = 	"SELECT messageblk "
		"FROM %smessageblks "
		"WHERE physmessage_id = '%llu' "
		"AND is_header = '1'";
	return _retrieve(self, query_template);

}

/*
 *
 * retrieve the full message
 *
 */
static struct DbmailMessage * _fetch_full(struct DbmailMessage *self) 
{
	char *query_template = "SELECT messageblk "
		"FROM %smessageblks "
		"WHERE physmessage_id = '%llu' "
		"ORDER BY messageblk_idnr";
	return _retrieve(self, query_template);
}

/* \brief retrieve message
 * \param empty DbmailMessage
 * \param physmessage_id
 * \param filter (header-only or full message)
 * \return filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter)
{
	assert(physid);
	
	dbmail_message_set_physid(self, physid);
	
	switch (filter) {
		case DBMAIL_MESSAGE_FILTER_HEAD:
			self = _fetch_head(self);
			break;

		case DBMAIL_MESSAGE_FILTER_BODY:
		case DBMAIL_MESSAGE_FILTER_FULL:
			self = _fetch_full(self);
			break;
	}
	
	if ((!self) || (! self->content)) {
		trace(TRACE_ERROR, 
				"%s,%s: retrieval failed for physid [%llu]", 
				__FILE__, __func__, dbmail_message_get_physid(self)
				);
		return NULL;
	}

	return self;
}


/* \brief store a temporary copy of a message.
 * \param 	filled DbmailMessage
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int dbmail_message_store(struct DbmailMessage *self)
{
	u64_t user_idnr;
	u64_t messageblk_idnr;
	char unique_id[UID_SIZE];
	char *hdrs, *body;
	u64_t hdrs_size, body_size, rfcsize;
	
	switch (auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr)) {
	case -1:
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user " "[%s]\n",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
		break;
	case 0:
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user "
		      "[%s]. Make sure this system user is in the database!\n",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
		break;
	}
	
	create_unique_id(unique_id, user_idnr);
	/* create a message record */
	if(_message_insert(self, user_idnr, DBMAIL_TEMPMBOX, unique_id) < 0)
		return -1;

	hdrs = dbmail_message_hdrs_to_string(self);
	body = dbmail_message_body_to_string(self);
	hdrs_size = (u64_t)dbmail_message_get_hdrs_size(self, FALSE);
	body_size = (u64_t)dbmail_message_get_body_size(self, FALSE);
	rfcsize = (u64_t)dbmail_message_get_rfcsize(self);
	
	if(db_insert_message_block(hdrs, hdrs_size, self->id, &messageblk_idnr,1) < 0)
		return -1;
	
	trace(TRACE_DEBUG, "%s,%s: allocating [%ld] bytes of memory "
	      "for readblock", __FILE__, __func__, READ_BLOCK_SIZE);
	
	/* store body in several blocks (if needed */
	if (store_message_in_blocks(body, body_size, self->id) < 0)
		return -1;

	if (db_update_message(self->id, unique_id, (hdrs_size + body_size), rfcsize) < 0) 
		return -1;

	/* store message headers */
	if (dbmail_message_headers_cache(self) < 0)
		return -1;

	g_free(hdrs);
	g_free(body);

	return 1;
}

int _message_insert(struct DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id)
{
	u64_t mailboxid;
	u64_t physmessage_id;
	char *internal_date = NULL;
	char *physid = g_new0(char, 16);

	assert(unique_id);

	if (!mailbox)
		mailbox = dm_strdup("INBOX");

	if (db_find_create_mailbox(mailbox, user_idnr, &mailboxid) == -1)
		return -1;
	
	if (mailboxid == 0) {
		trace(TRACE_ERROR, "%s,%s: mailbox [%s] could not be found!", 
				__FILE__, __func__, mailbox);
		return -1;
	}

	
	/* insert a new physmessage entry */
	internal_date = dbmail_message_get_internal_date(self);
	if (db_insert_physmessage_with_internal_date(internal_date, &physmessage_id) == -1) 
		return -1;

	/* insert the physmessage-id into the message-headers */
	g_snprintf(physid, 16, "%llu", physmessage_id);
	dbmail_message_set_physid(self, physmessage_id);
	dbmail_message_set_header(self, "X-DBMail-PhysMessage-ID", physid);
	g_free(physid);
	
	/* now insert an entry into the messages table */
	snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
		 "%smessages(mailbox_idnr, physmessage_id, unique_id,"
		 "recent_flag, status) "
		 "VALUES ('%llu', '%llu', '%s', '1', '%d')",
		 DBPFX, mailboxid, physmessage_id, unique_id,
		 MESSAGE_STATUS_INSERT);

	if (db_query(query) == -1) {
		trace(TRACE_STOP, "%s,%s: query failed", __FILE__, __func__);
		return -1;
	}

	self->id = db_insert_result("message_idnr");
	return 1;
}



int dbmail_message_headers_cache(struct DbmailMessage *self)
{
	assert(self);
	assert(self->physid);
	g_mime_header_foreach(GMIME_OBJECT(self->content)->headers, _header_cache, self);
	
	dbmail_message_cache_tofield(self);
	dbmail_message_cache_ccfield(self);
	dbmail_message_cache_fromfield(self);
	dbmail_message_cache_datefield(self);
	dbmail_message_cache_replytofield(self);
	dbmail_message_cache_subjectfield(self);
	dbmail_message_cache_referencesfield(self);
	
	return 1;
}

static int _header_get_id(struct DbmailMessage *self, const char *header, u64_t *id)
{
	u64_t tmp;
	gpointer cacheid;
	cacheid = g_hash_table_lookup(self->header_dict, (gconstpointer)header);

	if (cacheid) {
		*id = GPOINTER_TO_UINT(cacheid);
		return 1;
	}
	
	GString *q = g_string_new("");
	g_string_printf(q, "SELECT id FROM %sheadername WHERE headername='%s'", DBPFX, header);
	if (db_query(q->str) == -1) {
		g_string_free(q,TRUE);
		return -1;
	}
	if (db_num_rows() < 1) {
		g_string_printf(q, "INSERT INTO %sheadername (headername) VALUES ('%s')", DBPFX, header);
		if (db_query(q->str) == -1) {
			g_string_free(q,TRUE);
			return -1;
		}
		tmp = db_insert_result("headername_idnr");
	} else {
		tmp = db_get_result_u64(0,0);
		db_free_result();
	}
	*id = tmp;
	g_hash_table_insert(self->header_dict, (gpointer)(g_strdup(header)), GUINT_TO_POINTER((unsigned)tmp));
	g_string_free(q,TRUE);
	return 1;
}

void _header_cache(const char *header, const char *value, gpointer user_data)
{
	u64_t id;
	struct DbmailMessage *self = (struct DbmailMessage *)user_data;
	GString *q;
	char *safe_value = NULL;
	char *clean_value = NULL;
	
	dm_errno = 0;
	
	/* skip headernames with spaces like From_ */
	if (strchr(header, ' '))
		return;
	
	if ((_header_get_id(self, header, &id) < 0))
		return;
	
	clean_value = g_strdup(value);
	if (! (safe_value = dm_stresc(clean_value))) {
		g_free(clean_value);	
		return;
	}

	/* clip oversized headervalues */
	if (strlen(safe_value) >= 255)
		safe_value[255] = '\0';
	
	
	q = g_string_new("");
	
	g_string_printf(q,"INSERT INTO %sheadervalue (headername_id, physmessage_id, headervalue) "
			"VALUES (%llu,%llu,'%s')", DBPFX, id, self->physid, safe_value);
	
	if (db_query(q->str)) {
		/* ignore possible duplicate key collisions */
		trace(TRACE_WARNING,"%s,%s: insert  headervalue failed", __FILE__,__func__);
	}
	g_string_free(q,TRUE);
	g_free(safe_value);
	g_free(clean_value);
}

static void insert_address_cache(u64_t physid, const char *field, InternetAddressList *ialist)
{
	InternetAddress *ia;
	GString *q = g_string_new("");
	
	g_return_if_fail(ialist != NULL);
	
	gchar *safe_name;
	gchar *safe_addr;
	
	while (ialist->address) {
		
		ia = ialist->address;
		g_return_if_fail(ia != NULL);
	
		safe_name = dm_stresc(ia->name ? ia->name : "");
		safe_addr = dm_stresc(ia->value.addr ? ia->value.addr : "");
		
		g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sname, %saddr) "
				"VALUES (%llu,'%s','%s')", DBPFX, field, field, field, 
				physid, safe_name , safe_addr);
		
		g_free(safe_name);
		g_free(safe_addr);
		
		if (db_query(q->str)) 
			trace(TRACE_WARNING, "%s,%s: insert %sfield failed [%s]",
					__FILE__, __func__, field, q->str);

		if (ialist->next == NULL)
			break;
		
		ialist = ialist->next;
	}
	
	g_string_free(q,TRUE);
}

static void insert_field_cache(u64_t physid, const char *field, const char *value)
{
	GString *q = g_string_new("");
	gchar *safe_value;

	safe_value = dm_stresc(value);
	
	g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sfield) "
			"VALUES (%llu,'%s')", DBPFX, field, field, physid, 
			safe_value);

	g_free(safe_value);
	
	if (db_query(q->str)) 
		trace(TRACE_WARNING, "%s,%s: insert %sfield failed [%s]",
				__FILE__, __func__, field, q->str);
	g_string_free(q,TRUE);
}


void dbmail_message_cache_tofield(struct DbmailMessage *self)
{
	InternetAddressList *list;

	list = (InternetAddressList *)g_mime_message_get_recipients((GMimeMessage *)(self->content), GMIME_RECIPIENT_TYPE_TO);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "to", list);
}

void dbmail_message_cache_ccfield(struct DbmailMessage *self)
{
	InternetAddressList *list;
	
	list = (InternetAddressList *)g_mime_message_get_recipients((GMimeMessage *)(self->content), GMIME_RECIPIENT_TYPE_CC);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "cc", list);
	
}
void dbmail_message_cache_fromfield(struct DbmailMessage *self)
{
	const char *addr;
	InternetAddressList *list;

	addr = g_mime_message_get_sender((GMimeMessage *)(self->content));
	list = internet_address_parse_string(addr);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "from", list);
	internet_address_list_destroy(list);

}
void dbmail_message_cache_replytofield(struct DbmailMessage *self)
{
	const char *addr;
	InternetAddressList *list;

	addr = g_mime_message_get_reply_to((GMimeMessage *)(self->content));
	list = internet_address_parse_string(addr);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "replyto", list);
	internet_address_list_destroy(list);

}

void dbmail_message_cache_datefield(struct DbmailMessage *self)
{
	char *value;
	time_t date;

	if (! (value = dbmail_message_get_header(self,"Date")))
		date = (time_t)0;
	else
		date = g_mime_utils_header_decode_date(value,NULL);
	
	if (date == (time_t)-1)
		date = (time_t)0;

	value = g_new0(char,20);
	strftime(value,20,"%Y-%m-%d %H:%M:%S",gmtime(&date));

	insert_field_cache(self->physid, "date", value);
	
	g_free(value);
}

void dbmail_message_cache_subjectfield(struct DbmailMessage *self)
{
	char *value;
	char *subject;
	
	value = dbmail_message_get_header(self,"Subject");
	if (! value) {
		trace(TRACE_WARNING,"%s,%s: no subject field value [%llu]",
				__FILE__, __func__, self->physid);
		return;
	}
	
	subject = dm_stresc(value);
	if (!subject)
		return;

	dm_base_subject(subject);

	if (strlen(subject)>255)
		subject[255]='\0';
	
	insert_field_cache(self->physid, "subject", subject);
	
	g_free(subject);
}

void dbmail_message_cache_referencesfield(struct DbmailMessage *self)
{
	GMimeReferences *refs, *head;
	const char *field;

	field = dbmail_message_get_header(self,"References");
	if (! field)
		field = dbmail_message_get_header(self,"In-Reply-to");
	if (! field) 
		return;

	refs = g_mime_references_decode(field);
	
	if (! refs) {
		trace(TRACE_WARNING, "%s,%s: reference_decode failed [%llu]",
				__FILE__, __func__, self->physid);
		return;
	}
	
	head = refs;
	
	while (refs->msgid) {
		insert_field_cache(self->physid, "references", refs->msgid);

		if (refs->next == NULL)
			break;
		refs = refs->next;
	}
	g_mime_references_clear(&head);

}
	

/* Run the user's sorting rules on this message
 * Retrieve the action list as either
 * a linked list of things to do, or a 
 * single thing to do. Not sure yet...
 *
 * Then do it!
 * */
dsn_class_t sort_and_deliver(struct DbmailMessage *message, u64_t useridnr, const char *mailbox)
{
	u64_t mboxidnr, newmsgidnr;

	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);
	u64_t msgidnr = message->id;
	
	if (! mailbox)
		mailbox="INBOX";

	if (db_find_create_mailbox(mailbox, useridnr, &mboxidnr) != 0) {
		trace(TRACE_ERROR, "%s,%s: mailbox [%s] not found",
				__FILE__, __func__,
				mailbox);
		return DSN_CLASS_FAIL;
	} else {
		switch (db_copymsg(msgidnr, mboxidnr, useridnr, &newmsgidnr)) {
		case -2:
			trace(TRACE_DEBUG, "%s, %s: error copying message to user [%llu],"
					"maxmail exceeded", 
					__FILE__, __func__, 
					useridnr);
			return DSN_CLASS_QUOTA;
		case -1:
			trace(TRACE_ERROR, "%s, %s: error copying message to user [%llu]", 
					__FILE__, __func__, 
					useridnr);
			return DSN_CLASS_TEMP;
		default:
			trace(TRACE_MESSAGE, "%s, %s: message id=%llu, size=%d is inserted", 
					__FILE__, __func__, 
					newmsgidnr, msgsize);
			message->id = newmsgidnr;
			return DSN_CLASS_OK;
		}
	}
}


