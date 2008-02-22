/*
  

  Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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


extern GTree * global_cache;

extern db_param_t * _db_params;
#define DBPFX _db_params->pfx

#define MESSAGE_MAX_LINE_SIZE 1024

#define DBMAIL_TEMPMBOX "INBOX"

#define THIS_MODULE "message"


//#define dprint(fmt, args...) printf(fmt, ##args)
#define dprint(fmt, args...) 0


/*
 * _register_header
 *
 * register a message header in a ghashtable dictionary
 *
 */
static void _register_header(const char *header, const char *value, gpointer user_data);
static gboolean _header_cache(const char *header, const char *value, gpointer user_data);

static DbmailMessage * _retrieve(DbmailMessage *self, const char *query_template);
static void _map_headers(DbmailMessage *self);
static int _set_content(DbmailMessage *self, const GString *content);
static int _set_content_from_stream(DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type);
static int _message_insert(DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id); 


/* general mime utils (missing from gmime?) */

unsigned find_end_of_header(const char *h)
{
	gchar c, p1 = 0, p2 = 0;
	unsigned i = 0;
	size_t l;

	assert(h);

	l  = strlen(h);

	while (h++ && i<=l) {
		i++;
		c = *h;
		if (c == '\n' && ((p1 == '\n') || (p1 == '\r' && p2 == '\n'))) {
			if (l > i) 
				i++;
			break;
		}
		p2 = p1;
		p1 = c;
	}
	return i;
}


gchar * g_mime_object_get_body(const GMimeObject *object)
{
	gchar *s = NULL, *b = NULL;
        unsigned i;
	size_t l;
	
	g_return_val_if_fail(object != NULL, NULL);

	s = g_mime_object_to_string(GMIME_OBJECT(object));
	assert(s);

	i = find_end_of_header(s);
	if (i >= strlen(s))
		return NULL;
	
	b = s+i;
	l = strlen(b);
	memmove(s,b,l);
	s[l] = '\0';
	s = g_realloc(s, l+1);

	return s;
}

gchar * get_crlf_encoded_opt(const gchar *string, int dots)
{
	GMimeStream *ostream, *fstream;
	GMimeFilter *filter;
	gchar *encoded, *buf;
	GString *raw;
	
	ostream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	if (dots) {
		filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
	} else {
		filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	}
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

static u64_t blob_exists(const char *buf, const char *hash)
{
	int i, rows;
	u64_t id = 0;
	const char *data;
	char query[DEF_QUERYSIZE];
	size_t len;
	assert(buf);

	memset(query,0,DEF_QUERYSIZE);
	snprintf(query, DEF_QUERYSIZE, "SELECT id,data FROM %smimeparts WHERE hash='%s'", DBPFX, hash);
	if (db_query(query) == DM_EQUERY)
		TRACE(TRACE_FATAL,"Unable to select from mimeparts table");
	
	len = strlen(buf);
	rows = db_num_rows();
	if (rows > 1)
		TRACE(TRACE_INFO,"possible collision for hash [%s]", hash);

	for (i=0; i< rows; i++) {
		data = db_get_result(i,1);
		if (memcmp(buf, data, len)==0) {
			id = db_get_result_u64(i,0);
			break;
		}
	}

	return id;
}

static u64_t blob_insert(const char *buf, const char *hash)
{
	GString *q;
	char *safe = NULL;
	u64_t id = 0;

	assert(buf);
	q = g_string_new("");
	safe = dm_strbinesc(buf);
	g_string_printf(q, "INSERT INTO %smimeparts (hash, data, size) VALUES ("
			"'%s', '%s', %zd)", DBPFX, hash, safe, strlen(buf));
	g_free(safe);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return 0;
	}

	id = db_insert_result("mimeparts_id");

	db_free_result();
	g_string_free(q,TRUE);

	return id;
}

static int register_blob(DbmailMessage *m, u64_t id, gboolean is_header)
{
	GString *q;

	q = g_string_new("");
	g_string_printf(q, "INSERT INTO %spartlists "
		"(physmessage_id, is_header, part_key, part_depth, part_order, part_id) "
		"VALUES (%llu,%u,%u,%u,%u,%llu)", 
		DBPFX, dbmail_message_get_physid(m), is_header, 
		m->part_key, m->part_depth, m->part_order, id);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return DM_EQUERY;
	}

	db_free_result();
	g_string_free(q,TRUE);

	return DM_SUCCESS;
}
static u64_t blob_store(const char *buf)
{
	u64_t id;
	char *hash = dm_get_hash_for_string(buf);


	// store this message fragment
	if ((id = blob_exists(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}

	if ((id = blob_insert(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}
	
	return 0;
}

static int store_blob(DbmailMessage *m, const char *buf, gboolean is_header)
{
	u64_t id;

	if (! buf) return 0;

	if (is_header) {
		m->part_key++;
		m->part_order=0;
	}

	dprint("<blob is_header=\"%d\" part_key=\"%d\" part_order=\"%d\">\n%s\n</blob>\n", is_header, m->part_key, m->part_order, buf);
	if (! (id = blob_store(buf)))
		return DM_EQUERY;

	// register this message fragment
	if (register_blob(m, id, is_header) == DM_EQUERY)
		return DM_EQUERY;

	m->part_order++;

	return 0;

}
static const char * find_boundary(const char *s)
{
	GMimeContentType *type;
	char header[128];
	const char *boundary;
	char *rest;
	int i=0, j=0;

	memset(header,0,sizeof(header));

	rest = g_strcasestr(s, "\nContent-type: ");
	if (! rest) {
		if ((g_strncasecmp(s, "Content-type: ", 14)) == 0)
			rest = (char *)s;
	}
	if (! rest)
		return NULL;

	i = 13;
	while (rest[i]) {
		if (((rest[i] == '\n') || (rest[i] == '\r')) && (!isspace(rest[i+1]))) {
			break;
		}
		header[j++]=rest[i++];
	}
	header[j]='\0';
	g_strstrip(header);
	type = g_mime_content_type_new_from_string(header);
	boundary = g_mime_content_type_get_parameter(type,"boundary");
        g_free(type);
	
	return boundary;
}


static DbmailMessage * _mime_retrieve(DbmailMessage *self)
{
	char *str;
	const char *boundary = NULL;
	const char *internal_date = NULL;
	char **blist = g_new0(char *,32);
	int prevdepth, depth = 0, order, row, rows;
	int key = 1;
	gboolean got_boundary = FALSE, prev_boundary = FALSE;
	gboolean is_header = TRUE, prev_header;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);
	GString *m;
	gboolean finalized=FALSE;

	assert(dbmail_message_get_physid(self));

	snprintf(query, DEF_QUERYSIZE, "SELECT data,l.part_key,l.part_depth,l.part_order,l.is_header,%s "
		"FROM %smimeparts p "
		"JOIN %spartlists l ON p.id = l.part_id "
		"JOIN %sphysmessage ph ON ph.id = l.physmessage_id "
		"WHERE l.physmessage_id = %llu ORDER BY l.part_key,l.part_order ASC", 
		date2char_str("ph.internal_date"), DBPFX, DBPFX, DBPFX, dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		TRACE(TRACE_ERROR, "sql error");
		return NULL;
	}
	
	rows = db_num_rows();
	if (rows < 1) {
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	m = g_string_new("");

	for (row=0; row < rows; row++) {

		prevdepth = depth;
		prev_header = is_header;

		str = g_strdup_printf("%s",((char *)db_get_result(row,0)));
		key = db_get_result_int(row,1);
		depth = db_get_result_int(row,2);
		order = db_get_result_int(row,3);
		is_header = db_get_result_bool(row,4);
		if (row == 0)
			internal_date = db_get_result(row,5);

		if (is_header)
			prev_boundary = got_boundary;

		got_boundary = FALSE;
		if (is_header && ((boundary = find_boundary(str)) != NULL)) {
			got_boundary = TRUE;
			dprint("<boundary depth=\"%d\">%s</boundary>\n", depth, boundary);
			blist[depth] = (char *)boundary;
		}

		if (prevdepth > depth && blist[depth]) {
			dprint("\n--%s at %d--\n", blist[depth], depth);
			g_string_append_printf(m, "\n--%s--\n", blist[depth]);
			blist[depth] = NULL;
			finalized=TRUE;
		}

		if (depth>0 && blist[depth-1])
			boundary = (const char *)blist[depth-1];

		if (is_header && (!prev_header|| prev_boundary)) {
			dprint("\n--%s\n", boundary);
			g_string_append_printf(m, "\n--%s\n", boundary);
		}

		g_string_append(m, str);
		dprint("<part is_header=\"%d\" depth=\"%d\" key=\"%d\" order=\"%d\">\n%s\n</part>\n", 
			is_header, depth, key, order, str);

		if (is_header)
			g_string_append_printf(m,"\n");
		
		g_free(str);
		
	}
	if (rows > 1 && boundary && !finalized) {
		dprint("\n--%s-- final\n", boundary);
		g_string_append_printf(m, "\n--%s--\n", boundary);
	}

	if (rows > 1 && depth > 0 && blist[0] && !finalized) {
		if (strcmp(blist[0],boundary)!=0) {
			dprint("\n--%s-- final\n", blist[0]);
			g_string_append_printf(m, "\n--%s--\n\n", blist[0]);
		} else
			g_string_append_printf(m, "\n");
	}

	db_free_result();

	self = dbmail_message_init_with_string(self,m);
	if (strlen(internal_date))
		dbmail_message_set_internal_date(self, (char *)internal_date);
	g_string_free(m,TRUE);
	g_free(blist);

	return self;
}

static gboolean store_mime_object(GMimeObject *object, DbmailMessage *m);

static void store_head(GMimeObject *object, DbmailMessage *m)
{
	char *head = g_mime_object_get_headers(object);
	store_blob(m, head, 1);
	g_free(head);
}

static void store_body(GMimeObject *object, DbmailMessage *m)
{
	char *text = g_mime_object_get_body(object);
	if (! text)
		return;

	store_blob(m, text, 0);
	g_free(text);
}


static gboolean store_mime_text(GMimeObject *object, DbmailMessage *m, gboolean skiphead)
{
	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	if (! skiphead) store_head(object, m);
	store_body(object, m);

	return FALSE;
}

static gboolean store_mime_multipart(GMimeObject *object, DbmailMessage *m, const GMimeContentType *content_type, gboolean skiphead)
{
	const char *boundary;
	int n;

	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	boundary = g_mime_content_type_get_parameter(content_type,"boundary");

	if (! skiphead) store_head(object,m);

	if (g_mime_content_type_is_type(content_type, "multipart", "*"))
		store_blob(m, g_mime_multipart_get_preface((GMimeMultipart *)object), 0);

	if (boundary) {
		m->part_depth++;
		n = m->part_order;
		m->part_order=0;
	}

	g_mime_multipart_foreach((GMimeMultipart *)object, (GMimePartFunc)store_mime_object, m);

	if (boundary) {
		n++;
		m->part_depth--;
		m->part_order=n;
	}

	if (g_mime_content_type_is_type(content_type, "multipart", "*"))
		store_blob(m, g_mime_multipart_get_postface((GMimeMultipart *)object), 0);

	return FALSE;
}

static gboolean store_mime_message(GMimeObject * object, DbmailMessage *m, gboolean skiphead)
{
	GMimeMessage *m2;

	if (! skiphead) store_head(object, m);

	m2 = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));

	g_return_val_if_fail(GMIME_IS_MESSAGE(m2), TRUE);

	store_mime_object(GMIME_OBJECT(m2), m);

	g_object_unref(m2);
	
	return FALSE;
	
}

gboolean store_mime_object(GMimeObject *object, DbmailMessage *m)
{
	const GMimeContentType *content_type;
	GMimeObject *mime_part;
	gboolean r = FALSE;
	gboolean skiphead = FALSE;

	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	if (GMIME_IS_MESSAGE(object)) {
		dprint("\n<message>\n");

		store_head(object,m);

		// we need to skip the first (post-rfc822) mime-headers
		// of the mime_part because they are already included as
		// part of the rfc822 headers
		skiphead = TRUE;

		g_mime_header_set_raw (GMIME_MESSAGE(object)->mime_part->headers, NULL);
		mime_part = g_mime_message_get_mime_part((GMimeMessage *)object);
	} else
		mime_part = object;

	content_type = g_mime_object_get_content_type(mime_part);

	if (g_mime_content_type_is_type(content_type, "multipart", "*"))
		r = store_mime_multipart((GMimeObject *)mime_part, m, content_type, skiphead);

	else if (g_mime_content_type_is_type(content_type, "message","*"))
		r = store_mime_message((GMimeObject *)mime_part, m, skiphead);

	else if (g_mime_content_type_is_type(content_type, "text","*"))
		if (GMIME_IS_MESSAGE(object))
			store_body(object,m);
		else
			r = store_mime_text((GMimeObject *)mime_part, m, skiphead);
	else
		r = store_mime_text((GMimeObject *)mime_part, m, skiphead);

	if (GMIME_IS_MESSAGE(object)) {
		g_object_unref(mime_part);
		dprint("\n</message>\n");
	}

	return r;
}


static gboolean _dm_message_store(DbmailMessage *m)
{
	gboolean r;
	r = store_mime_object((GMimeObject *)m->content, m);
	return r;
}


/* Useful for debugging. Uncomment if/when needed.
 *//*
static void dump_to_file(const char *filename, const char *buf)
{
	gint se;
	g_assert(filename);
	FILE *f = fopen(filename,"a");
	if (! f) {
		se=errno;
		TRACE(TRACE_DEBUG,"opening dumpfile failed [%s]", strerror(se));
		errno=se;
		return;
	}
	fprintf(f,"%s",buf);
	fclose(f);
}
*/


/*  \brief create a new empty DbmailMessage struct
 *  \return the DbmailMessage
 */

DbmailMessage * dbmail_message_new(void)
{
	DbmailMessage *self = g_new0(DbmailMessage,1);
	
	self->envelope_recipient = g_string_new("");

	/* provide quick case-insensitive header name searches */
	self->header_name = g_tree_new((GCompareFunc)g_ascii_strcasecmp);
	/* provide quick case-sensitive header value searches */
	self->header_value = g_tree_new((GCompareFunc)strcmp);
	
	
	/* internal cache: header_dict[headername.name] = headername.id */
	self->header_dict = g_hash_table_new_full((GHashFunc)g_str_hash,
			(GEqualFunc)g_str_equal, (GDestroyNotify)g_free, NULL);
	
	dbmail_message_set_class(self, DBMAIL_MESSAGE);
	
	return self;
}

void dbmail_message_free(DbmailMessage *self)
{
	if (! self)
		return;

	if (self->headers) {
		g_relation_destroy(self->headers);
		self->headers = NULL;
	}
	if (self->content) {
		g_object_unref(self->content);
		self->content = NULL;
	}
	if (self->charset) {
		g_free(self->charset);
		self->charset = NULL;
	}

	g_string_free(self->envelope_recipient,TRUE);
	g_hash_table_destroy(self->header_dict);
	g_tree_destroy(self->header_name);
	g_tree_destroy(self->header_value);
	
	self->id=0;
	g_free(self);
	self = NULL;
}


/* \brief create and initialize a new DbmailMessage
 * \param FILE *instream from which to read
 * \param int streamtype is DBMAIL_STREAM_PIPE or DBMAIL_STREAM_LMTP
 * \return the new DbmailMessage
 */
DbmailMessage * dbmail_message_new_from_stream(FILE *instream, int streamtype) 
{
	
	GMimeStream *stream;
	DbmailMessage *message, *retmessage;
	
	assert(instream);
	message = dbmail_message_new();
	stream = g_mime_stream_fs_new(dup(fileno(instream)));
	retmessage = dbmail_message_init_with_stream(message, stream, streamtype);
	g_object_unref(stream);

	if (retmessage)
		return retmessage;
	
	dbmail_message_free(message);
	return NULL;
}

/* \brief set the type flag for this DbmailMessage
 * \param the DbmailMessage on which to set the flag
 * \param type flag is either DBMAIL_MESSAGE or DBMAIL_MESSAGE_PART
 * \return non-zero in case of error
 */
int dbmail_message_set_class(DbmailMessage *self, int klass)
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
int dbmail_message_get_class(const DbmailMessage *self)
{
	return self->klass;
}

/* \brief initialize a previously created DbmailMessage using a GString
 * \param the empty DbmailMessage
 * \param GString *content contains the raw message
 * \return the filled DbmailMessage
 */
DbmailMessage * dbmail_message_init_with_string(DbmailMessage *self, const GString *content)
{

	_set_content(self,content);

	if (! (GMIME_IS_MESSAGE(self->content))) {
		dbmail_message_set_class(self, DBMAIL_MESSAGE_PART);
		g_object_unref(self->content);
		self->content=NULL;
		_set_content(self, content);
	}
	
	_map_headers(self);
	
	return self;
}

DbmailMessage * dbmail_message_init_from_gmime_message(DbmailMessage *self, GMimeMessage *message)
{
	g_return_val_if_fail(GMIME_IS_MESSAGE(message), NULL);

	self->content = GMIME_OBJECT(message);
	_map_headers(self);

	return self;

}

/* \brief initialize a previously created DbmailMessage using a GMimeStream
 * \param empty DbmailMessage
 * \param stream from which to read
 * \param type which indicates either pipe/network style streaming
 * \return the filled DbmailMessage
 */
DbmailMessage * dbmail_message_init_with_stream(DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
	{
	int res;

	res = _set_content_from_stream(self,stream,type);
	if (res != 0)
		return NULL;

	_map_headers(self);
	return self;
}

static int _set_content(DbmailMessage *self, const GString *content)
{
	int res;
	GMimeStream *stream;

	stream = g_mime_stream_mem_new_with_buffer(content->str, content->len+1);
	res = _set_content_from_stream(self, stream, DBMAIL_STREAM_PIPE);
	g_mime_stream_close(stream);
	g_object_unref(stream);
	return res;
}

static int _set_content_from_stream(DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
{
	/* 
	 * We convert all messages to crlf->lf for internal usage and
	 * db-insertion
	 */
	
	GMimeStream *fstream, *bstream, *mstream;
	GMimeFilter *filter;
	GMimeParser *parser;
	gchar *buf, *from = NULL;
	ssize_t getslen, putslen;
	FILE *tmp;
	int res = 0;
	gboolean firstline=TRUE;

	/*
	 * buildup the memory stream buffer
	 * we will read from stream until either EOF or <dot><crlf> is encountered
	 * depending on the streamtype
	 */

	if (self->content) {
		g_object_unref(self->content);
		self->content=NULL;
	}
	
	parser = g_mime_parser_new();
		
	switch(type) {
		case DBMAIL_STREAM_LMTP:
		case DBMAIL_STREAM_PIPE:
			
			buf = g_new0(char, MESSAGE_MAX_LINE_SIZE);

			// stream -> bstream (buffer) -> fstream (filter) -> mstream (in-memory copy)
			bstream = g_mime_stream_buffer_new(stream,GMIME_STREAM_BUFFER_BLOCK_READ);
//			mstream = g_mime_stream_mem_new();

			tmp = tmpfile(); 
			mstream = g_mime_stream_file_new(tmp);

			assert(mstream);
			fstream = g_mime_stream_filter_new_with_stream(mstream);
			filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
			g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
			
			while ((getslen = g_mime_stream_buffer_gets(bstream, buf, MESSAGE_MAX_LINE_SIZE)) > 0) {
				if (firstline && strncmp(buf,"From ",5)==0) {
					from = g_strdup(buf);
					firstline=FALSE;
					continue;
				}

				if ((type==DBMAIL_STREAM_LMTP) && (strncmp(buf,".\r\n",3)==0))
					break;

				putslen = g_mime_stream_write(fstream, buf, getslen);

				if (g_mime_stream_flush(fstream)) {
					TRACE(TRACE_ERROR, "Failed to flush, is your /tmp filesystem full?");
					res = 1;
					break;
				}

				if (putslen < getslen && getslen > putslen+1) {
					TRACE(TRACE_ERROR, "Short write [%zd < %zd], is your /tmp filesystem full?", 
						putslen, getslen);
					res = 1;
					break;
				}
			}

			if (getslen < 0) {
				TRACE(TRACE_ERROR, "Read failed, did the client drop the connection?");
				res = 1;
			}

			g_free(buf);
			
			g_mime_stream_reset(mstream);
			g_mime_parser_init_with_stream(parser, mstream);

			g_object_unref(filter);
			g_object_unref(fstream);
			g_object_unref(bstream);
			g_object_unref(mstream);

		break;

		default:
		case DBMAIL_STREAM_RAW:
			g_mime_parser_init_with_stream(parser, stream);
		break;

	}

	switch (dbmail_message_get_class(self)) {
		case DBMAIL_MESSAGE:
			TRACE(TRACE_DEBUG,"parse message");
			self->content = GMIME_OBJECT(g_mime_parser_construct_message(parser));
			// adding a header will prime the gmime message structure, but we want
			// to add an innocuous header
			dbmail_message_set_header(self,"MIME-Version","1.0"); 
			if (from) {
				dbmail_message_set_internal_date(self, from);
				g_free(from);
			}
			break;
		case DBMAIL_MESSAGE_PART:
			TRACE(TRACE_DEBUG,"parse part");
			self->content = GMIME_OBJECT(g_mime_parser_construct_part(parser));
			break;
	}

	g_object_unref(parser);

	return res;
}

static void _map_headers(DbmailMessage *self) 
{
	GMimeObject *part;
	assert(self->content);
	self->headers = g_relation_new(2);

//	g_mime_message_set_header(GMIME_MESSAGE(self->content), "X-DBMail", "transient header");
//	g_mime_object_remove_header(GMIME_OBJECT(self->content), "X-DBMail");

	g_relation_index(self->headers, 0, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);
	g_relation_index(self->headers, 1, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);

	if (GMIME_IS_MESSAGE(self->content)) {
		char *message_id = NULL;
		char *type = NULL;

		// this is needed to correctly initialize gmime's mime iterator
		if (GMIME_MESSAGE(self->content)->mime_part)
			g_mime_header_set_raw (GMIME_MESSAGE(self->content)->mime_part->headers, NULL);

		/* make sure the message has a message-id, else threading breaks */
		if (! (message_id = (char *)g_mime_message_get_message_id(GMIME_MESSAGE(self->content)))) {
			char *domainname = g_new0(gchar, 255);
			if (getdomainname(domainname,255))
				strcpy(domainname,"(none)");
			message_id = g_mime_utils_generate_message_id(domainname);
			g_mime_message_set_message_id(GMIME_MESSAGE(self->content), message_id);
			g_free(message_id);
			g_free(domainname);
		}


		// gmime doesn't consider the content-type header to be a message-header so extract 
		// and register it separately
		part = g_mime_message_get_mime_part(GMIME_MESSAGE(self->content));
		if ((type = (char *)g_mime_object_get_header(part,"Content-Type"))!=NULL)
			_register_header("Content-Type",type, (gpointer)self);
		g_object_unref(part);
	}

	g_mime_header_foreach(GMIME_OBJECT(self->content)->headers, _register_header, self);
}

static void _register_header(const char *header, const char *value, gpointer user_data)
{
	const char *hname, *hvalue;
	DbmailMessage *m = (DbmailMessage *)user_data;
	if (! (hname = g_tree_lookup(m->header_name,header))) {
		g_tree_insert(m->header_name,(gpointer)header,(gpointer)header);
		hname = header;
	}
	if (! (hvalue = g_tree_lookup(m->header_value,value))) {
		g_tree_insert(m->header_value,(gpointer)value,(gpointer)value);
		hvalue = value;
	}
	
	if (m->headers && (! g_relation_exists(m->headers, hname, hvalue)))
		g_relation_insert(m->headers, hname, hvalue);
}

void dbmail_message_set_physid(DbmailMessage *self, u64_t physid)
{
	self->physid = physid;
}

u64_t dbmail_message_get_physid(const DbmailMessage *self)
{
	return self->physid;
}

void dbmail_message_set_internal_date(DbmailMessage *self, char *internal_date)
{
	if (internal_date && strlen(internal_date)) {
		TRACE(TRACE_DEBUG,"[%p] [%s]", self, internal_date);
		self->internal_date = g_mime_utils_header_decode_date(internal_date, self->internal_date_gmtoff);
	}
}

/* thisyear is a workaround for some broken gmime version. */
gchar * dbmail_message_get_internal_date(const DbmailMessage *self, int thisyear)
{
	char *res;
	struct tm gmt;
	if (! self->internal_date)
		return NULL;
	
	res = g_new0(char, TIMESTRING_SIZE+1);
	memset(&gmt,'\0', sizeof(struct tm));
	gmtime_r(&self->internal_date, &gmt);

	/* override if the date is not sane */
	if (thisyear && gmt.tm_year + 1900 > thisyear + 1) {
		gmt.tm_year = thisyear - 1900;
	}

	strftime(res, TIMESTRING_SIZE, "%Y-%m-%d %T", &gmt);
	return res;
}

void dbmail_message_set_envelope_recipient(DbmailMessage *self, const char *envelope_recipient)
{
	if (envelope_recipient)
		g_string_printf(self->envelope_recipient,"%s", envelope_recipient);
}

gchar * dbmail_message_get_envelope_recipient(const DbmailMessage *self)
{
	if (self->envelope_recipient->len > 0)
		return self->envelope_recipient->str;
	return NULL;
}

void dbmail_message_set_header(DbmailMessage *self, const char *header, const char *value)
{
	g_mime_message_set_header(GMIME_MESSAGE(self->content), header, value);
	_register_header(header, value, (gpointer)self);
}

const gchar * dbmail_message_get_header(const DbmailMessage *self, const char *header)
{
	return g_mime_message_get_header(GMIME_MESSAGE(self->content), header);
}

GTuples * dbmail_message_get_header_repeated(const DbmailMessage *self, const char *header)
{
	const char *hname;
	if (! (hname = g_tree_lookup(self->header_name,header)))
		hname = header;
	return g_relation_select(self->headers, hname, 0);
}

GList * dbmail_message_get_header_addresses(DbmailMessage *message, const char *field_name)
{
	InternetAddressList *ialisthead, *ialist;
	InternetAddress *ia;
	GList *result = NULL;
	const char *field_value;

	if (!message || !field_name) {
		TRACE(TRACE_WARNING, "received a NULL argument, this is a bug");
		return NULL; 
	}

	field_value = dbmail_message_get_header(message, field_name);
	TRACE(TRACE_INFO, "mail address parser looking at field [%s] with value [%s]", field_name, field_value);
	
	if ((ialist = internet_address_parse_string(field_value)) == NULL) {
		TRACE(TRACE_MESSAGE, "mail address parser error parsing header field");
		return NULL;
	}

	ialisthead = ialist;
	while (1) {
		ia = ialist->address;
		result = g_list_append(result, g_strdup(ia->value.addr));
		if (! ialist->next)
			break;
		ialist = ialist->next;
	}
	
	internet_address_list_destroy(ialisthead);

	TRACE(TRACE_DEBUG, "mail address parser found [%d] email addresses", g_list_length(result));

	return result;
}
char * dbmail_message_get_charset(DbmailMessage *self)
{
	if (! self->charset)
		self->charset = message_get_charset((GMimeMessage *)self->content);
	return self->charset;
}

/* dump message(parts) to char ptrs */
gchar * dbmail_message_to_string(const DbmailMessage *self) 
{
	return g_mime_object_to_string(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_body_to_string(const DbmailMessage *self)
{
	return g_mime_object_get_body(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_hdrs_to_string(const DbmailMessage *self)
{
	gchar *h;
	unsigned i = 0;

	h = dbmail_message_to_string(self);
	i = find_end_of_header(h);
	h[i] = '\0';
	h = g_realloc(h, i+1);

	return h;
}


/* 
 * Some dynamic accessors.
 * 
 * Don't cache these values to allow changes in message content!!
 * 
 */
size_t dbmail_message_get_size(const DbmailMessage *self, gboolean crlf)
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
size_t dbmail_message_get_hdrs_size(const DbmailMessage *self, gboolean crlf)
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
size_t dbmail_message_get_body_size(const DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_body_to_string(self);

	if (! s) return 0;

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


static DbmailMessage * _retrieve(DbmailMessage *self, const char *query_template)
{
	
	int row = 0, rows = 0;
	GString *m;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);
	DbmailMessage *store;
	const char *internal_date;
	
	assert(dbmail_message_get_physid(self));
	
	store = self;

	if ((self = _mime_retrieve(self)))
		return self;

	self = store;

	snprintf(query, DEF_QUERYSIZE, query_template, date2char_str("p.internal_date"), 
		DBPFX, DBPFX, dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		TRACE(TRACE_ERROR, "sql error");
		return NULL;
	}
	
	rows = db_num_rows();
	if (rows < 1) {
		TRACE(TRACE_ERROR, "blk error");
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	m = g_string_new("");
	for (row=0; row < rows; row++) {
		char *str = (char *)db_get_result(row,0);
		if (row == 0)
			internal_date = db_get_result(row,2);

		g_string_append_printf(m, "%s", str);
	}
	db_free_result();
	
	self = dbmail_message_init_with_string(self,m);
	if (strlen(internal_date))
		dbmail_message_set_internal_date(self, (char *)internal_date);

	g_string_free(m,TRUE);

	return self;
}

/*
 *
 * retrieve the header messageblk
 *
 * TODO: this call is yet unused in the code, but here for
 * forward compatibility's sake.
 *
 */
static DbmailMessage * _fetch_head(DbmailMessage *self)
{
	const char *query_template = 	"SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"AND b.is_header = '1'";
	return _retrieve(self, query_template);

}

/*
 *
 * retrieve the full message
 *
 */
static DbmailMessage * _fetch_full(DbmailMessage *self) 
{
	const char *query_template = "SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"ORDER BY b.messageblk_idnr";
	return _retrieve(self, query_template);
}

/* \brief retrieve message
 * \param empty DbmailMessage
 * \param physmessage_id
 * \param filter (header-only or full message)
 * \return filled DbmailMessage
 */
DbmailMessage * dbmail_message_retrieve(DbmailMessage *self, u64_t physid, int filter)
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
		TRACE(TRACE_ERROR, "retrieval failed for physid [%llu]", physid);
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
int dbmail_message_store(DbmailMessage *self)
{
	u64_t user_idnr;
	char unique_id[UID_SIZE];
	char *hdrs, *body;
	u64_t hdrs_size, body_size, rfcsize;
	int i=1, retry=10, delay=200;
	
	if (auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr) <= 0) {
		TRACE(TRACE_ERROR, "unable to find user_idnr for user [%s]. Make sure this system user is in the database!", DBMAIL_DELIVERY_USERNAME);
		return DM_EQUERY;
	}
	
	create_unique_id(unique_id, user_idnr);

	while (i++ < retry) {
		db_begin_transaction();

		/* create a message record */
		if(_message_insert(self, user_idnr, DBMAIL_TEMPMBOX, unique_id) < 0) {
			db_rollback_transaction();
			usleep(delay*i);
			continue;
		}

		hdrs = dbmail_message_hdrs_to_string(self);
		body = dbmail_message_body_to_string(self);

		hdrs_size = (u64_t)dbmail_message_get_hdrs_size(self, FALSE);
		body_size = (u64_t)dbmail_message_get_body_size(self, FALSE);

		if (_dm_message_store(self)) {
			TRACE(TRACE_FATAL,"Failed to store mimeparts");
			db_rollback_transaction();
			usleep(delay*i);
			continue;
		}

		rfcsize = (u64_t)dbmail_message_get_size(self,TRUE);
		if (db_update_message(self->id, unique_id, (hdrs_size + body_size), rfcsize) < 0) {
			db_rollback_transaction();
			usleep(delay*i);
			continue;
		}

		/* store message headers */
		if (dbmail_message_cache_headers(self) < 0) {
			db_rollback_transaction();
			usleep(delay*i);
			continue;
			}
		
		/* ready for commit */
		break;
	}

	return db_commit_transaction();
}

int _message_insert(DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id)
{
	u64_t mailboxid;
	u64_t physmessage_id;
	char *internal_date = NULL;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);

	assert(unique_id);
	assert(mailbox);

	if (db_find_create_mailbox(mailbox, BOX_DEFAULT, user_idnr, &mailboxid) == -1)
		return -1;
	
	if (mailboxid == 0) {
		TRACE(TRACE_ERROR, "mailbox [%s] could not be found!", mailbox);
		return -1;
	}

	/* get the messages date, but override it if it's from the future */
	struct timeval tv;
	struct tm gmt;
	int thisyear;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &gmt);
	thisyear = gmt.tm_year + 1900;
	internal_date = dbmail_message_get_internal_date(self, thisyear);
	int result = 0;

	/* insert a new physmessage entry */
	if (db_insert_physmessage_with_internal_date(internal_date, &physmessage_id) == -1)  {
		g_free(internal_date);
		return -1;
	}
	g_free(internal_date);

	dbmail_message_set_physid(self, physmessage_id);

	/* now insert an entry into the messages table */
	snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
			"%smessages(mailbox_idnr, physmessage_id, unique_id,"
			"recent_flag, status) "
			"VALUES (%llu, %llu, '%s', 1, %d)",
			DBPFX, mailboxid, physmessage_id, unique_id,
			MESSAGE_STATUS_INSERT);

	if ((result = db_query(query)) == DM_EQUERY) {
		TRACE(TRACE_ERROR,"inserting message failed");
		return result;
	}

	self->id = db_insert_result("message_idnr");
	TRACE(TRACE_DEBUG,"new message_idnr [%llu]", self->id);
	db_free_result();

	return result;
}

int dbmail_message_cache_headers(const DbmailMessage *self)
{
	assert(self);
	assert(self->physid);

	if (! GMIME_IS_MESSAGE(self->content)) {
		TRACE(TRACE_ERROR,"self->content is not a message");
		return -1;
	}

	g_tree_foreach(self->header_name, (GTraverseFunc)_header_cache, (gpointer)self);
	
	dbmail_message_cache_tofield(self);
	dbmail_message_cache_ccfield(self);
	dbmail_message_cache_fromfield(self);
	dbmail_message_cache_datefield(self);
	dbmail_message_cache_replytofield(self);
	dbmail_message_cache_subjectfield(self);
	dbmail_message_cache_referencesfield(self);
	dbmail_message_cache_envelope(self);

	return DM_SUCCESS;
}
#define CACHE_WIDTH_VALUE 255
#define CACHE_WIDTH_FIELD 255
#define CACHE_WIDTH_ADDR 100
#define CACHE_WIDTH_NAME 100


static int _header_get_id(const DbmailMessage *self, const char *header, u64_t *id)
{
	u64_t tmp;
	u64_t *cid;
	gpointer cacheid;
	gchar *case_header;
	gchar *safe_header;
	gchar *tmpheader;
	int try=3;

	// rfc822 headernames are case-insensitive
	if (! (tmpheader = dm_strnesc(header,CACHE_WIDTH_NAME)))
		return -1;
	safe_header = g_ascii_strdown(tmpheader,-1);
	g_free(tmpheader);

	cacheid = g_hash_table_lookup(self->header_dict, (gconstpointer)safe_header);
	if (cacheid) {
		*id = GPOINTER_TO_UINT(cacheid);
		g_free(safe_header);
		return 1;
	}
		
	GString *q = g_string_new("");

	case_header = g_strdup_printf(db_get_sql(SQL_STRCASE),"headername");

	while (try-- > 0) { // deal with race conditions from other process inserting the same headername
		db_savepoint("header_id");	
		g_string_printf(q, "SELECT id FROM %sheadername WHERE %s='%s'", DBPFX, case_header, safe_header);
		g_free(case_header);

		if ((cid = global_cache_lookup(q->str))) {
			tmp = *cid;
			break;
		}

		if (db_query(q->str) == -1) {
			db_savepoint_rollback("header_id");
			g_string_free(q,TRUE);
			g_free(safe_header);
			return -1;
		}
		if (db_num_rows() < 1) {
			db_free_result();
			g_string_printf(q, "INSERT %s INTO %sheadername (headername) VALUES ('%s')", 
				db_get_sql(SQL_IGNORE), DBPFX, safe_header);
			if (db_query(q->str) == -1) {
				db_savepoint_rollback("header_id");
			} else {
				tmp = db_insert_result("headername_idnr");
			}
		} else {
			tmp = db_get_result_u64(0,0);
			db_free_result();
		}
		if (tmp) {
			cid = g_new0(u64_t,1);
			*cid = tmp;
			global_cache_insert(g_strdup(q->str),cid);
			break;
		}
		usleep(200);
	}

	*id = tmp;
	g_hash_table_insert(self->header_dict, (gpointer)(g_strdup(safe_header)), GUINT_TO_POINTER((unsigned)tmp));
	g_free(safe_header);
	g_string_free(q,TRUE);
	return 1;
}

static gboolean _header_cache(const char UNUSED *key, const char *header, gpointer user_data)
{
	u64_t id;
	DbmailMessage *self = (DbmailMessage *)user_data;
	gchar *safe_value;
	GString *q;
	GTuples *values;
	unsigned char *raw;
	unsigned i;
	gboolean isaddr = 0;

	/* skip headernames with spaces like From_ */
	if (strchr(header, ' '))
		return FALSE;

	if ((_header_get_id(self, header, &id) < 0))
		return TRUE;

	if (g_ascii_strcasecmp(header,"From")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"To")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Reply-to")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Cc")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Bcc")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Return-path")==0)
		isaddr=1;

	q = g_string_new("");
	values = g_relation_select(self->headers,header,0);
	for (i=0; i<values->len;i++) {
		raw = (unsigned char *)g_tuples_index(values,i,1);
		TRACE(TRACE_DEBUG,"raw header value [%s]", raw);
		
 		char *value = NULL;
  		const char *charset = dbmail_message_get_charset(self);
  
 		value = dbmail_iconv_decode_field((const char *)raw, charset, isaddr);
 
 		if ((! value) || (strlen(value) == 0))
 			continue;
 
 		safe_value = dm_stresc(value);
 		g_free(value);

		g_string_printf(q,"INSERT INTO %sheadervalue (headername_id, physmessage_id, headervalue) "
				"VALUES (%llu,%llu,'%s')", DBPFX, id, self->physid, safe_value);
		g_free(safe_value);
		safe_value = NULL;

		if (db_query(q->str)) {
			TRACE(TRACE_ERROR,"insert headervalue failed");
			g_string_free(q,TRUE);
			g_tuples_destroy(values);
			return TRUE;
		}
	}
	g_string_free(q,TRUE);
	g_tuples_destroy(values);
	return FALSE;
}

static void insert_address_cache(u64_t physid, const char *field, InternetAddressList *ialist, const DbmailMessage *self)
{
	InternetAddress *ia;
	
	g_return_if_fail(ialist != NULL);

	GString *q = g_string_new("");
	gchar *name, *rname;
	gchar *addr;
	char *charset = dbmail_message_get_charset((DbmailMessage *)self);

	for (; ialist != NULL && ialist->address; ialist = ialist->next) {
		
		ia = ialist->address;
		g_return_if_fail(ia != NULL);

		rname=dbmail_iconv_str_to_db(ia->name ? ia->name: "", charset);
		/* address fields are truncated to column width */
		name = dm_strnesc(rname, CACHE_WIDTH_ADDR);
		addr = dm_strnesc(ia->value.addr ? ia->value.addr : "", CACHE_WIDTH_ADDR);

		g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sname, %saddr) "
				"VALUES (%llu,'%s','%s')", DBPFX, field, field, field, 
				physid, name, addr);
		
		g_free(name);
		g_free(addr);
		g_free(rname);
		
		if (db_query(q->str)) {
			TRACE(TRACE_ERROR, "insert %sfield failed [%s]", field, q->str);
		}

	}
	
	g_string_free(q,TRUE);
}

static void insert_field_cache(u64_t physid, const char *field, const char *value)
{
	GString *q;
	gchar *clean_value;

	g_return_if_fail(value != NULL);

	/* field values are truncated to 255 bytes */
	clean_value = dm_strnesc(value,CACHE_WIDTH_FIELD);

	q = g_string_new("");

	g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sfield) "
			"VALUES (%llu,'%s')", DBPFX, field, field, physid, clean_value);

	g_free(clean_value);

	if (db_query(q->str)) {
		TRACE(TRACE_ERROR, "insert %sfield failed [%s]", field, q->str);
	}
	g_string_free(q,TRUE);
}
#define DM_ADDRESS_TYPE_TO "To"
#define DM_ADDRESS_TYPE_CC "Cc"
#define DM_ADDRESS_TYPE_FROM "From"
#define DM_ADDRESS_TYPE_REPL "Reply-to"

static InternetAddressList * dm_message_get_addresslist(const DbmailMessage *self, const char * type)
{
	const char *addr = NULL;
	char *charset = NULL;
	char *value = NULL;
	InternetAddressList *list;

	if (! (addr = (char *)dbmail_message_get_header(self, type)))
		return NULL;

	charset = dbmail_message_get_charset((DbmailMessage *)self);
	value = dbmail_iconv_decode_field(addr, charset, TRUE);
	list = internet_address_parse_string(value);

	g_free(value);

	return list;
}

void dbmail_message_cache_tofield(const DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_TO)))
		return;
	insert_address_cache(self->physid, "to", list,self);
	internet_address_list_destroy(list);
}

void dbmail_message_cache_ccfield(const DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_CC)))
		return;
	insert_address_cache(self->physid, "cc", list,self);
	internet_address_list_destroy(list);

}

void dbmail_message_cache_fromfield(const DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_FROM)))
		return;
	insert_address_cache(self->physid, "from", list,self);
	internet_address_list_destroy(list);
}

void dbmail_message_cache_replytofield(const DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_REPL)))
		return;
	insert_address_cache(self->physid, "replyto", list,self);
	internet_address_list_destroy(list);
}


void dbmail_message_cache_datefield(const DbmailMessage *self)
{
	char *value;
	time_t date;

	if (! (value = (char *)dbmail_message_get_header(self,"Date")))
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

void dbmail_message_cache_subjectfield(const DbmailMessage *self)
{
	char *value, *raw, *s, *tmp;
	char *charset;
	
	charset = dbmail_message_get_charset((DbmailMessage *)self);

	// g_mime_message_get_subject fails to get 8-bit header, so we use dbmail_message_get_header
	raw = (char *)dbmail_message_get_header(self, "Subject");

	if (! raw) {
		TRACE(TRACE_MESSAGE,"no subject field value [%llu]", self->physid);
		return;
	}


	value = dbmail_iconv_str_to_utf8(raw, charset);
	s = dm_base_subject(value);
	
	// dm_base_subject returns utf-8 string, convert it into database encoding
	tmp = dbmail_iconv_str_to_db(s, charset);
	insert_field_cache(self->physid, "subject", tmp);
	g_free(tmp);
	g_free(s);
	
	g_free(value);
}

void dbmail_message_cache_referencesfield(const DbmailMessage *self)
{
	GMimeReferences *refs, *head;
	GTree *tree;
	const char *field;

	field = (char *)dbmail_message_get_header(self,"References");
	if (! field)
		field = dbmail_message_get_header(self,"In-Reply-to");
	if (! field) 
		return;

	refs = g_mime_references_decode(field);
	if (! refs) {
		TRACE(TRACE_MESSAGE, "reference_decode failed [%llu]", self->physid);
		return;
	}
	
	head = refs;
	tree = g_tree_new_full((GCompareDataFunc)strcmp, NULL, NULL, NULL);
	
	while (refs->msgid) {
		
		if (! g_tree_lookup(tree,refs->msgid)) {
			insert_field_cache(self->physid, "references", refs->msgid);
			g_tree_insert(tree,refs->msgid,refs->msgid);
		}

		if (refs->next == NULL)
			break;
		refs = refs->next;
	}

	g_tree_destroy(tree);
	g_mime_references_clear(&head);
}
	
void dbmail_message_cache_envelope(const DbmailMessage *self)
{
	char *q, *envelope, *clean;

	envelope = imap_get_envelope(GMIME_MESSAGE(self->content));
	clean = dm_stresc(envelope);

	q = g_strdup_printf("INSERT INTO %senvelope (physmessage_id, envelope) "
			"VALUES (%llu,'%s')", DBPFX, self->physid, clean);

	g_free(clean);
	g_free(envelope);

	if (db_query(q)) {
		TRACE(TRACE_ERROR, "insert envelope failed [%s]", q);
	}
	g_free(q);

}

// 
// construct a new message where only sender, recipient, subject and 
// a body are known. The body can be any kind of charset. Make sure
// it's not pre-encoded (base64, quopri)
//
// TODO: support text/html

DbmailMessage * dbmail_message_construct(DbmailMessage *self, 
		const gchar *to, const gchar *from, 
		const gchar *subject, const gchar *body)
{
	GMimeMessage *message;
	GMimePart *mime_part;
	GMimeDataWrapper *content;
	GMimeStream *stream, *fstream;
	GMimeContentType *mime_type;
	GMimePartEncodingType encoding = GMIME_PART_ENCODING_DEFAULT;
	GMimeFilter *filter = NULL;

	// FIXME: this could easily be expanded to allow appending
	// a new sub-part to an existing mime-part. But for now let's
	// require self to be a pristine (empty) DbmailMessage.
	g_return_val_if_fail(self->content==NULL, self);
	
	message = g_mime_message_new(FALSE);

	// determine the optimal encoding type for the body: how would gmime
	// encode this string. This will return either base64 or quopri.
	if (g_mime_utils_text_is_8bit((unsigned char *)body, strlen(body)))
		encoding = g_mime_utils_best_encoding((unsigned char *)body, strlen(body));

	// set basic headers
	g_mime_message_set_sender(message, from);
	g_mime_message_set_subject(message, subject);
	g_mime_message_add_recipients_from_string(message, GMIME_RECIPIENT_TYPE_TO, to);

	// construct mime-part
	mime_part = g_mime_part_new();
	
	// setup a stream-filter
	stream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(stream);
	
	switch(encoding) {
		case GMIME_PART_ENCODING_BASE64:
			filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_BASE64_ENC);
			break;
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
			filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_QP_ENC);
			break;
		default:
			break;
	}

	if (filter) {
		g_mime_stream_filter_add((GMimeStreamFilter *)fstream, filter);
		g_object_unref(filter);
	}
	
	// fill the stream and thus the mime-part
	g_mime_stream_write_string(fstream,body);
	content = g_mime_data_wrapper_new_with_stream(stream, encoding);
	g_mime_part_set_content_object(mime_part, content);
	
	// add the correct mime-headers
	
	// Content-Type
	mime_type = g_mime_content_type_new("text","plain");
	g_mime_object_set_content_type((GMimeObject *)mime_part, mime_type);
	// We originally tried to use g_mime_charset_best to pick a charset,
	// but it regularly failed to choose utf-8 when utf-8 data was given to it.
	g_mime_object_set_content_type_parameter((GMimeObject *)mime_part, "charset", "utf-8");

	// Content-Transfer-Encoding
	switch(encoding) {
		case GMIME_PART_ENCODING_BASE64:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "base64");
			break;
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "quoted-printable");
			break;
		default:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "7bit");
			break;
	}

	// attach the mime-part to the mime-message
	g_mime_message_set_mime_part(message, (GMimeObject *)mime_part);

	// attach the message to the DbmailMessage struct
	self->content = (GMimeObject *)message;

	// cleanup
	g_object_unref(mime_part);
	g_object_unref(content);
	g_object_unref(stream);
	g_object_unref(fstream);
	return self;
}


/* old stuff moved here from dbmsgbuf.c */

DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter)
{
	DbmailMessage *msg;

	int result;
	u64_t physid = 0;
	if ((result = db_get_physmessage_id(msg_idnr, &physid)) != DM_SUCCESS)
		return NULL;
	msg = dbmail_message_new();
	if (! (msg = dbmail_message_retrieve(msg, physid, filter)))
		return NULL;

	return msg;
}

/* moved here from sort.c */

/* Figure out where to deliver the message, then deliver it.
 * */
dsn_class_t sort_and_deliver(DbmailMessage *message,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source)
{
	int cancelkeep = 0;
	int reject = 0;
	dsn_class_t ret;
	field_t val;
	char *subaddress = NULL;

	/* Catch the brute force delivery right away.
	 * We skip the Sieve scripts, and down the call
	 * chain we don't check permissions on the mailbox. */
	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_MESSAGE, "Beginning brute force delivery for user [%llu] to mailbox [%s].",
				useridnr, mailbox);
		return sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
	}

	TRACE(TRACE_INFO, "Destination [%s] useridnr [%llu], mailbox [%s], source [%d]",
			destination, useridnr, mailbox, source);
	
	/* This is the only condition when called from pipe.c, actually. */
	if (! mailbox) {
		mailbox = "INBOX";
		source = BOX_DEFAULT;
	}

	/* Subaddress. */
	config_get_value("SUBADDRESS", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		int res;
		size_t sublen, subpos;
		res = find_bounded((char *)destination, '+', '@', &subaddress, &sublen, &subpos);
		if (res == 0 && sublen > 0) {
			/* We'll free this towards the end of the function. */
			mailbox = subaddress;
			source = BOX_ADDRESSPART;
			TRACE(TRACE_INFO, "Setting BOX_ADDRESSPART mailbox to [%s]", mailbox);
		}
	}

	/* Give Sieve access to the envelope recipient. */
	dbmail_message_set_envelope_recipient(message, destination);

	/* Sieve. */
	config_get_value("SIEVE", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0
	&& db_check_sievescript_active(useridnr) == 0) {
		TRACE(TRACE_INFO, "Calling for a Sieve sort");
		sort_result_t *sort_result;
		sort_result = sort_process(useridnr, message);
		if (sort_result) {
			cancelkeep = sort_get_cancelkeep(sort_result);
			reject = sort_get_reject(sort_result);
			sort_free_result(sort_result);
		}
	}

	/* Sieve actions:
	 * (m = must implement, s = should implement, e = extension)
	 * m Keep - implicit default action.
	 * m Discard - requires us to skip the default action.
	 * m Redirect - add to the forwarding list.
	 * s Fileinto - change the destination mailbox.
	 * s Reject - nope, sorry. we killed bounce().
	 * e Vacation - share with the auto reply code.
	 */

	if (cancelkeep) {
		// The implicit keep has been cancelled.
		// This may necessarily imply that the message
		// is being discarded -- dropped flat on the floor.
		ret = DSN_CLASS_OK;
		TRACE(TRACE_INFO, "Keep was cancelled. Message may be discarded.");
	} else {
		ret = sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
		TRACE(TRACE_INFO, "Keep was not cancelled. Message will be delivered by default.");
	}

	/* Might have been allocated by the subaddress calculation. NULL otherwise. */
	g_free(subaddress);

	/* Reject probably implies cancelkeep,
	 * but we'll not assume that and instead
	 * just test this as a separate block. */
	if (reject) {
		TRACE(TRACE_INFO, "Message will be rejected.");
		ret = DSN_CLASS_FAIL;
	}

	return ret;
}

dsn_class_t sort_deliver_to_mailbox(DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source,
		int *msgflags)
{
	u64_t mboxidnr, newmsgidnr;
	field_t val;
	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);

	TRACE(TRACE_INFO,"useridnr [%llu] mailbox [%s]", useridnr, mailbox);

	if (db_find_create_mailbox(mailbox, source, useridnr, &mboxidnr) != 0) {
		TRACE(TRACE_ERROR, "mailbox [%s] not found", mailbox);
		return DSN_CLASS_FAIL;
	}

	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_INFO, "Brute force delivery; skipping ACL checks on mailbox.");
	} else {
		// Check ACL's on the mailbox. It must be read-write,
		// it must not be no_select, and it may require an ACL for
		// the user whose Sieve script this is, since it's possible that
		// we've looked up a #Public or a #Users mailbox.
		TRACE(TRACE_DEBUG, "Checking if we have the right to post incoming messages");
        
		MailboxInfo mbox;
		memset(&mbox, '\0', sizeof(mbox));
		mbox.uid = mboxidnr;
		
		switch (acl_has_right(&mbox, useridnr, ACL_RIGHT_POST)) {
		case -1:
			TRACE(TRACE_MESSAGE, "error retrieving right for [%llu] to deliver mail to [%s]",
					useridnr, mailbox);
			return DSN_CLASS_TEMP;
		case 0:
			// No right.
			TRACE(TRACE_MESSAGE, "user [%llu] does not have right to deliver mail to [%s]",
					useridnr, mailbox);
			// Switch to INBOX.
			if (strcmp(mailbox, "INBOX") == 0) {
				// Except if we've already been down this path.
				TRACE(TRACE_MESSAGE, "already tried to deliver to INBOX");
				return DSN_CLASS_FAIL;
			}
			return sort_deliver_to_mailbox(message, useridnr, "INBOX", BOX_DEFAULT, msgflags);
		case 1:
			// Has right.
			TRACE(TRACE_INFO, "user [%llu] has right to deliver mail to [%s]",
					useridnr, mailbox);
			break;
		default:
			TRACE(TRACE_ERROR, "invalid return value from acl_has_right");
			return DSN_CLASS_FAIL;
		}
	}

	// if the mailbox already holds this message we're done
	GETCONFIGVALUE("suppress_duplicates", "DELIVERY", val);
	if (strcasecmp(val,"yes")==0) {
		const char *messageid = dbmail_message_get_header(message, "message-id");
		if ( messageid && ((db_mailbox_has_message_id(mboxidnr, messageid)) > 0) ) {
			TRACE(TRACE_INFO, "suppress_duplicate: [%s]", messageid);
			return DSN_CLASS_OK;
		}
	}

	// Ok, we have the ACL right, time to deliver the message.
	switch (db_copymsg(message->id, mboxidnr, useridnr, &newmsgidnr)) {
	case -2:
		TRACE(TRACE_DEBUG, "error copying message to user [%llu],"
				"maxmail exceeded", useridnr);
		return DSN_CLASS_QUOTA;
	case -1:
		TRACE(TRACE_ERROR, "error copying message to user [%llu]", 
				useridnr);
		return DSN_CLASS_TEMP;
	default:
		TRACE(TRACE_MESSAGE, "message id=%llu, size=%zd is inserted", 
				newmsgidnr, msgsize);
		if (msgflags) {
			TRACE(TRACE_MESSAGE, "message id=%llu, setting imap flags", 
				newmsgidnr);
			db_set_msgflag(newmsgidnr, mboxidnr, msgflags, NULL, IMAPFA_ADD, NULL);
		}
		message->id = newmsgidnr;
		return DSN_CLASS_OK;
	}
}
// from dm_pipe.c

static int valid_sender(const char *addr) 
{
	int ret = 1;
	char *testaddr;
	testaddr = g_ascii_strdown(addr, -1);
	if (strstr(testaddr, "mailer-daemon@"))
		ret = 0;
	if (strstr(testaddr, "daemon@"))
		ret = 0;
	if (strstr(testaddr, "postmaster@"))
		ret = 0;
	g_free(testaddr);
	return ret;
}

static int parse_and_escape(const char *in, char **out)
{
	InternetAddressList *ialist;
	InternetAddress *ia;

	TRACE(TRACE_DEBUG, "parsing address [%s]", in);
	ialist = internet_address_parse_string(in);
	if (!ialist) {
                TRACE(TRACE_MESSAGE, "unable to parse email address [%s]", in);
                return -1;
	}

        ia = ialist->address;
        if (!ia || ia->type != INTERNET_ADDRESS_NAME) {
		TRACE(TRACE_MESSAGE, "unable to parse email address [%s]", in);
		internet_address_list_destroy(ialist);
		return -1;
	}

	if (! (*out = dm_shellesc(ia->value.addr))) {
		TRACE(TRACE_ERROR, "out of memory calling dm_shellesc");
		internet_address_list_destroy(ialist);
		return -1;
	}

	internet_address_list_destroy(ialist);

	return 0;
}
/* Sends a message. */
int send_mail(DbmailMessage *message,
		const char *to, const char *from,
		const char *preoutput,
		enum sendwhat sendwhat, char *sendmail_external)
{
	FILE *mailpipe = NULL;
	char *escaped_to = NULL;
	char *escaped_from = NULL;
	char *message_string = NULL;
	char *sendmail_command = NULL;
	field_t sendmail, postmaster;
	int result;

	if (!from || strlen(from) < 1) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
		}
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}

	if (config_get_value("SENDMAIL", "DBMAIL", sendmail) < 0) {
		TRACE(TRACE_ERROR, "error getting value for SENDMAIL in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (strlen(sendmail) < 1) {
		TRACE(TRACE_ERROR, "SENDMAIL not set in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (!sendmail_external) {
		if (parse_and_escape(to, &escaped_to) < 0) {
			TRACE(TRACE_MESSAGE, "could not prepare 'to' address.");
			return 1;
		}
		if (parse_and_escape(from, &escaped_from) < 0) {
			g_free(escaped_to);
			TRACE(TRACE_MESSAGE, "could not prepare 'from' address.");
			return 1;
		}
		sendmail_command = g_strconcat(sendmail, " -f ", escaped_from, " ", escaped_to, NULL);
		g_free(escaped_to);
		g_free(escaped_from);
		if (!sendmail_command) {
			TRACE(TRACE_ERROR, "out of memory calling g_strconcat");
			return -1;
		}
	} else {
		sendmail_command = sendmail_external;
	}

	TRACE(TRACE_INFO, "opening pipe to [%s]", sendmail_command);

	if (!(mailpipe = popen(sendmail_command, "w"))) {
		TRACE(TRACE_ERROR, "could not open pipe to sendmail");
		g_free(sendmail_command);
		return 1;
	}

	TRACE(TRACE_DEBUG, "pipe opened");

	switch (sendwhat) {
	case SENDRAW:
		// This is a hack so forwards can give a From line.
		if (preoutput)
			fprintf(mailpipe, "%s\n", preoutput);
		// This function will dot-stuff the message.
		db_send_message_lines(mailpipe, message->id, -2, 1);
		break;
	case SENDMESSAGE:
		message_string = dbmail_message_to_string(message);
		fprintf(mailpipe, "%s", message_string);
		g_free(message_string);
		break;
	default:
		TRACE(TRACE_ERROR, "invalid sendwhat in call to send_mail: [%d]", sendwhat);
		break;
	}

	result = pclose(mailpipe);
	TRACE(TRACE_DEBUG, "pipe closed");

	/* Adapted from the Linux waitpid 2 man page. */
	if (WIFEXITED(result)) {
		result = WEXITSTATUS(result);
		TRACE(TRACE_INFO, "sendmail exited normally");
	} else if (WIFSIGNALED(result)) {
		result = WTERMSIG(result);
		TRACE(TRACE_INFO, "sendmail was terminated by signal");
	} else if (WIFSTOPPED(result)) {
		result = WSTOPSIG(result);
		TRACE(TRACE_INFO, "sendmail was stopped by signal");
	}

	if (result != 0) {
		TRACE(TRACE_ERROR, "sendmail error return value was [%d]", result);

		if (!sendmail_external)
			g_free(sendmail_command);
		return 1;
	}

	if (!sendmail_external)
		g_free(sendmail_command);
	return 0;
} 
int send_forward_list(DbmailMessage *message, GList *targets, const char *from)
{
	int result = 0;
	field_t postmaster;

	if (!from) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0)
			TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}
	targets = g_list_first(targets);
	TRACE(TRACE_INFO, "delivering to [%u] external addresses", g_list_length(targets));
	while (targets) {
		char *to = (char *)targets->data;

		if (!to || strlen(to) < 1) {
			TRACE(TRACE_ERROR, "forwarding address is zero length, message not forwarded.");
		} else {
			if (to[0] == '!') {
				// The forward is a command to execute.
				// Prepend an mbox From line.
				char timestr[50];
				time_t td;
				struct tm tm;
				char *fromline;
                        
				time(&td);		/* get time */
				tm = *localtime(&td);	/* get components */
				strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y", &tm);
                        
				TRACE(TRACE_DEBUG, "prepending mbox style From header to pipe returnpath: %s", from);
                        
				/* Format: From<space>address<space><space>Date */
				fromline = g_strconcat("From ", from, "  ", timestr, NULL);

				result |= send_mail(message, "", "", fromline, SENDRAW, to+1);
				g_free(fromline);
			} else if (to[0] == '|') {
				// The forward is a command to execute.
				result |= send_mail(message, "", "", NULL, SENDRAW, to+1);

			} else {
				// The forward is an email address.
				result |= send_mail(message, to, from, NULL, SENDRAW, SENDMAIL);
			}
		}
		if (! g_list_next(targets))
			break;
		targets = g_list_next(targets);

	}

	return result;
}

/* 
 * Send an automatic notification.
 */
static int send_notification(DbmailMessage *message UNUSED, const char *to)
{
	field_t from = "";
	field_t subject = "";
	int result;

	if (config_get_value("POSTMASTER", "DBMAIL", from) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
	}

	if (config_get_value("AUTO_NOTIFY_SENDER", "DELIVERY", from) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for AUTO_NOTIFY_SENDER");
	}

	if (config_get_value("AUTO_NOTIFY_SUBJECT", "DELIVERY", subject) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for AUTO_NOTIFY_SUBJECT");
	}

	if (strlen(from) < 1)
		g_strlcpy(from, AUTO_NOTIFY_SENDER, FIELDSIZE);

	if (strlen(subject) < 1)
		g_strlcpy(subject, AUTO_NOTIFY_SUBJECT, FIELDSIZE);

	DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, subject, "");

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	dbmail_message_free(new_message);

	return result;
}


	
/*
 * Send an automatic reply.
 */
#define REPLY_DAYS 7
static int send_reply(DbmailMessage *message, const char *body)
{
	const char *from, *to, *subject;
	const char *x_dbmail_reply;
	int result;

	x_dbmail_reply = dbmail_message_get_header(message, "X-Dbmail-Reply");
	if (x_dbmail_reply) {
		TRACE(TRACE_MESSAGE, "reply loop detected [%s]", x_dbmail_reply);
		return 0;
	}
	
	subject = dbmail_message_get_header(message, "Subject");

	from = dbmail_message_get_header(message, "Delivered-To");
	if (!from)
		from = message->envelope_recipient->str;
	if (!from)
		from = ""; // send_mail will change this to DEFAULT_POSTMASTER

	to = dbmail_message_get_header(message, "Reply-To");
	if (!to)
		to = dbmail_message_get_header(message, "Return-Path");
	if (!to) {
		TRACE(TRACE_ERROR, "no address to send to");
		return 0;
	}
	if (!valid_sender(to)) {
		TRACE(TRACE_DEBUG, "sender invalid. skip auto-reply.");
		return 0;
	}

	if (db_replycache_validate(to, from, "replycache", REPLY_DAYS) != DM_SUCCESS) {
		TRACE(TRACE_DEBUG, "skip auto-reply");
		return 0;
	}

	char *newsubject = g_strconcat("Re: ", subject, NULL);

	DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, from, to, newsubject, body);
	dbmail_message_set_header(new_message, "X-DBMail-Reply", from);

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	if (result == 0) {
		db_replycache_register(to, from, "replycache");
	}

	g_free(newsubject);
	dbmail_message_free(new_message);

	return result;
}


/* Yeah, RAN. That's Reply And Notify ;-) */
static int execute_auto_ran(DbmailMessage *message, u64_t useridnr)
{
	field_t val;
	int do_auto_notify = 0, do_auto_reply = 0;
	char *reply_body = NULL;
	char *notify_address = NULL;

	/* message has been succesfully inserted, perform auto-notification & auto-reply */
	if (config_get_value("AUTO_NOTIFY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERROR, "error getting config value for AUTO_NOTIFY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (config_get_value("AUTO_REPLY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERROR, "error getting config value for AUTO_REPLY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	if (do_auto_notify != 0) {
		TRACE(TRACE_DEBUG, "starting auto-notification procedure");

		if (db_get_notify_address(useridnr, &notify_address) != 0)
			TRACE(TRACE_ERROR, "error fetching notification address");
		else {
			if (notify_address == NULL)
				TRACE(TRACE_DEBUG, "no notification address specified, skipping");
			else {
				TRACE(TRACE_DEBUG, "sending notifcation to [%s]", notify_address);
				if (send_notification(message, notify_address) < 0) {
					TRACE(TRACE_ERROR, "error in call to send_notification.");
					g_free(notify_address);
					return -1;
				}
				g_free(notify_address);
			}
		}
	}

	if (do_auto_reply != 0) {
		TRACE(TRACE_DEBUG, "starting auto-reply procedure");

		if (db_get_reply_body(useridnr, &reply_body) != 0)
			TRACE(TRACE_ERROR, "error fetching reply body");
		else {
			if (reply_body == NULL || reply_body[0] == '\0')
				TRACE(TRACE_DEBUG, "no reply body specified, skipping");
			else {
				if (send_reply(message, reply_body) < 0) {
					TRACE(TRACE_ERROR, "error in call to send_reply");
					g_free(reply_body);
					return -1;
				}
				g_free(reply_body);
				
			}
		}
	}

	return 0;
}


/* Here's the real *meat* of this source file!
 *
 * Function: insert_messages()
 * What we get:
 *   - A pointer to the incoming message stream
 *   - The header of the message 
 *   - A list of destination addresses / useridnr's
 *   - The default mailbox to delivery to
 *
 * What we do:
 *   - Read in the rest of the message
 *   - Store the message to the DBMAIL user
 *   - Process the destination addresses into lists:
 *     - Local useridnr's
 *     - External forwards
 *     - No such user bounces
 *   - Store the local useridnr's
 *     - Run the message through each user's sorting rules
 *     - Potentially alter the delivery:
 *       - Different mailbox
 *       - Bounce
 *       - Reply with vacation message
 *       - Forward to another address
 *     - Check the user's quota before delivering
 *       - Do this *after* their sorting rules, since the
 *         sorting rules might not store the message anyways
 *   - Send out the no such user bounces
 *   - Send out the external forwards
 *   - Delete the temporary message from the database
 * What we return:
 *   - 0 on success
 *   - -1 on full failure
 */
int insert_messages(DbmailMessage *message, GList *dsnusers)
{
	u64_t bodysize, rfcsize;
	u64_t tmpid;
	u64_t msgsize;
	int result=0;

 	delivery_status_t final_dsn;

	/* first start a new database transaction */

	if ((result = dbmail_message_store(message)) == DM_EQUERY) {
		TRACE(TRACE_ERROR,"storing message failed");
		return result;
	} 

	TRACE(TRACE_DEBUG, "temporary msgidnr is [%llu]", message->id);

	tmpid = message->id; // for later removal

	bodysize = (u64_t)dbmail_message_get_body_size(message, FALSE);
	rfcsize = (u64_t)dbmail_message_get_rfcsize(message);
	msgsize = (u64_t)dbmail_message_get_size(message, FALSE);

	// TODO: Run a Sieve script associated with the internal delivery user.
	// Code would go here, after we've inserted the message blocks but
	// before we've started delivering the message.

	/* Loop through the users list. */
	dsnusers = g_list_first(dsnusers);
	while (dsnusers) {
		
		GList *userids;

		int has_2 = 0, has_4 = 0, has_5 = 0, has_5_2 = 0;
		
		deliver_to_user_t *delivery = (deliver_to_user_t *) dsnusers->data;
		
		/* Each user may have a list of user_idnr's for local
		 * delivery. */
		userids = g_list_first(delivery->userids);
		while (userids) {
			u64_t *useridnr = (u64_t *) userids->data;
			TRACE(TRACE_DEBUG, "calling sort_and_deliver for useridnr [%llu]", *useridnr);

			switch (sort_and_deliver(message, delivery->address, *useridnr, delivery->mailbox, delivery->source)) {
			case DSN_CLASS_OK:
				TRACE(TRACE_INFO, "successful sort_and_deliver for useridnr [%llu]", *useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				TRACE(TRACE_ERROR, "permanent failure sort_and_deliver for useridnr [%llu]", *useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_QUOTA:
				TRACE(TRACE_MESSAGE, "mailbox over quota, message rejected for useridnr [%llu]", *useridnr);
				has_5_2 = 1;
				break;
			case DSN_CLASS_TEMP:
			default:
				TRACE(TRACE_ERROR, "unknown temporary failure in sort_and_deliver for useridnr [%llu]", *useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			if (execute_auto_ran(message, *useridnr) < 0)
				TRACE(TRACE_ERROR, "error in execute_auto_ran(), but continuing delivery normally.");

			if (! g_list_next(userids))
				break;
			userids = g_list_next(userids);

		}

		final_dsn.class = dsnuser_worstcase_int(has_2, has_4, has_5, has_5_2);
		switch (final_dsn.class) {
		case DSN_CLASS_OK:
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			break;
		case DSN_CLASS_TEMP:
			/* sort_and_deliver returns TEMP is useridnr is 0, aka,
			 * if nothing was delivered at all, or for any other failures. */	

			/* If there's a problem with the delivery address, but
			 * there are proper forwarding addresses, we're OK. */
			if (g_list_length(delivery->forwards) > 0) {
				/* Success. Address related. Valid. */
				set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
				break;
			}
			/* Fall through to FAIL. */
		case DSN_CLASS_FAIL:
			/* Permanent failure. Address related. Does not exist. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			break;
		case DSN_CLASS_QUOTA:
			/* Permanent failure. Mailbox related. Over quota limit. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 2, 2);
			break;
		case DSN_CLASS_NONE:
			/* Leave the DSN status at whatever dsnuser_resolve set it at. */
			break;
		}

		TRACE(TRACE_DEBUG, "deliver [%u] messages to external addresses", g_list_length(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (g_list_length(delivery->forwards) > 0) {

			TRACE(TRACE_DEBUG, "delivering to external addresses");
			const char *from = dbmail_message_get_header(message, "Return-Path");

			/* Forward using the temporary stored message. */
			if (send_forward_list(message, delivery->forwards, from) < 0) {
				/* If forward fails, tell the sender that we're
				 * having a transient error. They'll resend. */
				TRACE(TRACE_MESSAGE, "forwaring failed, reporting transient error.");
				set_dsn(&delivery->dsn, DSN_CLASS_TEMP, 1, 1);
			}
		}
		if (! g_list_next(dsnusers))
			break;
		dsnusers = g_list_next(dsnusers);

	}

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	if (db_delete_message(tmpid) < 0) 
		TRACE(TRACE_ERROR, "failed to delete temporary message [%llu]", message->id);
	TRACE(TRACE_DEBUG, "temporary message deleted from database. Done.");

	return 0;
}

