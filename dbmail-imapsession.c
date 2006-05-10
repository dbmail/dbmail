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

/* $Id: dbmail-imapsession.c 2113 2006-05-09 14:10:52Z paul $
 * 
 * dm_imaputil.c
 *
 * IMAP-server utility functions implementations
 */

#include "dbmail.h"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE (10*1024)
#endif

#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];


/* cache */
extern cache_t cached_msg;

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

static int _imap_session_fetch_parse_partspec(struct ImapSession *self, int idx);
static int _imap_session_fetch_parse_octet_range(struct ImapSession *self, int idx);

static void _imap_show_body_sections(struct ImapSession *self);
static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data);
	

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
			null_free(((imap_userdata_t*)self->ci->userData)->mailbox.seq_list);
			null_free(self->ci->userData);
		}
		g_free(self->ci);
		self->ci = NULL;
	}
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
	if (! self)
		trace(TRACE_ERROR,"%s,%s: OOM error", __FILE__, __func__);

	dbmail_imap_session_resetFi(self);
	
	return self;
}

struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self)
{
	if (! self->fi) {
		self->fi = g_new0(fetch_items_t,1);
		return self;
	}
	
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)g_free,NULL);
	memset(self->fi,'\0',sizeof(fetch_items_t));
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
struct ImapSession * dbmail_imap_session_setMsginfo(struct ImapSession * self, msginfo_t * msginfo)
{
	if (self->msginfo)
		g_free(self->msginfo);
	self->msginfo = msginfo;
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
struct ImapSession * dbmail_imap_session_setArgs(struct ImapSession * self, char ** args)
{
	self->args = args;
	return self;
}


void dbmail_imap_session_delete(struct ImapSession * self)
{
	close_cache();

	_imap_fetchitem_free(self);
	_imap_clientinfo_free(self);
	
	if (self->fstream) {
		g_object_unref(self->fstream);
		self->fstream = NULL;
	}

	if (self->msginfo) {
		g_free(self->msginfo);
		self->msginfo = NULL;
	}
	if (self->tag) {
		g_free(self->tag);
		self->tag = NULL;
	}
	if (self->command) {
		g_free(self->command);
		self->command = NULL;
	}
	
	g_free(self);
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

	trace(TRACE_DEBUG,"%s,%s: cache message [%llu] filter [%d]",
			__FILE__, __func__, self->msg_idnr, filter);

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
	
	trace(TRACE_DEBUG,"%s,%s: cache size [%llu]", __FILE__, __func__, outcnt);	
	return outcnt;
}

static int _imap_session_fetch_parse_partspec(struct ImapSession *self, int idx)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	char *token, *nexttoken;

	token=self->args[idx];
	nexttoken=self->args[idx+1];

	trace(TRACE_DEBUG,"%s,%s: token [%s], nexttoken [%s]",__FILE__, __func__, token, nexttoken);

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
		idx++;	/* should be at '(' now */
		token = self->args[idx];
		nexttoken = self->args[idx+1];
		
		if (! MATCH(token,"("))
			return -2;	/* error DONE */

		idx++;	/* at first item of field list now, remember idx */
		dbmail_imap_session_bodyfetch_set_argstart(self, idx);

		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(self->args[idx],")") )
			idx++;

		token = self->args[idx];
		nexttoken = self->args[idx+1];
		
		dbmail_imap_session_bodyfetch_set_argcnt(self, idx);

		if (dbmail_imap_session_bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;	/* error DONE */
	}
	return idx + 1;
}

static int _imap_session_fetch_parse_octet_range(struct ImapSession *self, int idx) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[idx];
	
	if (! token)
		return idx;
	
	trace(TRACE_DEBUG,"%s,%s: parse token [%s]",__FILE__, __func__, token);

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
		return idx;
	}

	return idx + 1;	/* DONE */
}

/*
 * dbmail_imap_session_fetch_parse_args()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int dbmail_imap_session_fetch_parse_args(struct ImapSession * self, int idx)
{
	int invalidargs, ispeek = 0;
	
	invalidargs = 0;

	if (!self->args[idx])
		return -1;	/* no more */

	if (self->args[idx][0] == '(')
		idx++;

	if (!self->args[idx])
		return -2;	/* error */

	
	char *token = NULL, *nexttoken = NULL;
	
	token = self->args[idx];
	nexttoken = self->args[idx+1];

	trace(TRACE_DEBUG,"%s,%s: parse args[%d] = [%s]",
		__FILE__,__func__, idx, token);

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
		
		nexttoken = (char *)self->args[idx+1];
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek)
				return -2;	/* error DONE */
			self->fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			/* now read the argument list to body */
			idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			idx++;	/* now pointing at what should be the item type */

			token = (char *)self->args[idx];
			nexttoken = (char *)self->args[idx+1];

			if (MATCH(token,"]")) {
				if (ispeek)
					self->fi->getBodyTotalPeek = 1;
				else
					self->fi->getBodyTotal = 1;
				return _imap_session_fetch_parse_octet_range(self,idx+1);
			}
			
			if (ispeek)
				self->fi->noseen = 1;

			if ((idx = _imap_session_fetch_parse_partspec(self,idx)) < 0) {
				trace(TRACE_DEBUG,"%s,%s: fetch_parse_partspec return with error", 
						__FILE__, __func__);
				return -2;
			}
			/* idx points to ']' now */
			return _imap_session_fetch_parse_octet_range(self,idx+1);
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
		self->fi->msgparse_needed=1;
		self->fi->getEnvelope = 1;
	} else {			
		if ((! nexttoken) && (strcmp(token,")") == 0)) {
			/* only allowed if last arg here */
			return -1;
		}
		return -2;	/* DONE */
	}
	trace(TRACE_DEBUG, "%s,%s: args[idx = %d] = %s (returning %d)\n",
	      __FILE__,__func__, idx, self->args[idx], idx + 1);
	return idx + 1;
}

int dbmail_imap_session_fetch_get_unparsed(struct ImapSession *self, u64_t fetch_start, u64_t fetch_end)
{

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t lo, hi;
	
	if (!self->use_uid) {
		lo = ud->mailbox.seq_list[fetch_start];
		hi = ud->mailbox.seq_list[fetch_end];
	} else {
		lo = fetch_start;
		hi = fetch_end;
	}

	return dbmail_imap_session_get_msginfo_range(self, lo, hi);
}

int dbmail_imap_session_get_msginfo_range(struct ImapSession *self, u64_t msg_idnr_low, u64_t msg_idnr_high)
{
	unsigned nrows, i, j;
	const char *query_result;
	char *to_char_str;
	msginfo_t *result;
	
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	db_free_result();

	to_char_str = date2char_str("internal_date");
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT seen_flag, answered_flag, deleted_flag, flagged_flag, "
		 "draft_flag, recent_flag, %s, rfcsize, message_idnr "
		 "FROM %smessages msg, %sphysmessage pm "
		 "WHERE pm.id = msg.physmessage_id "
		 "AND message_idnr BETWEEN '%llu' AND '%llu' "
		 "AND mailbox_idnr = '%llu' AND status IN ('%d','%d') "
		 "ORDER BY message_idnr ASC",to_char_str,DBPFX,DBPFX,
		 msg_idnr_low, msg_idnr_high, ud->mailbox.uid,
		 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);
	dm_free(to_char_str);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select message",
		      __FILE__, __func__);
		return (-1);
	}

	if ((nrows = db_num_rows()) == 0) {
		db_free_result();
		return 0;
	}

	result = g_new0(msginfo_t, nrows);

	for (i = 0; i < nrows; i++) {
		/* flags */
		for (j = 0; j < IMAP_NFLAGS; j++)
			result[i].flags[j] = db_get_result_bool(i, j);

		/* internal date */
		query_result = db_get_result(i, IMAP_NFLAGS);
		strncpy(result[i].internaldate,
			(query_result) ? query_result :
			"01-Jan-1970 00:00:01 +0100",
			IMAP_INTERNALDATE_LEN);
		
		/* rfcsize */
		result[i].rfcsize = db_get_result_u64(i, IMAP_NFLAGS + 1);
		
		/* uid */
		result[i].uid = db_get_result_u64(i, IMAP_NFLAGS + 2);
	}
	db_free_result();
	dbmail_imap_session_setMsginfo(self, result);

	return (int)nrows;
}

#define SEND_SPACE if (self->fi->isfirstfetchout) \
				self->fi->isfirstfetchout = 0; \
			else \
				dbmail_imap_session_printf(self, " ")

int dbmail_imap_session_fetch_get_items(struct ImapSession *self, u64_t row)
{
	int result, j = 0;
	u64_t actual_cnt, tmpdumpsize, i = row;
	gchar *s;
	
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	

	GList *sublist = NULL;;
	GString *tmp = g_string_new("");
	
	self->fi->isfirstfetchout = 1;
	
	if (self->fi->getInternalDate) {
		SEND_SPACE;
		dbmail_imap_session_printf(self, "INTERNALDATE \"%s\"", date_sql2imap(self->msginfo[i].internaldate));
	}
	if (self->fi->getSize) {
		SEND_SPACE;
		dbmail_imap_session_printf(self, "RFC822.SIZE %llu", self->msginfo[i].rfcsize);
	}
	if (self->fi->getFlags) {
		SEND_SPACE;
		sublist = NULL;
		for (j = 0; j < IMAP_NFLAGS; j++) {
			if (self->msginfo[i].flags[j]) 
				sublist = g_list_append(sublist,g_strdup((gchar *)imap_flag_desc_escaped[j]));
		}
		s = dbmail_imap_plist_as_string(sublist);
		dbmail_imap_session_printf(self,"FLAGS %s",s);
		g_list_foreach(sublist,(GFunc)g_free,NULL);
		g_list_free(sublist);
		g_free(s);

	}
	if (self->fi->getUID) {
		SEND_SPACE;
		dbmail_imap_session_printf(self, "UID %llu", self->msginfo[i].uid);
	}

	if (self->fi->getMIME_IMB) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((cached_msg.dmsg)->content), 1))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		dbmail_imap_session_printf(self, "BODYSTRUCTURE %s", s);
		g_free(s);
	}

	if (self->fi->getMIME_IMB_noextension) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((cached_msg.dmsg)->content), 0))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body\r\n");
			return -1;
		}
		dbmail_imap_session_printf(self, "BODY %s",s);
		g_free(s);
	}

	if (self->fi->getEnvelope) {

		SEND_SPACE;

		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_envelope(GMIME_MESSAGE((cached_msg.dmsg)->content)))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching envelope structure\r\n");
			return -1;
		}
		dbmail_imap_session_printf(self, "ENVELOPE %s", s);
		g_free(s);
	}

	if (self->fi->getRFC822 || self->fi->getRFC822Peek) {

		SEND_SPACE;

		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		dbmail_imap_session_printf(self, "RFC822 {%llu}\r\n", cached_msg.dumpsize);
		send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);

		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {

		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			dbmail_imap_session_printf(self, "BODY[] {%llu}\r\n", cached_msg.dumpsize);
			send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);
		} else {
			mseek(cached_msg.memdump, dbmail_imap_session_bodyfetch_get_last_octetstart(self), SEEK_SET);
			actual_cnt = (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) >
			     (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)))
			    ? (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)) 
			    : dbmail_imap_session_bodyfetch_get_last_octetcnt(self);

			dbmail_imap_session_printf(self, "BODY[]<%llu> {%llu}\r\n", 
					dbmail_imap_session_bodyfetch_get_last_octetstart(self), actual_cnt);
			send_data(self->ci->tx, cached_msg.memdump, actual_cnt);
		}

		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;

	}

	if (self->fi->getRFC822Header) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_HEAD);
		dbmail_imap_session_printf(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
		send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);
	}

	if (self->fi->getRFC822Text) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_BODY);
		dbmail_imap_session_printf(self, "RFC822.TEXT {%llu}\r\n", tmpdumpsize);
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
			dbmail_imap_session_printf(self, "\r\n *BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			result = db_set_msgflag(self->msg_idnr, ud->mailbox.uid, setSeenSet, IMAPFA_ADD);
			if (result == -1) {
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
		}

		self->fi->getFlags = 1;
		dbmail_imap_session_printf(self, " ");
	}

	dbmail_imap_session_printf(self, ")\r\n");
	g_string_free(tmp,TRUE);
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
		dbmail_imap_session_printf(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
		mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
	} else {
		cnt = size;
		dbmail_imap_session_printf(self, "] {%llu}\r\n", size);
		mrewind(cached_msg.tmpdump);
	}
	send_data(self->ci->tx, cached_msg.tmpdump, cnt);
}


static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data) 
{
	long long cnt = 0;
	u64_t tmpdumpsize;
	GList *tlist = NULL, *flist = NULL;
	GString *ts;
	GMimeObject *part = NULL;
	char *tmp;
	gboolean condition = FALSE;
	int k;
	u64_t physid;
	struct ImapSession *self = (struct ImapSession *)data;
	
	if (bodyfetch->itemtype < 0)
		return 0;
	
	trace(TRACE_DEBUG,"%s,%s: itemtype [%d] partspec [%s]", __FILE__, __func__, bodyfetch->itemtype, bodyfetch->partspec);
	
	if (self->fi->msgparse_needed) {
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (bodyfetch->partspec[0]) {
			if (bodyfetch->partspec[0] == '0') {
				dbmail_imap_session_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
				trace(TRACE_DEBUG, "PROTOCOL ERROR");
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
	dbmail_imap_session_printf(self, "BODY[%s", bodyfetch->partspec);

	switch (bodyfetch->itemtype) {

	case BFIT_TEXT:
		dbmail_imap_session_printf(self, "TEXT");
		/* fall through */
		
	case BFIT_TEXT_SILENT:
		if (!part)
			dbmail_imap_session_printf(self, "] NIL");
		else {
			tmp = imap_get_logical_part(part,"TEXT");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);
			
			if (!tmpdumpsize) 
				dbmail_imap_session_printf(self, "] NIL");
			else 
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}
		break;

	case BFIT_HEADER:
		dbmail_imap_session_printf(self, "HEADER");
		if (!part)
			dbmail_imap_session_printf(self, "] NIL");
		else {
			tmp = imap_get_logical_part(part,"HEADER");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);
			
			if (!tmpdumpsize) 
				dbmail_imap_session_printf(self, "] NIL");
			else 
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}
		break;
		
	case BFIT_MIME:
		dbmail_imap_session_printf(self, "MIME");

		if (!part)
			dbmail_imap_session_printf(self, "NIL");
		else {
			tmp = imap_get_logical_part(part,"MIME");
			tmpdumpsize = _imap_cache_set_dump(tmp,IMAP_CACHE_TMPDUMP);
			g_free(tmp);

			if (!tmpdumpsize)
				dbmail_imap_session_printf(self, "NIL");
			else
				imap_cache_send_tmpdump(self,bodyfetch,tmpdumpsize);
		}

		break;

	case BFIT_HEADER_FIELDS_NOT:
		condition=TRUE;
		
	case BFIT_HEADER_FIELDS:
		tlist = NULL;
		
		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, g_strdup(self->args[k + bodyfetch->argstart]));

		tmp = dbmail_imap_plist_as_string(tlist);
		dbmail_imap_session_printf(self,"HEADER.FIELDS%s %s] ", condition ? ".NOT" : "", tmp);
		g_free(tmp);
		
		if (db_get_physmessage_id(self->msg_idnr, &physid) == DM_EQUERY)
			return DM_EGENERAL;

		tmp = imap_message_fetch_headers(physid, tlist, condition);
	
		tmpdumpsize=0;
		
		if (!tmp) {
			dbmail_imap_session_printf(self, "{2}\r\n\r\n");
		} else {
			ts = g_string_new(tmp);
			g_free(tmp);
			
			if (bodyfetch->octetcnt > 0) {
				
				if (bodyfetch->octetstart > 0 && bodyfetch->octetstart < ts->len)
					ts = g_string_erase(ts, 0, bodyfetch->octetstart);
				
				if (ts->len > bodyfetch->octetcnt)
					ts = g_string_truncate(ts, bodyfetch->octetcnt);
				
				tmp = get_crlf_encoded(ts->str);
				cnt = strlen(tmp);
				
				dbmail_imap_session_printf(self, "<%llu> {%llu}\r\n%s\r\n", 
						bodyfetch->octetstart, 
						cnt+2,
						tmp);
			} else {
				tmp = get_crlf_encoded(ts->str);
				cnt = strlen(tmp);
				dbmail_imap_session_printf(self, "{%llu}\r\n%s\r\n", cnt+2, tmp);
			}
			g_string_free(ts,TRUE);
			g_free(tmp);
		}

		g_list_foreach(tlist, (GFunc)g_free, NULL);
		g_list_free(tlist);
		g_list_foreach(flist, (GFunc)g_free, NULL);
		g_list_free(flist);

		break;
	default:
		dbmail_imap_session_printf(self, "\r\n* BYE internal server error\r\n");
		return -1;
	}
	
	return 0;
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
		if (!self->args[i]) {
			/* error: need more args */
			dbmail_imap_session_printf(self,
				"%s BAD missing argument%s to %s\r\n", self->tag,
				(minargs == 1) ? "" : "(s)", command);
			return 0;
		}
	}

	for (i = 0; self->args[i]; i++);

	if (maxargs && (i > maxargs)) {
		/* error: too many args */
		dbmail_imap_session_printf(self, "%s BAD too many arguments to %s\r\n", self->tag,
			command);
		return 0;
	}

	/* succes */
	return 1;
}
	
int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...)
{
        va_list ap;
        int len;
        FILE * fd;
        int maxlen=100;
        int result = 0;
        gchar *ln;

        gchar *re = g_new0(gchar, maxlen+1);

	va_start(ap, message);
	ln = g_strdup_vprintf(message,ap);
	va_end(ap);

        if (! ln)
                return -1;
	
        if ((result = snprintf(re,maxlen,"%s",ln))<0)
                return -1;

        fd = self->ci->tx;

        if (feof(fd) || fflush(fd) < 0)
                trace(TRACE_FATAL, "%s,%s: client socket closed", __FILE__, __func__);

        len = g_mime_stream_write_string(self->fstream,ln);
	
        if (len < 0)
                trace(TRACE_FATAL, "%s,%s: write to client socket failed", __FILE__, __func__);

        if (result < maxlen)
                trace(TRACE_DEBUG,"RESPONSE: [%s]", re);
        else
                trace(TRACE_DEBUG,"RESPONSE: [%s...]", re);

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
		alarm(ci->timeout);
		if (fgets(buffer, MAX_LINESIZE, ci->rx) == NULL) {
			alarm(0);
			trace(TRACE_ERROR, "%s,%s: error reading from client", __FILE__, __func__);
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
	
	memset(buffer, 0, MAX_LINESIZE);
	alarm(ci->timeout);
	if (fgets(buffer, MAX_LINESIZE, ci->rx) == NULL) {
		trace(TRACE_ERROR, "%s,%s: error reading from client", __FILE__, __func__);
		return -1;
	}
	len = strlen(buffer);
	if (len >= (MAX_LINESIZE-1)) {
		trace(TRACE_ERROR, "%s,%s: too long line from client (discarding)", __FILE__, __func__);
		alarm(0);
		/* Note: we do preserve the partial read here -- so that 
		 * the command parser can extract a tag if need be */
		if (dbmail_imap_session_discard_to_eol(self) < 0) {
			return -1;
		} else {
			return 0;
		}
	}

	alarm(0);
	return strlen(buffer);
}
	
int dbmail_imap_session_handle_auth(struct ImapSession * self, char * username, char * password)
{

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	timestring_t timestring;
	create_current_timestring(&timestring);
	
	u64_t userid = 0;
	
	trace(TRACE_DEBUG, "%s,%s: trying to validate user [%s], pass [%s]", 
			__FILE__, __func__, username, (password ? "XXXX" : "(null)") );
	
	int valid = auth_validate(self->ci, username, password, &userid);
	
	trace(TRACE_MESSAGE, "%s,%s: user (id:%llu, name %s) tries login",
			__FILE__, __func__, userid, username);

	if (valid == -1) {
		/* a db-error occurred */
		dbmail_imap_session_printf(self, "* BYE internal db error validating user\r\n");
		trace(TRACE_ERROR,
		      "%s,%s: db-validate error while validating user %s (pass %s).",
		      __FILE__, __func__, username, password);
		return -1;
	}

	if (valid == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		trace(TRACE_MESSAGE, "IMAPD [PID %d]: user (name %s) login rejected @ %s",
		      (int) getpid(), username, timestring);
		dbmail_imap_session_printf(self, "%s NO login rejected\r\n", self->tag);

		return 1;
	}

	/* login ok */
	trace(TRACE_MESSAGE, "%s,%s: user (id %llu, name %s) login accepted @ %s",
	      __FILE__, __func__, 
	      userid, username, timestring);

	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	return 0;

}


/* Value must be preallocated to MAX_LINESIZE length. */
int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value )
{
	char *buf, *prompt64, *promptcat;
	int buflen;
	
	if (! (buf = g_new0(char, MAX_LINESIZE))) {
		trace(TRACE_ERROR, "%s,%s: oom failure", __FILE__, __func__);
		return -1;
	}
			
	/* base64 encoding increases string length by about 40%. */
	if (! (prompt64 = g_new0(char, strlen(prompt) * 2))) {
		trace(TRACE_ERROR, "%s,%s: oom failure", __FILE__, __func__);
		dm_free(buf);
		return -1;
	}

	promptcat = g_strconcat(prompt, "\r\n", NULL);
	base64_encode(prompt64, promptcat, strlen(promptcat));

	dbmail_imap_session_printf(self, "+ %s\r\n", prompt64);
	fflush(self->ci->tx);
	
	if ( (dbmail_imap_session_readln(self, buf) <= 0) )  {
		dm_free(buf);
		dm_free(prompt64);
		dm_free(promptcat);
		return -1;
	}

	/* value is the same size as buf.
	 * base64 decoding is always shorter. */
	buflen = base64_decode(value, buf);
	value[buflen] = '\0';
	
	/* Double check in case the algorithm went nuts. */
	if (buflen >= (MAX_LINESIZE - 1)) {
		/* Oh shit. */
		trace(TRACE_FATAL, "%s,%s: possible memory corruption", __FILE__, __func__);
		return -1;
	}

	dm_free(buf);
	dm_free(prompt64);
	dm_free(promptcat);
	
	return 0;
}

u64_t dbmail_imap_session_mailbox_get_idnr(struct ImapSession * self, char * mailbox)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t uid;
	int i;
	
	/* remove trailing '/' if present */
	while (strlen(mailbox) > 0 && mailbox[strlen(mailbox) - 1] == '/')
		mailbox[strlen(mailbox) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; mailbox[i] && mailbox[i] == '/'; i++);
	memmove(&mailbox[0], &mailbox[i],
		(strlen(mailbox) - i) * sizeof(char));

	db_findmailbox(mailbox, ud->userid, &uid);
	
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

int dbmail_imap_session_mailbox_show_info(struct ImapSession * self) 
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result = db_getmailbox(&ud->mailbox);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	/* msg counts */
	dbmail_imap_session_printf(self, "* %u EXISTS\r\n", ud->mailbox.exists);
	dbmail_imap_session_printf(self, "* %u RECENT\r\n", ud->mailbox.recent);

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
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		list = g_list_append(list,"\\Recent");
	string = g_list_join(list," ");
	g_list_free(list);
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
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		list = g_list_append(list,"\\Recent");
	string = g_list_join(list," ");
	g_list_free(list);
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
	
int dbmail_imap_session_mailbox_open(struct ImapSession * self, char * mailbox)
{
	int result;
	u64_t mailbox_idnr;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	char *tmp;
	
	/* get the mailbox_idnr */
	mailbox_idnr = dbmail_imap_session_mailbox_get_idnr(self, mailbox);

	if ((! mailbox_idnr ) && (strncasecmp(mailbox,"INBOX",5)==0)) {
		/* create missing INBOX for this authenticated user */
		tmp = g_ascii_strup(mailbox,-1);
		result = db_createmailbox(tmp, ud->userid, &mailbox_idnr);
		g_free(tmp);
	}
		
	if (! mailbox_idnr) {
		ud->state = IMAPCS_AUTHENTICATED;
		dm_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
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

	return 0;
}

int dbmail_imap_session_mailbox_select_recent(struct ImapSession *self) {
	unsigned i, j;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	self->recent = NULL;
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages WHERE recent_flag = 1 AND mailbox_idnr = '%llu'",
		 DBPFX, ud->mailbox.uid);

	if (db_query(query) == -1) 
		return (-1);

	j = db_num_rows();
	for (i = 0; i < j; i++) 
		self->recent = g_list_append(self->recent, g_strdup(db_get_result(i, 0)));
	
	db_free_result();
	trace(TRACE_DEBUG, "%s,%s: recent [%d] in mailbox [%llu]",
			__FILE__, __func__, g_list_length(self->recent), ud->mailbox.uid);

	return g_list_length(self->recent);
}

int dbmail_imap_session_mailbox_update_recent(struct ImapSession *self) {
	GList *slices = NULL;
	
	if (self->recent == NULL)
		return 0;

	slices = g_list_slices(self->recent,100);
	slices = g_list_first(slices);
	while (slices) {
		snprintf(query, DEF_QUERYSIZE, "update %smessages set recent_flag = 0 "
				"where message_idnr in (%s)", DBPFX, (gchar *)slices->data);
		if (db_query(query) == -1) 
			return (-1);
		if (! g_list_next(slices))
			break;
		slices = g_list_next(slices);
	}
	
	g_list_foreach(self->recent,(GFunc)g_free, NULL);
	g_list_free(self->recent);
	g_list_foreach(slices, (GFunc)g_free, NULL);
	g_list_free(slices);
	
	self->recent = NULL;
	return 0;
}
int dbmail_imap_session_set_state(struct ImapSession *self, int state)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	switch (state) {
		case IMAPCS_AUTHENTICATED:
			ud->state = state;
			dm_free(ud->mailbox.seq_list);
			memset(&ud->mailbox, 0, sizeof(ud->mailbox));
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

void dbmail_imap_session_bodyfetch_free(struct ImapSession *self) 
{
	assert(self->fi);
	g_list_foreach(self->fi->bodyfetch, (GFunc)g_free, NULL);
	g_list_free(self->fi->bodyfetch);
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
int dbmail_imap_session_bodyfetch_set_argstart(struct ImapSession *self, int idx) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argstart = idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_get_last_argstart(struct ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(struct ImapSession *self, int idx) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argcnt = idx - bodyfetch->argstart;
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

char *the_args[MAX_ARGS];

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

static void free_args(void)
{
	int i;
	for (i = 0; i < MAX_ARGS && the_args[i]; i++)
		dm_free(the_args[i]);
}

char **build_args_array_ext(struct ImapSession *self, const char *originalString)
{
	int nargs = 0, inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i;
	char parlist[MAX_LINESIZE];
	char s[MAX_LINESIZE];
	char *tmp, *lastchar;
	int quotedSize, cnt, dataidx;
	static int init_args = 0;
	int result;
	clientinfo_t *ci = self->ci;

	/* Clear the_args the very first time. */
	if (!init_args) {
		memset(the_args, 0, MAX_ARGS * (sizeof(char *)));
		init_args = 1;
	}

	/* this is done for the possible extra lines to be read from the client:
	 * the line is read into currline; s will always point to the line currently
	 * being processed
	 */
	strncpy(s, originalString, MAX_LINESIZE);

	if (!s)
		return NULL;

	/* check for empty string */
	if (!(*s)) {
		the_args[0] = NULL;
		return the_args;
	}

	/* free the last round of arguments */
	free_args();

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	for (i = 0, nargs = 0; s[i] && nargs < MAX_ARGS - 1; i++) {
		/* check quotes */
		if (s[i] == '"' && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				if (! (the_args[nargs] = g_new0(char,(i - quotestart)))) {
					free_args();
					trace(TRACE_ERROR, "%s,%s: out-of-memory error.", __FILE__, __func__);
					return NULL;
				}

				memcpy((void *) the_args[nargs], (void *) &s[quotestart + 1], i - quotestart - 1);
				the_args[nargs][i - quotestart - 1] = '\0';

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
			if (!  (the_args[nargs] = (char *) dm_malloc(sizeof(" ")))) {
				/* out of mem */
				free_args();
				trace(TRACE_ERROR, "%s,%s: out-of-memory error.", __FILE__, __func__);
				return NULL;
			}
			the_args[nargs][0] = s[i];
			the_args[nargs][1] = '\0';

			nargs++;
			continue;
		}

		if (s[i] == ' ')
			continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			quotedSize = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			trace(TRACE_DEBUG, "%s,%s: last char = %c", __FILE__, __func__, *lastchar);
			if ((*lastchar == '+' && *(lastchar + 1) == '}' && 
			     *(lastchar + 2) == '\0') || 
			    (*lastchar == '}' && *(lastchar + 1) == '\0')) {
				/* allocate space for this argument (could be a message when used with APPEND) */
				if (! (the_args[nargs] = (char *) dm_malloc(sizeof(char) * (quotedSize + 1)))) {
					trace(TRACE_ERROR, "%s,%s: out-of-memory allocating [%u] bytes for extra string",
							__FILE__, __func__, quotedSize + 1);
					free_args();
					return NULL;
				}

				ci_write(ci->tx, "+ OK gimme that string\r\n");
				
				alarm(ci->timeout);	/* dont wait forever */
				for (cnt = 0, dataidx = 0; cnt < quotedSize; cnt++) {
					the_args[nargs][dataidx] = fgetc(ci->rx);

					if (the_args[nargs][dataidx] != '\r')
						dataidx++;	/* only store if it is not \r */
				}

				alarm(0);
				the_args[nargs][dataidx] = '\0';	/* terminate string */
				nargs++;

				if (!ci->rx || !ci->tx || ferror(ci->rx) || ferror(ci->tx)) {
					trace(TRACE_ERROR, "%s,%s: timeout occurred", 
							__FILE__, __func__);
					free_args();
					return NULL;
				}

				/* now read the rest of this line */
				result = dbmail_imap_session_readln(self, s);

				if (result < 0 || !ci->rx || !ci->tx || ferror(ci->rx) || ferror(ci->tx)) {
					trace(TRACE_ERROR, "%s,%s: timeout occurred", 
							__FILE__, __func__);
					free_args();
					return NULL;
				}

				/* remove trailing \r\n */
				tmp = &s[strlen(s)];
				tmp--;	/* go before trailing \0; watch this with empty strings! */
				while (tmp >= s && (*tmp == '\r' || *tmp == '\n')) {
					*tmp = '\0';
					tmp--;
				}

				trace(TRACE_DEBUG, "%s,%s: got extra line [%s]", 
						__FILE__, __func__, s);

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

		if (!  (the_args[nargs] = g_new0(char,(i - argstart + 1)))) {
			free_args();

			trace(TRACE_ERROR,
			      "IMAPD: Not enough memory while building up argument array.");
			return NULL;
		}

		memcpy((void *) the_args[nargs], (void *) &s[argstart], i - argstart);
		the_args[nargs][i - argstart] = '\0';

		nargs++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) {
		/* error in parenthesis structure */
		while (--nargs >= 0) {
			dm_free(the_args[nargs]);
			the_args[nargs] = NULL;
		}
		return NULL;
	}

	the_args[nargs] = NULL;	/* terminate */

	/* dump args (debug) */
	for (i = 0; the_args[i]; i++) {
		trace(TRACE_DEBUG, "arg[%d]: '%s'\n", i, the_args[i]);
	}

	return the_args;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR



