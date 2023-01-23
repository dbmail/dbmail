/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
 * IMAP-server utility functions implementations
 */

#include "dbmail.h"
#include "dm_mempool.h"

#define THIS_MODULE "imap"
#define BUFLEN 2048
#define SEND_BUF_SIZE 8192
#define MAX_ARGS 512
#define IDLE_TIMEOUT 30



extern DBParam_T db_params;
#define DBPFX db_params.pfx

extern Mempool_T queue_pool;
extern Mempool_T small_pool;

gboolean imap_feature_idle_status = FALSE;

extern const char *month_desc[];
extern const int month_len[];
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];
extern volatile sig_atomic_t alarm_occured;

extern GAsyncQueue *queue;
extern ServerConfig_T *server_conf;

/*
 * send_data()
 *
 */
static void send_data(ImapSession *self, const String_T stream, size_t offset, size_t len)
{
	char buf[SEND_BUF_SIZE];
	size_t l = 0;
	char *head;

	assert(stream);
	if (p_string_len(stream) < (offset+len))
		return;

	head = (char *)p_string_str(stream)+offset;

	TRACE(TRACE_DEBUG,"[%p] stream [%p] offset [%ld] len [%ld]", self, stream, offset, len);
	while (len > 0) {
		l = min(len, sizeof(buf)-1);
		memset(buf,0,sizeof(buf));
		strncpy(buf, head, l);
		dbmail_imap_session_buff_printf(self, "%s", buf);
		head += l;
		len -= l;
	}
}

static void mailboxstate_destroy(MailboxState_T M)
{
	MailboxState_free(&M);
}


/* 
 * initializer and accessors for ImapSession
 */
ImapSession * dbmail_imap_session_new(Mempool_T pool)
{
	ImapSession * self;
	Field_T val;
	gboolean login_disabled = TRUE;

	self = mempool_pop(pool, sizeof(ImapSession));

	if (! queue_pool)
		self->buff = p_string_new(pool, "");
	else
		self->buff = p_string_new(queue_pool, "");

	self->pool = pool;

	pthread_mutex_init(&self->lock, NULL);

	GETCONFIGVALUE("login_disabled", "IMAP", val);
	if (SMATCH(val, "no"))
		login_disabled = FALSE;

	self->state = CLIENTSTATE_NON_AUTHENTICATED;
	self->args = mempool_pop(self->pool, sizeof(String_T) * MAX_ARGS);
	self->fi = mempool_pop(self->pool, sizeof(fetch_items));
	self->capa = Capa_new(self->pool);
	self->preauth_capa = Capa_new(self->pool);
	Capa_remove(self->preauth_capa, "ACL");
	Capa_remove(self->preauth_capa, "RIGHTS=texk");
	Capa_remove(self->preauth_capa, "NAMESPACE");
	Capa_remove(self->preauth_capa, "CHILDREN");
	Capa_remove(self->preauth_capa, "SORT");
	Capa_remove(self->preauth_capa, "QUOTA");
	Capa_remove(self->preauth_capa, "THREAD=ORDEREDSUBJECT");
	Capa_remove(self->preauth_capa, "UNSELECT");
	Capa_remove(self->preauth_capa, "IDLE");
	Capa_remove(self->preauth_capa, "UIDPLUS");
	Capa_remove(self->preauth_capa, "WITHIN");
	Capa_remove(self->preauth_capa, "CONDSTORE");
	Capa_remove(self->preauth_capa, "ENABLE");
	Capa_remove(self->preauth_capa, "QRESYNC");

	if (! (server_conf && server_conf->ssl))
		Capa_remove(self->preauth_capa, "STARTTLS");

	if (! Capa_match(self->preauth_capa, "STARTTLS"))
		login_disabled = FALSE;

	if (login_disabled) {
		if (! Capa_match(self->preauth_capa, "LOGINDISABLED"))
			Capa_add(self->preauth_capa, "LOGINDISABLED");
		Capa_remove(self->preauth_capa, "AUTH=LOGIN");
		Capa_remove(self->preauth_capa, "AUTH=CRAM-MD5");
	} else {
		Capa_remove(self->preauth_capa, "LOGINDISABLED");
	}
	if (MATCH(db_params.authdriver, "LDAP")) {
		Capa_remove(self->capa, "AUTH=CRAM-MD5");
		Capa_remove(self->preauth_capa, "AUTH=CRAM-MD5");
	}

	if (! (server_conf && server_conf->ssl)) {
		Capa_remove(self->capa, "STARTTLS");
		Capa_remove(self->preauth_capa, "STARTTLS");
	}

	self->physids = g_tree_new((GCompareFunc)ucmp);
	self->mbxinfo = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)uint64_free,(GDestroyNotify)mailboxstate_destroy);

	TRACE(TRACE_DEBUG,"imap session [%p] created", self);
	return self;
}

static uint64_t dbmail_imap_session_message_load(ImapSession *self)
{
	TRACE(TRACE_DEBUG, "Call: dbmail_imap_session_message_load");
	uint64_t *id = NULL;
	
	
	if (! (id = g_tree_lookup(self->physids, &(self->msg_idnr)))) {
		uint64_t *uid;
		
		id = mempool_pop(self->pool, sizeof(uint64_t));
		
		if (self->mailbox->mbstate != NULL){
			/* the state is ready, get the phys id from state, avoid one query in database */
			MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), &self->msg_idnr);
			*id=msginfo->phys_id;
		}else{
			/* previous behavior, a query will be performed in db */
			if ((db_get_physmessage_id(self->msg_idnr, id)) != DM_SUCCESS) {
				TRACE(TRACE_ERR,"can't find physmessage_id for message_idnr [%" PRIu64 "]", self->msg_idnr);
				g_free(id);
				return 0;
			}
		}
		uid = mempool_pop(self->pool, sizeof(uint64_t));
		*uid = self->msg_idnr;
		g_tree_insert(self->physids, uid, id);
		
	}
		
	if (self->message) {
		if (*id != self->message->id) {
			dbmail_message_free(self->message);
			self->message = NULL;
		}
	}

	assert(id);

	if (! self->message) {
		DbmailMessage *msg = dbmail_message_new(self->pool);
		if ((msg = dbmail_message_retrieve(msg, *id)) != NULL)
			self->message = msg;
	}

	if (! self->message) {
		TRACE(TRACE_ERR,"message retrieval failed");
		return 0;
	}

	assert(*id == self->message->id);
	return 1;
}

ImapSession * dbmail_imap_session_set_command(ImapSession * self, const char * command)
{
	g_strlcpy(self->command, command, sizeof(self->command));
	return self;
}

static gboolean _physids_free(gpointer key, gpointer value, gpointer data)
{
	ImapSession *self = (ImapSession *)data;
	mempool_push(self->pool, key, sizeof(uint64_t));
	mempool_push(self->pool, value, sizeof(uint64_t));
	return FALSE;
}

void dbmail_imap_session_delete(ImapSession ** s)
{
	ImapSession *self = *s;
	Mempool_T pool;

	TRACE(TRACE_DEBUG, "[%p]", self);
	Capa_free(&self->preauth_capa);
	Capa_free(&self->capa);

	dbmail_imap_session_args_free(self, TRUE);
	dbmail_imap_session_fetch_free(self, TRUE);

	if (self->mailbox) {
		dbmail_mailbox_free(self->mailbox);
		self->mailbox = NULL;
	}
	if (self->mbxinfo) {
		g_tree_destroy(self->mbxinfo);
		self->mbxinfo = NULL;
	}
	if (self->message) {
		dbmail_message_free(self->message);
		self->message = NULL;
	}
	if (self->physids) {
		g_tree_foreach(self->physids, (GTraverseFunc)_physids_free, (gpointer)self);
		g_tree_destroy(self->physids);
		self->physids = NULL;
	}
	if (self->buff) {
		p_string_free(self->buff, TRUE);
		self->buff = NULL;
	}

	pthread_mutex_destroy(&self->lock);
	pool = self->pool;
	mempool_push(pool, self, sizeof(ImapSession));
	mempool_close(&pool);
	self = NULL;
}

void dbmail_imap_session_fetch_free(ImapSession *self, gboolean all) 
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
	if (self->fi->bodyfetch) {
		dbmail_imap_session_bodyfetch_free(self);
		self->fi->bodyfetch = NULL;
	}
	if (all) {
		mempool_push(self->pool, self->fi, sizeof(fetch_items));
		self->fi = NULL;
	} else {
		memset(self->fi, 0, sizeof(fetch_items));
	}
}

void dbmail_imap_session_args_free(ImapSession *self, gboolean all)
{
	int i;
	for (i = 0; i < MAX_ARGS && self->args[i]; i++) {
		if (self->args[i])
		       	p_string_free(self->args[i], TRUE);
		self->args[i] = NULL;
	}
	self->args_idx = 0;

	if (all) {
		mempool_push(self->pool, self->args, sizeof(String_T) * MAX_ARGS);
		self->args = NULL;
	}
}

/*************************************************************************************
 *
 *
 * imap utilities using ImapSession
 *
 *
 ************************************************************************************/
static int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length) 
{
	assert(self->fi);
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	memset(bodyfetch->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(bodyfetch->partspec,partspec,length);
	return 0;
}
int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype) 
{
	assert(self->fi);
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	bodyfetch->itemtype = itemtype;
	return 0;
}
int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self) 
{
	assert(self->fi);
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	bodyfetch->argstart = self->args_idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self) 
{
	assert(self->fi);
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	bodyfetch->argcnt = self->args_idx - bodyfetch->argstart;
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self) 
{
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet)
{
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	bodyfetch->octetstart = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self)
{
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	return bodyfetch->octetstart;
}
int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet)
{
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	bodyfetch->octetcnt = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self)
{
	body_fetch *bodyfetch = p_list_data(self->fi->bodyfetch);
	return bodyfetch->octetcnt;
}


static int _imap_session_fetch_parse_partspec(ImapSession *self)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	const char *token, *nexttoken;

	token = p_string_str(self->args[self->args_idx]);
	nexttoken = p_string_str(self->args[self->args_idx+1]);

	TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

	for (j = 0; token[j]; j++) {
		if (isdigit(token[j])) {
			indigit = 1;
			continue;
		} else if (token[j] == '.') {
			if (!indigit) return -2; /* error, single dot specified */
			indigit = 0;
			continue;
		} else
			break;	/* other char found */
	}
	
	if (j > 0) {
		if (indigit && token[j]) return -2;		/* error DONE */
		/* partspecifier present, save it */
		if (j >= IMAP_MAX_PARTSPEC_LEN) return -2;	/* error DONE */
		dbmail_imap_session_bodyfetch_set_partspec(self, (char *)token, j);
	}

	const char *partspec = &token[j];
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
		if (j == 0) return -2;				/* error DONE */
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_MIME);
		shouldclose = 1;
	} else if (token[j] == '\0') {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_ALL);
		shouldclose = 1;
	} else {
		return -2;					/* error DONE */
	}
	if (shouldclose) {
		if (! MATCH(nexttoken, "]")) return -2;		/* error DONE */
	} else {
		self->args_idx++;	/* should be at '(' now */
		token = p_string_str(self->args[self->args_idx]);
		nexttoken = p_string_str(self->args[self->args_idx+1]);

		if (! MATCH(token,"(")) return -2;		/* error DONE */

		self->args_idx++;	/* at first item of field list now, remember idx */
		dbmail_imap_session_bodyfetch_set_argstart(self); 
		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(p_string_str(self->args[self->args_idx]),")") ) 
			self->args_idx++;

		token = p_string_str(self->args[self->args_idx]);
		nexttoken = p_string_str(self->args[self->args_idx+1]);
		
		TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

		dbmail_imap_session_bodyfetch_set_argcnt(self);

		if (dbmail_imap_session_bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;				/* error DONE */
	}

	return 0;
}

#define RANGESIZE 128

static int _imap_session_fetch_parse_octet_range(ImapSession *self) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	char token[RANGESIZE];
	
	if (! self->args[self->args_idx])
		return 0;

	memset(token, 0, sizeof(token));
	g_strlcpy(token, p_string_str(self->args[self->args_idx]), sizeof(token));

	if (token[0] == '\0') 
		return 0;
	
	TRACE(TRACE_DEBUG,"[%p] parse token [%s]", self, token);

	if (token[0] == '<') {

		/* check argument */
		if (token[strlen(token) - 1] != '>') return -2;	/* error DONE */
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
		self->fi->msgparse_needed=1;
		guint64 octetstart = strtoull(&token[1], NULL, 10);
		gint64 octetcnt = strtoll(&token [delimpos + 1], NULL, 10);
		//TRACE(TRACE_DEBUG, "octetstart [%lu] octetcnt [%lu]", octetstart, octetcnt);
		dbmail_imap_session_bodyfetch_set_octetstart(self, octetstart);
		dbmail_imap_session_bodyfetch_set_octetcnt(self, octetcnt);

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


#define TOKENAT(a) (self->args[self->args_idx+a]?p_string_str(self->args[self->args_idx+a]):NULL)
#define NEXTTOKEN TOKENAT(1)
#define TOKEN TOKENAT(0)

int dbmail_imap_session_fetch_parse_args(ImapSession * self)
{
	int ispeek = 0;
	const char *token = TOKEN;
	const char *nexttoken = NEXTTOKEN;

	if (!token) return -1; // done
	if ((token[0] == ')') && (! nexttoken)) return -1; // done
	if (token[0] == '(') return 1; // skip

	TRACE(TRACE_DEBUG,"[%p] parse args[%" PRIu64 "] = [%s]", self, self->args_idx, token);

	if ((! nexttoken) && (strcmp(token,")") == 0))
		return -1; // done

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
		
		body_fetch *bodyfetch = mempool_pop(self->pool, sizeof(body_fetch));
		self->fi->bodyfetch = p_list_append(self->fi->bodyfetch, bodyfetch);

		if (MATCH(token,"body.peek")) ispeek=1;
		
		nexttoken = NEXTTOKEN;
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek) return -2;	/* error DONE */
			self->fi->msgparse_needed = 1;
			self->fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			int res = 0;
			/* now read the argument list to body */
			self->args_idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			self->args_idx++;	/* now pointing at what should be the item type */

			token = TOKEN;
			nexttoken = NEXTTOKEN;

			TRACE(TRACE_DEBUG,"[%p] token [%s], nexttoken [%s]", self, token, nexttoken);

			if (MATCH(token,"]")) {
				if (ispeek) {
					self->fi->msgparse_needed = 1;
					self->fi->getBodyTotalPeek = 1;
				} else {
					self->fi->msgparse_needed = 1;
					self->fi->getBodyTotal = 1;
				}
				self->args_idx++;				
				res = _imap_session_fetch_parse_octet_range(self);
				if (res == -2)
					TRACE(TRACE_DEBUG,"[%p] fetch_parse_octet_range return with error", self);
				return res;
					
			}
			
			if (ispeek) self->fi->noseen = 1;

			if (_imap_session_fetch_parse_partspec(self) < 0) {
				TRACE(TRACE_DEBUG,"[%p] fetch_parse_partspec return with error", self);
				return -2;
			}
			
			self->args_idx++; // idx points to ']' now
			self->args_idx++; // idx points to octet range now 
			res = _imap_session_fetch_parse_octet_range(self);
			if (res == -2)
				TRACE(TRACE_DEBUG,"[%p] fetch_parse_octet_range return with error", self);
			return res;
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
		self->fi->getMIME_IMB_noextension = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"bodystructure")) {
		self->fi->msgparse_needed=1;
		self->fi->getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		self->fi->getEnvelope = 1;
	} else if (Capa_match(self->capa, "CONDSTORE") && (MATCH(token,"changedsince"))) {
		char *rest = (char *)nexttoken;
		uint64_t seq = dm_strtoull(nexttoken, &rest, 10);
		if (rest == nexttoken)
			return -2;
		self->fi->changedsince = seq;
		self->mailbox->condstore = true;
		self->args_idx++;
	} else if (Capa_match(self->capa, "CONDSTORE") && (MATCH(token, "modseq"))) {
		self->mailbox->condstore = true;
	} else if (MATCH(token, "vanished")) {
		if (self->enabled.qresync && self->use_uid)
			self->fi->vanished = true;
		else
			return -2;
	}


	return 1; //theres more...
}


#define SEND_SPACE if (self->fi->isfirstfetchout) \
				self->fi->isfirstfetchout = 0; \
			else \
				dbmail_imap_session_buff_printf(self, " ")

#define QUERY_BATCHSIZE 2000

void _send_headers(ImapSession *self, const body_fetch *bodyfetch, gboolean not)
{
	long long cnt = 0;
	gchar *tmp;
	gchar *s;
	String_T ts;

	dbmail_imap_session_buff_printf(self,"HEADER.FIELDS%s %s] ", not ? ".NOT" : "", bodyfetch->hdrplist);

	if (! (s = g_tree_lookup(bodyfetch->headers, &(self->msg_idnr)))) {
		dbmail_imap_session_buff_printf(self, "{2}\r\n\r\n");
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] [%s] [%s]", self, bodyfetch->hdrplist, s);

	ts = p_string_new(self->pool, s);
	tmp = get_crlf_encoded(p_string_str(ts));
	cnt = strlen(tmp);

	if (bodyfetch->octetcnt > 0) {
		char *p = tmp;
		if (bodyfetch->octetstart > 0 && bodyfetch->octetstart < (guint64)cnt) {
			p += bodyfetch->octetstart;
			cnt -= bodyfetch->octetstart;
		}
		
		if ((guint64)cnt > bodyfetch->octetcnt) {
			p[bodyfetch->octetcnt] = '\0';
			cnt = bodyfetch->octetcnt;
		}
		
		dbmail_imap_session_buff_printf(self, "<%" PRIu64 "> {%" PRIu64 "}\r\n%s\r\n", 
				bodyfetch->octetstart, cnt+2, p);
	} else {
		dbmail_imap_session_buff_printf(self, "{%" PRIu64 "}\r\n%s\r\n", cnt+2, tmp);
	}

	p_string_free(ts,TRUE);
	ts = NULL;
	g_free(tmp);
	tmp = NULL;
}


/* get headers or not */
static void _fetch_headers(ImapSession *self, body_fetch *bodyfetch, gboolean not)
{
	Connection_T c; ResultSet_T r; volatile int t = FALSE;
	gchar *fld, *val, *old, *new = NULL;
	uint64_t *mid;
	uint64_t id;
	GList *last;
	GString *fieldorder = NULL;
	GString *headerIDs = NULL;
	int k;
	int fieldseq=0;
	String_T query = NULL;
	String_T range = NULL;

	if (! bodyfetch->headers) {
		TRACE(TRACE_DEBUG, "[%p] init bodyfetch->headers", self);
		bodyfetch->headers = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)uint64_free,(GDestroyNotify)g_free);
		self->ceiling = 0;
		self->hi = 0;
		self->lo = 0;
	}

	if (! bodyfetch->hdrnames) {

		GList *tlist = NULL;
		GString *h = NULL;

		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, (void *)p_string_str(self->args[k + bodyfetch->argstart]));

		bodyfetch->hdrplist = dbmail_imap_plist_as_string(tlist);
		h = g_list_join((GList *)tlist,"','");
		bodyfetch->names = tlist;

		h = g_string_ascii_down(h);

		bodyfetch->hdrnames = h->str;

		g_string_free(h,FALSE);
	}

	TRACE(TRACE_DEBUG,"[%p] for %" PRIu64 "%s [%s]", self, self->msg_idnr, not?"NOT":"", bodyfetch->hdrplist);

	// did we prefetch this message already?
	if (self->msg_idnr <= self->ceiling) {
		_send_headers(self, bodyfetch, not);
		return;
	}

	// let's fetch the required message and prefetch a batch if needed.
	range = p_string_new(self->pool, "");
	

	if (! (last = g_list_nth(self->ids_list, self->lo+(uint64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(uint64_t *)last->data;

	if (self->msg_idnr == self->hi)
		p_string_printf(range, "= %" PRIu64 "", self->msg_idnr);
	else
		p_string_printf(range, "BETWEEN %" PRIu64 " AND %" PRIu64 "", self->msg_idnr, self->hi);

	TRACE(TRACE_DEBUG,"[%p] prefetch %" PRIu64 ":%" PRIu64 " ceiling %" PRIu64 " [%s]", self, self->msg_idnr, self->hi, self->ceiling, bodyfetch->hdrplist);
	
	headerIDs = g_string_new("0");
	if (! not) {
		fieldorder = g_string_new(", CASE ");
		
		fieldseq = 0;
		bodyfetch->names = g_list_first(bodyfetch->names);

		while (bodyfetch->names) {
			char *raw = (char *)bodyfetch->names->data;
			char *name = g_ascii_strdown(raw, strlen(raw));
			g_string_append_printf(fieldorder, "WHEN n.headername='%s' THEN %d ",
					name, fieldseq);
			
			if (! g_list_next(bodyfetch->names)){
				g_free(name);
				break;
			}
			bodyfetch->names = g_list_next(bodyfetch->names);
			fieldseq++;
			/* get the id of the header */
			query = p_string_new(self->pool, "");
			p_string_printf(query, "select id from %sheadername "
				"where headername='%s' ",
				DBPFX,
				name);
			
			c = db_con_get();	
			TRY
				r = db_query(c, p_string_str(query));	
				while (db_result_next(r)) { 
					id = db_result_get_u64(r, 0);
					g_string_append_printf(headerIDs, ",%ld",id);
				}
			CATCH(SQLException) 
				LOG_SQLERROR;
				t = DM_EQUERY; 
			FINALLY
				db_con_close(c);
			END_TRY;
			p_string_free(query, TRUE);		
			g_free(name);
		}
		fieldseq++;
		//adding default value, useful in NOT conditions, Cosmin Cioranu
		g_string_append_printf(fieldorder, "ELSE %d END AS seq",fieldseq);
	}
	TRACE(TRACE_DEBUG, "[headername ids %s] ", headerIDs->str);
	query = p_string_new(self->pool, "");
	p_string_printf(query, "SELECT m.message_idnr, n.headername, v.headervalue%s "
			"FROM %sheader h "
			"LEFT JOIN %smessages m ON h.physmessage_id=m.physmessage_id "
			"LEFT JOIN %sheadername n ON h.headername_id=n.id "
			"LEFT JOIN %sheadervalue v ON h.headervalue_id=v.id "
			"WHERE "
			"h.headername_id %s IN (%s) "
			"AND m.mailbox_idnr = %" PRIu64 " "
			"AND m.message_idnr %s "
			"AND status < %d "

			//"AND n.headername %s IN ('%s') "	//old, from the sql point of view is slow, CC 2020
			"GROUP By m.message_idnr, n.headername, v.headervalue "
			// "having seq %s %d "
			"ORDER BY m.message_idnr, seq",
			not?"":fieldorder->str,
			DBPFX, DBPFX, DBPFX, DBPFX,
			not?"NOT":"", headerIDs->str,
			self->mailbox->id, p_string_str(range),
			//not?"NOT":"", bodyfetch->hdrnames	//old 
			MESSAGE_STATUS_DELETE			//return information only related to valid messages
			//not?"=":"<",fieldseq			//patch Cosmin Cioranu, added the having conditions and also the 'not' handler
		    );

	if (fieldorder)
		g_string_free(fieldorder, TRUE);
	if (headerIDs)
		g_string_free(headerIDs, TRUE);
	c = db_con_get();	
	TRY
		r = db_query(c, p_string_str(query));
		while (db_result_next(r)) {
			int l;	
			const void *blob;

			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			fld = (char *)db_result_get(r, 1);
			blob = db_result_get_blob(r, 2, &l);
			char *str = g_new0(char, l + 1);
			str = strncpy(str, blob, l);
			val = dbmail_iconv_db_to_utf7(str);
			g_free(str);
			if (! val) {
				TRACE(TRACE_DEBUG, "[%p] [%" PRIu64 "] no headervalue [%s]", self, id, fld);
			} else {
				mid = mempool_pop(small_pool, sizeof(uint64_t));
				*mid = id;

				old = g_tree_lookup(bodyfetch->headers, (gconstpointer)mid);
				fld[0] = toupper(fld[0]);
				/* Build content as Header: value \n */
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
	END_TRY;

	p_string_free(range, TRUE);
	p_string_free(query, TRUE);

	if (t == DM_EQUERY) return;
	
	self->lo += QUERY_BATCHSIZE;
	self->ceiling = self->hi;

	_send_headers(self, bodyfetch, not);

	return;
}

static uint64_t get_dumpsize(body_fetch *bodyfetch, uint64_t dumpsize) 
{
	if (bodyfetch->octetstart > dumpsize)
		return 0;
	if ((bodyfetch->octetstart + dumpsize) > bodyfetch->octetcnt)
		return bodyfetch->octetcnt;
	return (dumpsize - bodyfetch->octetstart);
}

static void _imap_send_part(ImapSession *self, GMimeObject *part, body_fetch *bodyfetch, const char *type)
{
	TRACE(TRACE_DEBUG,"[%p] type [%s]", self, type?type:"");
	if ( !part ) { 
		dbmail_imap_session_buff_printf(self, "] NIL");
	} else {
		char *tmp = imap_get_logical_part(part,type);
		String_T str = p_string_new(self->pool, tmp);
		size_t len = p_string_len(str);
		g_free(tmp);

		if (len < 1) {
			dbmail_imap_session_buff_printf(self, "] NIL");
		} else {
			uint64_t cnt = 0;
			if (bodyfetch->octetcnt > 0) {
				cnt = get_dumpsize(bodyfetch, len);
				dbmail_imap_session_buff_printf(self, "]<%" PRIu64 "> {%" PRIu64 "}\r\n", bodyfetch->octetstart, cnt);
				p_string_erase(str,0,min(bodyfetch->octetstart,len));
				p_string_truncate(str,cnt);
			} else {
				dbmail_imap_session_buff_printf(self, "] {%" PRIu64 "}\r\n", len);
			}
			dbmail_imap_session_buff_printf(self,"%s", p_string_str(str));
		}
		p_string_free(str,TRUE);
	}
}


static int _imap_show_body_section(body_fetch *bodyfetch, gpointer data) 
{
	GMimeObject *part = NULL;
	gboolean condition = FALSE;
	ImapSession *self = (ImapSession *)data;
	
	if (bodyfetch->itemtype < BFIT_TEXT)
	       	return 0;
	
	TRACE(TRACE_DEBUG,"[%p] itemtype [%d] partspec [%s]", self, bodyfetch->itemtype, bodyfetch->partspec);
	
	if (self->fi->msgparse_needed) {
		if (bodyfetch->partspec[0]) {
			if (bodyfetch->partspec[0] == '0') {
				dbmail_imap_session_buff_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
				TRACE(TRACE_ERR, "[%p] PROTOCOL ERROR", self);
				return 1;
			}
			part = imap_get_partspec(GMIME_OBJECT((self->message)->content), bodyfetch->partspec);
		} else {
			part = GMIME_OBJECT((self->message)->content);
		}
	}

	SEND_SPACE;

	if (! self->fi->noseen) self->fi->setseen = 1;
	dbmail_imap_session_buff_printf(self, "BODY[%s", bodyfetch->partspec);

	switch (bodyfetch->itemtype) {

		case BFIT_ALL:
			_imap_send_part(self, part, bodyfetch, NULL);
			break;
		case BFIT_TEXT:
			dbmail_imap_session_buff_printf(self, "TEXT");
			_imap_send_part(self, part, bodyfetch, "TEXT");
			break;
			// fall-through
		case BFIT_HEADER:
			dbmail_imap_session_buff_printf(self, "HEADER");
			_imap_send_part(self, part, bodyfetch, "HEADER");
			break;
		case BFIT_MIME:
			dbmail_imap_session_buff_printf(self, "MIME");
			_imap_send_part(self, part, bodyfetch, "MIME");
			break;
		case BFIT_HEADER_FIELDS_NOT:
			condition=TRUE;
			// fall-through
		case BFIT_HEADER_FIELDS:
			_fetch_headers(self, bodyfetch, condition);
			break;
		default:
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal server error\r\n");
			return -1;

	}
	return 0;
}

/* get envelopes */
static void _fetch_envelopes(ImapSession *self)
{
	Connection_T c; ResultSet_T r; volatile int t = FALSE;
	INIT_QUERY;
	gchar *s;
	uint64_t *mid;
	uint64_t id;
	char range[DEF_FRAGSIZE];
	GList *last;
	memset(range,0,sizeof(range));

	if (! self->envelopes) {
		self->envelopes = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)uint64_free,(GDestroyNotify)g_free);
		self->lo = 0;
		self->hi = 0;
	}

	if ((s = g_tree_lookup(self->envelopes, &(self->msg_idnr))) != NULL) {
		dbmail_imap_session_buff_printf(self, "ENVELOPE %s", s);
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] lo: %" PRIu64 "", self, self->lo);

	if (! (last = g_list_nth(self->ids_list, self->lo+(uint64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(uint64_t *)last->data;

	if (self->msg_idnr == self->hi)
		snprintf(range,DEF_FRAGSIZE-1,"= %" PRIu64 "", self->msg_idnr);
	else
		snprintf(range,DEF_FRAGSIZE-1,"BETWEEN %" PRIu64 " AND %" PRIu64 "", self->msg_idnr, self->hi);

	snprintf(query, DEF_QUERYSIZE-1, "SELECT message_idnr,envelope "
			"FROM %senvelope e "
			"LEFT JOIN %smessages m USING (physmessage_id) "
			"WHERE m.mailbox_idnr = %" PRIu64 " "
			"AND message_idnr %s",
			DBPFX, DBPFX,  
			self->mailbox->id, range);
	c = db_con_get();
	TRY
		r = db_query(c, query);
		while (db_result_next(r)) {
			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			mid = mempool_pop(small_pool, sizeof(uint64_t));
			*mid = id;
			
			g_tree_insert(self->envelopes,mid,g_strdup(ResultSet_getString(r, 2)));
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return;

	self->lo += QUERY_BATCHSIZE;

	s = g_tree_lookup(self->envelopes, &(self->msg_idnr));
	dbmail_imap_session_buff_printf(self, "ENVELOPE %s", s?s:"");
}

static void _imap_show_body_sections(ImapSession *self) 
{
	List_T head;
	if (! self->fi->bodyfetch)
		return;

	head = p_list_first(self->fi->bodyfetch);
	while (head) {
		body_fetch *bodyfetch = (body_fetch *)p_list_data(head);
		if (bodyfetch)
			_imap_show_body_section(bodyfetch, self);
		head = p_list_next(head);
	}
}

static int _fetch_get_items(ImapSession *self, uint64_t *uid)
{
	
	int result;
	uint64_t size = 0;
	gchar *s = NULL;
	uint64_t *id = uid;
	gboolean reportflags = FALSE;
	String_T stream = NULL;
	
	TRACE(TRACE_DEBUG,"Call: _fetch_get_items");
	
	MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), uid);

	if (! msginfo) {
		TRACE(TRACE_INFO, "[%p] failed to lookup msginfo struct for message [%" PRIu64 "]", self, *uid);
		return 0;
	}
	
	id = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate),uid);

	g_return_val_if_fail(id,-1);

	if (self->fi->changedsince && (msginfo->seq <= self->fi->changedsince))
		return 0;

	self->msg_idnr = *uid;
	self->fi->isfirstfetchout = 1;

	if (self->fi->msgparse_needed) {
		if (! (dbmail_imap_session_message_load(self)))
			return 0;

		stream = self->message->crlf;
		size = p_string_len(stream);
	}

	dbmail_imap_session_buff_printf(self, "* %" PRIu64 " FETCH (", *id);

	if (self->mailbox->condstore || self->enabled.qresync) {
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "MODSEQ (%" PRIu64 ")",
				msginfo->seq?msginfo->seq:1);
	}
	if (self->fi->getInternalDate) {
		SEND_SPACE;
		char *s =date_sql2imap(msginfo->internaldate);
		dbmail_imap_session_buff_printf(self, "INTERNALDATE \"%s\"", s);
		g_free(s);
	}
	if (self->fi->getSize) {
		uint64_t rfcsize = msginfo->rfcsize;
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "RFC822.SIZE %" PRIu64 "", rfcsize);
	}
	if (self->fi->getFlags) {
		SEND_SPACE;

		GList *sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);
		dbmail_imap_session_buff_printf(self,"FLAGS %s",s);
		g_free(s);
	}
	if (self->fi->getUID) {
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "UID %" PRIu64 "", msginfo->uid);
	}
	if (self->fi->getMIME_IMB) {
		SEND_SPACE;
		if ((s = imap_get_structure(GMIME_MESSAGE((self->message)->content), 1))==NULL) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		dbmail_imap_session_buff_printf(self, "BODYSTRUCTURE %s", s);
		g_free(s);
	}

	if (self->fi->getMIME_IMB_noextension) {
		SEND_SPACE;
		if ((s = imap_get_structure(GMIME_MESSAGE((self->message)->content), 0))==NULL) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE error fetching body\r\n");
			return -1;
		}
		dbmail_imap_session_buff_printf(self, "BODY %s",s);
		g_free(s);
	}

	if (self->fi->getEnvelope) {
		SEND_SPACE;
		_fetch_envelopes(self);
	}

	if (self->fi->getRFC822 || self->fi->getRFC822Peek) {
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "RFC822 {%" PRIu64 "}\r\n", size);
		send_data(self, stream, 0, size);
		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {
		SEND_SPACE;
		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			dbmail_imap_session_buff_printf(self, "BODY[] {%" PRIu64 "}\r\n", size);
			send_data(self, stream, 0, size);
		} else {
			uint64_t start = dbmail_imap_session_bodyfetch_get_last_octetstart(self);
			uint64_t count = dbmail_imap_session_bodyfetch_get_last_octetcnt(self);
			uint64_t length = 0;
			if (start <= size)
				length = ((start + count) > size)?(size - start):count;
			dbmail_imap_session_buff_printf(self, "BODY[]<%" PRIu64 "> {%" PRIu64 "}\r\n", 
					start, length);
			send_data(self, stream, start, length);
		}
		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;
	}

	if (self->fi->getRFC822Header) {
		SEND_SPACE;
		char *tmp = imap_get_logical_part(self->message->content, "HEADER");
		dbmail_imap_session_buff_printf(self, "RFC822.HEADER {%ld}\r\n%s", strlen(tmp), tmp);
		free(tmp);
		tmp = NULL;
	}

	if (self->fi->getRFC822Text) {
		SEND_SPACE;
		char *tmp = imap_get_logical_part(self->message->content, "TEXT");
		dbmail_imap_session_buff_printf(self, "RFC822.TEXT {%ld}\r\n%s", strlen(tmp), tmp);
		free(tmp);
		tmp = NULL;
		self->fi->setseen = 1;

	}

	_imap_show_body_sections(self);

	/* set \Seen flag if necessary; note the absence of an error-check 
	 * for db_get_msgflag()!
	 */
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	if (self->fi->setseen && db_get_msgflag("seen", self->msg_idnr) != 1) {
		/* only if the user has an ACL which grants
		   him rights to set the flag should the
		   flag be set! */
		result = acl_has_right(self->mailbox->mbstate, self->userid, ACL_RIGHT_SEEN);
		if (result == -1) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			reportflags = TRUE;
			result = db_set_msgflag(self->msg_idnr, setSeenSet, NULL, IMAPFA_ADD, 0, msginfo);
			if (result == -1) {
				dbmail_imap_session_buff_clear(self);
				dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
			db_mailbox_seq_update(MailboxState_getId(self->mailbox->mbstate), self->msg_idnr);
		}

		self->fi->getFlags = 1;
	}

	dbmail_imap_session_buff_printf(self, ")\r\n");

	if (reportflags) {
		char *t = NULL;
		GList *sublist = NULL;
		if (self->use_uid)
			t = g_strdup_printf("UID %" PRIu64 " ", *uid);
		
		sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);

		dbmail_imap_session_buff_printf(self,"* %" PRIu64 " FETCH (%sFLAGS %s)\r\n", *id, t?t:"", s);
		if (t) g_free(t);
		g_free(s);
	}

	return 0;
}

static gboolean _do_fetch(uint64_t *uid, gpointer UNUSED value, ImapSession *self)
{
	/* go fetch the items */
	if (_fetch_get_items(self,uid) < 0) {
		TRACE(TRACE_ERR, "[%p] _fetch_get_items returned with error", self);
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
		self->error = FALSE;
		g_tree_foreach(self->ids, (GTraverseFunc) _do_fetch, self);
		dbmail_imap_session_buff_flush(self);
		if (self->error) return -1;
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
void dbmail_imap_session_buff_clear(ImapSession *self)
{
	self->buff = p_string_truncate(self->buff, 0);
}	

void dbmail_imap_session_buff_flush(ImapSession *self)
{
	if (self->state >= CLIENTSTATE_LOGOUT) return;
	if (p_string_len(self->buff) < 1) return;

	gpointer session = self;
	gpointer data = self->buff;
	if (! queue_pool)
		self->buff = p_string_new(self->pool, "");
	else
		self->buff = p_string_new(queue_pool, "");

	dm_queue_push(dm_thread_data_sendmessage, session, data);
}

#define IMAP_BUF_SIZE 4096

int dbmail_imap_session_buff_printf(ImapSession * self, char * message, ...)
{
	va_list ap, cp;
	uint64_t j = 0, l;

	assert(message);
	j = p_string_len(self->buff);

	va_start(ap, message);
	va_copy(cp, ap);
	p_string_append_vprintf(self->buff, message, cp);
	va_end(cp);
	va_end(ap);
	l = p_string_len(self->buff);

	if (l >= IMAP_BUF_SIZE) dbmail_imap_session_buff_flush(self);

	return (int)(l-j);
}

int dbmail_imap_session_handle_auth(ImapSession * self, const char * username, const char * password)
{
	uint64_t userid = 0;
	int valid = 0;
	
	if (self->ci->auth)
		username = Cram_getUsername(self->ci->auth);

	TRACE(TRACE_DEBUG, "[%p] trying to validate user [%s]", self, username);

	valid = auth_validate(self->ci, username, password, &userid);
	
	
	switch(valid) {
		case -1: /* a db-error occurred */
			dbmail_imap_session_buff_printf(self, "* BYE internal db error validating user\r\n");
			return -1;

		case 0:
			sleep(2);	/* security */
			ci_authlog_init(self->ci, THIS_MODULE, username, AUTHLOG_ERR);
			if (self->ci->auth) { // CRAM-MD5 auth failed
				char *enctype = NULL;
				if (userid) enctype = auth_getencryption(userid);
				if ((! enctype) || (! MATCH(enctype,""))) {
					Capa_remove(self->capa,"AUTH=CRAM-MD5");
					dbmail_imap_session_buff_printf(self, "* CAPABILITY %s\r\n", Capa_as_string(self->capa));
				}
				if (enctype) g_free(enctype);
			}
			dbmail_imap_session_buff_printf(self, "%s NO login rejected\r\n", self->tag);
			TRACE(TRACE_NOTICE, "[%p] login rejected: user [%s] from [%s:%s]", self, username, 
				self->ci->src_ip, self->ci->src_port);
			return 1;

		case 1:
			self->userid = userid;
			ci_authlog_init(self->ci, THIS_MODULE, username, AUTHLOG_ACT);
			TRACE(TRACE_NOTICE, "[%p] login accepted: id [%" PRIu64 "] user [%s] from [%s:%s]",
				       	self, userid, username, self->ci->src_ip, self->ci->src_port);
			break;

		default:
			TRACE(TRACE_ERR, "[%p] auth_validate returned [%d]", self, valid);
			return -1;
	}

	dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);

	return 0;

}

static int dbmail_imap_session_prompt(ImapSession * self, char * prompt)
{
	char *prompt64, *promptcat;
	
	g_return_val_if_fail(prompt != NULL, -1);
	
	/* base64 encoding increases string length by about 40%. */
	promptcat = g_strdup_printf("%s\r\n", prompt);
	prompt64 = (char *)g_base64_encode((const guchar *)promptcat, strlen(promptcat));
	dbmail_imap_session_buff_printf(self, "+ %s\r\n", prompt64);
	dbmail_imap_session_buff_flush(self);
	
	g_free(prompt64);
	g_free(promptcat);
	
	return 0;
}

static void notify_fetch(ImapSession *self, MailboxState_T N, uint64_t *uid)
{
	uint64_t *msn;

	GList *ol = NULL, *nl = NULL;
	char *oldflags = NULL, *newflags = NULL;
	MessageInfo *old = NULL, *new = NULL;
	MailboxState_T M = self->mailbox->mbstate;
	gboolean flagschanged = false, modseqchanged = false;

	assert(uid);

	if (! (MailboxState_getMsginfo(N) && *uid && (new = g_tree_lookup(MailboxState_getMsginfo(N), uid))))
		return;

	if (! (msn = g_tree_lookup(MailboxState_getIds(M), uid)))
		return;

	MailboxState_merge_recent(N, M);

	// FETCH
	if ((old = g_tree_lookup(MailboxState_getMsginfo(M), uid)))
		ol = MailboxState_message_flags(M, old);
	oldflags = dbmail_imap_plist_as_string(ol);

	nl = MailboxState_message_flags(N, new);
	newflags = dbmail_imap_plist_as_string(nl);

	g_list_destroy(ol);
	g_list_destroy(nl);

	if ((!old) || (old->seq < new->seq))
		modseqchanged = true;
	if (oldflags && (! MATCH(oldflags, newflags)))
		flagschanged = true;

	if (modseqchanged || flagschanged) {
		GList *plist = NULL;
		char *response = NULL;
		if (self->use_uid) {
			char *u = g_strdup_printf("UID %" PRIu64, *uid);
			plist = g_list_append(plist, u);
		}

		if (modseqchanged && self->mailbox->condstore) {
			TRACE(TRACE_DEBUG, "seq [%" PRIu64 "] -> [%" PRIu64 "]", old?old->seq:0, new->seq);
			char *m = g_strdup_printf("MODSEQ (%" PRIu64 ")", new->seq);
			plist = g_list_append(plist, m);
		}

		if (flagschanged) {
			TRACE(TRACE_DEBUG, "flags [%s] -> [%s]", oldflags, newflags);
			char *f = g_strdup_printf("FLAGS %s", newflags);
			plist = g_list_append(plist, f);
		}

		response = dbmail_imap_plist_as_string(plist);

		dbmail_imap_session_buff_printf(self, "* %" PRIu64 " FETCH %s\r\n", 
				*msn, response);
		g_free(response);
		g_list_destroy(plist);
	}

	if (oldflags) g_free(oldflags);
	g_free(newflags);
}

static gboolean notify_expunge(ImapSession *self, uint64_t *uid)
{
	uint64_t *msn = NULL, m = 0;

	if (! (msn = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate), uid))) {
		TRACE(TRACE_DEBUG,"[%p] can't find uid [%" PRIu64 "]", self, *uid);
		return TRUE;
	}

	switch (self->command_type) {
		case IMAP_COMM_FETCH:
		case IMAP_COMM_STORE:
		case IMAP_COMM_SEARCH:
			break;
		default:
			m = *msn;
			if (MailboxState_removeUid(self->mailbox->mbstate, *uid) == DM_SUCCESS)
				dbmail_imap_session_buff_printf(self, "* %" PRIu64 " EXPUNGE\r\n", m);
			else
				return TRUE;
		break;
	}

	return FALSE;
}

static void mailbox_notify_expunge(ImapSession *self, MailboxState_T N)
{
	uint64_t *uid, *msn;
	MailboxState_T M;
	GList *ids;
	if (! N) return;

	M = self->mailbox->mbstate;

	ids  = g_tree_keys(MailboxState_getIds(M));
	ids = g_list_reverse(ids);

	// send expunge updates
	
	if (ids) {
		uid = (uint64_t *)ids->data;
		GTree * treeM=MailboxState_getIds(M);
		if (treeM != NULL){
		    msn = g_tree_lookup(treeM, uid);
		    if (msn && (*msn > MailboxState_getExists(M))) {
			    TRACE(TRACE_DEBUG,"exists new [%d] old: [%d]", MailboxState_getExists(N), MailboxState_getExists(M)); 
			    dbmail_imap_session_buff_printf(self, "* %d EXISTS\r\n", MailboxState_getExists(M));
		    }
		}
	}
	while (ids) {
		uid = (uint64_t *)ids->data;
		MessageInfo *messageInfo=g_tree_lookup(MailboxState_getMsginfo(N), uid);
		if (messageInfo!=NULL && !g_tree_lookup(MailboxState_getIds(N), uid)) {
			/* mark message as expunged, it should be ok to be removed from list, see state_load_message */
			messageInfo->expunged=1;
			notify_expunge(self, uid);
		}

		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}
	ids = g_list_first(ids);
	g_list_free(ids);
}

static void mailbox_notify_fetch(ImapSession *self, MailboxState_T N)
{
	uint64_t *uid, *id;
	GList *ids;
	if (! N) return;

	// send fetch updates
	ids = g_tree_keys(MailboxState_getIds(self->mailbox->mbstate));
	ids = g_list_first(ids);
	while (ids) {
		uid = (uint64_t *)ids->data;
		notify_fetch(self, N, uid);
		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}
	g_list_free(g_list_first(ids));

	// switch active mailbox view
	self->mailbox->mbstate = N;
	id = mempool_pop(small_pool, sizeof(uint64_t));
	*id = MailboxState_getId(N);

	 

	g_tree_replace(self->mbxinfo, id, N);

	MailboxState_flush_recent(N);
}

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update)
{
	/* 
		C: a047 NOOP
		S: * 22 EXPUNGE
		S: * 23 EXISTS
		S: * 3 RECENT
		S: * 14 FETCH (FLAGS (\Seen \Deleted))
	*/

	MailboxState_T M, N = NULL;
	gboolean showexists = FALSE, showrecent = FALSE, showflags = FALSE;
	unsigned oldexists;

	if (self->state != CLIENTSTATE_SELECTED) return FALSE;

	if (update) {
		unsigned oldseq, newseq;
		uint64_t olduidnext;
		char *oldflags, *newflags;

		M = self->mailbox->mbstate;
		oldseq = MailboxState_getSeq(M);
		oldflags = MailboxState_flags(M);
		oldexists = MailboxState_getExists(M);
		olduidnext = MailboxState_getUidnext(M);

                // check the mailbox sequence without a 
		// full reload
		N = MailboxState_new(self->pool, 0);
		MailboxState_setId(N, self->mailbox->id);
		newseq = MailboxState_getSeq(N);
		MailboxState_free(&N);
		N = NULL;
 
		TRACE(TRACE_DEBUG, "seq: [%u] -> [%u]", oldseq, newseq);
		if (oldseq != newseq) {
			int mailbox_update_strategy = config_get_value_default_int("mailbox_update_strategy", "IMAP", 1); 
			
			if (mailbox_update_strategy == 1){
			    TRACE(TRACE_DEBUG, "Strategy reload: 1 (full reload)");
			    /* do a full reload: re-read flags and counters */
			    N = MailboxState_new(self->pool, self->mailbox->id);
			}else{
			    if (mailbox_update_strategy == 2){    
					TRACE(TRACE_DEBUG, "Strategy reload: 2 (differential reload)");
					/* do a diff reload, experimental */
					N = MailboxState_update(self->pool, M);
				}else{
					TRACE(TRACE_DEBUG, "Strategy reload: default (full reload)");
					/* default strategy is full reload, case 1*/
					N = MailboxState_new(self->pool, self->mailbox->id);
			    }
			}
			
			unsigned newexists = MailboxState_getExists(N);
			MailboxState_setExists(N, max(oldexists, newexists));

			// rebuild uid/msn trees
			// ATTN: new messages shouldn't be visible in any way to a 
			// client session until it has been announced with EXISTS

			// EXISTS response may never decrease
			if ((MailboxState_getUidnext(N) > olduidnext)) {
				showexists = TRUE;
			}

			if (MailboxState_getRecent(N))
				showrecent = TRUE;

			newflags = MailboxState_flags(N);
			if (! MATCH(newflags,oldflags))
				showflags = TRUE;
			g_free(newflags);
		}
		g_free(oldflags);
	}

	// command specific overrides
	switch (self->command_type) {
		case IMAP_COMM_EXAMINE:
		case IMAP_COMM_SELECT:
		case IMAP_COMM_SEARCH:
		case IMAP_COMM_SORT:
			showexists = showrecent = TRUE;
		break;

		default:
			// ok show them if needed
		break;
	}

	// never decrease without first sending expunge !!
	if (N) {
		if (showexists && MailboxState_getExists(N)) 
			dbmail_imap_session_buff_printf(self, "* %u EXISTS\r\n", MailboxState_getExists(N));

		if (showrecent && MailboxState_getRecent(N))
			dbmail_imap_session_buff_printf(self, "* %u RECENT\r\n", MailboxState_getRecent(N));

		mailbox_notify_expunge(self, N);

		if (showflags) {
			char *flags = MailboxState_flags(N);
			dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
			dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)] Flags allowed.\r\n", flags);
			g_free(flags);
		}

		mailbox_notify_fetch(self, N);
	}

	return 0;
}

MailboxState_T dbmail_imap_session_mbxinfo_lookup(ImapSession *self, uint64_t mailbox_id)
{
	MailboxState_T M = NULL;
	uint64_t *id;
	if (self->mailbox && self->mailbox->mbstate && (MailboxState_getId(self->mailbox->mbstate) == mailbox_id)) {
		// selected state
		M = self->mailbox->mbstate;
	} else {
		M = (MailboxState_T)g_tree_lookup(self->mbxinfo, &mailbox_id);
		if (! M) {
			id = mempool_pop(small_pool, sizeof(uint64_t));
			*id = mailbox_id;
			M = MailboxState_new(self->pool, mailbox_id);
			g_tree_replace(self->mbxinfo, id, M);
		} else {
			unsigned newseq = 0, oldseq = 0;
			unsigned newexists = 0, oldexists = 0;
			MailboxState_T N = NULL;
			N = MailboxState_new(self->pool, 0);
			MailboxState_setId(N, mailbox_id);
			oldseq = MailboxState_getSeq(M);
			newseq = MailboxState_getSeq(N);
			oldexists = MailboxState_getExists(M);
			MailboxState_free(&N);
			if (oldseq < newseq) {
				id = mempool_pop(small_pool, sizeof(uint64_t));
				*id = mailbox_id;
				M = MailboxState_new(self->pool, mailbox_id);
				newexists = MailboxState_getExists(M);
				MailboxState_setExists(M, max(oldexists, newexists));
				g_tree_replace(self->mbxinfo, id, M);
			}
		}


	}

	assert(M);

	return M;
}

int dbmail_imap_session_set_state(ImapSession *self, ClientState_T state)
{
	ClientState_T current;

	PLOCK(self->lock);
	current = self->state;
	PUNLOCK(self->lock);

	if ((current == state) || (current == CLIENTSTATE_QUIT_QUEUED))
		return 1;

	switch (state) {
		case CLIENTSTATE_ERROR:
			assert(self->ci);
			if (self->ci->wev) event_del(self->ci->wev);
			// fall-through...

		case CLIENTSTATE_LOGOUT:
			assert(self->ci);
			if (self->ci->rev) event_del(self->ci->rev);
			break;

		case CLIENTSTATE_AUTHENTICATED:
			// change from login_timeout to main timeout
			assert(self->ci);
			TRACE(TRACE_DEBUG,"[%p] set timeout to [%d]", self, server_conf->timeout);
			self->ci->timeout.tv_sec = server_conf->timeout; 
			Capa_remove(self->capa, "AUTH=login");
			Capa_remove(self->capa, "AUTH=CRAM-MD5");

			break;

		default:
			break;
	}

	TRACE(TRACE_DEBUG,"[%p] state [%d]->[%d]", self, current, state);
	PLOCK(self->lock);
	self->state = state;
	PUNLOCK(self->lock);

	return 0;
}

static gboolean _do_expunge(uint64_t *id, ImapSession *self)
{
	MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), id);
	assert(msginfo);

	if (! msginfo->flags[IMAP_FLAG_DELETED]) return FALSE;

	if (db_exec(self->c, "UPDATE %smessages SET status=%d WHERE message_idnr=%" PRIu64 " ", DBPFX, MESSAGE_STATUS_DELETE, *id) == DM_EQUERY)
		return TRUE;

	return notify_expunge(self, id);
}

int dbmail_imap_session_mailbox_expunge(ImapSession *self, const char *set, uint64_t *modseq)
{
	uint64_t mailbox_size;
	int i;
	GList *ids;
	GTree *uids = NULL;
	MailboxState_T M = self->mailbox->mbstate;

	if (! (i = g_tree_nnodes(MailboxState_getIds(M))))
		return DM_SUCCESS;

	if (db_get_mailbox_size(self->mailbox->id, 1, &mailbox_size) == DM_EQUERY)
		return DM_EQUERY;

	if (set) {
		uids = dbmail_mailbox_get_set(self->mailbox, set, self->use_uid);
		ids = g_tree_keys(uids);
	} else {
		ids = g_tree_keys(MailboxState_getIds(M));
	}

	ids = g_list_reverse(ids);

	if (ids) {
		self->c = db_con_get();
		db_begin_transaction(self->c);
		g_list_foreach(ids, (GFunc) _do_expunge, self);
		db_commit_transaction(self->c);
		db_con_close(self->c);
		self->c = NULL;
		g_list_free(g_list_first(ids));
	}

	if (uids)
		g_tree_destroy(uids);

	*modseq = 0;
	if (i > g_tree_nnodes(MailboxState_getIds(M))) {
		*modseq = db_mailbox_seq_update(self->mailbox->id, 0);
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
static void _body_fetch_free(body_fetch *bodyfetch, gpointer data)
{
	ImapSession *self = (ImapSession *)data;
	if (! bodyfetch) return;
	if (bodyfetch->names) {
		g_list_free(g_list_first(bodyfetch->names));
		bodyfetch->names = NULL;
	}

	if (bodyfetch->hdrnames) g_free(bodyfetch->hdrnames);
	if (bodyfetch->hdrplist) g_free(bodyfetch->hdrplist);
	if (bodyfetch->headers) {
		g_tree_destroy(bodyfetch->headers);
		bodyfetch->headers = NULL;
	}
	mempool_push(self->pool, bodyfetch, sizeof(body_fetch));
}

void dbmail_imap_session_bodyfetch_free(ImapSession *self) 
{
	List_T first, head = p_list_first(self->fi->bodyfetch);
	first = head;
	while (head) {
		body_fetch *bodyfetch = p_list_data(head);
		_body_fetch_free(bodyfetch, self);
		head = p_list_next(head);
	}
	p_list_free(&first);
	return;
}


/* local defines */
#define NORMPAR 1
#define SQUAREPAR 2
#define NOPAR 0

/*
 * imap4_tokenizer_main()
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

int imap4_tokenizer_main(ImapSession *self, const char *buffer)
{
	int inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i = 0;
	size_t max;
	char parlist[MAX_LINESIZE];
	char *s, *lastchar;

	assert(buffer);

	s = (char *)buffer;
	max = strlen(s);

	if (max < 1)
		goto finalize;


//	assert(max <= MAX_LINESIZE);

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	// if we're not fetching string-literals it's safe to strip NL
	if (self->ci->rbuff_size) {
		assert(max <= self->ci->rbuff_size);

		if (! self->args[self->args_idx])
			self->args[self->args_idx] = p_string_new(self->pool, "");

		p_string_append_len(self->args[self->args_idx], buffer, max);
		self->ci->rbuff_size -= max;
		if (self->ci->rbuff_size == 0) {
			self->args_idx++; // move on to next token
			TRACE(TRACE_DEBUG, "string literal complete. last-char [%c]", s[max-1]);
		}

		return 0;

	} else {
		g_strchomp(s); 
		max = strlen(s);
	}

	TRACE(TRACE_DEBUG,"[%p] tokenize [%" PRIu64 "/%" PRIu64 "] [%s]", self, 
			(uint64_t)max, (uint64_t)self->ci->rbuff_size, s);

	if (self->args[0]) {
		if (MATCH(s, "*")) {
			/* cancel the authentication exchange */
			return -1;
		}

		if (MATCH(p_string_str(self->args[0]),"LOGIN")) {
			uint64_t len;
			char *tmp = dm_base64_decode(s, &len);
			if (! tmp) {
				return -1;
			}
			self->args[self->args_idx++] = p_string_new(self->pool, tmp);
			g_free(tmp);

			if (self->args_idx == 3) {
				/* got password */
				goto finalize;
			} else if (self->args_idx == 2) {
				/* got username, ask for password */
				dbmail_imap_session_prompt(self,"password");
				return 0;
			}
		} else if (MATCH(p_string_str(self->args[0]),"CRAM-MD5")) {
			if (self->args_idx == 1) {
				/* decode and store the response */
				if (! Cram_decode(self->ci->auth, s)) {
					Cram_free(&self->ci->auth);
					return -1;
				}
				self->args_idx++;
				goto finalize; // done
			}
		}
	}

	for (i = 0; (i < max) && s[i] && (self->args_idx < MAX_ARGS - 1); i++) {
		/* check quotes */
		if ((s[i] == '"') && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				self->args[self->args_idx] = p_string_new(self->pool, "");
				p_string_append_len(self->args[self->args_idx], &s[quotestart + 1], i - quotestart - 1);
				TRACE(TRACE_DEBUG, "arg[%" PRIu64 "] [%s]", self->args_idx, p_string_str(self->args[self->args_idx]));
				self->args_idx++;
				inquote = 0;
			} else {
				inquote = 1;
				quotestart = i;
			}
			continue;
		}

		if (inquote) continue;

		//if strchr("[]()",s[i]) {
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

			if (paridx < 0) return -1; /* error in parenthesis structure */
				
			/* add this parenthesis to the arg list and continue */
			self->args[self->args_idx] = p_string_new(self->pool, "");
			p_string_printf(self->args[self->args_idx], "%c", s[i]);
			TRACE(TRACE_DEBUG, "arg[%" PRIu64 "] [%s]", self->args_idx, p_string_str(self->args[self->args_idx]));
			self->args_idx++;
			continue;
		}

		if (s[i] == ' ') continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			unsigned long int octets = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			if (
					(*lastchar == '}' && *(lastchar + 1) == '\0') ||
					(*lastchar == '+' && *(lastchar + 1) == '}' && *(lastchar + 2) == '\0')
			   ) {
				Field_T maxsize;
				unsigned long maxoctets = 0;
				config_get_value("MAX_MESSAGE_SIZE", "IMAP", maxsize);
				if (strlen(maxsize)) {
					maxoctets = strtoul(maxsize, NULL, 0);
					TRACE(TRACE_DEBUG, "using MAX_MESSAGE_SIZE [%s -> %" PRIu64 " octets]", 
							maxsize, (uint64_t)maxoctets);
				}

				if ((maxoctets > 0) && (octets > maxoctets)) {
					dbmail_imap_session_buff_printf(self,
							"%s NO %s failed: message size too large\r\n",
							self->tag, self->command);
					self->command_state = TRUE;
				} else {
					self->ci->rbuff_size += octets;
					if (*lastchar == '}')
						dbmail_imap_session_buff_printf(self, "+ OK\r\n");
				}
				dbmail_imap_session_buff_flush(self);
				return 0;
			}
		}
		/* at an argument start now, walk on until next delimiter
		 * and save argument 
		 */
		for (argstart = i; i < strlen(s) && !strchr(" []()", s[i]); i++) {
			if (s[i] == '"') {
				if (s[i - 1] == '\\')
					continue;
				else
					break;
			}
		}

		self->args[self->args_idx] = p_string_new(self->pool, "");
		p_string_append_len(self->args[self->args_idx], &s[argstart], i - argstart);
		TRACE(TRACE_DEBUG, "arg[%" PRIu64 "] [%s]", self->args_idx, p_string_str(self->args[self->args_idx]));
		self->args_idx++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) return -1; /* error in parenthesis structure */
		
finalize:
	if (self->args_idx == 1) {
		if (Capa_match(self->preauth_capa, "AUTH=LOGIN") && MATCH(p_string_str(self->args[0]),"LOGIN")) {
			TRACE(TRACE_DEBUG, "[%p] prompt for authenticate tokens", self);

			/* ask for username */
			dbmail_imap_session_prompt(self,"username");
			return 0;
		} else if (Capa_match(self->preauth_capa, "AUTH=CRAM-MD5") && MATCH(p_string_str(self->args[0]),"CRAM-MD5")) {
			const gchar *s;
			gchar *t;
			self->ci->auth = Cram_new();
			s = Cram_getChallenge(self->ci->auth);
			t = (char *)g_base64_encode((const guchar *)s, strlen(s));
			dbmail_imap_session_buff_printf(self, "+ %s\r\n", t);
			dbmail_imap_session_buff_flush(self);
			g_free(t);
	
			return 0;
		}
	}


	TRACE(TRACE_DEBUG, "[%p] tag: [%s], command: [%s], [%" PRIu64 "] args", self, self->tag, self->command, self->args_idx);
	self->args[self->args_idx] = NULL;	/* terminate */

#ifdef DEBUG
	for (i = 0; i<=self->args_idx && self->args[i]; i++) { 
		TRACE(TRACE_DEBUG, "[%p] arg[%d]: '%s'\n", self, i, p_string_str(self->args[i])); 
	}
#endif
	self->args_idx = 0;

	return 1;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR



