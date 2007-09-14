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

/* 
 * 
 * dm_imaputil.c
 *
 * IMAP-server utility functions implementations
 */

#include "dbmail.h"

#define THIS_MODULE "imapsession"
#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512
#define IDLE_TIMEOUT 30
#define RANGE_SIZE 128

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* cache */
cache_t cached_msg;

gboolean imap_feature_idle_status = FALSE;

extern const char *month_desc[];
/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "03-Nov-1979 00:00:00 +0000"
extern char _imapdate[IMAP_INTERNALDATE_LEN];

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"
extern char _sqldate[SQL_INTERNALDATE_LEN + 1];
extern const int month_len[];
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];

extern volatile sig_atomic_t alarm_occured;

static int _imap_session_fetch_parse_partspec(struct ImapSession *self);
static int _imap_session_fetch_parse_octet_range(struct ImapSession *self);

static void _imap_show_body_sections(struct ImapSession *self);
static void _fetch_envelopes(struct ImapSession *self);
static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data);
	


/*
 * send_data()
 *
 * sends cnt bytes from a MEM structure to a FILE stream
 * uses a simple buffering system
 */
static void send_data(FILE * to, MEM * from, int cnt)
{
	char buf[SEND_BUF_SIZE];

	for (cnt -= SEND_BUF_SIZE; cnt >= 0; cnt -= SEND_BUF_SIZE) {
		mread(buf, SEND_BUF_SIZE, from);
		fwrite(buf, SEND_BUF_SIZE, 1, to);
	}

	if (cnt < 0) {
		mread(buf, cnt + SEND_BUF_SIZE, from);
		fwrite(buf, cnt + SEND_BUF_SIZE, 1, to);
	}

	fflush(to);
}

/* 
 * init cache 
 */
static int init_cache(void)
{
	int serr;
	cached_msg.dmsg = NULL;
	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	if (! (cached_msg.memdump = mopen())) {
		serr = errno;
		TRACE(TRACE_ERROR,"mopen() failed [%s]", strerror(serr));
		errno = serr;
		return -1;
	}

	
	if (! (cached_msg.tmpdump = mopen())) {
		serr = errno;
		TRACE(TRACE_ERROR,"mopen() failed [%s]", strerror(serr));
		errno = serr;
		mclose(&cached_msg.memdump);
		return -1;
	}

	cached_msg.file_dumped = 0;
	cached_msg.dumpsize = 0;
	return 0;
}
/*
 * closes the msg cache
 */
static void close_cache(void)
{
	if (cached_msg.dmsg)
		dbmail_message_free(cached_msg.dmsg);

	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	mclose(&cached_msg.memdump);
	mclose(&cached_msg.tmpdump);
}




static u64_t get_dumpsize(body_fetch_t *bodyfetch, u64_t tmpdumpsize); 

static void _imap_fetchitem_free(struct ImapSession * self)
{
	if (self->fi) {
		dbmail_imap_session_bodyfetch_free(self);
		g_free(self->fi);
		self->fi = NULL;
	}
}

static void _imap_clientinfo_free(struct ImapSession * self)
{
	if (self->ci) {
		if (self->ci->userData) {
			null_free(self->ci->userData);
		}
		g_free(self->ci);
		self->ci = NULL;
	}

	if (self->fstream) {
		g_object_unref(self->fstream);
		self->fstream = NULL;
	}
}

static void free_args(struct ImapSession *self)
{
	int i;
	for (i = 0; i < MAX_ARGS && self->args[i]; i++) {
		g_free(self->args[i]);
		self->args[i] = NULL;
	}
	self->args_idx = 0;
}


/* 
 *
 * initializer and accessors for ImapSession
 *
 */

struct ImapSession * dbmail_imap_session_new(void)
{
	struct ImapSession * self;

	/* init: cache */
	if (init_cache() != 0)
		return NULL;

	self = g_new0(struct ImapSession,1);

	self->args = g_new0(char *, MAX_ARGS);
	self->buff = g_string_new("");

	dbmail_imap_session_resetFi(self);
	
	return self;
}

struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self)
{
	if (! self->fi) {
		self->fi = g_new0(fetch_items_t,1);
		return self;
	}
	
	dbmail_imap_session_bodyfetch_free(self);
	g_free(self->fi);
	self->fi = g_new0(fetch_items_t,1);
	return self;
}
     
struct ImapSession * dbmail_imap_session_setClientinfo(struct ImapSession * self, clientinfo_t *ci)
{
	GMimeStream *ostream;
        GMimeFilter *filter;
	
	_imap_clientinfo_free(self);
	
	self->ci = g_new0(clientinfo_t,1);
	memcpy(self->ci, ci, sizeof(clientinfo_t));
	
	/* attach a CRLF encoding stream to the client's write filehandle */
        ostream = g_mime_stream_fs_new(dup(fileno(self->ci->tx)));
        self->fstream = g_mime_stream_filter_new_with_stream(ostream);
        filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
        g_mime_stream_filter_add((GMimeStreamFilter *) self->fstream, filter);

        g_object_unref(filter);
        g_object_unref(ostream);

	return self;
}

struct ImapSession * dbmail_imap_session_setTag(struct ImapSession * self, char * tag)
{
	if (self->tag) {
		g_free(self->tag);
		self->tag = NULL;
	}
	
	self->tag = g_strdup(tag);
	return self;
}
struct ImapSession * dbmail_imap_session_setCommand(struct ImapSession * self, char * command)
{
	if (self->command) {
		g_free(self->command);
		self->command = NULL;
	}
	self->command = g_strdup(command);
	return self;
}

void dbmail_imap_session_delete(struct ImapSession * self)
{
	close_cache();

	_imap_fetchitem_free(self);
	_imap_clientinfo_free(self);
	
	if (self->tag) {
		g_free(self->tag);
		self->tag = NULL;
	}
	if (self->command) {
		g_free(self->command);
		self->command = NULL;
	}
	if (self->mailbox) {
		dbmail_mailbox_free(self->mailbox);
		self->mailbox = NULL;
	}
	if (self->msginfo) {
		g_tree_destroy(self->msginfo);
		self->msginfo = NULL;
	}
	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}

	dbmail_imap_session_fetch_free(self);
	dbmail_imap_session_args_free(self, TRUE);
	
	g_string_free(self->buff,TRUE);

	g_free(self);
}

void dbmail_imap_session_fetch_free(struct ImapSession *self) 
{
	if (self->headers) {
		g_tree_destroy(self->headers);
		self->headers = NULL;
	}
	if (self->envelopes) {
		g_tree_destroy(self->envelopes);
		self->envelopes = NULL;
	}
	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}
	if (self->ids_list) {
		g_list_free(g_list_first(self->ids_list));
		self->ids_list = NULL;
	}
	if (self->fi) 
		dbmail_imap_session_bodyfetch_free(self);
}

void dbmail_imap_session_args_free(struct ImapSession *self, gboolean all)
{
	free_args(self);
	if (all)
		g_free(self->args);
}

/*************************************************************************************
 *
 *
 * imap utilities using ImapSession
 *
 *
 ************************************************************************************/
#define IMAP_CACHE_MEMDUMP 1
#define IMAP_CACHE_TMPDUMP 2


static u64_t _imap_cache_set_dump(char *buf, int dumptype)
{
	u64_t outcnt = 0;
	char *rfc = get_crlf_encoded(buf);

	switch (dumptype) {
		case IMAP_CACHE_MEMDUMP:
			mrewind(cached_msg.memdump);
			outcnt = mwrite(rfc, strlen(rfc), cached_msg.memdump);
			mrewind(cached_msg.memdump);
		break;
		case IMAP_CACHE_TMPDUMP:
			mrewind(cached_msg.tmpdump);
			outcnt = mwrite(rfc, strlen(rfc), cached_msg.tmpdump);
			mrewind(cached_msg.tmpdump);
		break;
	}
	g_free(rfc);
	
	return outcnt;
}

static u64_t _imap_cache_update(struct ImapSession *self, message_filter_t filter)
{
	u64_t tmpcnt = 0, outcnt = 0;
	char *buf = NULL;
	char *rfc = NULL;

	TRACE(TRACE_DEBUG,"cache message [%llu] filter [%d]", self->msg_idnr, filter);

	if (cached_msg.file_dumped == 1 && cached_msg.num == self->msg_idnr) {
		outcnt = cached_msg.dumpsize;
	} else {
		if (cached_msg.dmsg != NULL && GMIME_IS_MESSAGE(cached_msg.dmsg->content)) {
			dbmail_message_free(cached_msg.dmsg);
			cached_msg.dmsg = NULL;
		}

		cached_msg.dmsg = db_init_fetch(self->msg_idnr, DBMAIL_MESSAGE_FILTER_FULL);
		buf = dbmail_message_to_string(cached_msg.dmsg);
	
		outcnt = _imap_cache_set_dump(buf,IMAP_CACHE_MEMDUMP);
		tmpcnt = _imap_cache_set_dump(buf,IMAP_CACHE_TMPDUMP);
		
		assert(tmpcnt==outcnt);
		
		cached_msg.dumpsize = outcnt;

		if (cached_msg.num != self->msg_idnr) 
			cached_msg.num = self->msg_idnr;
		
		cached_msg.file_dumped = 1;

		g_free(buf);
		g_free(rfc);
	}
	
	switch (filter) {
		/* for these two update the temp MEM buffer */	
		case DBMAIL_MESSAGE_FILTER_HEAD:
			buf = dbmail_message_hdrs_to_string(cached_msg.dmsg);
			outcnt = _imap_cache_set_dump(buf,IMAP_CACHE_TMPDUMP);
			g_free(buf);
		break;

		case DBMAIL_MESSAGE_FILTER_BODY:
			buf = dbmail_message_body_to_string(cached_msg.dmsg);
			outcnt = _imap_cache_set_dump(buf,IMAP_CACHE_TMPDUMP);	
			g_free(buf);
		break;
		case DBMAIL_MESSAGE_FILTER_FULL:
			/* done */
		break;

	}
	
	mrewind(cached_msg.memdump);
	mrewind(cached_msg.tmpdump);
	
	TRACE(TRACE_DEBUG,"cache size [%llu]", outcnt);	
	return outcnt;
}

static int _imap_session_fetch_parse_partspec(struct ImapSession *self)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	char *token, *nexttoken;

	token=self->args[self->args_idx];
	nexttoken=self->args[self->args_idx+1];

	TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

	for (j = 0; token[j]; j++) {
		if (isdigit(token[j])) {
			indigit = 1;
			continue;
		} else if (token[j] == '.') {
			if (!indigit)
				/* error, single dot specified */
				return -2;
			indigit = 0;
			continue;
		} else
			break;	/* other char found */
	}
	
	
	if (j > 0) {
		if (indigit && token[j])
			return -2;	/* error DONE */
		/* partspecifier present, save it */
		if (j >= IMAP_MAX_PARTSPEC_LEN)
			return -2;	/* error DONE */
		dbmail_imap_session_bodyfetch_set_partspec(self, token, j);
	}

	char *partspec = &token[j];


	int shouldclose = 0;

	if (MATCH(partspec, "header.fields")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS);
	} else if (MATCH(partspec, "header.fields.not")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS_NOT);

		
	} else if (MATCH(partspec, "text")) {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT);
		shouldclose = 1;
	} else if (MATCH(partspec, "header")) {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER);
		shouldclose = 1;
	} else if (MATCH(partspec, "mime")) {
		self->fi->msgparse_needed=1;
		if (j == 0)
			return -2;	/* error DONE */

		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_MIME);
		shouldclose = 1;
	} else if (token[j] == '\0') {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT_SILENT);
		shouldclose = 1;
	} else {
		return -2;	/* error DONE */
	}
	if (shouldclose) {
		if (! MATCH(nexttoken, "]"))
			return -2;	/* error DONE */
	} else {
		self->args_idx++;	/* should be at '(' now */
		token = self->args[self->args_idx];
		nexttoken = self->args[self->args_idx+1];
		
		if (! MATCH(token,"("))
			return -2;	/* error DONE */

		self->args_idx++;	/* at first item of field list now, remember idx */
		dbmail_imap_session_bodyfetch_set_argstart(self); 

		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(self->args[self->args_idx],")") )
			self->args_idx++;

		token = self->args[self->args_idx];
		nexttoken = self->args[self->args_idx+1];
		
		TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

		dbmail_imap_session_bodyfetch_set_argcnt(self);

		if (dbmail_imap_session_bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;	/* error DONE */
	}

	return 0;
}

static int _imap_session_fetch_parse_octet_range(struct ImapSession *self) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[self->args_idx];
	
	if (! token)
		return 0;
//FIXME wrong return value?	return self->args_idx;
	
	TRACE(TRACE_DEBUG,"parse token [%s]", token);

	if (token[0] == '<') {

		/* check argument */
		if (token[strlen(token) - 1] != '>')
			return -2;	/* error DONE */

		delimpos = -1;
		for (j = 1; j < strlen(token) - 1; j++) {
			if (token[j] == '.') {
				if (delimpos != -1) 
					return -2;
				delimpos = j;
			} else if (!isdigit (token[j]))
				return -2;
		}
		if (delimpos == -1 || delimpos == 1 || delimpos == (int) (strlen(token) - 2))
			return -2;	/* no delimiter found or at first/last pos OR invalid args DONE */

		/* read the numbers */
		token[strlen(token) - 1] = '\0';
		token[delimpos] = '\0';
		dbmail_imap_session_bodyfetch_set_octetstart(self, strtoll(&token[1], NULL, 10));
		dbmail_imap_session_bodyfetch_set_octetcnt(self,strtoll(&token [delimpos + 1], NULL, 10));

		/* restore argument */
		token[delimpos] = '.';
		token[strlen(token) - 1] = '>';
	} else {
		self->args_idx--;
		return self->args_idx;
	}

	self->args_idx++;
	return 0;	/* DONE */
}

/*
 * dbmail_imap_session_fetch_parse_args()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int dbmail_imap_session_fetch_parse_args(struct ImapSession * self)
{
	int invalidargs, ispeek = 0, i;
	
	invalidargs = 0;

        /* dump args (debug) */
        for (i = self->args_idx; self->args[i]; i++) {
                TRACE(TRACE_DEBUG, "arg[%d]: '%s'\n", i, self->args[i]);
        }


	if (!self->args[self->args_idx])
		return -1;	/* no more */

	if (self->args[self->args_idx][0] == '(')
		self->args_idx++;

	if (!self->args[self->args_idx])
		return -2;	/* error */

	
	char *token = NULL, *nexttoken = NULL;
	
	token = self->args[self->args_idx];
	nexttoken = self->args[self->args_idx+1];

	TRACE(TRACE_DEBUG,"parse args[%llu] = [%s]", self->args_idx, token);

	if (MATCH(token,"flags")) {
		self->fi->getFlags = 1;
	} else if (MATCH(token,"internaldate")) {
		self->fi->getInternalDate=1;
	} else if (MATCH(token,"uid")) {
		self->fi->getUID=1;
	} else if (MATCH(token,"rfc822.size")) {
		self->fi->getSize = 1;
	} else if (MATCH(token,"fast")) {
		self->fi->getInternalDate = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
		
	/* from here on message parsing will be necessary */
	
	} else if (MATCH(token,"rfc822")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822=1;
	} else if (MATCH(token,"rfc822.header")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Header = 1;
	} else if (MATCH(token,"rfc822.peek")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Peek = 1;
	} else if (MATCH(token,"rfc822.text")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Text = 1;
	
	} else if (MATCH(token,"body") || MATCH(token,"body.peek")) {
		
		/* setting msgparse_needed deferred to fetch_parse_partspec 
		 * since we don't need to parse just to retrieve headers */
		
		dbmail_imap_session_bodyfetch_new(self);

		if (MATCH(token,"body.peek"))
			ispeek=1;
		
		nexttoken = (char *)self->args[self->args_idx+1];
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek)
				return -2;	/* error DONE */
			self->fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			/* now read the argument list to body */
			self->args_idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			self->args_idx++;	/* now pointing at what should be the item type */

			token = (char *)self->args[self->args_idx];
			nexttoken = (char *)self->args[self->args_idx+1];

			TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

			if (MATCH(token,"]")) {
				if (ispeek)
					self->fi->getBodyTotalPeek = 1;
				else
					self->fi->getBodyTotal = 1;
				self->args_idx++;				
				return _imap_session_fetch_parse_octet_range(self);
			}
			
			if (ispeek)
				self->fi->noseen = 1;

			if (_imap_session_fetch_parse_partspec(self) < 0) {
				TRACE(TRACE_DEBUG,"fetch_parse_partspec return with error");
				return -2;
			}
			
			self->args_idx++; // idx points to ']' now
			self->args_idx++; // idx points to octet range now 
			return _imap_session_fetch_parse_octet_range(self);
		}
	} else if (MATCH(token,"all")) {		
		self->fi->msgparse_needed=1; // because of getEnvelope
		self->fi->getInternalDate = 1;
		self->fi->getEnvelope = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"full")) {
		self->fi->msgparse_needed=1;
		self->fi->getInternalDate = 1;
		self->fi->getEnvelope = 1;
		self->fi->getMIME_IMB = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"bodystructure")) {
		self->fi->msgparse_needed=1;
		self->fi->getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		self->fi->getEnvelope = 1;
	} else {			
		if ((! nexttoken) && (strcmp(token,")") == 0)) {
			/* only allowed if last arg here */
			return -1;
		}
		TRACE(TRACE_INFO,"error [%s]", token);
		return -2;	/* DONE */
	}

	return 1; //theres more...
}


GTree * dbmail_imap_session_get_msginfo(struct ImapSession *self, GTree *ids)
{

	unsigned nrows, i, j, k;
	const char *query_result;
	char *to_char_str;
	msginfo_t *result;
	GTree *msginfo;
	GList *l, *t;
	u64_t *uid, *lo, *hi;
	u64_t id;
	char query[DEF_QUERYSIZE], range[RANGE_SIZE];
	memset(query,0,DEF_QUERYSIZE);
	memset(range,0,RANGE_SIZE);
	
	if (! (ids && g_tree_nnodes(ids)>0))
		return NULL;

	l = g_tree_keys(ids);
	t = l;

	lo = (u64_t *)l->data;

	l = g_list_last(l);
	hi = (u64_t *)l->data;

	g_list_free(t);	

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	k = 0;
	to_char_str = date2char_str("internal_date");
		
	db_free_result();

	if (*lo == *hi) 
		snprintf(range,RANGE_SIZE,"= %llu", *lo);
	else
		snprintf(range,RANGE_SIZE,"BETWEEN %llu AND %llu", *lo, *hi);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT seen_flag, answered_flag, deleted_flag, flagged_flag, "
		 "draft_flag, recent_flag, %s, rfcsize, message_idnr "
		 "FROM %smessages msg, %sphysmessage pm "
		 "WHERE pm.id = msg.physmessage_id "
		 "AND message_idnr %s "
		 "AND mailbox_idnr = %llu AND status IN (%d,%d,%d) "
		 "ORDER BY message_idnr ASC",to_char_str,DBPFX,DBPFX,
		 range, ud->mailbox.uid,
		 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,MESSAGE_STATUS_DELETE);
	g_free(to_char_str);

	if (db_query(query) == -1) {
		TRACE(TRACE_ERROR, "could not select message");
		return NULL;
	}

	if ((nrows = db_num_rows()) == 0) {
		TRACE(TRACE_ERROR, "empty result set");
		db_free_result();
		return NULL;
	}

	msginfo = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);

	for (i = 0; i < nrows; i++) {

		id = db_get_result_u64(i, IMAP_NFLAGS + 2);

		if (! g_tree_lookup(ids,&id))
			continue;
		
		result = g_new0(msginfo_t,1);
		/* flags */
		for (j = 0; j < IMAP_NFLAGS; j++)
			result->flags[j] = db_get_result_bool(i, j);

		/* internal date */
		query_result = db_get_result(i, IMAP_NFLAGS);
		strncpy(result->internaldate,
			(query_result) ? query_result :
			"01-Jan-1970 00:00:01 +0100",
			IMAP_INTERNALDATE_LEN);
		
		/* rfcsize */
		result->rfcsize = db_get_result_u64(i, IMAP_NFLAGS + 1);
		
		/* uid */
		result->uid = id;

		uid = g_new0(u64_t,1);
		*uid = result->uid;
		
		g_tree_insert(msginfo, uid, result); 
	}

	db_free_result();

	return msginfo;
}

#define SEND_SPACE if (self->fi->isfirstfetchout) \
				self->fi->isfirstfetchout = 0; \
			else \
				dbmail_imap_session_buff_append(self, " ")

static gboolean _get_mailbox(u64_t UNUSED *id, mailbox_t *mb, struct ImapSession *self)
{
	int result;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	result = acl_has_right(mb, ud->userid, ACL_RIGHT_READ);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		self->error = -1;
		return TRUE;
	}
	if (result == 0) {
		self->error = 1;
		return TRUE;
	}

	if (db_getmailbox(mb) != DM_SUCCESS)
		return TRUE;
	return FALSE;
}

GTree * dbmail_imap_session_get_mbxinfo(struct ImapSession *self)
{
	GTree *mbxinfo = NULL;
	int i, r;
	u64_t *id;
	mailbox_t *mb;
	char q[DEF_QUERYSIZE];
	memset(q,0,DEF_QUERYSIZE);
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	
	snprintf(q,DEF_QUERYSIZE,"SELECT mailbox_id FROM %ssubscription "
		"WHERE user_id=%llu",DBPFX, ud->userid);
	
	if (db_query(q)==DM_EQUERY)
		return (GTree *)NULL;

	r = db_num_rows();
	
	mbxinfo = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);

	for (i=0;i<r;i++) {	
		id = g_new0(u64_t,1);
		mb = g_new0(mailbox_t,1);

		*id = db_get_result_u64(i,0);
		mb->uid = *id;

		g_tree_insert(mbxinfo, id, mb);
	}
	db_free_result();

	self->error = 0;
	g_tree_foreach(mbxinfo, (GTraverseFunc)_get_mailbox, self);

	if (! self->error)
		return mbxinfo;

	if (self->error == DM_EQUERY)
		TRACE(TRACE_ERROR, "database error retrieving mbxinfo");
	if (self->error == DM_EGENERAL)
		TRACE(TRACE_ERROR, "failure retrieving mbxinfo for unreadable mailbox");

	g_tree_destroy(mbxinfo);

	return (GTree *)NULL;
}


static int _fetch_get_items(struct ImapSession *self, u64_t *uid)
{
	int result;
	u64_t actual_cnt, tmpdumpsize;
	gchar *s = NULL;
	
	msginfo_t *msginfo = g_tree_lookup(self->msginfo, uid);

	g_return_val_if_fail(msginfo,-1);
	
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	
	self->msg_idnr = *uid;
	self->fi->isfirstfetchout = 1;
	
        /* queue this message's recent_flag for removal */
        if (ud->mailbox.permission == IMAPPERM_READWRITE)
            self->recent = g_list_prepend(self->recent, g_strdup_printf("%llu",self->msg_idnr));

	if (self->fi->getInternalDate) {
		SEND_SPACE;
		dbmail_imap_session_buff_append(self, "INTERNALDATE \"%s\"", date_sql2imap(msginfo->internaldate));
	}
	if (self->fi->getSize) {
		SEND_SPACE;
		dbmail_imap_session_buff_append(self, "RFC822.SIZE %llu", msginfo->rfcsize);
	}
	if (self->fi->getFlags) {
		SEND_SPACE;
		s = imap_flags_as_string(msginfo);
		dbmail_imap_session_buff_append(self,"FLAGS %s",s);
		g_free(s);

	}
	if (self->fi->getUID) {
		SEND_SPACE;
		dbmail_imap_session_buff_append(self, "UID %llu", msginfo->uid);
	}

	if (self->fi->getMIME_IMB) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((cached_msg.dmsg)->content), 1))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		dbmail_imap_session_buff_append(self, "BODYSTRUCTURE %s", s);
		g_free(s);
	}

	if (self->fi->getMIME_IMB_noextension) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((cached_msg.dmsg)->content), 0))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body\r\n");
			return -1;
		}
		dbmail_imap_session_buff_append(self, "BODY %s",s);
		g_free(s);
	}

	if (self->fi->getEnvelope) {

		SEND_SPACE;
		
		_fetch_envelopes(self);
	}

	if (self->fi->getRFC822 || self->fi->getRFC822Peek) {

		SEND_SPACE;

		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		dbmail_imap_session_buff_append(self, "RFC822 {%llu}\r\n", cached_msg.dumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);

		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {

		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			dbmail_imap_session_buff_append(self, "BODY[] {%llu}\r\n", cached_msg.dumpsize);
			dbmail_imap_session_buff_flush(self);
			send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);
		} else {
			mseek(cached_msg.memdump, dbmail_imap_session_bodyfetch_get_last_octetstart(self), SEEK_SET);
			actual_cnt = (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) >
			     (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)))
			    ? (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)) 
			    : dbmail_imap_session_bodyfetch_get_last_octetcnt(self);

			dbmail_imap_session_buff_append(self, "BODY[]<%llu> {%llu}\r\n", 
					dbmail_imap_session_bodyfetch_get_last_octetstart(self), actual_cnt);
			dbmail_imap_session_buff_flush(self);
			send_data(self->ci->tx, cached_msg.memdump, actual_cnt);
		}

		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;

	}

	if (self->fi->getRFC822Header) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_HEAD);
		dbmail_imap_session_buff_append(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);
	}

	if (self->fi->getRFC822Text) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_BODY);
		dbmail_imap_session_buff_append(self, "RFC822.TEXT {%llu}\r\n", tmpdumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);

		self->fi->setseen = 1;
	}

	_imap_show_body_sections(self);

	/* set \Seen flag if necessary; note the absence of an error-check 
	 * for db_get_msgflag()!
	 */
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	if (self->fi->setseen && db_get_msgflag("seen", self->msg_idnr, ud->mailbox.uid) != 1) {
		/* only if the user has an ACL which grants
		   him rights to set the flag should the
		   flag be set! */
		result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_SEEN);
		if (result == -1) {
			dbmail_imap_session_buff_append(self, "\r\n *BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			result = db_set_msgflag(self->msg_idnr, ud->mailbox.uid, setSeenSet, IMAPFA_ADD);
			if (result == -1) {
				dbmail_imap_session_buff_append(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
		}

		self->fi->getFlags = 1;
		dbmail_imap_session_buff_append(self, " ");
	}

	dbmail_imap_session_buff_append(self, ")\r\n");

	return 0;
}


static gboolean _do_fetch(u64_t *uid, gpointer UNUSED value, struct ImapSession *self)
{
	u64_t *id = uid;
	
	id = g_tree_lookup(self->mailbox->ids,uid);

	g_return_val_if_fail(id,TRUE);
	
	dbmail_imap_session_buff_append(self, "* %llu FETCH (", *id);

	/* go fetch the items */
	fflush(self->ci->tx);
	if (_fetch_get_items(self,uid) < 0) {
		self->error = TRUE;
		return TRUE;
	}

	if (self->buff->len > 4096)
		dbmail_imap_session_buff_flush(self);

	return FALSE;
}

int dbmail_imap_session_fetch_get_items(struct ImapSession *self)
{
	GTree *t;
	
	t = self->msginfo;

	if ((self->msginfo=dbmail_imap_session_get_msginfo(self, self->ids)) == NULL) {
		TRACE(TRACE_INFO, "unable to retrieve msginfo. empty mailbox?");
	}
        if(t)
                g_tree_destroy(t);

	if (! self->ids)
		TRACE(TRACE_INFO, "self->ids is NULL");
	else {
		dbmail_imap_session_buff_clear(self);
		g_tree_foreach(self->ids, (GTraverseFunc) _do_fetch, self);
		dbmail_imap_session_buff_flush(self);
		if (self->error)
			return -1;
		dbmail_imap_session_mailbox_update_recent(self);
	}
	return 0;
	
}
static void _imap_show_body_sections(struct ImapSession *self) 
{
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)_imap_show_body_section, (gpointer)self);
	dbmail_imap_session_bodyfetch_rewind(self);
}

static void imap_cache_send_tmpdump(struct ImapSession *self, body_fetch_t *bodyfetch, u64_t size)
{
	u64_t cnt = 0;

	if (bodyfetch->octetcnt > 0) {
		cnt = get_dumpsize(bodyfetch, size);
		dbmail_imap_session_buff_append(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
		mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
	} else {
		cnt = size;
		dbmail_imap_session_buff_append(self, "] {%llu}\r\n", size);
		mrewind(cached_msg.tmpdump);
	}
	dbmail_imap_session_buff_flush(self);
	send_data(self->ci->tx, cached_msg.tmpdump, cnt);
}

#define QUERY_BATCHSIZE 500

/* get envelopes */
static void _fetch_envelopes(struct ImapSession *self)
{
	unsigned i=0, rows=0;
	GString *q;
	gchar *env, *s;
	u64_t *mid;
	u64_t id;
	static int lo = 0;
	static u64_t hi = 0;
	char range[RANGE_SIZE];
	GList *last;
	memset(range,0,RANGE_SIZE);

	if (! self->envelopes) {
		self->envelopes = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		hi = 0;
		lo = 0;
	}

	if ((s = g_tree_lookup(self->envelopes, &(self->msg_idnr))) != NULL) {
		dbmail_imap_session_buff_append(self, "ENVELOPE %s", s?s:"");
		return;
	}

	TRACE(TRACE_DEBUG,"lo: %d", lo);

	if (! (last = g_list_nth(self->ids_list, lo+(int)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	hi = *(u64_t *)last->data;

	if (self->msg_idnr == hi)
		snprintf(range,RANGE_SIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,RANGE_SIZE,"BETWEEN %llu AND %llu", self->msg_idnr, hi);

        q = g_string_new("");
	g_string_printf(q,"SELECT message_idnr,envelope "
			"FROM %senvelope e "
			"JOIN %smessages m ON m.physmessage_id=e.physmessage_id "
			"WHERE m.mailbox_idnr = %llu "
			"AND message_idnr %s",
			DBPFX, DBPFX,  
			self->mailbox->id, range);
	
	if (db_query(q->str)==-1) {
                g_string_free(q,TRUE);
		return;
        }
	
	rows = db_num_rows();
	
	for(i=0;i<rows;i++) {
		
		id = db_get_result_u64(i,0);
		
		if (! g_tree_lookup(self->ids,&id))
			continue;
		
		mid = g_new0(u64_t,1);
		*mid = id;
		
		env = g_strdup((char *)db_get_result(i,1));
		
		g_tree_insert(self->envelopes,mid,env);

	}
	db_free_result();
	g_string_free(q,TRUE);

	lo += QUERY_BATCHSIZE;

	s = g_tree_lookup(self->envelopes, &(self->msg_idnr));
	dbmail_imap_session_buff_append(self, "ENVELOPE %s", s?s:"");
}

void _send_headers(struct ImapSession *self, const body_fetch_t *bodyfetch, gboolean not, const gchar *s)
{
	long long cnt = 0;
	gchar *tmp;
	GString *ts;

	dbmail_imap_session_buff_append(self,"HEADER.FIELDS%s %s] ", not ? ".NOT" : "", bodyfetch->hdrplist);

	if (!s) {
		dbmail_imap_session_buff_append(self, "{2}\r\n\r\n");
		return;
	}

	ts = g_string_new(s);

	if (bodyfetch->octetcnt > 0) {
		
		if (bodyfetch->octetstart > 0 && bodyfetch->octetstart < ts->len)
			ts = g_string_erase(ts, 0, bodyfetch->octetstart);
		
		if (ts->len > bodyfetch->octetcnt)
			ts = g_string_truncate(ts, bodyfetch->octetcnt);
		
		tmp = get_crlf_encoded(ts->str);
		cnt = strlen(tmp);
		
		dbmail_imap_session_buff_append(self, "<%llu> {%llu}\r\n%s\r\n", 
				bodyfetch->octetstart, cnt+2, tmp);
	} else {
		tmp = get_crlf_encoded(ts->str);
		cnt = strlen(tmp);
		dbmail_imap_session_buff_append(self, "{%llu}\r\n%s\r\n", cnt+2, tmp);
	}

	g_string_free(ts,TRUE);
	g_free(tmp);
}


/* get headers or not */
static void _fetch_headers(struct ImapSession *self, body_fetch_t *bodyfetch, gboolean not)
{
	unsigned i=0, rows=0;
	GString *q = g_string_new("");
	gchar *fld, *val, *old, *new = NULL, *s;
	u64_t *mid;
	u64_t id;
	GList *last;
	int k;
	static int lo = 0;
	static u64_t hi = 0;
	static u64_t ceiling = 0;
	char range[RANGE_SIZE];
	memset(range,0,RANGE_SIZE);

	if (! self->headers) {
		TRACE(TRACE_DEBUG, "init self->headers");
		self->headers = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		ceiling = 0;
		hi = 0;
		lo = 0;
	}

	if (! bodyfetch->hdrnames) {

		GList *tlist = NULL;
		GString *h = NULL;

		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, g_strdup(self->args[k + bodyfetch->argstart]));

		bodyfetch->hdrplist = dbmail_imap_plist_as_string(tlist);
		h = g_list_join((GList *)tlist,"','");
		g_list_destroy(tlist);

		h = g_string_ascii_down(h);

		bodyfetch->hdrnames = h->str;

		g_string_free(h,FALSE);


	}

	TRACE(TRACE_DEBUG,"for %llu [%s]", self->msg_idnr, bodyfetch->hdrplist);

	// did we prefetch this message already?
	s = g_tree_lookup(self->headers, &(self->msg_idnr));
	if (self->msg_idnr <= ceiling) {
		_send_headers(self, bodyfetch, not, s);
		return;
	}

	// let's fetch the required message and prefetch a batch if needed.
	
	if (! (last = g_list_nth(self->ids_list, lo+(int)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	hi = *(u64_t *)last->data;

	if (self->msg_idnr == hi)
		snprintf(range,RANGE_SIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,RANGE_SIZE,"BETWEEN %llu AND %llu", self->msg_idnr, hi);

	g_string_printf(q,"SELECT message_idnr,headername,headervalue "
			"FROM %sheadervalue v "
			"JOIN %smessages m ON v.physmessage_id=m.physmessage_id "
			"JOIN %sheadername n ON v.headername_id=n.id "
			"WHERE m.mailbox_idnr = %llu "
			"AND message_idnr %s "
			"AND lower(headername) %s IN ('%s')",
			DBPFX, DBPFX, DBPFX,
			self->mailbox->id, range, 
			not?"NOT":"", bodyfetch->hdrnames);
	
	if (db_query(q->str)==-1)
		return;
	
	rows = db_num_rows();

	for(i=0;i<rows;i++) {
		
		id = db_get_result_u64(i,0);
		
		if (! g_tree_lookup(self->ids,&id))
			continue;
		
		mid = g_new0(u64_t,1);
		*mid = id;
		
		fld = (char *)db_get_result(i,1);
		val = dbmail_iconv_db_to_utf7((char *)db_get_result(i,2));
		TRACE(TRACE_DEBUG,"got hdrval [%s]", val);
		
		old = g_tree_lookup(self->headers, (gconstpointer)mid);
		new = g_strdup_printf("%s%s: %s\n", old?old:"", fld, val);
		g_free(val);
		
		g_tree_insert(self->headers,mid,new);
		
	}

	lo += QUERY_BATCHSIZE;
	ceiling = hi;

	db_free_result();
	g_string_free(q,TRUE);

	s = g_tree_lookup(self->headers, &(self->msg_idnr));
	_send_headers(self, bodyfetch, not, s);

	return;
}

static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data) 
{
	u64_t tmpdumpsize;
	GMimeObject *part = NULL;
	char *tmp;
	gboolean condition = FALSE;
	struct ImapSession *self = (struct ImapSession *)data;
	
	if (bodyfetch->itemtype < 0)
		return 0;
	
	TRACE(TRACE_DEBUG,"itemtype [%d] partspec [%s]", bodyfetch->itemtype, bodyfetch->partspec);
	
	if (self->fi->msgparse_needed) {
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (bodyfetch->partspec[0]) {
			if (bodyfetch->partspec[0] == '0') {
				dbmail_imap_session_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
				TRACE(TRACE_ERROR, "PROTOCOL ERROR");
				return 1;
			}
			part = imap_get_partspec(GMIME_OBJECT((cached_msg.dmsg)->content), bodyfetch->partspec);
		} else {
			part = GMIME_OBJECT((cached_msg.dmsg)->content);
		}
	}

	SEND_SPACE;

	if (! self->fi->noseen)
		self->fi->setseen = 1;
	dbmail_imap_session_buff_append(self, "BODY[%s", bodyfetch->partspec);

	switch (bodyfetch->itemtype) {

	case BFIT_TEXT:
		dbmail_imap_session_buff_append(self, "TEXT");
		/* fall through */
		
	case BFIT_TEXT_SILENT:
		if (!part)
			dbmail_imap_session_buff_append(self, "] NIL");
		else {
			tmp = imap_get_logical_part(part,"TEXT");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);
			
			if (!tmpdumpsize) 
				dbmail_imap_session_buff_append(self, "] NIL");
			else 
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}
		break;

	case BFIT_HEADER:
		dbmail_imap_session_buff_append(self, "HEADER");
		if (!part)
			dbmail_imap_session_buff_append(self, "] NIL");
		else {
			tmp = imap_get_logical_part(part,"HEADER");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);
			
			if (!tmpdumpsize) 
				dbmail_imap_session_buff_append(self, "] NIL");
			else 
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}
		break;
		
	case BFIT_MIME:
		dbmail_imap_session_buff_append(self, "MIME");

		if (!part)
			dbmail_imap_session_buff_append(self, "NIL");
		else {
			tmp = imap_get_logical_part(part,"MIME");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);

			if (!tmpdumpsize)
				dbmail_imap_session_buff_append(self, "NIL");
			else
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}

		break;

	case BFIT_HEADER_FIELDS_NOT:
		condition=TRUE;
		
	case BFIT_HEADER_FIELDS:
		_fetch_headers(self, bodyfetch, condition);
		break;

	default:
		dbmail_imap_session_printf(self, "\r\n* BYE internal server error\r\n");
		return -1;
	}
	
	return 0;
}



int client_is_authenticated(struct ImapSession * self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	return (ud->state != IMAPCS_NON_AUTHENTICATED);
}

/*
 * check_state_and_args()
 *
 * checks if the user is in the right state & the numbers of arguments;
 * a state of -1 specifies any state
 * arguments can be grouped by means of parentheses
 *
 * returns 1 on succes, 0 on failure
 */
int check_state_and_args(struct ImapSession * self, const char *command, int minargs, int maxargs, int state)
{
	int i;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	/* check state */
	if (state != -1) {
		if (ud->state != state) {
			if (!  (state == IMAPCS_AUTHENTICATED && ud->state == IMAPCS_SELECTED)) {
				dbmail_imap_session_printf(self,
					"%s BAD %s command received in invalid state\r\n",
					self->tag, command);
				return 0;
			}
		}
	}

	/* check args */
	for (i = 0; i < minargs; i++) {
		if (!self->args[self->args_idx+i]) {
			/* error: need more args */
			dbmail_imap_session_printf(self,
				"%s BAD missing argument%s to %s\r\n", self->tag,
				(minargs == 1) ? "" : "(s)", command);
			return 0;
		}
	}

	for (i = 0; self->args[self->args_idx+i]; i++);

	if (maxargs && (i > maxargs)) {
		/* error: too many args */
		dbmail_imap_session_printf(self, "%s BAD too many arguments to %s\r\n", self->tag,
			command);
		return 0;
	}

	/* succes */
	return 1;
}

void dbmail_imap_session_buff_clear(struct ImapSession *self)
{
	g_string_printf(self->buff, "%s", "");
}	

void dbmail_imap_session_buff_append(struct ImapSession *self, char *message, ...)
{
	va_list ap;
	gchar *ln;
	assert(message);

	va_start(ap, message);
	ln = g_strdup_vprintf(message, ap);
	va_end(ap);

	g_string_append_printf(self->buff, "%s", ln);

	g_free(ln);

}

void dbmail_imap_session_buff_flush(struct ImapSession *self)
{
	dbmail_imap_session_printf(self, "%s", self->buff->str);
	dbmail_imap_session_buff_clear(self);
}

/* Returns -1 on error, -2 on serious error. */
int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...)
{
        va_list ap;
        int len;
        FILE * fd;
        int maxlen=100;
        int result = 0;
        gchar *ln;
	static int errcount = 0;

	assert(message);

	va_start(ap, message);
	ln = g_strdup_vprintf(message,ap);
	va_end(ap);

        if (! ln)
                return -1;
	
        gchar *re = g_new0(gchar, maxlen+1);
	
        if ((result = snprintf(re,maxlen,"%s",ln))<0) {
		g_free(re);
		g_free(ln);
                return -1;
	}

        fd = self->ci->tx;

        if (feof(fd) || fflush(fd) < 0) {
		g_free(re);
		g_free(ln);
                TRACE(TRACE_ERROR, "client socket closed");
		return -2;
	}

        len = g_mime_stream_write_string(self->fstream,ln);
	
        if (len < 0) {
		g_free(re);
		g_free(ln);
                TRACE(TRACE_ERROR, "write to client socket failed");
		if (errcount++ > 1)
			TRACE(TRACE_FATAL,"uncaught error on socket, exiting");
		return -2;
	}
	errcount = 0;

        if (result < maxlen)
                TRACE(TRACE_DEBUG,"RESPONSE: [%s]", re);
        else
                TRACE(TRACE_DEBUG,"RESPONSE: [%s...]", re);

        g_free(re);
	g_free(ln);

        return len;
}

int dbmail_imap_session_discard_to_eol(struct ImapSession *self)
{
	int len;
	int done = 0;
	char buffer[MAX_LINESIZE];
	clientinfo_t *ci = self->ci;

	/* loop until we get a newline terminated chunk */
	while (!done)  {
		memset(buffer, 0, MAX_LINESIZE);
		alarm(self->timeout);
		if (fgets(buffer, MAX_LINESIZE, ci->rx) == NULL) {
			alarm(0);
			TRACE(TRACE_ERROR, "error reading from client");
			return -1;
		}
		len = strlen(buffer);
		if (len <= 0) {
			alarm(0);
			return -1;
		}

		/* Do we need to check for \r\n ? */
		if (len >= 1 && buffer[len-1]=='\n') {
			alarm(0);
			done = 1;
		}
	}

	alarm(0);
	return len;
}


int dbmail_imap_session_readln(struct ImapSession *self, char * buffer)
{
	int len;
	clientinfo_t *ci = self->ci;
	
	assert(buffer);
	memset(buffer, 0, MAX_LINESIZE);

	alarm(self->timeout);
	alarm_occured = 0;

	fgets(buffer, MAX_LINESIZE, ci->rx);

	alarm(0);

	if (alarm_occured) {
		alarm_occured = 0;
		dbmail_imap_session_printf(self, "%s", IMAP_TIMEOUT_MSG);
		TRACE(TRACE_ERROR, "timeout occurred in dbmail_imap_session_readln");
		client_close();
		return -1;
	}

	if (feof(ci->rx) || ferror(ci->rx)) {
		return -1;
	}

	len = strlen(buffer);
	if (len >= (MAX_LINESIZE-1)) {
		TRACE(TRACE_WARNING, "too long line from client (discarding)");
		/* Note: we do preserve the partial read here -- so that 
		 * the command parser can extract a tag if need be */
		if (dbmail_imap_session_discard_to_eol(self) < 0)
			return -1;
		else
			return 0;
	}

	return strlen(buffer);
}
	
int dbmail_imap_session_handle_auth(struct ImapSession * self, char * username, char * password)
{

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	timestring_t timestring;
	create_current_timestring(&timestring);
	
	u64_t userid = 0;
	
	TRACE(TRACE_DEBUG, "trying to validate user [%s], pass [%s]", 
			username, (password ? "XXXX" : "(null)") );
	
	int valid = auth_validate(self->ci, username, password, &userid);
	
	TRACE(TRACE_MESSAGE, "user (id:%llu, name %s) tries login",
			userid, username);

	if (valid == -1) {
		/* a db-error occurred */
		dbmail_imap_session_printf(self, "* BYE internal db error validating user\r\n");
		TRACE(TRACE_ERROR, "db-validate error while validating user %s (pass %s).",
			       	username, password ? "XXXX" : "(null)");
		return -1;
	}

	if (valid == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		TRACE(TRACE_MESSAGE, "user (name %s) coming from [%s] login rejected", username, self->ci->ip_src);
		dbmail_imap_session_printf(self, "%s NO login rejected\r\n", self->tag);

		return 1;
	}

	/* login ok */
	TRACE(TRACE_MESSAGE, "user (id %llu, name %s) login accepted", userid, username);

	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);

	return 0;

}


/* Value must be preallocated to MAX_LINESIZE length. */
int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value )
{
	char *buf, *prompt64, *promptcat;
	int buflen;
	
	g_return_val_if_fail(prompt != NULL, -1);
	
	buf = g_new0(char, MAX_LINESIZE);
			
	/* base64 encoding increases string length by about 40%. */
	prompt64 = g_new0(char, strlen(prompt) * 2);

	promptcat = g_strdup_printf("%s\r\n", prompt);
	base64_encode((unsigned char *)prompt64, (unsigned char *)promptcat, strlen(promptcat));

	dbmail_imap_session_printf(self, "+ %s\r\n", prompt64);
	fflush(self->ci->tx);
	
	if ( (dbmail_imap_session_readln(self, buf) <= 0) )  {
		g_free(buf);
		g_free(prompt64);
		g_free(promptcat);
		return -1;
	}

	/* value is the same size as buf.
	 * base64 decoding is always shorter. */
	buflen = base64_decode(value, buf);
	value[buflen] = '\0';
	
	/* Double check in case the algorithm went nuts. */
	if (buflen >= (MAX_LINESIZE - 1)) {
		/* Oh shit. */
		TRACE(TRACE_FATAL, "possible memory corruption");
		return -1;
	}

	g_free(buf);
	g_free(prompt64);
	g_free(promptcat);
	
	return 0;
}

u64_t dbmail_imap_session_mailbox_get_idnr(struct ImapSession * self, const char * mailbox)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	char * mbox = g_strdup(mailbox);
	u64_t uid;
	int i;
	
	/* remove trailing '/' if present */
	while (strlen(mbox) > 0 && mbox[strlen(mbox) - 1] == '/')
		mbox[strlen(mbox) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; mbox[i] && mbox[i] == '/'; i++);
	memmove(&mbox[0], &mbox[i], (strlen(mbox) - i) * sizeof(char));

	db_findmailbox(mbox, ud->userid, &uid);
	
	g_free(mbox);

	return uid;
}

int dbmail_imap_session_mailbox_check_acl(struct ImapSession * self, u64_t idnr,  ACLRight_t acl)
{
	int access;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	mailbox_t mailbox;

	memset(&mailbox, '\0', sizeof(mailbox_t));
	mailbox.uid = idnr;
	access = acl_has_right(&mailbox, ud->userid, acl);
	
	if (access < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (access == 0) {
		dbmail_imap_session_printf(self, "%s NO permission denied\r\n", self->tag);
		dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);
		return 1;
	}
	return 0;
}

int dbmail_imap_session_mailbox_get_selectable(struct ImapSession * self, u64_t idnr)
{
	/* check if mailbox is selectable */
	int selectable;
	selectable = db_isselectable(idnr);
	if (selectable == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (selectable == 0) {
		dbmail_imap_session_printf(self, "%s NO specified mailbox is not selectable\r\n", self->tag);
		return 1;
	}
	return 0;
}

static gboolean imap_msginfo_notify(u64_t *uid, msginfo_t *msginfo, struct ImapSession *self)
{
	u64_t *msn;
	msginfo_t *newmsginfo;
	char *s;
	int i;

	newmsginfo = g_tree_lookup(self->msginfo, uid);
	msn = g_tree_lookup(self->mailbox->ids, uid);

	if (! msn) {
		TRACE(TRACE_DEBUG,"can't find uid [%llu]", *uid);
		return TRUE;
	}
	// EXPUNGE
	switch (self->command_type) {
		case IMAP_COMM_NOOP:
		case IMAP_COMM_IDLE:
			if (! newmsginfo) {
				dbmail_imap_session_printf(self, "* %llu EXPUNGE\r\n", *msn);
				dbmail_mailbox_remove_uid(self->mailbox, uid);
				return FALSE;

			}
		default:
		break;
	}

	// FETCH
	for (i=0; i< IMAP_NFLAGS; i++) {
		if (msginfo->flags[i] != newmsginfo->flags[i]) {
			s = imap_flags_as_string(newmsginfo);
			dbmail_imap_session_printf(self,"* %llu FETCH (FLAGS %s)\r\n", *msn, s);
			g_free(s);
			break;
		}
	}

	return FALSE;
}
static gboolean imap_mbxinfo_notify(u64_t UNUSED *id, mailbox_t *mb, struct ImapSession *self)
{
	time_t oldmtime = mb->mtime;
	unsigned oldexists = mb->exists;
	unsigned oldrecent = mb->recent;
	unsigned oldunseen = mb->unseen;
	u64_t olduidnext = mb->msguidnext;
	int changed = 0;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	GList *plst = NULL;
	gchar *astring, *pstring;

	if (ud->mailbox.uid == mb->uid)
		return FALSE;

	if (db_getmailbox(mb) != DM_SUCCESS) {
		self->error = 1;
		return TRUE;
	}

	if (oldmtime == mb->mtime)
		return FALSE;

	TRACE(TRACE_DEBUG,"oldmtime[%d] != mtime[%d]", (int)oldmtime, (int)mb->mtime);

	if (oldexists != mb->exists) {
		plst = g_list_append_printf(plst,"MESSAGES %u", mb->exists);
		changed++;
	}
	if (oldrecent != mb->recent) {
		plst = g_list_append_printf(plst,"RECENT %u", mb->recent);
		changed++;
	}
	if (oldunseen != mb->unseen) {
		plst = g_list_append_printf(plst,"UNSEEN %u", mb->unseen);
		changed++;
	}
	if (olduidnext != mb->msguidnext) {
		plst = g_list_append_printf(plst,"UIDNEXT %llu", mb->msguidnext);
		changed++;
	}
	
	if (! changed)
		return FALSE;

	astring = dbmail_imap_astring_as_string(mb->name);
	pstring = dbmail_imap_plist_as_string(plst); 

	g_list_foreach(g_list_first(plst), (GFunc)g_free, NULL);
	plst = NULL;

	dbmail_imap_session_printf(self, "* STATUS %s %s\r\n", astring, pstring);

	g_free(astring);
	g_free(pstring);
	
	return FALSE;
}

static int dbmail_imap_session_mbxinfo_notify(struct ImapSession *self)
{
	if (! imap_feature_idle_status)
		return DM_SUCCESS;

	if (! self->mbxinfo)
		self->mbxinfo = dbmail_imap_session_get_mbxinfo(self);
	
	self->error = 0;
	if (g_tree_nnodes(self->mbxinfo) == 0)
		return self->error;
		
	g_tree_foreach(self->mbxinfo, (GTraverseFunc)imap_mbxinfo_notify, self);
	return self->error;
}

int dbmail_imap_session_mailbox_status(struct ImapSession * self, gboolean update)
{
	/* 
	   FIXME: this should be called more often?
	 
		C: a047 NOOP
		S: * 22 EXPUNGE
		S: * 23 EXISTS
		S: * 3 RECENT
		S: * 14 FETCH (FLAGS (\Seen \Deleted))

	But beware! The dovecot team discovered some client bugs:
	#     Send EXISTS/RECENT new mail notifications only when replying to NOOP
	#     and CHECK commands. Some clients ignore them otherwise, for example OSX
	#     Mail (<v2.1). Outlook Express breaks more badly though, without this it
	#     may show user "Message no longer in server" errors. Note that OE6 still
	#     breaks even with this workaround if synchronization is set to
	#     "Headers Only".

	*/

	int res;
	time_t oldmtime;
	GTree *oldmsginfo, *msginfo = NULL;
	unsigned oldexists, oldrecent, olduidnext;
        struct DbmailMailbox *mailbox = NULL;
	gboolean showexists = FALSE;
	static int idle_keepalive = 0;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	if (! ud->state == IMAPCS_SELECTED)
		return 0;

	oldmtime = ud->mailbox.mtime;
	oldexists = ud->mailbox.exists;
	oldrecent = ud->mailbox.recent;
	olduidnext = ud->mailbox.msguidnext;
        oldmsginfo = self->msginfo;

	if (! self->mbxinfo)
		self->mbxinfo = dbmail_imap_session_get_mbxinfo(self);

	if (update) {
                // re-read flags and counters
		if ((res = db_getmailbox(&(ud->mailbox))) != DM_SUCCESS)
			return res;

		if (oldmtime != ud->mailbox.mtime) {
			// rebuild uid/msn trees
			// ATTN: new messages shouldn't be visible in any way to a 
			// client session until it has been announced with EXISTS
			mailbox = dbmail_mailbox_new(self->mailbox->id);
			if ((res = dbmail_mailbox_open(mailbox)) != DM_SUCCESS) {
				dbmail_mailbox_free(mailbox);
				return res;
			}

			if ((msginfo = dbmail_imap_session_get_msginfo(self, mailbox->ids)) == NULL) {
				TRACE(TRACE_ERROR, "unable to retrieve msginfo");
				dbmail_mailbox_free(mailbox);
				return -1;
			}
		}
	}


	/* msg counts */
	// EXPUNGE
	switch (self->command_type) {
		case IMAP_COMM_IDLE:
			// send a keepalive message every 10 * IDLE_TIMEOUT seconds
			// this will cause most clients to issue a DONE/IDLE cycle
			if (idle_keepalive++ == 10) {
				dbmail_imap_session_printf(self, "* OK\r\n");
				idle_keepalive = 0;
			}
			// experimental: send '* STATUS' updates for all subscribed 
			// mailboxes if they have changed
			dbmail_imap_session_mbxinfo_notify(self);

			// fall-through
		case IMAP_COMM_NOOP:
		case IMAP_COMM_CHECK:
		case IMAP_COMM_SELECT:
		case IMAP_COMM_EXAMINE:
	
		if ((olduidnext != ud->mailbox.msguidnext) && (ud->mailbox.exists >= oldexists))
			showexists = TRUE;

		if ((!update) || (showexists)) // never decrements
			dbmail_imap_session_printf(self, "* %u EXISTS\r\n", ud->mailbox.exists);

		if ((!update) || (ud->mailbox.recent != oldrecent))
			dbmail_imap_session_printf(self, "* %u RECENT\r\n", ud->mailbox.recent);

		break;
		default:
		break;
	}


	if (update) {
		if (msginfo) {
			self->msginfo = msginfo;
			if (oldmsginfo) {
				g_tree_foreach(oldmsginfo, (GTraverseFunc)imap_msginfo_notify, self);
				g_tree_destroy(oldmsginfo);
			}
		}

		if (mailbox) {
			struct DbmailMailbox *b;
			b = self->mailbox;
			self->mailbox = mailbox;
			dbmail_mailbox_free(b);
		}
	}
	return 0;
}
int dbmail_imap_session_mailbox_show_info(struct ImapSession * self) 
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	g_return_val_if_fail( ud != NULL, DM_EGENERAL);

	dbmail_imap_session_mailbox_status(self, FALSE);

	GString *string;
	/* flags */
	GList *list = NULL;
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		list = g_list_append(list,"\\Seen");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		list = g_list_append(list,"\\Answered");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		list = g_list_append(list,"\\Deleted");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		list = g_list_append(list,"\\Flagged");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		list = g_list_append(list,"\\Draft");
	string = g_list_join(list," ");
	g_list_free(g_list_first(list));
	dbmail_imap_session_printf(self, "* FLAGS (%s)\r\n", string->str);

	/* permanent flags */
	list = NULL;
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		list = g_list_append(list,"\\Seen");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		list = g_list_append(list,"\\Answered");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		list = g_list_append(list,"\\Deleted");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		list = g_list_append(list,"\\Flagged");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		list = g_list_append(list,"\\Draft");
	string = g_list_join(list," ");
	g_list_free(g_list_first(list));
	dbmail_imap_session_printf(self, "* OK [PERMANENTFLAGS (%s)]\r\n", string->str);

	/* UIDNEXT */
	dbmail_imap_session_printf(self, "* OK [UIDNEXT %llu] Predicted next UID\r\n",
		ud->mailbox.msguidnext);
	
	/* UID */
	dbmail_imap_session_printf(self, "* OK [UIDVALIDITY %llu] UID value\r\n",
		ud->mailbox.uid);

	g_string_free(string,TRUE);
	return 0;
}
	
int dbmail_imap_session_mailbox_open(struct ImapSession * self, const char * mailbox)
{
	int result;
	GTree *t;
	u64_t mailbox_idnr;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	
	/* get the mailbox_idnr */
	mailbox_idnr = dbmail_imap_session_mailbox_get_idnr(self, mailbox);
	
	/* create missing INBOX for this authenticated user */
	if ((! mailbox_idnr ) && (strcasecmp(mailbox, "INBOX")==0)) {
		TRACE(TRACE_INFO, "Auto-creating INBOX for user id [%llu]", ud->userid);
		result = db_createmailbox("INBOX", ud->userid, &mailbox_idnr);
	}
	
	/* close the currently opened mailbox */
	dbmail_imap_session_mailbox_close(self);

	if (! mailbox_idnr) {
		dbmail_imap_session_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1; /* error */
	}

	/* check if user has right to select mailbox */
	if ((result = dbmail_imap_session_mailbox_check_acl(self, mailbox_idnr, ACL_RIGHT_READ)))
		return result;
	
	/* check if mailbox is selectable */
	if ((result = dbmail_imap_session_mailbox_get_selectable(self, mailbox_idnr)))
		return result;
	
	ud->mailbox.uid = mailbox_idnr;
	
	/* read info from mailbox */ 
	if ((result = db_getmailbox(&ud->mailbox)) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;	/* fatal  */
	}
	
	self->mailbox = dbmail_mailbox_new(mailbox_idnr);

	//because more than one mailbox can be open before session is destroyed
	t = self->msginfo;
	if ((self->msginfo = dbmail_imap_session_get_msginfo(self, self->mailbox->ids)) == NULL)
	       TRACE(TRACE_DEBUG, "unable to retrieve msginfo");
	if(t)
		g_tree_destroy(t);

	return 0;
}

#define IDLE_BUFFER 8
int dbmail_imap_session_mailbox_idle(struct ImapSession *self)
{
	char buffer[IDLE_BUFFER];
	int result = 0, idle_timeout = IDLE_TIMEOUT;
	clientinfo_t *ci = self->ci;
	field_t val;
	
	GETCONFIGVALUE("idle_timeout", "IMAP", val);
	if ( strlen(val) && (idle_timeout = atoi(val)) <= 0 ) {
		TRACE(TRACE_ERROR, "illegal value for idle_timeout [%s]", val);
		idle_timeout = IDLE_TIMEOUT;	
	}

	GETCONFIGVALUE("idle_status", "IMAP", val);
	if (strlen(val) && (g_strncasecmp(val,"yes",3)==0))
		imap_feature_idle_status = TRUE;
	else
		imap_feature_idle_status = FALSE;

	memset(buffer, 0, IDLE_BUFFER);
	dbmail_imap_session_mailbox_status(self,TRUE);
	dbmail_imap_session_printf(self, "+ idling\r\n");
	
	while (1) {

		if (ferror(ci->tx) || feof(ci->rx)) {
			int serr = errno;
			TRACE(TRACE_DEBUG,"error on client socket [%s]", strerror(serr));
			errno = serr;
			result = -1;
			break;
		}

		alarm(idle_timeout);
		alarm_occured = 0;

		fgets(buffer, IDLE_BUFFER, ci->rx);
		alarm(0);

		if (strlen(buffer) > 4 && strncmp(buffer,"DONE",4)==0)
			break;
		else if (strlen(buffer) > 0) {
			dbmail_imap_session_printf(self,"%s BAD Expecting DONE\r\n", self->tag);
			result = -1;
			break;
		}
		memset(buffer, 0, IDLE_BUFFER);

		if (alarm_occured) {
			alarm_occured = 0;
			dbmail_imap_session_mailbox_status(self,TRUE);
			continue;
		}
	}
	alarm(0);

	return result;
}

int dbmail_imap_session_mailbox_close(struct ImapSession *self)
{
	// flush recent messages from previous select
	dbmail_imap_session_mailbox_update_recent(self);
	dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);
	if (self->mailbox) {
		dbmail_mailbox_free(self->mailbox);
		self->mailbox = NULL;
	}

	return 0;
}

static int imap_session_update_recent(GList *recent) 
{
	GList *slices, *topslices;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);

	if (recent == NULL)
		return 0;

	topslices = g_list_slices(recent,100);
	slices = g_list_first(topslices);

	db_begin_transaction();
	while (slices) {
		snprintf(query, DEF_QUERYSIZE, "UPDATE %smessages SET recent_flag = 0 "
				"WHERE message_idnr IN (%s) AND recent_flag = 1", 
				DBPFX, (gchar *)slices->data);
		if (db_query(query) == -1) {
			db_rollback_transaction();
			return (-1);
		}
		if (! g_list_next(slices))
			break;
		slices = g_list_next(slices);
	}
	db_commit_transaction();
	
        g_list_destroy(topslices);
	return 0;
}

int dbmail_imap_session_mailbox_update_recent(struct ImapSession *self) 
{
	imap_session_update_recent(self->recent);
	g_list_destroy(self->recent);
	self->recent = NULL;

	return 0;
}

int dbmail_imap_session_set_state(struct ImapSession *self, int state)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	switch (state) {
		case IMAPCS_AUTHENTICATED:
			ud->state = state;
			memset(&ud->mailbox, 0, sizeof(ud->mailbox));
			// change from login_timeout to main timeout
			self->timeout = self->ci->timeout; 
			break;
			
		default:
			ud->state = state;
			break;
	}
	return 0;
}


/*****************************************************************************
 *
 *
 *   bodyfetch
 *
 *
 ****************************************************************************/

void dbmail_imap_session_bodyfetch_new(struct ImapSession *self) 
{

	assert(self->fi);
	body_fetch_t *bodyfetch = g_new0(body_fetch_t, 1);
	bodyfetch->itemtype = -1;
	self->fi->bodyfetch = g_list_append(self->fi->bodyfetch, bodyfetch);
}

void dbmail_imap_session_bodyfetch_rewind(struct ImapSession *self) 
{
	assert(self->fi);
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
}

static void _body_fetch_free(body_fetch_t *bodyfetch, gpointer UNUSED data)
{
	if (! bodyfetch)
		return;
	if (bodyfetch->hdrnames) 
		g_free(bodyfetch->hdrnames);
	if (bodyfetch->hdrplist)
		g_free(bodyfetch->hdrplist);
	g_free(bodyfetch);
	bodyfetch = NULL;
}

void dbmail_imap_session_bodyfetch_free(struct ImapSession *self) 
{
	assert(self->fi);
	if (! self->fi->bodyfetch)
		return;
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
	g_list_foreach(self->fi->bodyfetch, (GFunc)_body_fetch_free, NULL);
	g_list_free(g_list_first(self->fi->bodyfetch));
	self->fi->bodyfetch = NULL;
}

body_fetch_t * dbmail_imap_session_bodyfetch_get_last(struct ImapSession *self) 
{
	assert(self->fi);
	if (self->fi->bodyfetch == NULL)
		dbmail_imap_session_bodyfetch_new(self);
	
	self->fi->bodyfetch = g_list_last(self->fi->bodyfetch);
	return (body_fetch_t *)self->fi->bodyfetch->data;
}

int dbmail_imap_session_bodyfetch_set_partspec(struct ImapSession *self, char *partspec, int length) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	memset(bodyfetch->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(bodyfetch->partspec,partspec,length);
	return 0;
}
char *dbmail_imap_session_bodyfetch_get_last_partspec(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->partspec;
}

int dbmail_imap_session_bodyfetch_set_itemtype(struct ImapSession *self, int itemtype) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->itemtype = itemtype;
	return 0;
}
int dbmail_imap_session_bodyfetch_get_last_itemtype(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->itemtype;
}
int dbmail_imap_session_bodyfetch_set_argstart(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argstart = self->args_idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_get_last_argstart(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argcnt = self->args_idx - bodyfetch->argstart;
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_get_last_argcnt(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argcnt;
}

int dbmail_imap_session_bodyfetch_set_octetstart(struct ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetstart = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(struct ImapSession *self)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetstart;
}


int dbmail_imap_session_bodyfetch_set_octetcnt(struct ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetcnt = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(struct ImapSession *self)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetcnt;
}

u64_t get_dumpsize(body_fetch_t *bodyfetch, u64_t dumpsize) 
{
	long long cnt = dumpsize - bodyfetch->octetstart;
	if (cnt < 0)
		cnt = 0;
	if ((guint64)cnt > bodyfetch->octetcnt)
		cnt = bodyfetch->octetcnt;
	return (u64_t)cnt;
}

/* local defines */
#define NORMPAR 1
#define SQUAREPAR 2
#define NOPAR 0

/*
 * build_args_array_ext()
 *
 * imap command tokenizer
 *
 * builds an dimensional array of strings containing arguments based upon 
 * strings on cmd line specified by {##}\0
 * (\r\n had been removed from string)
 * 
 * normal/square parentheses have special meaning:
 * '(body [all header])' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '['
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ']'
 * [6] = ')'
 *
 * quoted strings are those enclosed by double quotation marks and returned as a single argument
 * WITHOUT the enclosing quotation marks
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */
char **build_args_array_ext(struct ImapSession *self, const char *originalString)
{
	int nargs = 0, inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i;
	char parlist[MAX_LINESIZE];
	char s[MAX_LINESIZE];
	char *tmp, *lastchar;
	int quotedSize, cnt, dataidx;
	int gotc;
	int result;
	clientinfo_t *ci = self->ci;

	/* free the last round of arguments */
	free_args(self);

	/* Check for null input */
	if (!originalString)
		return NULL;

	/* Check for zero length input */
	if (!(originalString[0])) {
		self->args[0] = NULL;
		return self->args;
	}

	/* Make a local copy of the string */
	g_strlcpy(s, originalString, MAX_LINESIZE);

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	for (i = 0, nargs = 0; s[i] && nargs < MAX_ARGS - 1; i++) {
		/* check quotes */
		if (s[i] == '"' && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				self->args[nargs] = g_new0(char,(i - quotestart));
				memcpy((void *) self->args[nargs], (void *) &s[quotestart + 1], i - quotestart - 1);
				self->args[nargs][i - quotestart - 1] = '\0';

				nargs++;
				inquote = 0;
			} else {
				inquote = 1;
				quotestart = i;
			}

			continue;
		}

		if (inquote)
			continue;

		/* check for (, ), [ or ] in string */
		
		if (s[i] == '(' || s[i] == ')' || s[i] == '[' || s[i] == ']') {
			switch (s[i]) {
			/* check parenthese structure */
				case ')':
					
				if (paridx < 0 || parlist[paridx] != NORMPAR)
					paridx = -1;
				else {
					nnorm--;
					paridx--;
				}

				break;

				case ']':
				
				if (paridx < 0 || parlist[paridx] != SQUAREPAR)
					paridx = -1;
				else {
					paridx--;
					nsquare--;
				}

				break;

				case '(':
				
				parlist[++paridx] = NORMPAR;
				nnorm++;
				
				break;

				case '[':
				
				parlist[++paridx] = SQUAREPAR;
				nsquare++;

				break;
			}

			if (paridx < 0) {
				/* error in parenthesis structure */
				return NULL;
			}

			/* add this parenthesis to the arg list and continue */
			self->args[nargs] = g_new0(char,2);
			self->args[nargs][0] = s[i];
			self->args[nargs][1] = '\0';

			nargs++;
			continue;
		}

		if (s[i] == ' ')
			continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			quotedSize = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			TRACE(TRACE_DEBUG, "last char = %c", *lastchar);
			if ((*lastchar == '+' && *(lastchar + 1) == '}' && *(lastchar + 2) == '\0') || 
			    (*lastchar == '}' && *(lastchar + 1) == '\0')) {
				/* allocate space for this argument (could be a message when used with APPEND) */
				self->args[nargs] = g_new0(char, quotedSize+1);
			
				ci_write(ci->tx, "+ OK gimme that string\r\n");
				
				for (cnt = 0, dataidx = 0; cnt < quotedSize; cnt++) {
					/* dont wait forever, but reset after every fgetc */
					alarm(self->timeout);
					if (! (gotc = fgetc(ci->rx)))
						break;
					
					if (gotc == '\r')	/* only store if it is not \r */
						continue;
					
					self->args[nargs][dataidx++] = gotc;
				}
				
				alarm(0);

				self->args[nargs][dataidx] = '\0';	/* terminate string */
				nargs++;
				
				if (alarm_occured) {
					alarm_occured = 0;
					// FIXME: why is the alarm handler sometimes triggered though cnt == quotedSize
					if (cnt < quotedSize) {
						dbmail_imap_session_printf(self, "%s", IMAP_TIMEOUT_MSG);
						client_close();
						TRACE(TRACE_ERROR, "timeout occurred in fgetc; got [%d] of [%d]; timeout [%d]", 
								cnt, quotedSize, self->timeout);
						return NULL;
					}
				}

				if (ferror(ci->rx) || ferror(ci->tx)) {
					TRACE(TRACE_ERROR, "client socket has set error indicator in fgetc");
					return NULL;
				}
				/* now read the rest of this line */
				result = dbmail_imap_session_readln(self, s);
		
				if (result < 0){
					return NULL;
				}

				if (ferror(ci->rx) || ferror(ci->tx)) {
					TRACE(TRACE_ERROR, "client socket is set error indicator in dbmail_imap_session_readln");
					return NULL;
				}

				/* remove trailing \r\n */
				tmp = &s[strlen(s)];
				tmp--;	/* go before trailing \0; watch this with empty strings! */
				while (tmp >= s && (*tmp == '\r' || *tmp == '\n')) {
					*tmp = '\0';
					tmp--;
				}

				TRACE(TRACE_DEBUG, "got extra line [%s]", s);

				/* start over! */
				i = 0;
				continue;
			}
		}

		/* at an argument start now, walk on until next delimiter
		 * and save argument 
		 */

		for (argstart = i; i < strlen(s) && !strchr(" []()", s[i]); i++)
			if (s[i] == '"') {
				if (s[i - 1] == '\\')
					continue;
				else
					break;
			}

		self->args[nargs] = g_new0(char,(i - argstart + 1));
		memcpy((void *) self->args[nargs], (void *) &s[argstart], i - argstart);
		self->args[nargs][i - argstart] = '\0';

		nargs++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) {
		/* error in parenthesis structure */
		return NULL;
	}

	self->args[nargs] = NULL;	/* terminate */

	/* dump args (debug) */
	for (i = 0; self->args[i]; i++) {
		TRACE(TRACE_DEBUG, "arg[%d]: '%s'\n", i, self->args[i]);
	}

	return self->args;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR



