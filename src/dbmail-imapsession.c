/*
  
 Copyright (c) 2004-2007 NFG Net Facilities Group BV support@nfg.nl

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
 * dbmail-imapsession.c
 *
 * IMAP-server utility functions implementations
 */

#include "dbmail.h"

#define THIS_MODULE "imapsession"
#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512
#define IDLE_TIMEOUT 30

extern volatile db_param_t * _db_params;
#define DBPFX _db_params->pfx

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

static int _imap_session_fetch_parse_partspec(ImapSession *self);
static int _imap_session_fetch_parse_octet_range(ImapSession *self);

static void _imap_show_body_sections(ImapSession *self);
static void _fetch_envelopes(ImapSession *self);
static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data);
	

/*
 * send_data()
 *
 * sends cnt bytes from a MEM structure to a FILE stream
 * uses a simple buffering system
 */
static void send_data(clientinfo_t *ci, MEM * from, int cnt)
{
	char buf[SEND_BUF_SIZE];

	for (cnt -= SEND_BUF_SIZE; cnt >= 0; cnt -= SEND_BUF_SIZE) {
		mread(buf, SEND_BUF_SIZE, from);
		bufferevent_write(ci->wev, (void *)buf, SEND_BUF_SIZE);
	}

	if (cnt < 0) {
		mread(buf, cnt + SEND_BUF_SIZE, from);
		bufferevent_write(ci->wev, (void *)buf, cnt + SEND_BUF_SIZE);
	}
}

/* 
 * init cache 
 */
static cache_t * init_cache(void)
{
	int serr;
	cache_t *cached_msg = g_new0(cache_t,1);

	cached_msg->num = -1;
	if (! (cached_msg->memdump = mopen())) {
		serr = errno;
		TRACE(TRACE_ERROR,"mopen() failed [%s]", strerror(serr));
		g_free(cached_msg);
		errno = serr;
		return NULL;
	}
	
	if (! (cached_msg->tmpdump = mopen())) {
		serr = errno;
		TRACE(TRACE_ERROR,"mopen() failed [%s]", strerror(serr));
		errno = serr;
		mclose(&cached_msg->memdump);
		g_free(cached_msg);
		return NULL;
	}

	return cached_msg;
}
/*
 * closes the msg cache
 */
static void close_cache(cache_t *cached_msg)
{
	if (cached_msg->dmsg)
		dbmail_message_free(cached_msg->dmsg);

	cached_msg->num = -1;
	cached_msg->msg_parsed = 0;
	mclose(&cached_msg->memdump);
	mclose(&cached_msg->tmpdump);
	g_free(cached_msg);
}




static u64_t get_dumpsize(body_fetch_t *bodyfetch, u64_t tmpdumpsize); 

static void _imap_fetchitem_free(ImapSession * self)
{
	if (self->fi) {
		dbmail_imap_session_bodyfetch_free(self);
		g_free(self->fi);
		self->fi = NULL;
	}
}

static void free_args(ImapSession *self)
{
	int i;
	for (i = 0; i < MAX_ARGS && self->args[i]; i++) {
		if (self->args[i]) g_free(self->args[i]);
		self->args[i] = NULL;
	}
	self->args_idx = 0;
}


/* 
 *
 * initializer and accessors for ImapSession
 *
 */

ImapSession * dbmail_imap_session_new(void)
{
	ImapSession * self;
	cache_t *cached_msg;

	/* init: cache */
	if (! (cached_msg = init_cache()))
		return NULL;

	self = g_new0(ImapSession,1);
	self->cached_msg = cached_msg;
	self->args = g_new0(char *, MAX_ARGS);
	self->buff = g_string_new("");

	dbmail_imap_session_resetFi(self);
	
	return self;
}

ImapSession * dbmail_imap_session_resetFi(ImapSession * self)
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
     
ImapSession * dbmail_imap_session_setTag(ImapSession * self, char * tag)
{
	if (self->tag) {
		g_free(self->tag);
		self->tag = NULL;
	}
	
	self->tag = g_strdup(tag);
	return self;
}

ImapSession * dbmail_imap_session_setCommand(ImapSession * self, char * command)
{
	if (self->command) {
		g_free(self->command);
		self->command = NULL;
	}
	self->command = g_strdup(command);
	return self;
}


static void _mbxinfo_keywords_destroy(u64_t UNUSED *id, MailboxInfo *mb, gpointer UNUSED x)
{
	if (mb->keywords)
		g_list_destroy(mb->keywords);
}

static void _mbxinfo_destroy(ImapSession *self)
{
	g_tree_foreach(self->mbxinfo, (GTraverseFunc)_mbxinfo_keywords_destroy, NULL);
	g_tree_destroy(self->mbxinfo);
	self->mbxinfo = NULL;
}
void dbmail_imap_session_delete(ImapSession * self)
{
	close_cache(self->cached_msg);

	_imap_fetchitem_free(self);
	
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
	if (self->mbxinfo) {
		_mbxinfo_destroy(self);
	}
	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}

	dbmail_imap_session_fetch_free(self);
	dbmail_imap_session_args_free(self, TRUE);
	
	g_string_free(self->buff,TRUE);

	g_free(self);
	self = NULL;
}

void dbmail_imap_session_fetch_free(ImapSession *self) 
{
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

void dbmail_imap_session_args_free(ImapSession *self, gboolean all)
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


static u64_t _imap_cache_set_dump(ImapSession *self, char *buf, int dumptype)
{
	u64_t outcnt = 0;
	char *rfc = get_crlf_encoded(buf);

	switch (dumptype) {
		case IMAP_CACHE_MEMDUMP:
			mrewind(self->cached_msg->memdump);
			outcnt = mwrite(rfc, strlen(rfc), self->cached_msg->memdump);
			mrewind(self->cached_msg->memdump);
		break;
		case IMAP_CACHE_TMPDUMP:
			mrewind(self->cached_msg->tmpdump);
			outcnt = mwrite(rfc, strlen(rfc), self->cached_msg->tmpdump);
			mrewind(self->cached_msg->tmpdump);
		break;
	}
	g_free(rfc);
	
	return outcnt;
}

static void imap_cache_send_tmpdump(ImapSession *self, body_fetch_t *bodyfetch, u64_t size)
{
	u64_t cnt = 0;

	if (bodyfetch->octetcnt > 0) {
		cnt = get_dumpsize(bodyfetch, size);
		dbmail_imap_session_buff_append(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
		mseek(self->cached_msg->tmpdump, bodyfetch->octetstart, SEEK_SET);
	} else {
		cnt = size;
		dbmail_imap_session_buff_append(self, "] {%llu}\r\n", size);
		mrewind(self->cached_msg->tmpdump);
	}
	dbmail_imap_session_buff_flush(self);
	send_data(self->ci, (void *)self->cached_msg->tmpdump, cnt);
}

static u64_t _imap_cache_update(ImapSession *self, message_filter_t filter)
{
	u64_t tmpcnt = 0, outcnt = 0;
	char *buf = NULL;

	TRACE(TRACE_DEBUG,"cache message [%llu] filter [%d]", self->msg_idnr, filter);

	if (self->cached_msg->file_dumped == 1 && self->cached_msg->num == self->msg_idnr) {
		outcnt = self->cached_msg->dumpsize;
	} else {
		if (self->cached_msg->dmsg != NULL && GMIME_IS_MESSAGE(self->cached_msg->dmsg->content)) {
			dbmail_message_free(self->cached_msg->dmsg);
			self->cached_msg->dmsg = NULL;
		}

		self->cached_msg->dmsg = db_init_fetch(self->msg_idnr, DBMAIL_MESSAGE_FILTER_FULL);
		buf = dbmail_message_to_string(self->cached_msg->dmsg);
	
		outcnt = _imap_cache_set_dump(self,buf,IMAP_CACHE_MEMDUMP);
		tmpcnt = _imap_cache_set_dump(self,buf,IMAP_CACHE_TMPDUMP);
		
		assert(tmpcnt==outcnt);
		
		self->cached_msg->dumpsize = outcnt;

		if (self->cached_msg->num != self->msg_idnr) 
			self->cached_msg->num = self->msg_idnr;
		
		self->cached_msg->file_dumped = 1;

		g_free(buf);
	}
	
	switch (filter) {
		/* for these two update the temp MEM buffer */	
		case DBMAIL_MESSAGE_FILTER_HEAD:
			buf = dbmail_message_hdrs_to_string(self->cached_msg->dmsg);
			outcnt = _imap_cache_set_dump(self,buf,IMAP_CACHE_TMPDUMP);
			g_free(buf);
		break;

		case DBMAIL_MESSAGE_FILTER_BODY:
			buf = dbmail_message_body_to_string(self->cached_msg->dmsg);
			outcnt = _imap_cache_set_dump(self,buf,IMAP_CACHE_TMPDUMP);	
			g_free(buf);
		break;
		case DBMAIL_MESSAGE_FILTER_FULL:
			/* done */
		break;

	}
	
	mrewind(self->cached_msg->memdump);
	mrewind(self->cached_msg->tmpdump);
	
	TRACE(TRACE_DEBUG,"cache size [%llu]", outcnt);	
	return outcnt;
}

static int _imap_session_fetch_parse_partspec(ImapSession *self)
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

static int _imap_session_fetch_parse_octet_range(ImapSession *self) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[self->args_idx];
	
	if (! token)
		return 0;
//FIXME wrong return value?	return self->args_idx;
	
	TRACE(TRACE_DEBUG,"[%p] parse token [%s]", self, token);

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
int dbmail_imap_session_fetch_parse_args(ImapSession * self)
{
	int invalidargs, ispeek = 0, i;
	
	invalidargs = 0;

        /* dump args (debug) */
        for (i = self->args_idx; self->args[i]; i++) {
                TRACE(TRACE_DEBUG, "[%p] arg[%d]: '%s'\n", self, i, self->args[i]);
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

	TRACE(TRACE_DEBUG,"[%p] parse args[%llu] = [%s]", self, self->args_idx, token);

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

			TRACE(TRACE_DEBUG,"[%p] token [%s], nexttoken [%s]", self, token, nexttoken);

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
				TRACE(TRACE_DEBUG,"[%p] fetch_parse_partspec return with error", self);
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
		TRACE(TRACE_INFO,"[%p] error [%s]", self, token);
		return -2;	/* DONE */
	}

	return 1; //theres more...
}

#define SEND_SPACE if (self->fi->isfirstfetchout) \
				self->fi->isfirstfetchout = 0; \
			else \
				dbmail_imap_session_buff_append(self, " ")

static gboolean _get_mailbox(u64_t UNUSED *id, MailboxInfo *mb, ImapSession *self)
{
	int result;
	result = acl_has_right(mb, self->userid, ACL_RIGHT_READ);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		self->error = -1;
		return TRUE;
	}
	if (result == 0) {
		self->error = 1;
		return TRUE;
	}

	if (db_getmailbox(mb, self->userid) != DM_SUCCESS)
		return TRUE;

	return FALSE;
}

void dbmail_imap_session_get_mbxinfo(ImapSession *self)
{
	C c; R r; int t = FALSE;
	GTree *mbxinfo = NULL;
	u64_t *id;
	MailboxInfo *mb;
	
	if (self->mbxinfo)
		_mbxinfo_destroy(self);

	mbxinfo = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT mailbox_id FROM %ssubscription WHERE user_id=%llu",DBPFX, self->userid);
		while (db_result_next(r)) {
			id = g_new0(u64_t,1);
			mb = g_new0(MailboxInfo,1);

			*id = db_result_get_u64(r, 0);
			mb->uid = *id;

			g_tree_insert(mbxinfo, id, mb);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
		t = DM_EQUERY;
	END_TRY;

	if (t == DM_EQUERY) return;

	self->error = 0;
	g_tree_foreach(mbxinfo, (GTraverseFunc)_get_mailbox, self);

	if (! self->error) {
		self->mbxinfo = mbxinfo;
		return;
	}

	if (self->error == DM_EQUERY)
		TRACE(TRACE_ERROR, "[%p] database error retrieving mbxinfo", self);
	if (self->error == DM_EGENERAL)
		TRACE(TRACE_ERROR, "[%p] failure retrieving mbxinfo for unreadable mailbox", self);

	g_tree_destroy(mbxinfo);

	return;
}


static int _fetch_get_items(ImapSession *self, u64_t *uid)
{
	int result;
	u64_t actual_cnt, tmpdumpsize;
	gchar *s = NULL;
	u64_t *id = uid;

	MessageInfo *msginfo = g_tree_lookup(self->mailbox->msginfo, uid);

	if (! msginfo) {
		TRACE(TRACE_INFO, "[%p] failed to lookup msginfo struct for message [%llu]", self, *uid);
		return 0;
	}
	
	id = g_tree_lookup(self->mailbox->ids,uid);

	g_return_val_if_fail(id,-1);
	
	dbmail_imap_session_buff_append(self, "* %llu FETCH (", *id);

	self->msg_idnr = *uid;
	self->fi->isfirstfetchout = 1;
	
        /* queue this message's recent_flag for removal */
        if (self->mailbox->info->permission == IMAPPERM_READWRITE)
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
		dbmail_imap_session_buff_append(self, "UID %llu", msginfo->id);
	}

	if (self->fi->getMIME_IMB) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((self->cached_msg->dmsg)->content), 1))==NULL) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		dbmail_imap_session_buff_append(self, "BODYSTRUCTURE %s", s);
		g_free(s);
	}

	if (self->fi->getMIME_IMB_noextension) {
		
		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if ((s = imap_get_structure(GMIME_MESSAGE((self->cached_msg->dmsg)->content), 0))==NULL) {
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
		dbmail_imap_session_buff_append(self, "RFC822 {%llu}\r\n", self->cached_msg->dumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci, (void *)self->cached_msg->memdump, self->cached_msg->dumpsize);

		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {

		SEND_SPACE;
		
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			dbmail_imap_session_buff_append(self, "BODY[] {%llu}\r\n", self->cached_msg->dumpsize);
			dbmail_imap_session_buff_flush(self);
			send_data(self->ci, (void *)self->cached_msg->memdump, self->cached_msg->dumpsize);
		} else {
			mseek(self->cached_msg->memdump, dbmail_imap_session_bodyfetch_get_last_octetstart(self), SEEK_SET);
			actual_cnt = (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) >
			     (((long long)self->cached_msg->dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)))
			    ? (((long long)self->cached_msg->dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)) 
			    : dbmail_imap_session_bodyfetch_get_last_octetcnt(self);

			dbmail_imap_session_buff_append(self, "BODY[]<%llu> {%llu}\r\n", 
					dbmail_imap_session_bodyfetch_get_last_octetstart(self), actual_cnt);
			dbmail_imap_session_buff_flush(self);
			send_data(self->ci, (void *)self->cached_msg->memdump, actual_cnt);
		}

		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;

	}

	if (self->fi->getRFC822Header) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_HEAD);
		dbmail_imap_session_buff_append(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci, (void *)self->cached_msg->tmpdump, tmpdumpsize);
	}

	if (self->fi->getRFC822Text) {

		SEND_SPACE;

		tmpdumpsize = _imap_cache_update(self,DBMAIL_MESSAGE_FILTER_BODY);
		dbmail_imap_session_buff_append(self, "RFC822.TEXT {%llu}\r\n", tmpdumpsize);
		dbmail_imap_session_buff_flush(self);
		send_data(self->ci, (void *)self->cached_msg->tmpdump, tmpdumpsize);

		self->fi->setseen = 1;
	}

	_imap_show_body_sections(self);

	/* set \Seen flag if necessary; note the absence of an error-check 
	 * for db_get_msgflag()!
	 */
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	if (self->fi->setseen && db_get_msgflag("seen", self->msg_idnr, self->mailbox->info->uid) != 1) {
		/* only if the user has an ACL which grants
		   him rights to set the flag should the
		   flag be set! */
		result = acl_has_right(self->mailbox->info, self->userid, ACL_RIGHT_SEEN);
		if (result == -1) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_printf(self, "\r\n *BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			result = db_set_msgflag(self->msg_idnr, self->mailbox->info->uid, setSeenSet, NULL, IMAPFA_ADD, msginfo);
			if (result == -1) {
				dbmail_imap_session_buff_clear(self);
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
		}

		self->fi->getFlags = 1;
		dbmail_imap_session_buff_append(self, " ");
	}

	dbmail_imap_session_buff_append(self, ")\r\n");

	return 0;
}


static gboolean _do_fetch(u64_t *uid, gpointer UNUSED value, ImapSession *self)
{
	/* go fetch the items */
	if (_fetch_get_items(self,uid) < 0) {
		TRACE(TRACE_ERROR, "[%p] _fetch_get_items returned with error", self);
		dbmail_imap_session_buff_clear(self);
		self->error = TRUE;
		return TRUE;
	}

	dbmail_imap_session_buff_flush(self);

	return FALSE;
}

int dbmail_imap_session_fetch_get_items(ImapSession *self)
{
	if (! self->ids)
		TRACE(TRACE_INFO, "[%p] self->ids is NULL", self);
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
static void _imap_show_body_sections(ImapSession *self) 
{
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)_imap_show_body_section, (gpointer)self);
	dbmail_imap_session_bodyfetch_rewind(self);
}

#define QUERY_BATCHSIZE 500

/* get envelopes */
static void _fetch_envelopes(ImapSession *self)
{
	C c; R r; int t = FALSE;
	GString *q;
	gchar *env, *s;
	u64_t *mid;
	u64_t id;
	char range[DEF_FRAGSIZE];
	GList *last;
	memset(range,0,DEF_FRAGSIZE);

	if (! self->envelopes) {
		self->envelopes = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		self->lo = 0;
		self->hi = 0;
	}

	if ((s = g_tree_lookup(self->envelopes, &(self->msg_idnr))) != NULL) {
		dbmail_imap_session_buff_append(self, "ENVELOPE %s", s?s:"");
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] lo: %llu", self, self->lo);

	if (! (last = g_list_nth(self->ids_list, self->lo+(u64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(u64_t *)last->data;

	if (self->msg_idnr == self->hi)
		snprintf(range,DEF_FRAGSIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,DEF_FRAGSIZE,"BETWEEN %llu AND %llu", self->msg_idnr, self->hi);

        q = g_string_new("");
	g_string_printf(q,"SELECT message_idnr,envelope "
			"FROM %senvelope e "
			"JOIN %smessages m ON m.physmessage_id=e.physmessage_id "
			"WHERE m.mailbox_idnr = %llu "
			"AND message_idnr %s",
			DBPFX, DBPFX,  
			self->mailbox->id, range);
	c = db_con_get();
	TRY
		r = db_query(c, q->str);
		while (db_result_next(r)) {
			
			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			mid = g_new0(u64_t,1);
			*mid = id;
			
			env = g_strdup((char *)db_result_get(r, 1));
			
			g_tree_insert(self->envelopes,mid,env);

		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
		g_string_free(q,TRUE);
	END_TRY;

	if (t == DM_EQUERY) return;

	self->lo += QUERY_BATCHSIZE;

	s = g_tree_lookup(self->envelopes, &(self->msg_idnr));
	dbmail_imap_session_buff_append(self, "ENVELOPE %s", s?s:"");
}

void _send_headers(ImapSession *self, const body_fetch_t *bodyfetch, gboolean not)
{
	long long cnt = 0;
	gchar *tmp;
	gchar *s;
	GString *ts;

	dbmail_imap_session_buff_append(self,"HEADER.FIELDS%s %s] ", not ? ".NOT" : "", bodyfetch->hdrplist);

	if (! (s = g_tree_lookup(bodyfetch->headers, &(self->msg_idnr)))) {
		dbmail_imap_session_buff_append(self, "{2}\r\n\r\n");
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] [%s] [%s]", self, bodyfetch->hdrplist, s);

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
static void _fetch_headers(ImapSession *self, body_fetch_t *bodyfetch, gboolean not)
{
	C c; R r; int t = FALSE;
	GString *q = g_string_new("");
	gchar *fld, *val, *old, *new = NULL;
	u64_t *mid;
	u64_t id;
	GList *last;
	int k;
	char range[DEF_FRAGSIZE];
	memset(range,0,DEF_FRAGSIZE);

	if (! bodyfetch->headers) {
		TRACE(TRACE_DEBUG, "[%p] init bodyfetch->headers", self);
		bodyfetch->headers = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		self->ceiling = 0;
		self->hi = 0;
		self->lo = 0;
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

	TRACE(TRACE_DEBUG,"[%p] for %llu [%s]", self, self->msg_idnr, bodyfetch->hdrplist);

	// did we prefetch this message already?
	if (self->msg_idnr <= self->ceiling) {
		_send_headers(self, bodyfetch, not);
		return;
	}

	// let's fetch the required message and prefetch a batch if needed.
	
	if (! (last = g_list_nth(self->ids_list, self->lo+(u64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(u64_t *)last->data;

	if (self->msg_idnr == self->hi)
		snprintf(range,DEF_FRAGSIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,DEF_FRAGSIZE,"BETWEEN %llu AND %llu", self->msg_idnr, self->hi);

	TRACE(TRACE_DEBUG,"[%p] prefetch %llu:%llu ceiling %llu [%s]", self, self->msg_idnr, self->hi, self->ceiling, bodyfetch->hdrplist);
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

	c = db_con_get();	
	TRY
		r = db_query(c, q->str);
		while (db_result_next(r)) {
			
			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			mid = g_new0(u64_t,1);
			*mid = id;
			
			fld = (char *)db_result_get(r, 1);
			val = dbmail_iconv_db_to_utf7((char *)db_result_get(r, 2));
			if (! val) {
				TRACE(TRACE_DEBUG, "[%p] [%llu] no headervalue [%s]", self, id, fld);
			} else {
				old = g_tree_lookup(bodyfetch->headers, (gconstpointer)mid);
				new = g_strdup_printf("%s%s: %s\n", old?old:"", fld, val);
				g_free(val);
				g_tree_insert(bodyfetch->headers,mid,new);
			}
			
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
		g_string_free(q,TRUE);
	END_TRY;

	if (t == DM_EQUERY) return;
	
	self->lo += QUERY_BATCHSIZE;
	self->ceiling = self->hi;

	_send_headers(self, bodyfetch, not);

	return;
}

static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data) 
{
	u64_t tmpdumpsize;
	GMimeObject *part = NULL;
	char *tmp;
	gboolean condition = FALSE;
	ImapSession *self = (ImapSession *)data;
	
	if (bodyfetch->itemtype < 0)
		return 0;
	
	TRACE(TRACE_DEBUG,"[%p] itemtype [%d] partspec [%s]", self, bodyfetch->itemtype, bodyfetch->partspec);
	
	if (self->fi->msgparse_needed) {
		_imap_cache_update(self, DBMAIL_MESSAGE_FILTER_FULL);
		if (bodyfetch->partspec[0]) {
			if (bodyfetch->partspec[0] == '0') {
				dbmail_imap_session_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
				TRACE(TRACE_ERROR, "[%p] PROTOCOL ERROR", self);
				return 1;
			}
			part = imap_get_partspec(GMIME_OBJECT((self->cached_msg->dmsg)->content), bodyfetch->partspec);
		} else {
			part = GMIME_OBJECT((self->cached_msg->dmsg)->content);
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
			tmpdumpsize = _imap_cache_set_dump(self,tmp,IMAP_CACHE_TMPDUMP);

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
			tmpdumpsize = _imap_cache_set_dump(self,tmp,IMAP_CACHE_TMPDUMP);
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
			tmpdumpsize = _imap_cache_set_dump(self,tmp,IMAP_CACHE_TMPDUMP);
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
		dbmail_imap_session_buff_clear(self);
		dbmail_imap_session_printf(self, "\r\n* BYE internal server error\r\n");
		return -1;
	}

	dbmail_imap_session_buff_flush(self);
	
	return 0;
}



int client_is_authenticated(ImapSession * self)
{
	return (self->state != IMAPCS_NON_AUTHENTICATED);
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
int check_state_and_args(ImapSession * self, const char *command, int minargs, int maxargs, int state)
{
	int i;

	/* check state */
	if (state != -1) {
		if (self->state != state) {
			if (!  (state == IMAPCS_AUTHENTICATED && self->state == IMAPCS_SELECTED)) {
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

void dbmail_imap_session_buff_clear(ImapSession *self)
{
	g_string_printf(self->buff, "%s", "");
}	

void dbmail_imap_session_buff_append(ImapSession *self, char *message, ...)
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

void dbmail_imap_session_buff_flush(ImapSession *self)
{
	dbmail_imap_session_printf(self, "%s", self->buff->str);
	dbmail_imap_session_buff_clear(self);
}

/* Returns -1 on error, -2 on serious error. */
int dbmail_imap_session_printf(ImapSession * self, char * message, ...)
{
        va_list ap;
        int len;
        //FILE * fd;
        gchar *ln;

	assert(message);
	assert(self->ci->wev);

	va_start(ap, message);
	ln = g_strdup_vprintf(message,ap);
	va_end(ap);

        if (! ln) return -1;
	
	len = strlen(ln);
	bufferevent_write(self->ci->wev, (void *)ln, len);

	TRACE(TRACE_INFO,"[%p] RESPONSE: [%s]", self, ln);
	g_free(ln);

        return len;
}

int dbmail_imap_session_readln(ImapSession *self, char * buffer)
{
	return (int) ci_readln(self->ci, buffer);
}
	
int dbmail_imap_session_handle_auth(ImapSession * self, char * username, char * password)
{

	timestring_t timestring;
	create_current_timestring(&timestring);
	
	u64_t userid = 0;
	
	int valid = auth_validate(self->ci, username, password, &userid);
	
	TRACE(TRACE_DEBUG, "[%p] trying to validate user [%s], pass [%s]", 
			self, username, (password ? "XXXX" : "(null)") );
	
	if (valid == -1) {
		/* a db-error occurred */
		dbmail_imap_session_printf(self, "* BYE internal db error validating user\r\n");
		TRACE(TRACE_ERROR, "[%p] db-validate error while validating user %s (pass %s).",
			       	self, username, password ? "XXXX" : "(null)");
		return -1;
	}

	if (valid == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		TRACE(TRACE_MESSAGE, "[%p] user (name %s) coming from [%s] login rejected", 
			self, username, self->ci->ip_src);
		dbmail_imap_session_printf(self, "%s NO login rejected\r\n", self->tag);

		return 1;
	}

	/* login ok */
	TRACE(TRACE_MESSAGE, "[%p] user (name %s) coming from [%s] login accepted", 
		self, username, self->ci->ip_src);

	self->userid = userid;

	dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);

	return 0;

}

int dbmail_imap_session_prompt(ImapSession * self, char * prompt)
{
	char *prompt64, *promptcat;
	
	g_return_val_if_fail(prompt != NULL, -1);
	
	/* base64 encoding increases string length by about 40%. */
	promptcat = g_strdup_printf("%s\r\n", prompt);
	prompt64 = (char *)g_base64_encode((const guchar *)promptcat, strlen(promptcat));
	dbmail_imap_session_printf(self, "+ %s\r\n", prompt64);
	
	g_free(prompt64);
	g_free(promptcat);
	
	return 0;
}

u64_t dbmail_imap_session_mailbox_get_idnr(ImapSession * self, const char * mailbox)
{
	char * mbox = g_strdup(mailbox);
	u64_t uid;
	int i;
	
	/* remove trailing '/' if present */
	while (strlen(mbox) > 0 && mbox[strlen(mbox) - 1] == '/')
		mbox[strlen(mbox) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; mbox[i] && mbox[i] == '/'; i++);
	memmove(&mbox[0], &mbox[i], (strlen(mbox) - i) * sizeof(char));

	db_findmailbox(mbox, self->userid, &uid);
	
	g_free(mbox);

	return uid;
}

int dbmail_imap_session_mailbox_check_acl(ImapSession * self, u64_t idnr,  ACLRight_t acl)
{
	int access;
	MailboxInfo *mailbox;

	mailbox = dbmail_imap_session_mbxinfo_lookup(self, idnr);

	access = acl_has_right(mailbox, self->userid, acl);
	
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

int dbmail_imap_session_mailbox_get_selectable(ImapSession * self, u64_t idnr)
{
	/* check if mailbox is selectable */
	int selectable;
	selectable = db_isselectable(idnr);
	if (selectable == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (selectable == FALSE) {
		dbmail_imap_session_printf(self, "%s NO specified mailbox is not selectable\r\n", self->tag);
		return 1;
	}
	return 0;
}

static void notify_expunge(ImapSession *self, u64_t *uid)
{
	u64_t * msn;

	if (! (msn = g_tree_lookup(self->mailbox->ids, uid))) {
		TRACE(TRACE_DEBUG,"[%p] can't find uid [%llu]", self, *uid);
		return;
	}

	switch (self->command_type) {
		case IMAP_COMM_NOOP:
		case IMAP_COMM_IDLE:
			dbmail_imap_session_printf(self, "* %llu EXPUNGE\r\n", *msn);
			dbmail_mailbox_remove_uid(self->mailbox, *uid);
		default:
		break;
	}
}

static void notify_fetch(ImapSession *self, DbmailMailbox *newbox, u64_t *uid)
{
	int i;
	u64_t *msn;
	char *s;
	MessageInfo *old, *new;

	if (! (new = g_tree_lookup(newbox->msginfo, uid)))
		return;

	if (! (msn = g_tree_lookup(newbox->ids, uid))) {
		TRACE(TRACE_DEBUG,"[%p] can't find uid [%llu]", self, *uid);
		return;
	}

	old = g_tree_lookup(self->mailbox->msginfo, uid);

	// FETCH
	for (i=0; i< IMAP_NFLAGS; i++) {
		if (old->flags[i] != new->flags[i]) {
			s = imap_flags_as_string(new);
			dbmail_imap_session_printf(self,"* %llu FETCH (FLAGS %s)\r\n", *msn, s);
			g_free(s);
			break;
		}
	}
}

static void mailbox_notify_update(ImapSession *self, DbmailMailbox *new)
{
	u64_t *uid;
	DbmailMailbox *old;
	GList *ids = g_tree_keys(self->mailbox->ids);

	ids = g_list_reverse(ids);

	while (ids) {
		uid = (u64_t *)ids->data;
		if (! g_tree_lookup(new->ids, uid))
			notify_expunge(self, uid);

		notify_fetch(self, new, uid);

		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}

	// switch active mailbox view
	old = self->mailbox;
	self->mailbox = new;
	dbmail_mailbox_free(old);
}

static gboolean imap_mbxinfo_notify(u64_t UNUSED *id, MailboxInfo *mb, ImapSession *self)
{
	time_t oldmtime = mb->mtime;
	unsigned oldexists = mb->exists;
	unsigned oldrecent = mb->recent;
	unsigned oldunseen = mb->unseen;
	u64_t olduidnext = mb->msguidnext;
	int changed = 0;

	GList *plst = NULL;
	gchar *astring, *pstring;

	if (self->mailbox->info->uid == mb->uid)
		return FALSE;

	if (db_getmailbox(mb, self->userid) != DM_SUCCESS) {
		self->error = 1;
		return TRUE;
	}

	if (oldmtime == mb->mtime)
		return FALSE;

	TRACE(TRACE_DEBUG,"[%p] oldmtime[%d] != mtime[%d]", self, (int)oldmtime, (int)mb->mtime);

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

static int dbmail_imap_session_mbxinfo_notify(ImapSession *self)
{
	if (! imap_feature_idle_status)
		return DM_SUCCESS;

	if (! self->mbxinfo)
		dbmail_imap_session_get_mbxinfo(self);

	self->error = 0;
	if ( (! self->mbxinfo) || (g_tree_nnodes(self->mbxinfo) == 0) )
		return self->error;
		
	g_tree_foreach(self->mbxinfo, (GTraverseFunc)imap_mbxinfo_notify, self);
	return self->error;
}

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update)
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
	gboolean unhandled=FALSE;
	time_t oldmtime;
	unsigned oldexists, oldrecent, olduidnext;
        DbmailMailbox *mailbox = NULL;
	MailboxInfo *info;
	gboolean showexists = FALSE, showrecent = FALSE;
	if (self->state != IMAPCS_SELECTED) {
		TRACE(TRACE_DEBUG,"[%p] do nothing: state [%d]", self, self->state);
		return 0;
	}

	info = self->mailbox->info;

	oldmtime = info->mtime;
	oldexists = info->exists;
	oldrecent = info->recent;
	olduidnext = info->msguidnext;

	if (! self->mbxinfo)
		dbmail_imap_session_get_mbxinfo(self);

	if (update) {
                // re-read flags and counters
		if ((res = db_getmailbox(info, self->userid)) != DM_SUCCESS)
			return res;

		if (oldmtime != info->mtime) {
			// rebuild uid/msn trees
			// ATTN: new messages shouldn't be visible in any way to a 
			// client session until it has been announced with EXISTS
			mailbox = dbmail_mailbox_new(self->mailbox->id);
			mailbox->info = info;
			mailbox->info->exists = g_tree_nnodes(mailbox->ids);

			// show updates conditionally
			//
			// EXISTS response may never decrease
			if ((info->msguidnext > olduidnext) && (info->exists > oldexists))
				showexists = TRUE;

			// RECENT response only when changed
			if (info->recent != oldrecent)
				showrecent = TRUE;
		}
	}


	// command specific over-rides
	switch (self->command_type) {
		case IMAP_COMM_SELECT:
		case IMAP_COMM_EXAMINE:
			// always show them
			showexists = showrecent = TRUE;
		break;

		case IMAP_COMM_IDLE:
#if 0 // experimental: send '* STATUS' updates for all subscribed mailboxes if they have changed
			dbmail_imap_session_mbxinfo_notify(self);
#endif
		break;

		case IMAP_COMM_APPEND:

			showrecent = FALSE;
			showexists = FALSE;
/* 
 * the rfc says we SHOULD send new message status notifications after APPEND
 * but it doesn't work like it should :-( So lets fall back to letting the
 * client issue a NOOP or CHECK instead
 *
 */
		//	showexists = TRUE;
		break;

		case IMAP_COMM_NOOP:
		case IMAP_COMM_CHECK:
			// ok show them if needed
		break;

		break;
		default:
			unhandled=TRUE;
		break;
	}

	if (showexists) // never decrease without first sending expunge !!
		dbmail_imap_session_printf(self, "* %u EXISTS\r\n", self->mailbox->info->exists);
	if (showrecent)
		dbmail_imap_session_printf(self, "* %u RECENT\r\n", self->mailbox->info->recent);

	if (update) {
		if (unhandled)
			TRACE(TRACE_ERROR, "[%p] EXISTS/RECENT changed but client "
				"is not notified", self);
		if (mailbox)
			mailbox_notify_update(self, mailbox);
	}
	return 0;
}
int dbmail_imap_session_mailbox_show_info(ImapSession * self) 
{
	GString *keywords;
	GString *string = g_string_new("\\Seen \\Answered \\Deleted \\Flagged \\Draft");

	dbmail_imap_session_mailbox_status(self, TRUE);

	if (self->mailbox->info->keywords) {
		keywords = g_list_join(self->mailbox->info->keywords," ");
		g_string_append_printf(string, " %s", keywords->str);
		g_string_free(keywords,TRUE);
	}

	/* flags */
	dbmail_imap_session_printf(self, "* FLAGS (%s)\r\n", string->str);

	/* permanent flags */
	dbmail_imap_session_printf(self, "* OK [PERMANENTFLAGS (%s \\*)]\r\n", string->str);

	/* UIDNEXT */
	dbmail_imap_session_printf(self, "* OK [UIDNEXT %llu] Predicted next UID\r\n",
		self->mailbox->info->msguidnext);
	
	/* UID */
	dbmail_imap_session_printf(self, "* OK [UIDVALIDITY %llu] UID value\r\n",
		self->mailbox->info->uid);

	g_string_free(string,TRUE);
	return 0;
}
	
MailboxInfo * dbmail_imap_session_mbxinfo_lookup(ImapSession *self, u64_t mailbox_idnr)
{
	MailboxInfo *mb = NULL;
	u64_t *id;

	if (! self->mbxinfo) dbmail_imap_session_get_mbxinfo(self);

	/* fetch the cached mailbox metadata */
	if ((mb = (MailboxInfo *)g_tree_lookup(self->mbxinfo, &mailbox_idnr)) == NULL) {
		mb = g_new0(MailboxInfo,1);
		id = g_new0(u64_t,1);

		*id = mailbox_idnr;
		mb->uid = mailbox_idnr;

		g_tree_insert(self->mbxinfo, id, mb);

	}
	_get_mailbox(0,mb,self);

	return mb;
}

int dbmail_imap_session_mailbox_open(ImapSession * self, const char * mailbox)
{
	int result;
	u64_t mailbox_idnr;
	
	/* get the mailbox_idnr */
	mailbox_idnr = dbmail_imap_session_mailbox_get_idnr(self, mailbox);
	
	/* create missing INBOX for this authenticated user */
	if ((! mailbox_idnr ) && (strcasecmp(mailbox, "INBOX")==0)) {
		TRACE(TRACE_INFO, "[%p] Auto-creating INBOX for user id [%llu]", self, self->userid);
		result = db_createmailbox("INBOX", self->userid, &mailbox_idnr);
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

	/* new mailbox structure */
	self->mailbox = dbmail_mailbox_new(mailbox_idnr);

	/* fetch mailbox metadata */
	self->mailbox->info = dbmail_imap_session_mbxinfo_lookup(self, mailbox_idnr);

	/* keep these in sync */
	self->mailbox->info->exists = g_tree_nnodes(self->mailbox->ids);

	return 0;
}

void imap_cb_idle_time (void *arg)
{
	ImapSession *self = (ImapSession *)arg;

	TRACE(TRACE_DEBUG,"[%p]", self);

	if (! (self->loop++ % 10))
		dbmail_imap_session_printf(self, "* OK\r\n");
	dbmail_imap_session_mailbox_status(self,TRUE);
	dbmail_imap_session_set_callbacks(self, NULL, NULL, 0);
}

#define IDLE_BUFFER 8
void imap_cb_idle_read (void *arg)
{
	char buffer[IDLE_BUFFER];
	ImapSession *self = (ImapSession *)arg;

	TRACE(TRACE_DEBUG,"[%p] [%s]", self, self->tag);

	memset(buffer,0,sizeof(buffer));
	if (! (ci_read(self->ci, buffer, IDLE_BUFFER)))
		return;

	if (strlen(buffer) > 4 && strncasecmp(buffer,"DONE",4)==0) {
		dbmail_imap_session_printf(self, "%s OK IDLE terminated\r\n", self->tag);
		dbmail_imap_session_reset_callbacks(self);
		self->command_state = TRUE; // done
	} else if (strlen(buffer) > 0) {
		dbmail_imap_session_printf(self,"%s BAD Expecting DONE\r\n", self->tag);
		self->state = IMAPCS_ERROR;
	}
}


int dbmail_imap_session_idle(ImapSession *self)
{
	int idle_timeout = IDLE_TIMEOUT;
	field_t val;

	bufferevent_disable(self->ci->rev, EV_READ);

	GETCONFIGVALUE("idle_timeout", "IMAP", val);
	if ( strlen(val) && (idle_timeout = atoi(val)) <= 0 ) {
		TRACE(TRACE_ERROR, "[%p] illegal value for idle_timeout [%s]", self, val);
		idle_timeout = IDLE_TIMEOUT;	
	}
#if 0
	GETCONFIGVALUE("idle_status", "IMAP", val);
	if (strlen(val) && (g_strncasecmp(val,"yes",3)==0))
		imap_feature_idle_status = TRUE;
	else
		imap_feature_idle_status = FALSE;
#endif

	dbmail_imap_session_set_callbacks(self, imap_cb_idle_read, imap_cb_idle_time, idle_timeout);
	dbmail_imap_session_mailbox_status(self,TRUE);
	TRACE(TRACE_DEBUG,"[%p] start IDLE [%s]", self, self->tag);
	dbmail_imap_session_printf(self, "+ idling\r\n");

	return 0;
}

int dbmail_imap_session_mailbox_close(ImapSession *self)
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

static int imap_session_update_recent(ImapSession *self) 
{
	C c; int t = FALSE;
	GList *slices, *topslices, *recent;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);
	MessageInfo *msginfo = NULL;
	gchar *uid = NULL;
	u64_t id = 0;

	recent = self->recent;

	if (recent == NULL)
		return t;

	topslices = g_list_slices(recent,100);
	slices = g_list_first(topslices);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		while (slices) {
			snprintf(query, DEF_QUERYSIZE, "UPDATE %smessages SET recent_flag = 0 "
					"WHERE message_idnr IN (%s) AND recent_flag = 1", 
					DBPFX, (gchar *)slices->data);
			db_exec(c, query);

			if (! g_list_next(slices)) break;
			slices = g_list_next(slices);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

        g_list_destroy(topslices);

	if (t == DM_EQUERY) return t;

	// update cached values
	recent = g_list_first(recent);
	while (recent) {
		// self->recent is a list of chars so we need to convert them
		// back to u64_t
		uid = (gchar *)recent->data;
		id = strtoull(uid, NULL, 10);
		assert(id);
		if ( (msginfo = g_tree_lookup(self->mailbox->msginfo, &id)) != NULL) {
			msginfo->flags[IMAP_FLAG_RECENT] = 0;
			if ( (self->mailbox->info) && (self->mailbox->info->uid == msginfo->mailbox_id) )
				self->mailbox->info->recent--;
		} else {
			TRACE(TRACE_WARNING,"[%p] can't find msginfo for [%llu]", self, id);
		}
		if (! g_list_next(recent))
			break;
		recent = g_list_next(recent);
	}

	if ( (self->mailbox->info) && (self->mailbox->info->uid) )
		db_mailbox_mtime_update(self->mailbox->info->uid);

	return 0;
}

int dbmail_imap_session_mailbox_update_recent(ImapSession *self) 
{
	imap_session_update_recent(self);
	g_list_destroy(self->recent);
	self->recent = NULL;

	return 0;
}

int dbmail_imap_session_set_state(ImapSession *self, int state)
{
	switch (state) {
		case IMAPCS_AUTHENTICATED:
			//memset(&self->mbx, 0, sizeof(self->mbx));
			// change from login_timeout to main timeout
			assert(self->ci);
			self->timeout = self->ci->timeout; 
			if (self->ci->rev)
				bufferevent_settimeout(self->ci->rev, self->timeout, 0);
		break;
		default:
		break;
	}

	self->state = state;

	TRACE(TRACE_DEBUG,"[%p] state [%d]", self, self->state);
	return 0;
}

static gboolean _do_expunge(u64_t *id, ImapSession *self)
{
	MessageInfo *msginfo = g_tree_lookup(self->mailbox->msginfo, id);
	u64_t *msn = g_tree_lookup(self->mailbox->ids, id);

	if (! msn) {
		TRACE(TRACE_WARNING,"can't find msn for [%llu]", *id);
		return FALSE;
	}

	if (! msginfo->flags[IMAP_FLAG_DELETED])
		return FALSE;

	if (db_msg_expunge(*id) == DM_EQUERY)
		return TRUE;

	if (self->command_type == IMAP_COMM_EXPUNGE) {
		dbmail_imap_session_printf(self, "* %llu EXPUNGE\r\n", *msn);
		dbmail_mailbox_remove_uid(self->mailbox, *id);
	}
		
	return FALSE;
}

int dbmail_imap_session_mailbox_expunge(ImapSession *self)
{
	u64_t mailbox_size;
	int i;
	GList *ids;

	if (! (i = g_tree_nnodes(self->mailbox->msginfo)))
		return DM_SUCCESS;

	if (db_get_mailbox_size(self->mailbox->id, 1, &mailbox_size) == DM_EQUERY)
		return DM_EQUERY;

	ids = g_tree_keys(self->mailbox->msginfo);
	ids = g_list_reverse(ids);
	g_list_foreach(ids, (GFunc) _do_expunge, self);

	if (i > g_tree_nnodes(self->mailbox->msginfo)) {
		db_mailbox_mtime_update(self->mailbox->id);
		if (! dm_quota_user_dec(self->userid, mailbox_size))
			return DM_EQUERY;
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

void dbmail_imap_session_bodyfetch_new(ImapSession *self) 
{

	assert(self->fi);
	body_fetch_t *bodyfetch = g_new0(body_fetch_t, 1);
	bodyfetch->itemtype = -1;
	self->fi->bodyfetch = g_list_append(self->fi->bodyfetch, bodyfetch);
}

void dbmail_imap_session_bodyfetch_rewind(ImapSession *self) 
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
	if (bodyfetch->headers) {
		g_tree_destroy(bodyfetch->headers);
		bodyfetch->headers = NULL;
	}
	g_free(bodyfetch);
	bodyfetch = NULL;
}

void dbmail_imap_session_bodyfetch_free(ImapSession *self) 
{
	assert(self->fi);
	if (! self->fi->bodyfetch)
		return;
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
	g_list_foreach(self->fi->bodyfetch, (GFunc)_body_fetch_free, NULL);
	g_list_free(g_list_first(self->fi->bodyfetch));
	self->fi->bodyfetch = NULL;

}

body_fetch_t * dbmail_imap_session_bodyfetch_get_last(ImapSession *self) 
{
	assert(self->fi);
	if (self->fi->bodyfetch == NULL)
		dbmail_imap_session_bodyfetch_new(self);
	
	self->fi->bodyfetch = g_list_last(self->fi->bodyfetch);
	return (body_fetch_t *)self->fi->bodyfetch->data;
}

int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	memset(bodyfetch->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(bodyfetch->partspec,partspec,length);
	return 0;
}
char *dbmail_imap_session_bodyfetch_get_last_partspec(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->partspec;
}

int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->itemtype = itemtype;
	return 0;
}
int dbmail_imap_session_bodyfetch_get_last_itemtype(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->itemtype;
}
int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argstart = self->args_idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_get_last_argstart(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argcnt = self->args_idx - bodyfetch->argstart;
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argcnt;
}

int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetstart = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetstart;
}


int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetcnt = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self)
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

int build_args_array_ext(ImapSession *self, const char *originalString)
{
	int inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i;
	size_t max;
	char parlist[MAX_LINESIZE];
	char s[MAX_LINESIZE];
	char *lastchar;

	assert(originalString);
	TRACE(TRACE_DEBUG,"[%p] tokenize [%d/%d] [%s]", self, self->ci->len, self->rbuff_size, originalString);

	/* Check for zero length input */
	if (! strlen(originalString)) goto finalize;

	/* Make a local copy of the string */
	g_strlcpy(s, originalString, MAX_LINESIZE);

	max = strlen(s);

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	if (self->rbuff_size <= 0)
		g_strchomp(s); // unless we're fetch string-literals it's safe to strip NL

	if (self->args[0] && MATCH(self->args[0],"LOGIN")) {
		if (self->args_idx == 2) {
			/* decode and store the password */
			size_t len;
			self->args[self->args_idx++] = (char *)g_base64_decode((const gchar *)s, &len);
			goto finalize; // done
		} else if (self->args_idx == 1) {
			/* decode and store the username */
			size_t len;
			self->args[self->args_idx++] = (char *)g_base64_decode((const gchar *)s, &len);
			/* ask for password */
			dbmail_imap_session_prompt(self,"password");
			return 0;
		}
	}

	for (i = 0; i < max && s[i] && self->args_idx < MAX_ARGS - 1; i++) {
		/* get bytes of string-literal */	
		if (self->rbuff_size > 0) {
			size_t r;
			char buff[self->rbuff_size];
			memset(buff, 0, sizeof(buff));

			if (! self->rbuff) self->rbuff = g_new0(char, self->rbuff_size+1);

			strncat(buff, &s[i], self->rbuff_size);
			r = strlen(buff);
			
			strncat(self->rbuff, buff, r);
			self->rbuff_size -= self->ci->len;

			if (self->rbuff_size > 0)
				return 0; // return to fetch more
				
			// the string-literal is complete
			self->args[self->args_idx++] = self->rbuff;
				
			i+=r;
			continue; // tokenize the rest of this line
		}

		/* check quotes */
		if (s[i] == '"' && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				self->args[self->args_idx] = g_new0(char,(i - quotestart));
				memcpy((void *) self->args[self->args_idx], (void *) &s[quotestart + 1], i - quotestart - 1);
				self->args[self->args_idx][i - quotestart - 1] = '\0';

				self->args_idx++;
				inquote = 0;
			} else {
				inquote = 1;
				quotestart = i;
			}
			continue;
		}

		if (inquote) continue;

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
				return -1;
			}

			/* add this parenthesis to the arg list and continue */
			self->args[self->args_idx] = g_new0(char,2);
			self->args[self->args_idx][0] = s[i];
			self->args[self->args_idx][1] = '\0';

			self->args_idx++;
			continue;
		}
		if (s[i] == ' ') continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			self->rbuff_size = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			TRACE(TRACE_DEBUG, "[%p] last char = %c", self, *lastchar);
			if ((*lastchar == '+' && *(lastchar + 1) == '}' && *(lastchar + 2) == '\0') || 
				(*lastchar == '}' && *(lastchar + 1) == '\0')) {
				dbmail_imap_session_printf(self, "+ OK gimme that string\r\n");
				return 0;
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

		self->args[self->args_idx] = g_new0(char,(i - argstart + 1));
		memcpy((void *) self->args[self->args_idx], (void *) &s[argstart], i - argstart);
		self->args[self->args_idx][i - argstart] = '\0';
		self->args_idx++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) {
		/* error in parenthesis structure */
		return -1;
	}

finalize:
	if (self->rbuff_size > 0) {
		TRACE(TRACE_DEBUG, "[%p] need more: [%d]", self, self->rbuff_size);
		return 0;
	}
	if ((self->args_idx == 1) && MATCH(self->args[0],"LOGIN") ) {
		TRACE(TRACE_DEBUG, "[%p] prompt for authenticate tokens", self);

		/* ask for username */
		dbmail_imap_session_prompt(self,"username");
		return 0;
	}

	TRACE(TRACE_DEBUG, "[%p] tag: [%s], command: [%s], [%llu] args", self, self->tag, self->command, self->args_idx);
	self->args[self->args_idx] = NULL;	/* terminate */
	for (i = 0; i<=self->args_idx && self->args[i]; i++) { 
		TRACE(TRACE_DEBUG, "[%p] arg[%d]: '%s'\n", self, i, self->args[i]); 
	}
	self->args_idx = 0;
	return 1;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR



