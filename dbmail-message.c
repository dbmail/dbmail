/*
  $Id: dbmail-message.c 2331 2006-10-26 20:47:22Z paul $

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


extern db_param_t _db_params;
#define DBPFX _db_params.pfx

#define MESSAGE_MAX_LINE_SIZE 1024
/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

static int dm_errno = 0;

#define DBMAIL_TEMPMBOX "INBOX"

#define THIS_MODULE "message"
/*
 * _register_header
 *
 * register a message header in a ghashtable dictionary
 *
 */
static void _register_header(const char *header, const char *value, gpointer user_data);
static gboolean _header_cache(const char *header, const char *value, gpointer user_data);

static struct DbmailMessage * _retrieve(struct DbmailMessage *self, char *query_template);
static void _map_headers(struct DbmailMessage *self);
static void _set_content(struct DbmailMessage *self, const GString *content);
static void _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type);
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

gchar * get_crlf_encoded_opt(gchar *string, int dots)
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

/* Useful for debugging. Uncomment if/when needed.
 *//*
static void dump_to_file(const char *filename, const char *buf)
{
	gint se;
	g_assert(filename);
	FILE *f = fopen(filename,"a");
	if (! f) {
		se=errno;
		trace(TRACE_DEBUG,"%s,%s: opening dumpfile failed [%s]",
				__FILE__, __func__, strerror(se));
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

struct DbmailMessage * dbmail_message_new(void)
{
	struct DbmailMessage *self = g_new0(struct DbmailMessage,1);
	if (! self) {
		trace(TRACE_ERROR, "%s,%s: memory error", __FILE__, __func__);
		return NULL;
	}
	
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

void dbmail_message_free(struct DbmailMessage *self)
{
	if (! self)
		return;

	if (self->headers)
		g_relation_destroy(self->headers);
	if (self->content)
		g_object_unref(self->content);
	if (self->raw)
		g_byte_array_free(self->raw,TRUE);
	
	self->headers=NULL;
	self->content=NULL;
	self->raw=NULL;
	
	g_string_free(self->envelope_recipient,TRUE);
	g_hash_table_destroy(self->header_dict);
	g_tree_destroy(self->header_name);
	g_tree_destroy(self->header_value);
	
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
int dbmail_message_get_class(const struct DbmailMessage *self)
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

	if (! (GMIME_IS_MESSAGE(self->content))) {
		dbmail_message_set_class(self, DBMAIL_MESSAGE_PART);
		g_object_unref(self->content);
		self->content=NULL;
		_set_content(self, content);
	}
	
	_map_headers(self);
	
	return self;
}

// 
// construct a new message where only sender, recipient, subject and 
// a body are known. The body can be any kind of charset/encoding, and we 
// let gmime decide which is which.
//
// TODO: support text/html

struct DbmailMessage * dbmail_message_construct(struct DbmailMessage *self, 
		const gchar *to, const gchar *from, 
		const gchar *subject, const gchar *body)
{
	GMimeMessage *message;
	GMimePart *mime_part;
	GMimeDataWrapper *content;
	GMimeStream *stream, *fstream;
	GMimeContentType *mime_type;
	const gchar *charset;
	GMimePartEncodingType encoding;
	GMimeFilter *filter = NULL;

	// FIXME: this could easily be expanded to allow appending
	// a new sub-part to an existing mime-part. But for now let's
	// require self to be a pristine (empty) DbmailMessage.
	g_return_val_if_fail(self->content==NULL, self);
	
	message = g_mime_message_new(FALSE);

	// determine the optimal encoding type for the body: how would gmime
	// encode this string
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
		case GMIME_PART_ENCODING_UUENCODE:
			filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_UU_ENC);
			break;
		// treat the rest as-is: assumption is the body is already in
		// an encoded state
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
		case GMIME_PART_ENCODING_DEFAULT:
		case GMIME_PART_ENCODING_7BIT:
		case GMIME_PART_ENCODING_8BIT:
		case GMIME_PART_ENCODING_BINARY:
		case GMIME_PART_NUM_ENCODINGS:
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
	if ((charset = g_mime_charset_best(body,strlen(body))) != NULL)
		g_mime_object_set_content_type_parameter((GMimeObject *)mime_part, "charset", charset);

	// Content-Transfer-Encoding
	switch(encoding) {
		case GMIME_PART_ENCODING_DEFAULT:
		case GMIME_PART_ENCODING_7BIT:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "7bit");
			break;
		case GMIME_PART_ENCODING_8BIT:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "8bit");
			break;
		case GMIME_PART_ENCODING_BINARY:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "binary");
			break;
		case GMIME_PART_ENCODING_BASE64:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "base64");
			break;
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "quoted-printable");
			break;

		case GMIME_PART_ENCODING_UUENCODE: // anyone still uses this?
		case GMIME_PART_NUM_ENCODINGS: // what's this?
		default:
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


/* \brief initialize a previously created DbmailMessage using a GMimeStream
 * \param empty DbmailMessage
 * \param stream from which to read
 * \param type which indicates either pipe/network style streaming
 * \return the filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
{
	_set_content_from_stream(self,stream,type);
	_map_headers(self);
	return self;
}

static void _set_content(struct DbmailMessage *self, const GString *content)
{

	GMimeStream *stream;
	if (self->raw) {
		g_byte_array_free(self->raw,TRUE);
		self->raw = NULL;
	}
	
	self->raw = g_byte_array_new();
	self->raw = g_byte_array_append(self->raw,(guint8 *)content->str, content->len+1);
	//stream = g_mime_stream_mem_new_with_byte_array(self->raw);
	stream = g_mime_stream_mem_new_with_buffer(content->str, content->len+1);
	_set_content_from_stream(self, stream, DBMAIL_STREAM_PIPE);
	g_mime_stream_close(stream);
	g_object_unref(stream);
}

static void _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
{
	/* 
	 * We convert all messages to crlf->lf for internal usage and
	 * db-insertion
	 */
	
	GMimeStream *fstream, *bstream, *mstream;
	GMimeFilter *filter;
	GMimeParser *parser;
	gchar *buf, *from;
	size_t t;
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

			bstream = g_mime_stream_buffer_new(stream,GMIME_STREAM_BUFFER_BLOCK_READ);
			mstream = g_mime_stream_mem_new();
			fstream = g_mime_stream_filter_new_with_stream(mstream);
			filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
			g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
			
			while ((t = g_mime_stream_buffer_gets(bstream, buf, MESSAGE_MAX_LINE_SIZE))) {
				if (firstline && strncmp(buf,"From ",5)==0)
					g_mime_parser_set_scan_from(parser,TRUE);
				firstline=FALSE;

				if ((type==DBMAIL_STREAM_LMTP) && (strncmp(buf,".\r\n",3)==0))
					break;
				g_mime_stream_write_string(fstream, buf);
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
			trace(TRACE_DEBUG,"%s,%s: parse message",__FILE__,__func__);
			self->content = GMIME_OBJECT(g_mime_parser_construct_message(parser));
			if (g_mime_parser_get_scan_from(parser)) {
				from = g_mime_parser_get_from(parser);
				dbmail_message_set_internal_date(self, from);
				g_free(from);
			}

			break;
		case DBMAIL_MESSAGE_PART:
		trace(TRACE_DEBUG,"%s,%s: parse part",__FILE__,__func__);
			self->content = GMIME_OBJECT(g_mime_parser_construct_part(parser));
			break;
	}
	
	g_object_unref(parser);
}

static void _map_headers(struct DbmailMessage *self) 
{
	GMimeObject *part;
	assert(self->content);
	self->headers = g_relation_new(2);
	g_relation_index(self->headers, 0, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);
	g_relation_index(self->headers, 1, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);

	 // gmime doesn't consider the content-type header to be a message-header so extract 
	 // and register it separately
	if (GMIME_IS_MESSAGE(self->content)) {
		char *type = NULL;
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
	struct DbmailMessage *m = (struct DbmailMessage *)user_data;
	if (! (hname = g_tree_lookup(m->header_name,header))) {
		g_tree_insert(m->header_name,(gpointer)header,(gpointer)header);
		hname = header;
	}
	if (! (hvalue = g_tree_lookup(m->header_value,value))) {
		g_tree_insert(m->header_value,(gpointer)value,(gpointer)value);
		hvalue = value;
	}
	
	if (! g_relation_exists(m->headers, hname, hvalue))
		g_relation_insert(m->headers, hname, hvalue);
}

void dbmail_message_set_physid(struct DbmailMessage *self, u64_t physid)
{
	self->physid = physid;
}

u64_t dbmail_message_get_physid(const struct DbmailMessage *self)
{
	return self->physid;
}

void dbmail_message_set_internal_date(struct DbmailMessage *self, char *internal_date)
{
	if (internal_date)
		self->internal_date = g_mime_utils_header_decode_date(internal_date, self->internal_date_gmtoff);
}

gchar * dbmail_message_get_internal_date(const struct DbmailMessage *self)
{
	char *res;
	struct tm gmt;
	if (! self->internal_date)
		return NULL;
	
	res = g_new0(char, TIMESTRING_SIZE+1);
	memset(&gmt,'\0', sizeof(struct tm));
	gmtime_r(&self->internal_date, &gmt);
	strftime(res, TIMESTRING_SIZE, "%Y-%m-%d %T", &gmt);
	return res;
}

void dbmail_message_set_envelope_recipient(struct DbmailMessage *self, const char *envelope_recipient)
{
	if (envelope_recipient)
		g_string_printf(self->envelope_recipient,"%s", envelope_recipient);
}

gchar * dbmail_message_get_envelope_recipient(const struct DbmailMessage *self)
{
	if (self->envelope_recipient->len > 0)
		return self->envelope_recipient->str;
	return NULL;
}

void dbmail_message_set_header(struct DbmailMessage *self, const char *header, const char *value)
{
	g_mime_message_set_header(GMIME_MESSAGE(self->content), header, value);
}

const gchar * dbmail_message_get_header(const struct DbmailMessage *self, const char *header)
{
	return g_mime_object_get_header(GMIME_OBJECT(self->content), header);
}

GTuples * dbmail_message_get_header_repeated(const struct DbmailMessage *self, const char *header)
{
	const char *hname;
	if (! (hname = g_tree_lookup(self->header_name,header)))
		hname = header;
	return g_relation_select(self->headers, hname, 0);
}

GList * dbmail_message_get_header_addresses(struct DbmailMessage *message, const char *field_name)
{
	InternetAddressList *ialisthead, *ialist;
	InternetAddress *ia;
	GList *result = NULL;
	const char *field_value;

	if (!message || !field_name) {
		trace(TRACE_WARNING, "%s,%s: received a NULL argument, this is a bug",
				__FILE__, __func__);
		return NULL; 
	}

	field_value = dbmail_message_get_header(message, field_name);
	trace(TRACE_INFO, "%s,%s: mail address parser looking at field [%s] with value [%s]",
			__FILE__, __func__, field_name, field_value);
	
	if ((ialist = internet_address_parse_string(field_value)) == NULL) {
		trace(TRACE_MESSAGE, "%s,%s: mail address parser error parsing header field",
			__FILE__, __func__);
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

	trace(TRACE_DEBUG, "%s,%s: mail address parser found [%d] email addresses",
			__FILE__, __func__, g_list_length(result));

	return result;
}

/* dump message(parts) to char ptrs */
gchar * dbmail_message_to_string(const struct DbmailMessage *self) 
{
	return g_mime_object_to_string(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_body_to_string(const struct DbmailMessage *self)
{
	return g_mime_object_get_body(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_hdrs_to_string(const struct DbmailMessage *self)
{
	gchar *h,*s;
	GString *m, *b;
	
	s = dbmail_message_to_string(self);
	m = g_string_new(s);
	g_free(s);

	s = dbmail_message_body_to_string(self);
	b = g_string_new(s);
	g_free(s);

	m = g_string_truncate(m,(m->len - b->len));
	h = m->str;
	
	g_string_free(b,TRUE);
	g_string_free(m,FALSE);
	
	return h;
}

/* 
 * Some dynamic accessors.
 * 
 * Don't cache these values to allow changes in message content!!
 * 
 */
size_t dbmail_message_get_size(const struct DbmailMessage *self, gboolean crlf)
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
size_t dbmail_message_get_hdrs_size(const struct DbmailMessage *self, gboolean crlf)
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
size_t dbmail_message_get_body_size(const struct DbmailMessage *self, gboolean crlf)
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
	GString *m;
	
	assert(dbmail_message_get_physid(self));
	
	snprintf(query, DEF_QUERYSIZE, query_template, DBPFX, 
			dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: sql error", __FILE__, __func__);
		return NULL;
	}
	
	rows = db_num_rows();
	if (rows < 1) {
		trace(TRACE_ERROR, "%s,%s: blk error", __FILE__, __func__);
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	m = g_string_new("");
	for (row=0; row < rows; row++)
		g_string_append_printf(m, "%s", db_get_result(row,0));
	
	db_free_result();
	
	self = dbmail_message_init_with_string(self,m);
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
static struct DbmailMessage * _fetch_head(struct DbmailMessage *self)
{
	char *query_template = 	"SELECT messageblk "
		"FROM %smessageblks "
		"WHERE physmessage_id = %llu "
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
		"WHERE physmessage_id = %llu "
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
		trace(TRACE_ERROR, "%s,%s: retrieval failed for physid [%llu]", 
			__FILE__, __func__, dbmail_message_get_physid(self));
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
	char *domainname;
	char *message_id;
	
	switch (auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr)) {
	case -1:
		trace(TRACE_ERROR, "%s,%s: unable to find user_idnr for user [%s]",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
		break;
	case 0:
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user "
		      "[%s]. Make sure this system user is in the database!",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
		break;
	}
	
	create_unique_id(unique_id, user_idnr);
	/* create a message record */
	if(_message_insert(self, user_idnr, DBMAIL_TEMPMBOX, unique_id) < 0)
		return -1;

	/* make sure the message has a message-id, else threading breaks */
	if (! (message_id = (char *)g_mime_message_get_message_id(GMIME_MESSAGE(self->content)))) {
		domainname = g_new0(gchar, 255);
		if (getdomainname(domainname,255))
			strcpy(domainname,"(none)");
		message_id = g_mime_utils_generate_message_id(domainname);
		g_mime_message_set_message_id(GMIME_MESSAGE(self->content), message_id);
		g_free(message_id);
		g_free(domainname);
	}

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
	if (dbmail_message_cache_headers(self) < 0)
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
	char *physid = NULL;

	assert(unique_id);
	assert(mailbox);

	if (db_find_create_mailbox(mailbox, BOX_DEFAULT, user_idnr, &mailboxid) == -1)
		return -1;
	
	if (mailboxid == 0) {
		trace(TRACE_ERROR, "%s,%s: mailbox [%s] could not be found!", 
				__FILE__, __func__, mailbox);
		return -1;
	}

	/* insert a new physmessage entry */
	internal_date = dbmail_message_get_internal_date(self);
	if (db_insert_physmessage_with_internal_date(internal_date, &physmessage_id) == -1)  {
		g_free(internal_date);
		return -1;
	}
	g_free(internal_date);

	/* insert the physmessage-id into the message-headers */
	physid = g_strdup_printf("%llu", physmessage_id);
	dbmail_message_set_physid(self, physmessage_id);
	dbmail_message_set_header(self, "X-DBMail-PhysMessage-ID", physid);
	g_free(physid);
	
	/* now insert an entry into the messages table */
	snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
		 "%smessages(mailbox_idnr, physmessage_id, unique_id,"
		 "recent_flag, status) "
		 "VALUES (%llu, %llu, '%s', 1, %d)",
		 DBPFX, mailboxid, physmessage_id, unique_id,
		 MESSAGE_STATUS_INSERT);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __func__);
		return -1;
	}

	self->id = db_insert_result("message_idnr");
	return 1;
}



int dbmail_message_cache_headers(const struct DbmailMessage *self)
{
	assert(self);
	assert(self->physid);

	g_tree_foreach(self->header_name, (GTraverseFunc)_header_cache, (gpointer)self);
	
	dbmail_message_cache_tofield(self);
	dbmail_message_cache_ccfield(self);
	dbmail_message_cache_fromfield(self);
	dbmail_message_cache_datefield(self);
	dbmail_message_cache_replytofield(self);
	dbmail_message_cache_subjectfield(self);
	dbmail_message_cache_referencesfield(self);
	dbmail_message_cache_envelope(self);

	return 1;
}
#define CACHE_WIDTH_VALUE 255
#define CACHE_WIDTH_FIELD 255
#define CACHE_WIDTH_ADDR 100
#define CACHE_WIDTH_NAME 100


static int _header_get_id(const struct DbmailMessage *self, const char *header, u64_t *id)
{
	u64_t tmp;
	gpointer cacheid;
	gchar *safe_header;

	if (! (safe_header = dm_strnesc(header,CACHE_WIDTH_NAME)))
		return -1;

	cacheid = g_hash_table_lookup(self->header_dict, (gconstpointer)safe_header);
	if (cacheid) {
		*id = GPOINTER_TO_UINT(cacheid);
		g_free(safe_header);
		return 1;
	}
		
	GString *q = g_string_new("");
	g_string_printf(q, "SELECT id FROM %sheadername WHERE headername='%s'", DBPFX, safe_header);
	if (db_query(q->str) == -1) {
		g_string_free(q,TRUE);
		g_free(safe_header);
		return -1;
	}
	if (db_num_rows() < 1) {
		db_free_result();
		g_string_printf(q, "INSERT INTO %sheadername (headername) VALUES ('%s')", DBPFX, safe_header);
		if (db_query(q->str) == -1) {
			g_string_free(q,TRUE);
			g_free(safe_header);
			return -1;
		}
		tmp = db_insert_result("headername_idnr");
	} else {
		tmp = db_get_result_u64(0,0);
		db_free_result();
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
	struct DbmailMessage *self = (struct DbmailMessage *)user_data;
	gchar *safe_value;
	GString *q;
	GTuples *values;
	const char *value;
	unsigned i;

	dm_errno = 0;

	/* skip headernames with spaces like From_ */
	if (strchr(header, ' '))
		return FALSE;

	if ((_header_get_id(self, header, &id) < 0))
		return TRUE;

	q = g_string_new("");
	values = g_relation_select(self->headers,header,0);
	for (i=0; i<values->len;i++) {
		value = g_tuples_index(values,i,1);

		safe_value = dm_stresc(value);
		g_string_printf(q,"INSERT INTO %sheadervalue (headername_id, physmessage_id, headervalue) "
				"VALUES (%llu,%llu,'%s')", DBPFX, id, self->physid, safe_value);
		g_free(safe_value);

		if (db_query(q->str)) {
			trace(TRACE_ERROR,"%s,%s: insert headervalue failed",
			      __FILE__,__func__);
			g_string_free(q,TRUE);
			g_tuples_destroy(values);
			return TRUE;
		}
	}
	g_string_free(q,TRUE);
	g_tuples_destroy(values);
	return FALSE;
}

static void insert_address_cache(u64_t physid, const char *field, InternetAddressList *ialist)
{
	InternetAddress *ia;
	
	g_return_if_fail(ialist != NULL);

	GString *q = g_string_new("");
	gchar *name;
	gchar *addr;

	for (; ialist != NULL && ialist->address; ialist = ialist->next) {
		
		ia = ialist->address;
		g_return_if_fail(ia != NULL);

		/* address fields are truncated to column width */
		name = dm_strnesc(ia->name ? ia->name : "", CACHE_WIDTH_ADDR);
		addr = dm_strnesc(ia->value.addr ? ia->value.addr : "", CACHE_WIDTH_ADDR);
		
		g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sname, %saddr) "
				"VALUES (%llu,'%s','%s')", DBPFX, field, field, field, 
				physid, name, addr);
		
		g_free(name);
		g_free(addr);
		
		if (db_query(q->str)) {
			trace(TRACE_ERROR, "%s,%s: insert %sfield failed [%s]",
					__FILE__, __func__, field, q->str);
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
		trace(TRACE_ERROR, "%s,%s: insert %sfield failed [%s]",
				__FILE__, __func__, field, q->str);
	}
	g_string_free(q,TRUE);
}


void dbmail_message_cache_tofield(const struct DbmailMessage *self)
{
	InternetAddressList *list;

	list = (InternetAddressList *)g_mime_message_get_recipients((GMimeMessage *)(self->content), GMIME_RECIPIENT_TYPE_TO);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "to", list);
}

void dbmail_message_cache_ccfield(const struct DbmailMessage *self)
{
	InternetAddressList *list;
	
	list = (InternetAddressList *)g_mime_message_get_recipients((GMimeMessage *)(self->content), GMIME_RECIPIENT_TYPE_CC);
	if (list == NULL)
		return;
	insert_address_cache(self->physid, "cc", list);
	
}
void dbmail_message_cache_fromfield(const struct DbmailMessage *self)
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
void dbmail_message_cache_replytofield(const struct DbmailMessage *self)
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

void dbmail_message_cache_datefield(const struct DbmailMessage *self)
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

void dbmail_message_cache_subjectfield(const struct DbmailMessage *self)
{
	char *value;
	char *subject, *s;
	
	value = (char *)dbmail_message_get_header(self,"Subject");
	if (! value) {
		trace(TRACE_MESSAGE,"%s,%s: no subject field value [%llu]",
				__FILE__, __func__, self->physid);
		return;
	}
	
	if (! (subject = g_strdup(value)))
		return;

	s = subject;
	dm_base_subject(s);

	insert_field_cache(self->physid, "subject", s);
	
	g_free(subject);
}

void dbmail_message_cache_referencesfield(const struct DbmailMessage *self)
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
	
void dbmail_message_cache_envelope(const struct DbmailMessage *self)
{
	GString *q;
	char *envelope, *clean;

	envelope = imap_get_envelope(GMIME_MESSAGE(self->content));
	
	clean = dm_stresc(envelope);

	q = g_string_new("");

	g_string_printf(q, "INSERT INTO %senvelope (physmessage_id, envelope) "
			"VALUES (%llu,'%s')", DBPFX, self->physid, clean);

	g_free(clean);

	if (db_query(q->str)) {
		trace(TRACE_ERROR, "%s,%s: insert envelope failed [%s]",
				__FILE__, __func__, q->str);
	}
	g_string_free(q,TRUE);

}

/* old stuff moved here from dbmsgbuf.c */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter)
{
	struct DbmailMessage *msg;

	int result;
	u64_t physid = 0;
	if ((result = db_get_physmessage_id(msg_idnr, &physid)) != DM_SUCCESS)
		return NULL;
	msg = dbmail_message_new();
	if (! (msg = dbmail_message_retrieve(msg, physid, filter)))
		return NULL;

	return msg;
}
