/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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
 * imapcommands.c
 * 
 * IMAP server command implementations
 */

#include "dbmail.h"
#define THIS_MODULE "imap"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

extern DBParam_T db_params;
#define DBPFX db_params.pfx

extern ServerConfig_T *server_conf;
extern int selfpipe[2];
extern pthread_mutex_t selfpipe_lock;
extern GAsyncQueue *queue;
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];
extern const char AcceptedMailboxnameChars[];

int imap_before_smtp = 0;

struct cmd_t {
	gboolean silent;
	int action;
	int flaglist[IMAP_NFLAGS];
	GList *keywords;
	uint64_t mailbox_id;
	uint64_t seq;
	uint64_t unchangedsince;
};

/* 
 * push a message onto the queue and notify the
 * event-loop by sending a char into the selfpipe
 */

#define SESSION_GET \
	ImapSession *self = D->session

#define SESSION_RETURN \
	D->session->command_state = TRUE; \
	g_async_queue_push(queue, (gpointer)D); \
	PLOCK(selfpipe_lock); \
	if (selfpipe[1] > -1) { \
		if (write(selfpipe[1], "D", 1)) { /* ignore */; } \
	} \
	PUNLOCK(selfpipe_lock); \
	return;

/* Macro for OK answers with optional response code */
#define SESSION_OK_COMMON(RESP_CODE_FORMAT, RESP_CODE_VALUE) \
	if (self->state != CLIENTSTATE_ERROR) \
		dbmail_imap_session_buff_printf(self, \
				"%s OK " RESP_CODE_FORMAT "%s%s completed\r\n", \
				self->tag, RESP_CODE_VALUE, self->use_uid ? "UID " : "", \
				self->command)

/* The original SESSION_OK macro has been refactored to SESSION_OK_COMMON */
#define SESSION_OK \
	SESSION_OK_COMMON("%s", "")

#define SESSION_OK_WITH_RESP_CODE(VALUE) \
	SESSION_OK_COMMON("[%s] ", VALUE)

static void _fetch_update(ImapSession *self, MessageInfo *msginfo, gboolean showmodseq, gboolean showflags);

static int check_state_and_args(ImapSession * self, int minargs, int maxargs, ClientState_T state)
{
	int i;

	if (self->state == CLIENTSTATE_ERROR) return 0;

	/* check state */
	if (state != CLIENTSTATE_ANY) {
		if (self->state != state) {
			if (!  (state == CLIENTSTATE_AUTHENTICATED && self->state == CLIENTSTATE_SELECTED)) {
				dbmail_imap_session_buff_printf(self, "%s BAD %s command received in invalid state [%d] != [%d]\r\n", 
					self->tag, self->command, self->state, state);
				return 0;
			}
		}
	}

	/* check args */
	for (i = 0; i < minargs; i++) {
		if (!self->args[self->args_idx+i]) {
			/* error: need more args */
			dbmail_imap_session_buff_printf(self, "%s BAD missing argument%s to %s\r\n", self->tag, (minargs == 1) ? "" : "(s)", 
					self->command);
			return 0;
		}
	}

	for (i = 0; self->args[self->args_idx+i]; i++);

	if (maxargs && (i > maxargs)) {
		/* error: too many args */
		dbmail_imap_session_buff_printf(self, "%s BAD too many arguments to %s\r\n", 
				self->tag, self->command);
		return 0;
	}

	/* succes */
	return 1;
}



/*
 * RETURN VALUES _ic_ functions:
 *
 * -1 Fatal error, close connection to user
 *  0 Succes
 *  1 Non-fatal error, connection stays alive
 */

/* 
 * ANY-STATE COMMANDS: capability, starttls, noop, logout, id
 */
/*
 * _ic_capability()
 *
 * returns a string to the client containing the server capabilities
 */
// a trivial silly thread example
void _ic_capability_enter(dm_thread_data *D)
{
	SESSION_GET;
	dbmail_imap_session_buff_printf(self, "* %s %s\r\n", self->command, Capa_as_string(self->capa));
	SESSION_OK;
	SESSION_RETURN;
}

int _ic_capability(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_ANY)) return 1;
	dm_thread_data_push((gpointer)self, _ic_capability_enter, _ic_cb_leave, NULL);
	return 0;
}

int _ic_starttls(ImapSession *self)
{
	int i;
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_ANY)) return 1;
	if (! server_conf->ssl) {
		ci_write(self->ci, "%s NO TLS not available\r\n", self->tag);
		return 1;
	}
	if (self->ci->sock->ssl_state) {
		ci_write(self->ci, "%s NO TLS already active\r\n", self->tag);
		return 1;
	}
	ci_write(self->ci, "%s OK Begin TLS now\r\n", self->tag);
	i = ci_starttls(self->ci);
	
	Capa_remove(self->capa, "STARTTLS");
	Capa_remove(self->capa, "LOGINDISABLED");
	
	if (i < 0) i = 0;

	if (i == 0) return 3; /* done */

	return i;
}

/*
 * _ic_noop()
 *
 * performs No operation
 */

void _ic_noop_enter(dm_thread_data *D) 
{
	SESSION_GET;
	if (self->state == CLIENTSTATE_SELECTED)
		dbmail_imap_session_mailbox_status(self, TRUE);
	SESSION_OK;
	SESSION_RETURN;
}


int _ic_noop(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_ANY)) return 1;
	dm_thread_data_push((gpointer)self, _ic_noop_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_logout()
 *
 * prepares logout from IMAP-server
 */
int _ic_logout(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_ANY)) return 1;
	dbmail_imap_session_set_state(self, CLIENTSTATE_LOGOUT);
	if (self->userid)
		TRACE(TRACE_NOTICE, "[%p] userid:[%" PRIu64 "]", self, self->userid);
	return 2;
}

void _ic_id_enter(dm_thread_data *D)
{
	SESSION_GET;
	struct utsname buf;
	memset(&buf, 0, sizeof(buf));
	uname(&buf);
	dbmail_imap_session_buff_printf(self, "* ID (\"name\" \"dbmail\" \"version\" \"%s\""
		" \"os\" \"%s\" \"os-version\" \"%s\")\r\n", DM_VERSION, &buf.sysname, &buf.release);
	SESSION_OK;
	SESSION_RETURN;
}

int _ic_id(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 0, CLIENTSTATE_ANY)) return 1;
	dm_thread_data_push((gpointer)self, _ic_id_enter, _ic_cb_leave, NULL);
	return 0;
}
/*
 * PRE-AUTHENTICATED STATE COMMANDS
 * login, authenticate
 */

/* _ic_login()
 *
 * Performs login-request handling.
 */
int _ic_login(ImapSession *self)
{
	return _ic_authenticate(self);
}


/* _ic_authenticate()
 * 
 * performs authentication using LOGIN mechanism:
 */
void _ic_authenticate_enter(dm_thread_data *D)
{
	int err;
	SESSION_GET;
	const char *username = NULL;
	const char *password = NULL;

	if (self->args[self->args_idx] && self->args[self->args_idx+1]) {
		username = p_string_str(self->args[self->args_idx]);
		password = p_string_str(self->args[self->args_idx+1]);
	}

	if ((err = dbmail_imap_session_handle_auth(self,username,password))) {
		D->status = err;
		SESSION_RETURN;
	}
	if (imap_before_smtp) 
		db_log_ip(self->ci->src_ip);

	if (self->state != CLIENTSTATE_ERROR) {
		if (self->ci->auth)
			username = Cram_getUsername(self->ci->auth);
		dbmail_imap_session_buff_printf(self, 
				"%s OK [CAPABILITY %s] User %s authenticated\r\n", 
				self->tag, Capa_as_string(self->capa), username);
	}
	SESSION_RETURN;
}

int _ic_authenticate(ImapSession *self)
{
	if (self->command_type == IMAP_COMM_AUTH) {
		if (!check_state_and_args(self, 1, 3, CLIENTSTATE_NON_AUTHENTICATED)) return 1;
		/* check authentication method */
		if ( (! MATCH(p_string_str(self->args[self->args_idx]), "login")) && \
			(! MATCH(p_string_str(self->args[self->args_idx]), "cram-md5")) ) {
			dbmail_imap_session_buff_printf(self, "%s NO Invalid authentication mechanism specified\r\n", self->tag);
			return 1;
		}
		self->args_idx++;
	} else {
		if (!check_state_and_args(self, 2, 2, CLIENTSTATE_NON_AUTHENTICATED)) return 1;
	}

	if (Capa_match(self->preauth_capa, "LOGINDISABLED") && (! self->ci->sock->ssl_state)) {
		dbmail_imap_session_buff_printf(self, "%s NO try STARTTLS first\r\n", self->tag);
		return 1;
	}
	dm_thread_data_push((gpointer)self, _ic_authenticate_enter, _ic_cb_leave, NULL);
	return 0;
}


/* 
 * AUTHENTICATED STATE COMMANDS 
 * select, examine, create, delete, rename, subscribe, 
 * unsubscribe, list, lsub, status, append
 */

/* _ic_select()
 * 
 * select a specified mailbox
 */
static gboolean mailbox_first_unseen(gpointer key, gpointer value, gpointer data)
{
	MessageInfo *msginfo = (MessageInfo *)value;
	if (msginfo->flags[IMAP_FLAG_SEEN])
	       	return FALSE;
	*(uint64_t *)data = *(uint64_t *)key;
	return TRUE;
}

static int imap_session_mailbox_close(ImapSession *self)
{
	dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);
	if (self->mailbox) {
		if (self->mailbox->mbstate)
			MailboxState_clear_recent(self->mailbox->mbstate);

		dbmail_mailbox_free(self->mailbox);
		self->mailbox = NULL;
		if (self->enabled.qresync && 
				((self->command_type == IMAP_COMM_SELECT) || \
				(self->command_type == IMAP_COMM_EXAMINE))) {
			dbmail_imap_session_buff_printf(self,
				       	"* OK [CLOSED]\r\n");
		}
	}

	return 0;
}

static int mailbox_check_acl(ImapSession *self, MailboxState_T S, ACLRight acl)
{
	int access = acl_has_right(S, self->userid, acl);
	if (access < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (access == 0) {
		dbmail_imap_session_buff_printf(self, "%s NO permission denied\r\n", self->tag);
		return 1;
	}
	TRACE(TRACE_DEBUG,"access granted");
	return 0;

}


static int imap_session_mailbox_open(ImapSession * self, const char * mailbox)
{
	uint64_t mailbox_idnr = 0;

	/* get the mailbox_idnr */
	if (db_findmailbox(mailbox, self->userid, &mailbox_idnr)) { /* ignored */ }
	
	/* create missing INBOX for this authenticated user */
	if ((! mailbox_idnr ) && (strcasecmp(mailbox, "INBOX")==0)) {
		int err = db_createmailbox("INBOX", self->userid, &mailbox_idnr);
		TRACE(TRACE_INFO, "[%p] [%d] Auto-creating INBOX for user id [%" PRIu64 "]", 
				self, err, self->userid);
	}
	
	if (! mailbox_idnr) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1; /* error */
	}

	/* new mailbox structure */
	self->mailbox = dbmail_mailbox_new(self->pool, mailbox_idnr);

	/* fetch mailbox metadata */
	self->mailbox->mbstate = dbmail_imap_session_mbxinfo_lookup(self, mailbox_idnr);

	/* check if user has right to select mailbox */
	if (mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_READ) == 1) {
		dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);
		return DM_EGENERAL;
	}
	
	if (self->command_type == IMAP_COMM_SELECT) {
		// SELECT
		MailboxState_setPermission(self->mailbox->mbstate,IMAPPERM_READWRITE);
	} else {
		// EXAMINE
		MailboxState_setPermission(self->mailbox->mbstate,IMAPPERM_READ);
	}

	/* check if mailbox is selectable */
	if (MailboxState_noSelect(self->mailbox->mbstate)) 
		return DM_EGENERAL;

	/* build list of recent messages */
	MailboxState_build_recent(self->mailbox->mbstate);

	self->mailbox->condstore = self->enabled.condstore;
	self->mailbox->qresync = self->enabled.qresync;

	return 0;
}

static int validate_arg(const char *arg)
{
	/* check for invalid characters */
	int i;
	const char valid[] = "0123456789,:";
	for (i = 0; arg[i]; i++) {
		if (!strchr(valid, arg[i])) {
			return 1;
		}
	}
	return 0;
}


static int _ic_select_parse_args(ImapSession *self)
{
	int i, idx;
	int paren = 0;
	uint64_t uidvalidity = 0;
	uint64_t modseq = 0;
	char *endptr;
	String_T known_uids = NULL;
	String_T known_seqset = NULL;
	String_T known_uidset = NULL;

	idx = self->args_idx;
	while (self->args[idx++])
		;
	idx--;

	if (idx == 1)
		return 0;

	if (idx == 4) {
		if (Capa_match(self->capa, "CONDSTORE") && \
				(MATCH(p_string_str(self->args[1]),"(")) && \
				(MATCH(p_string_str(self->args[2]), "condstore")) && \
				(MATCH(p_string_str(self->args[3]), ")"))) {
			self->enabled.condstore = true;
			return 0;
		}
	}
	for (i = 1; i < idx; i++) {
		const char *arg = p_string_str(self->args[i]);
		if (MATCH(arg, "(")) {
			paren++;
			continue;
		}
		if (MATCH(arg, ")")) {
			paren--;
			continue;
		}
		switch (i) {
			case 2:
				if (! MATCH(arg, "qresync")) {
					TRACE(TRACE_DEBUG, "unknown argument [%s]", arg);
					return 1;
				}
				break;
			case 4:
				if ((uidvalidity = strtoull(arg, &endptr, 10))) {
					if (*endptr != '\0')
						return 1;
					TRACE(TRACE_DEBUG, "uidvalidity [%" PRIu64 "]", uidvalidity);
				}
				break;
			case 5:
				if ((modseq = strtoull(arg, &endptr, 10))) {
					if (*endptr != '\0')
						return 1;
					TRACE(TRACE_DEBUG, "mod-sequence [%" PRIu64 "]", modseq);
				}
				break;
			case 6:
				if (validate_arg(arg))
					return 1;
				known_uids = self->args[i];
				break;
			case 8:
				if (validate_arg(arg))
					return 1;
				known_seqset = self->args[i];
				break;
			case 9:
				if (validate_arg(arg))
					return 1;
				known_uidset = self->args[i];
				break;
		}
	}

	if (paren) {
		TRACE(TRACE_DEBUG, "unbalanced parenthesis");
		return 1;
	}

	if (known_seqset && (! known_uidset)) {
		TRACE(TRACE_DEBUG, "known uidset missing");
		return 1;
	}

	if (! (uidvalidity && modseq)) {
		TRACE(TRACE_DEBUG, "required arguments missing");
		return 1;
	}

	self->qresync.uidvalidity = uidvalidity;
	self->qresync.modseq = modseq;
	self->qresync.known_uids = known_uids;
	self->qresync.known_seqset = known_seqset;
	self->qresync.known_uidset = known_uidset;

	return 0;
}

static gboolean _do_fetch_updates(uint64_t *id, gpointer UNUSED value, dm_thread_data *D)
{
	ImapSession *self = D->session;
	MessageInfo *msginfo = g_tree_lookup(
			MailboxState_getMsginfo(self->mailbox->mbstate), id);
	if (! msginfo)
		return TRUE;
	
	_fetch_update(self, msginfo, true, true);

	return FALSE;
}

struct expunged_helper {
	GString *response;
	GTree *msgs;
	GTree *expunged;
	GTree *known_seqset;
	GTree *known_uidset;
	qresync_args *qresync;
	uint64_t start_expunged;
	uint64_t last_expunged;
	gboolean prev_expunged;
};

static gboolean _get_expunged(uint64_t *id, gpointer UNUSED value, struct expunged_helper *data)
{
	MessageInfo *msg = g_tree_lookup(data->msgs, id);
	gboolean expunged;

	if (! msg)
		return TRUE;

	//
	if (data->known_uidset && data->known_seqset) {
		uint64_t *msn = NULL, *knownuid = NULL;
		if ((msn = g_tree_lookup(data->known_uidset, id)))
			knownuid = g_tree_lookup(data->known_seqset, msn);

		if (((!msn) || (!knownuid)) || (msn && knownuid && (*id != *knownuid))) 
			return TRUE;
	}

	if ((msg->seq > data->qresync->modseq) && (msg->status == MESSAGE_STATUS_DELETE)) {
		g_tree_insert(data->expunged, id, id);
		expunged = true;
	} else {
		expunged = false;
	}

	if (!expunged) {
		if (data->prev_expunged && (data->start_expunged < data->last_expunged)) {
			g_string_append_printf(data->response, ":%" PRIu64, data->last_expunged);
		}
		data->prev_expunged = expunged;
		return FALSE;
	}

	if (! data->prev_expunged) {
		if (data->last_expunged) {
			g_string_append_printf(data->response, ",%" PRIu64, *id);
		} else {
			data->start_expunged = *id;
			g_string_append_printf(data->response, "%" PRIu64, *id);
		}
	}

	data->last_expunged = *id;
	data->prev_expunged = expunged;

	return FALSE;
}

static GTree *MailboxState_getExpunged(MailboxState_T M, qresync_args *qresync, char **out)
{
	struct expunged_helper data;

	data.response = g_string_new("");
	data.start_expunged = 0;
	data.last_expunged = 0;
	data.prev_expunged = false;
	data.qresync = qresync;
	data.msgs = MailboxState_getMsginfo(M);
	data.expunged = g_tree_new_full((GCompareDataFunc)ucmpdata, NULL, NULL, NULL);
	data.known_seqset = NULL;
	data.known_uidset = NULL;

	if (qresync->known_seqset && qresync->known_uidset) {
		data.known_seqset = MailboxState_get_set(
				M, p_string_str(qresync->known_seqset), FALSE);

		data.known_uidset = MailboxState_get_set(
				M, p_string_str(qresync->known_uidset), TRUE);
	}

	g_tree_foreach(data.msgs, (GTraverseFunc) _get_expunged, &data);

	g_tree_destroy(data.known_seqset);
	g_tree_destroy(data.known_uidset);

	TRACE(TRACE_DEBUG, "vanished [%d] messages", g_tree_nnodes(data.expunged));
	if (data.prev_expunged && (data.start_expunged < data.last_expunged)) {
		g_string_append_printf(data.response, ":%" PRIu64, data.last_expunged);
	}
	
	TRACE(TRACE_DEBUG, "vanished (earlier) %s", data.response->str);

	*out = data.response->str;

	g_string_free(data.response, FALSE);

	return data.expunged;
}
		
static void _ic_select_enter(dm_thread_data *D)
{
	int err;
	char *flags;
	const char *okarg;
	MailboxState_T S;
	SESSION_GET;

	if (_ic_select_parse_args(self)) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid parameter\r\n",
				self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* close the currently opened mailbox */
	imap_session_mailbox_close(self);

	if ((err = imap_session_mailbox_open(self, p_string_str(self->args[self->args_idx])))) {
		D->status = err;
		SESSION_RETURN;
	}	

	dbmail_imap_session_set_state(self,CLIENTSTATE_SELECTED);

	S = self->mailbox->mbstate;

	dbmail_imap_session_buff_printf(self, "* %u EXISTS\r\n", MailboxState_getExists(S));
	dbmail_imap_session_buff_printf(self, "* %u RECENT\r\n", MailboxState_getRecent(S));

	/* flags */
	flags = MailboxState_flags(S);
	dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
	dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)] Flags allowed.\r\n", flags);
	g_free(flags);

	/* UIDNEXT */
	dbmail_imap_session_buff_printf(self, "* OK [UIDNEXT %" PRIu64 "] Predicted next UID\r\n",
			MailboxState_getUidnext(S));
	
	/* UID */
	dbmail_imap_session_buff_printf(self, "* OK [UIDVALIDITY %" PRIu64 "] UID value\r\n",
			MailboxState_getId(S));

	/* MODSEQ */
	if (Capa_match(self->capa, "CONDSTORE")) {
		dbmail_imap_session_buff_printf(self, "* OK [HIGHESTMODSEQ %" PRIu64 "] Highest\r\n",
				MailboxState_getSeq(S));
	}
	/* UNSEEN first element*/
	int command_select_allow_unseen = config_get_value_default_int("command_select_allow_unseen", "IMAP", 1);
	if(self->command_type == IMAP_COMM_SELECT && command_select_allow_unseen == 1){
		if (MailboxState_getExists(S)) { 
			/* show msn of first unseen msg (if present) */
			GTree *uids = MailboxState_getIds(S);
			GTree *info = MailboxState_getMsginfo(S);
			uint64_t key = 0, *msn = NULL;
			g_tree_foreach(info, (GTraverseFunc)mailbox_first_unseen, &key);
			if ( (key > 0) && (msn = g_tree_lookup(uids, &key))) {
				dbmail_imap_session_buff_printf(self, "* OK [UNSEEN %" PRIu64 "] first unseen message\r\n", *msn);
			}
		}
	}
	if (self->command_type == IMAP_COMM_SELECT) {
		okarg = "READ-WRITE";
		MailboxState_flush_recent(S);
	} else {
		okarg = "READ-ONLY";
	}
	
	if (self->qresync.uidvalidity == MailboxState_getId(S)) {
		TRACE(TRACE_DEBUG, "process QRESYNC arguments");
		GTree *changed;
		const char *set;
		gboolean uid = self->use_uid;
		self->mailbox->modseq = self->qresync.modseq;
		self->use_uid = TRUE;
		// report EXPUNGEs
		char *out = NULL;
		GTree *vanished = MailboxState_getExpunged(self->mailbox->mbstate, &self->qresync, &out);
		TRACE(TRACE_DEBUG, "vanished [%u] messages", g_tree_nnodes(vanished));
		if (g_tree_nnodes(vanished))
			dbmail_imap_session_buff_printf(self, "* VANISHED (EARLIER) %s\r\n",
					out);
		g_free(out);
		g_tree_destroy(vanished);

		// report FLAG changes
		if (self->qresync.known_uids) {
			set = p_string_str(self->qresync.known_uids);
		} else {
			set = "1:*";
		}
		changed = dbmail_mailbox_get_set(self->mailbox, set, TRUE);
		TRACE(TRACE_DEBUG, "messages changed since [%" PRIu64 "] [%u]",
				self->mailbox->modseq, g_tree_nnodes(changed));

		g_tree_foreach(changed, (GTraverseFunc) _do_fetch_updates, D);
		g_tree_destroy(changed);
		// done reporting
		self->use_uid = uid;

	}

	dbmail_imap_session_buff_printf(self, "%s OK [%s] %s completed%s\r\n", 
			self->tag, 
			okarg, 
			self->command,
			self->mailbox->condstore?", CONDSTORE is now enabled": "");

	SESSION_RETURN;
}

int _ic_select(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 0, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_select_enter, _ic_cb_leave, NULL);
	return 0;
}


int _ic_examine(ImapSession *self)
{
	return _ic_select(self);
}


static void _ic_enable_enter(dm_thread_data *D)
{
	String_T capability;
	SESSION_GET;

	while ((capability = self->args[self->args_idx++])) {
		const char *s = p_string_str(capability);
		gboolean changed = false;
		if (MATCH(s, "CONDSTORE") || MATCH(s, "QRESYNC")) {
			if (Capa_match(self->capa, s)) {
				if (MATCH(s, "CONDSTORE")) {
					if (! self->enabled.condstore)
						changed = true;
					self->enabled.condstore = 1;
				}
				if (MATCH(s, "QRESYNC")) {
					if (! self->enabled.qresync)
						changed = true;
					self->enabled.qresync = 1;
				}
				if (changed)
					dbmail_imap_session_buff_printf(self, 
							"* ENABLED %s\r\n", s);
			}
		}
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_enable(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 0, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_enable_enter, _ic_cb_leave, NULL);
	return 0;
}


/*
 * _ic_create()
 *
 * create a mailbox
 */

void _ic_create_enter(dm_thread_data *D)
{
	/* Create the mailbox and its parents. */
	int result;
	uint64_t mboxid;
	const char *message;
	SESSION_GET;

	result = db_mailbox_create_with_parents(p_string_str(self->args[self->args_idx]), BOX_IMAP, self->userid, &mboxid, &message);

	if (result > 0)
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, message);
	else if (result < 0)
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
	else
		SESSION_OK;

	SESSION_RETURN;
}


int _ic_create(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_create_enter, _ic_cb_leave, NULL);
	return DM_SUCCESS;
}


/* _ic_delete()
 *
 * deletes a specified mailbox
 */
static int imap_session_mailbox_check_acl(ImapSession * self, uint64_t idnr,  ACLRight acl)
{
	int result;
	MailboxState_T S = dbmail_imap_session_mbxinfo_lookup(self, idnr);
	if ((result = mailbox_check_acl(self, S, acl)) == 1)
		dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);
	return result;
}

void _ic_delete_enter(dm_thread_data *D)
{
	int result;
	uint64_t mailbox_idnr;
	GList *children = NULL;
	SESSION_GET;
	const char *mailbox = p_string_str(self->args[0]);
	unsigned nchildren = 0;
	
	/* check if there is an attempt to delete inbox */
	if (MATCH(mailbox, "INBOX")) {
		dbmail_imap_session_buff_printf(self, "%s NO cannot delete special mailbox INBOX\r\n", self->tag);
		D->status=1;
		SESSION_RETURN;
	}

	if (! (db_findmailbox(mailbox, self->userid, &mailbox_idnr)) ) {
		dbmail_imap_session_buff_printf(self, "%s NO mailbox doesn't exists\r\n", self->tag);
		D->status=1;
		SESSION_RETURN;
	}

	/* Check if the user has ACL delete rights to this mailbox */
	if ((result = imap_session_mailbox_check_acl(self, mailbox_idnr, ACL_RIGHT_DELETE))) {
		D->status=result;
		SESSION_RETURN;
	}
	
	/* check for children of this mailbox */
	if ((result = db_listmailboxchildren(mailbox_idnr, self->userid, &children)) == DM_EQUERY) {
		TRACE(TRACE_ERR, "[%p] cannot retrieve list of mailbox children", self);
		dbmail_imap_session_buff_printf(self, "* BYE dbase/memory error\r\n");
		D->status= -1;
		SESSION_RETURN;
	}

	children = g_list_first(children);
	nchildren = g_list_length(children);
	g_list_destroy(children);

	if (nchildren > 0) {
		TRACE(TRACE_DEBUG, "mailbox has children [%d]", nchildren);
		/* mailbox has inferior names; error if \noselect specified */


		result = db_isselectable(mailbox_idnr);
		if (result == FALSE) {
			dbmail_imap_session_buff_printf(self, "%s NO mailbox is non-selectable\r\n", self->tag);
			SESSION_RETURN;
		}
		if (result == DM_EQUERY) {
			dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
			D->status= -1;	/* fatal */
			SESSION_RETURN;
		}

		/* mailbox has inferior names; remove all msgs and set noselect flag */
		{
			Connection_T c; volatile int t = DM_SUCCESS;
			uint64_t mailbox_size;
			MailboxState_T S = dbmail_imap_session_mbxinfo_lookup(self, mailbox_idnr);

			if (! mailbox_is_writable(mailbox_idnr)) {
				D->status=DM_EQUERY;
				SESSION_RETURN;
			}

			if (db_get_mailbox_size(mailbox_idnr, 0, &mailbox_size) == DM_EQUERY) {
				D->status=DM_EQUERY;
				SESSION_RETURN;
			}

			/* update messages in this mailbox: mark as deleted (status MESSAGE_STATUS_PURGE) */
			c = db_con_get();
			TRY
				db_begin_transaction(c);
				db_exec(c, "UPDATE %smessages SET status=%d WHERE mailbox_idnr = %" PRIu64 "", DBPFX, MESSAGE_STATUS_PURGE, mailbox_idnr);
				db_exec(c, "UPDATE %smailboxes SET no_select = 1 WHERE mailbox_idnr = %" PRIu64 "", DBPFX, mailbox_idnr);
				db_commit_transaction(c);
			CATCH(SQLException)
				LOG_SQLERROR;
				db_rollback_transaction(c);
				t = DM_EQUERY;
			FINALLY
				db_con_close(c);
			END_TRY;

			if (t == DM_EQUERY) {
				D->status=t;
				SESSION_RETURN;
			}

			MailboxState_setNoSelect(S, TRUE);
			db_mailbox_seq_update(mailbox_idnr, 0);
			if (! dm_quota_user_dec(self->userid, mailbox_size)) {
				D->status=DM_EQUERY;
				SESSION_RETURN;
			}
		}

		/* check if this was the currently selected mailbox */
		if (self->mailbox && self->mailbox->mbstate && (mailbox_idnr == MailboxState_getId(self->mailbox->mbstate))) 
			dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);

		/* ok done */
		SESSION_OK;
		SESSION_RETURN;
	}

	/* ok remove mailbox */
	if (db_delete_mailbox(mailbox_idnr, 0, 1)) {
		dbmail_imap_session_buff_printf(self,"%s NO DELETE failed\r\n", self->tag);
		D->status=DM_EGENERAL;
		SESSION_RETURN;
	}

	/* check if this was the currently selected mailbox */
	if (self->mailbox && self->mailbox->mbstate && (mailbox_idnr == MailboxState_getId(self->mailbox->mbstate))) 
		dbmail_imap_session_set_state(self, CLIENTSTATE_AUTHENTICATED);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_delete(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_delete_enter, _ic_cb_leave, NULL);
	return 0;
}
/* _ic_rename()
 *
 * renames a specified mailbox
 */
static int mailbox_rename(MailboxState_T M, const char *newname)
{
	if ( (db_setmailboxname(MailboxState_getId(M), newname)) == DM_EQUERY) return DM_EQUERY;
	MailboxState_setName(M, newname);
	return DM_SUCCESS;
}
void _ic_rename_enter(dm_thread_data *D)
{
	SESSION_GET;
	GList *children = NULL;
	uint64_t mboxid, newmboxid;
	uint64_t parentmboxid = 0;
	size_t oldnamelen;
	int i, result;
	MailboxState_T M;
	const char *oldname, *newname;

	oldname = p_string_str(self->args[0]);
	newname = p_string_str(self->args[1]);


	if (! (db_findmailbox(oldname, self->userid, &mboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check if new name is valid */
        if (!checkmailboxname(newname)) {
	        dbmail_imap_session_buff_printf(self, "%s NO new mailbox name contains invalid characters\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
        }

	if ((db_findmailbox(newname, self->userid, &newmboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO new mailbox already exists\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	oldnamelen = strlen(oldname);

	/* check if new name would invade structure as in
	 * test (exists)
	 * rename test test/testing
	 * would create test/testing but delete test
	 */
	if (strncasecmp(oldname, newname, (int) oldnamelen) == 0 &&
	    strlen(newname) > oldnamelen && newname[oldnamelen] == '/') {
		dbmail_imap_session_buff_printf(self, "%s NO new mailbox would invade mailbox structure\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check if structure of new name is valid */
	/* i.e. only last part (after last '/' can be nonexistent) */
	for (i = strlen(newname) - 1; i >= 0 && newname[i] != '/'; i--);
	char tmpname[IMAP_MAX_MAILBOX_NAMELEN];
	memset(tmpname, 0, sizeof(tmpname));
	g_strlcpy(tmpname, newname, IMAP_MAX_MAILBOX_NAMELEN);
	if (i >= 0) {
		tmpname[i] = '\0';	/* note: original char was '/' */

		if (! db_findmailbox(tmpname, self->userid, &parentmboxid)) {
			/* parent mailbox does not exist */
			dbmail_imap_session_buff_printf(self, "%s NO new mailbox would invade mailbox structure\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}

	/* Check if the user has ACL delete rights to old name, 
	 * and create rights to the parent of the new name, or
	 * if the user just owns both mailboxes. */
	if ((result = imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_DELETE))) {
		D->status = result;
		SESSION_RETURN;
	}

	if (!parentmboxid) {
		TRACE(TRACE_DEBUG, "[%p] Destination is a top-level mailbox; not checking right to CREATE.", self);
	} else {
		TRACE(TRACE_DEBUG, "[%p] Checking right to CREATE under [%" PRIu64 "]", self, parentmboxid);
		if ((result = imap_session_mailbox_check_acl(self, parentmboxid, ACL_RIGHT_CREATE))) {
			D->status = result;
			SESSION_RETURN;
		}

		TRACE(TRACE_DEBUG, "[%p] We have the right to CREATE under [%" PRIu64 "]", self, parentmboxid);
	}

	/* check if it is INBOX to be renamed */
	if (MATCH(oldname, "INBOX")) {
		/* ok, renaming inbox */
		/* this means creating a new mailbox and moving all the INBOX msgs to the new mailbox */
		/* inferior names of INBOX are left unchanged */
		result = db_createmailbox(newname, self->userid, &newmboxid);
		if (result == -1) {
			dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
			D->status = DM_EQUERY;
			SESSION_RETURN;
		}

		result = db_movemsg(newmboxid, mboxid);
		if (result == -1) {
			dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
			D->status = result;
			SESSION_RETURN;
		}

		SESSION_OK;
		SESSION_RETURN;
	}

	/* check for inferior names */
	result = db_listmailboxchildren(mboxid, self->userid, &children);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = result;
		SESSION_RETURN;
	}

	/* replace name for each child */
	children = g_list_first(children);
	while (children) {
		char realnewname[IMAP_MAX_MAILBOX_NAMELEN];
		uint64_t childid = *(uint64_t *)children->data;
		const char *tname;
		M = dbmail_imap_session_mbxinfo_lookup(self, childid);
		tname = MailboxState_getName(M);

		g_snprintf(realnewname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s", newname, &tname[oldnamelen]);
		if ((mailbox_rename(M, realnewname)) != DM_SUCCESS) {
			dbmail_imap_session_buff_printf(self, "* BYE error renaming mailbox\r\n");
			g_list_destroy(children);
			D->status = DM_EGENERAL;
			SESSION_RETURN;
		}
		if (! g_list_next(children)) break;
		children = g_list_next(children);
	}

	if (children) g_list_destroy(children);

	/* now replace name */
	M = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	if ((mailbox_rename(M, newname)) != DM_SUCCESS) {
		dbmail_imap_session_buff_printf(self, "* BYE error renaming mailbox\r\n");
		D->status = DM_EGENERAL;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_rename(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_rename_enter, _ic_cb_leave, NULL);
	return 0;
}



/*
 * _ic_subscribe()
 *
 * subscribe to a specified mailbox
 */
void _ic_subscribe_enter(dm_thread_data *D)
{
	SESSION_GET;
	uint64_t mboxid;
	int result = 0;
	const char *mailbox = p_string_str(self->args[0]);
	
	if (! (db_findmailbox(mailbox, self->userid, &mboxid))) {
		dbmail_imap_session_buff_printf(self, "%s OK %s on mailbox that does not exist\r\n", self->tag, self->command);
		SESSION_RETURN;
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */

	if (imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_LOOKUP)) {
		D->status = 1;
		SESSION_RETURN;
	}

	if (self->command_type == IMAP_COMM_SUBSCRIBE) {
		result = db_subscribe(mboxid, self->userid);
	} else {
		result = db_unsubscribe(mboxid, self->userid);
	}

	if (result == DM_EQUERY) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = DM_EQUERY;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;

}
int _ic_subscribe(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_subscribe_enter, _ic_cb_leave, NULL);
	return 0;
}

int _ic_unsubscribe(ImapSession *self)
{
	return _ic_subscribe(self);
}

/**
 * Write out one element of found folders (contains found hierarchy too)
 *
 * This is called for each found folder in a loop.
 */
static gboolean _ic_list_write_out_found_folder(gpointer UNUSED key, MailboxState_T M, ImapSession *self)
{
	GList *plist = NULL;
	char *pstring = NULL;
	if (MailboxState_noSelect(M))
		plist = g_list_append(plist, "\\noselect");
	if (MailboxState_noInferiors(M))
		plist = g_list_append(plist, "\\noinferiors");
	if (MailboxState_noChildren(M))
		plist = g_list_append(plist, "\\hasnochildren");
	else
		plist = g_list_append(plist, "\\haschildren");

	/* show */
	pstring = dbmail_imap_plist_as_string(plist);
	dbmail_imap_session_buff_printf(self, "* %s %s \"%s\" \"%s\"\r\n", self->command,
			pstring, MAILBOX_SEPARATOR, MailboxState_getName(M));

	g_list_free(g_list_first(plist));
	g_free(pstring);

	return FALSE;
}

void free_mailboxstate(void *data)
{
	MailboxState_T M = (MailboxState_T)data;
	MailboxState_free(&M);
}

/*
 * _ic_list()
 *
 * executes a list command
 */
void _ic_list_enter(dm_thread_data *D)
{
	SESSION_GET;
	int list_is_lsub = 0;
	GList *children = NULL;
	// store the found real folders
	GTree *found_folders = NULL;
	// found hierarchy elements should have lower priority than real folders
	// that's why store them separately in another GTree
	// this is to not to let them mask out real folders if they are found first
	GTree *found_hierarchy = NULL;
	MailboxState_T M = NULL;
	unsigned i;
	char pattern[255];
	char mailbox[IMAP_MAX_MAILBOX_NAMELEN];
	const char *refname;

	/* check if self->args are both empty strings, i.e. A001 LIST "" "" 
	   this has special meaning; show root & delimiter */
	if (p_string_len(self->args[0]) == 0 && p_string_len(self->args[1]) == 0) {
		dbmail_imap_session_buff_printf(self, "* %s (\\NoSelect) \"/\" \"\"\r\n", self->command);
		SESSION_OK;
		SESSION_RETURN;
	}

	/* check the reference name, should contain only accepted mailboxname chars */
	refname = p_string_str(self->args[0]);
	for (i = 0; refname[i]; i++) {
		if (index(AcceptedMailboxnameChars, refname[i]) == NULL) {
			dbmail_imap_session_buff_printf(self, "%s BAD reference name contains invalid characters\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}
	memset(pattern, 0, sizeof(pattern));
	g_strlcat(pattern, refname, 255);
	g_strlcat(pattern, p_string_str(self->args[1]), 255);

	TRACE(TRACE_INFO, "[%p] search with pattern: [%s]", self, pattern);

	if (self->command_type == IMAP_COMM_LSUB) list_is_lsub = 1;

	D->status = db_findmailbox_by_regex(self->userid, pattern, &children, list_is_lsub);
	if (D->status == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		SESSION_RETURN;
	} else if (D->status == 1) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid pattern specified\r\n", self->tag);
		SESSION_RETURN;
	}

	found_folders = g_tree_new_full((GCompareDataFunc)dm_strcmpdata,NULL,g_free,free_mailboxstate);
	found_hierarchy = g_tree_new_full((GCompareDataFunc)dm_strcmpdata,NULL,g_free,free_mailboxstate);

	while ((! D->status) && children) {
		gboolean show = FALSE;
		// determine whether the found element is part of a hierarchy
		// if yes, it will be added to a separate tree (to have lower priority)
		gboolean hierarchy_element = FALSE;

		memset(&mailbox, 0, IMAP_MAX_MAILBOX_NAMELEN);

		uint64_t mailbox_id = *(uint64_t *)children->data;
		if ( (D->status = db_getmailboxname(mailbox_id, self->userid, mailbox)) != DM_SUCCESS) {
			break;
		}

		// avoid fully loading mailbox here
		M = MailboxState_new(self->pool, 0);
		MailboxState_setId(M, mailbox_id);
		MailboxState_info(M);
		MailboxState_setName(M, mailbox);

		/* Enforce match of mailbox to pattern. */
		TRACE(TRACE_DEBUG,"test if [%s] matches [%s]", mailbox, pattern);
		if (! listex_match(pattern, mailbox, MAILBOX_SEPARATOR, 0)) {
			TRACE(TRACE_DEBUG,"mailbox [%s] doesn't match pattern [%s]", mailbox, pattern);
			if (g_str_has_suffix(pattern,"%")) {
				TRACE(TRACE_DEBUG,"searching for matching hierarchy");
				/*
				   If the "%" wildcard is the last character of a mailbox name argument, matching levels
				   of hierarchy are also returned.  If these levels of hierarchy are not also selectable 
				   mailboxes, they are returned with the \Noselect mailbox name attribute
				   */

				// split mailbox name by delimiters
				char *m = NULL, **p = g_strsplit(mailbox,MAILBOX_SEPARATOR,0);
				int l = g_strv_length(p);
				// cut each part off from the name in each iteration
				// and check whether that partial name matches
				// that indicates a matching hierarchy
				while (l > 1) {
					if (p[l]) {
						g_free(p[l]);
						p[l] = NULL;
					}
					m = g_strjoinv(MAILBOX_SEPARATOR, p);
					if (listex_match(pattern, m, MAILBOX_SEPARATOR, 0)) {
						TRACE(TRACE_DEBUG,"partial hierarchy [%s] matches [%s]", m, pattern);

						MailboxState_setName(M, m);
						MailboxState_setNoSelect(M, TRUE);
						MailboxState_setNoChildren(M, FALSE);
						show = TRUE;
						hierarchy_element = TRUE;
						g_free(m);
						break;
					} else {
						TRACE(TRACE_DEBUG,"partial hierarchy [%s] doesn't match [%s]", m, pattern);
					}
					g_free(m);
					l--;
				}
				g_strfreev(p);
			}
		} else {
			TRACE(TRACE_DEBUG,"[%s] does match [%s]", mailbox, pattern);
			show = TRUE;
		}

		// add the result if either:
		//  - it's not a hierarchy element (so real folder) AND it cannot be found among real folders
		//  - it's a hierarchy element AND it cannot be found among hierarchy elements
		if (show && MailboxState_getName(M)) {
			char *s = g_strdup(MailboxState_getName(M));
			if (! hierarchy_element)
				g_tree_insert(found_folders, (gpointer)s, M);
			else 
				g_tree_insert(found_hierarchy, (gpointer)s, M);
		} else {
			MailboxState_free(&M);
		}

		if (! g_list_next(children)) break;
		children = g_list_next(children);
	}

	TRACE(TRACE_DEBUG,"copying found hierarchy to found_folders");
	g_tree_merge(found_folders, found_hierarchy, IST_SUBSEARCH_OR);

	TRACE(TRACE_DEBUG,"writing out found_folders");
	g_tree_foreach(found_folders, (GTraverseFunc)_ic_list_write_out_found_folder, self);

	if (found_hierarchy) g_tree_destroy(found_hierarchy);
	if (found_folders) g_tree_destroy(found_folders);
	if (children) g_list_destroy(children);

	if (! D->status) dbmail_imap_session_buff_printf(self, "%s OK %s completed\r\n", self->tag, self->command);

	SESSION_RETURN;
}

int _ic_list(ImapSession *self)
{

	if (!check_state_and_args(self, 2, 2, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_list_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_lsub()
 *
 * list subscribed mailboxes
 */
int _ic_lsub(ImapSession *self)
{
	return _ic_list(self);
}


/*
 * _ic_status()
 *
 * inquire the status of a mailbox
 */
static void _ic_status_enter(dm_thread_data *D)
{
	SESSION_GET;
	MailboxState_T M;
	uint64_t id;
	int i, endfound, result;
	GList *plst = NULL;
	gchar *pstring, *astring;
	
	if (p_string_str(self->args[1])[0] != '(') {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list should be parenthesed\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check final arg: should be ')' and no new '(' in between */
	for (i = 2, endfound = 0; self->args[i]; i++) {
		if (p_string_str(self->args[i])[0] == ')') {
			endfound = i;
			break;
		} else if (p_string_str(self->args[i])[0] == '(') {
			dbmail_imap_session_buff_printf(self, "%s BAD too many parentheses specified\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}

	if (endfound == 2) {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list empty\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}
	if (self->args[endfound + 1]) {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list too long\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check if mailbox exists */
	if (! db_findmailbox(p_string_str(self->args[0]), self->userid, &id)) {
		/* create missing INBOX for this authenticated user */
		if ((! id ) && (MATCH(p_string_str(self->args[0]), "INBOX"))) {
			TRACE(TRACE_INFO, "[%p] Auto-creating INBOX for user id [%" PRIu64 "]", self, self->userid);
			db_createmailbox("INBOX", self->userid, &id);
		}
		if (! id) {
			dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}

	// avoid fully loading mailbox here
	M = MailboxState_new(self->pool, 0);
	MailboxState_setId(M, id);
	if (MailboxState_info(M)) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		D->status = 1;
		MailboxState_free(&M);
		SESSION_RETURN;
	}

	MailboxState_setName(M, p_string_str(self->args[0]));
	MailboxState_count(M);

	if ((result = mailbox_check_acl(self, M, ACL_RIGHT_READ))) {
		D->status = result;
		MailboxState_free(&M);
		SESSION_RETURN;
	}

	for (i = 2; self->args[i]; i++) {
		const char *attr = p_string_str(self->args[i]);
		if (MATCH(attr, "messages"))
			plst = g_list_append_printf(plst,"MESSAGES %u", MailboxState_getExists(M));
		else if (MATCH(attr, "recent"))
			plst = g_list_append_printf(plst,"RECENT %u", MailboxState_getRecent(M));
		else if (MATCH(attr, "unseen"))
			plst = g_list_append_printf(plst,"UNSEEN %u", MailboxState_getUnseen(M));
		else if (MATCH(attr, "uidnext"))
			plst = g_list_append_printf(plst,"UIDNEXT %" PRIu64 "", MailboxState_getUidnext(M));
		else if (MATCH(attr, "uidvalidity"))
			plst = g_list_append_printf(plst,"UIDVALIDITY %" PRIu64 "", MailboxState_getId(M));
		else if (Capa_match(self->capa, "CONDSTORE") && MATCH(attr, "highestmodseq")) {
			plst = g_list_append_printf(plst,"HIGHESTMODSEQ %" PRIu64, MailboxState_getSeq(M));
			self->enabled.condstore = true;
		}
		else if (MATCH(attr, ")"))
			break;
		else {
			dbmail_imap_session_buff_printf(self, "\r\n%s BAD option '%s' specified\r\n",
				self->tag, attr);
			D->status = 1;
			MailboxState_free(&M);
			SESSION_RETURN;
		}
	}
	astring = dbmail_imap_astring_as_string(p_string_str(self->args[0]));
	pstring = dbmail_imap_plist_as_string(plst); 
	g_list_destroy(plst);

	dbmail_imap_session_buff_printf(self, "* STATUS %s %s\r\n", astring, pstring);	
	g_free(astring); g_free(pstring);
	MailboxState_free(&M);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_status(ImapSession *self)
{
	if (!check_state_and_args(self, 3, 0, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_status_enter, _ic_cb_leave, NULL);
	return 0;
}

/* _ic_idle
 *
 */

#define IDLE_TIMEOUT 30
int _ic_idle(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_AUTHENTICATED)) return 1;

	int idle_timeout = IDLE_TIMEOUT;
	Field_T val;

	ci_cork(self->ci);
	GETCONFIGVALUE("idle_timeout", "IMAP", val);
	if ( strlen(val) && (idle_timeout = atoi(val)) <= 0 ) {
		TRACE(TRACE_ERR, "[%p] illegal value for idle_timeout [%s]", self, val);
		idle_timeout = IDLE_TIMEOUT;	
	}
	
	TRACE(TRACE_DEBUG,"[%p] start IDLE [%s]", self, self->tag);
	self->command_state = IDLE;
	dbmail_imap_session_buff_printf(self, "+ idling\r\n");
	dbmail_imap_session_mailbox_status(self,TRUE);
	dbmail_imap_session_buff_flush(self);

	self->ci->timeout.tv_sec = idle_timeout;
	ci_uncork(self->ci);

	return 0;
}

/* _ic_append()
 *
 * append a message to a mailbox
 */

void _ic_append_enter(dm_thread_data *D)
{
	uint64_t mboxid, message_id = 0;
	int i, j, result;
	const char *internal_date = NULL;
	int flaglist[IMAP_NFLAGS], flagcount = 0;
	GList *keywords = NULL;
	MailboxState_T M;
	SESSION_GET;
	const char *message;
	gboolean recent = TRUE;
	MessageInfo *info;

	memset(flaglist,0,sizeof(flaglist));

	/* find the mailbox to place the message */
	if (! db_findmailbox(p_string_str(self->args[0]), self->userid, &mboxid)) {
		if ((strcasecmp(p_string_str(self->args[0]), "INBOX")==0)) {
			int err = db_createmailbox("INBOX", self->userid, &mboxid);
			TRACE(TRACE_INFO, "[%p] [%d] Auto-creating INBOX for user id [%" PRIu64 "]", 
					self, err, self->userid);
		}

		if (! mboxid) {
			dbmail_imap_session_buff_printf(self, "%s NO [TRYCREATE]\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}

	M = dbmail_imap_session_mbxinfo_lookup(self, mboxid);

	/* check if user has right to append to  mailbox */
	if ((result = imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_INSERT))) {
		D->status = result;
		SESSION_RETURN;
	}

	i = 1;

	/* check if a flag list has been specified */
	if (p_string_str(self->args[i])[0] == '(') {
		/* ok fetch the flags specified */
		TRACE(TRACE_DEBUG, "[%p] flag list found:", self);

		i++;
		while (self->args[i] && p_string_str(self->args[i])[0] != ')') {
			const char *arg = p_string_str(self->args[i]);
			TRACE(TRACE_DEBUG, "[%p] [%s]", self, arg);
			for (j = 0; j < IMAP_NFLAGS; j++) {
				if (MATCH(arg, imap_flag_desc_escaped[j])) {
					flaglist[j] = 1;
					flagcount++;
					break;
				}
			}
			if (j == IMAP_NFLAGS) {
				TRACE(TRACE_DEBUG,"[%p] found keyword [%s]", self, arg);
				keywords = g_list_append(keywords,g_strdup(arg));
				flagcount++;
			}

			i++;
		}

		i++;
		TRACE(TRACE_DEBUG, "[%p] )", self);
	}

	if (!self->args[i]) {
		TRACE(TRACE_INFO, "[%p] unexpected end of arguments", self);
		dbmail_imap_session_buff_printf(self, "%s BAD invalid arguments specified to APPEND\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/** check ACL's for STORE */
	if (flaglist[IMAP_FLAG_SEEN] == 1) {
		if ((result = mailbox_check_acl(self, M, ACL_RIGHT_SEEN))) {
			D->status = result;
			SESSION_RETURN;
		}
	}
	if (flaglist[IMAP_FLAG_DELETED] == 1) {
		if ((result = mailbox_check_acl(self, M, ACL_RIGHT_DELETED))) {
			D->status = result;
			SESSION_RETURN;
		}
	}
	if (flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    flaglist[IMAP_FLAG_RECENT] == 1 ||
	    flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    g_list_length(keywords) > 0) {
		if ((result = mailbox_check_acl(self, M, ACL_RIGHT_WRITE))) {
			D->status = result;
			SESSION_RETURN;
		}
	}


	/* there could be a literal date here, check if the next argument exists
	 * if so, assume this is the literal date.
	 */
	if (self->args[i + 1]) {
		internal_date = p_string_str(self->args[i]);
		i++;
		TRACE(TRACE_DEBUG, "[%p] internal date [%s] found, next arg [%s]",
				self, internal_date, p_string_str(self->args[i]));
	}

	if (self->state == CLIENTSTATE_SELECTED && self->mailbox->id == mboxid) {
		recent = FALSE;
	}
	
	message = p_string_str(self->args[i]);

	D->status = db_append_msg(message, mboxid, self->userid, internal_date, &message_id, recent);

	switch (D->status) {
	case -1:
		TRACE(TRACE_ERR, "[%p] error appending msg", self);
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error storing message\r\n");
		D->status=1;
		SESSION_RETURN;
		break;

	case -2:
		TRACE(TRACE_INFO, "[%p] quotum would exceed", self);
		dbmail_imap_session_buff_printf(self, "%s NO not enough quotum left\r\n", self->tag);
		D->status=1;
		SESSION_RETURN;
		break;

	case TRUE:
		TRACE(TRACE_ERR, "[%p] faulty msg", self);
		dbmail_imap_session_buff_printf(self, "%s NO invalid message specified\r\n", self->tag);
		SESSION_RETURN;
		break;
	case FALSE:
		if (flagcount) {
			if (db_set_msgflag(message_id, flaglist, keywords, IMAPFA_ADD, 0, NULL) < 0)
				TRACE(TRACE_ERR, "[%p] error setting flags for message [%" PRIu64 "]", self, message_id);
			else
				db_mailbox_seq_update(mboxid, message_id);
		}
		break;
	}

	if (message_id && self->state == CLIENTSTATE_SELECTED && self->mailbox->id == mboxid) {
		dbmail_imap_session_mailbox_status(self, TRUE);
	}

	// MessageInfo
	info = g_new0(MessageInfo,1);
	info->uid = message_id;
	info->mailbox_id = mboxid;
	for (flagcount = 0; flagcount < IMAP_NFLAGS; flagcount++)
		info->flags[flagcount] = flaglist[flagcount];
	info->flags[IMAP_FLAG_RECENT] = 1;
	strncpy(info->internaldate, 
			internal_date?internal_date:"01-Jan-1970 00:00:01 +0100",
		       	IMAP_INTERNALDATE_LEN-1);
	info->rfcsize = strlen(message);
	info->keywords = keywords;

	M = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	MailboxState_addMsginfo(M, message_id, info);

	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	g_snprintf(buffer, 1023, "APPENDUID %" PRIu64 " %" PRIu64, mboxid, message_id);
	SESSION_OK_WITH_RESP_CODE(buffer);
	SESSION_RETURN;
}

int _ic_append(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 0, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_append_enter, _ic_cb_leave, NULL);
	return 0;
}

/* 
 * SELECTED-STATE COMMANDS 
 * sort, check, close, expunge, search, fetch, store, copy, uid
 */

/* _ic_check()
 * 
 * request a checkpoint for the selected mailbox
 * (equivalent to NOOP)
 */
static void _ic_check_enter(dm_thread_data *D)
{
	SESSION_GET;
	dbmail_imap_session_mailbox_status(self, TRUE);
	SESSION_OK;
	SESSION_RETURN;
}

int _ic_check(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_check_enter, _ic_cb_leave, NULL);
	return 0;
}


/* _ic_close()
 *
 * expunge deleted messages from selected mailbox & return to AUTH state
 * do not show expunge-output
 */
static void _ic_close_enter(dm_thread_data *D)
{
	SESSION_GET;
	int result = acl_has_right(self->mailbox->mbstate, self->userid, ACL_RIGHT_EXPUNGE);
	uint64_t modseq = 0;
	if (result < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE Internal database error\r\n");
		D->status=result;
		SESSION_RETURN;
	}
	/* only perform the expunge if the user has the right to do it */
	if (result == 1) {
		if (MailboxState_getPermission(self->mailbox->mbstate) == IMAPPERM_READWRITE)
			dbmail_imap_session_mailbox_expunge(self, NULL, &modseq);
		imap_session_mailbox_close(self);
	}

	dbmail_imap_session_set_state(self, CLIENTSTATE_AUTHENTICATED);

	if (self->enabled.qresync && modseq) {
		char *response = g_strdup_printf("HIGHESTMODSEQ %" PRIu64, modseq);
		SESSION_OK_WITH_RESP_CODE(response);
		g_free(response);
	} else {
		SESSION_OK;
	}

	SESSION_RETURN;
}
	
int _ic_close(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_close_enter, _ic_cb_leave, NULL);
	return 0;
}

/* _ic_unselect
 *
 * non-expunging close for select mailbox and return to AUTH state
 */
static void _ic_unselect_enter(dm_thread_data *D)
{
	SESSION_GET;
	imap_session_mailbox_close(self);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_unselect(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_unselect_enter, _ic_cb_leave, NULL);
	return 0;
}

/* _ic_expunge()
 *
 * expunge deleted messages from selected mailbox
 * show expunge output per message
 */
	
static void _ic_expunge_enter(dm_thread_data *D)
{
	const char *set = NULL;
	int result;
	uint64_t modseq = 0;
	SESSION_GET;

	if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_EXPUNGE))) {
		D->status = result;
		SESSION_RETURN;
	}
	
	if (self->use_uid)
		set = p_string_str(self->args[self->args_idx]);

	if (dbmail_imap_session_mailbox_expunge(self, set, &modseq) != DM_SUCCESS) {
		dbmail_imap_session_buff_printf(self, "* BYE expunge failed\r\n");
		D->status = DM_EQUERY;
		SESSION_RETURN;
	}

	if (self->enabled.qresync && modseq) {
		char *response = g_strdup_printf("HIGHESTMODSEQ %" PRIu64, modseq);
		SESSION_OK_WITH_RESP_CODE(response);
		g_free(response);
	} else {
		SESSION_OK;
	}
	SESSION_RETURN;
}

int _ic_expunge(ImapSession *self)
{
	if (self->use_uid) {
		if (!check_state_and_args(self, 1, 1, CLIENTSTATE_SELECTED)) return 1;
	} else {
		if (!check_state_and_args(self, 0, 0, CLIENTSTATE_SELECTED)) return 1;
	}

	if (MailboxState_getPermission(self->mailbox->mbstate) != IMAPPERM_READWRITE) {
		dbmail_imap_session_buff_printf(self, "%s NO you do not have write permission on this folder\r\n", self->tag);
		return 1;
	}

	dm_thread_data_push((gpointer)self, _ic_expunge_enter, _ic_cb_leave, NULL);
	return 0;
}


/*
 * _ic_search()
 *
 * search the selected mailbox for messages
 *
 */
static void sorted_search_enter(dm_thread_data *D)
{
	SESSION_GET;
	DbmailMailbox *mb;
	int result = 0;
	gchar *s = NULL;
	gchar *imap_modseq = NULL;
	const gchar *cmd;

	search_order order = self->order;

	if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_READ))) {
		D->status = result;
		SESSION_RETURN;
	}

	if (self->state == CLIENTSTATE_SELECTED)
		dbmail_imap_session_mailbox_status(self, TRUE);

	mb = self->mailbox;
	switch(order) {
		case SEARCH_SORTED:
			cmd = "SORT";
			break;
		case SEARCH_UNORDERED:
			cmd = "SEARCH";
			break;
		case SEARCH_THREAD_REFERENCES:
		case SEARCH_THREAD_ORDEREDSUBJECT:
			cmd = "THREAD";
			break;
		default:// shouldn't happen
			cmd = "NO";
			break;
	}

	if (MailboxState_getExists(mb->mbstate) > 0) {
		dbmail_mailbox_set_uid(mb,self->use_uid);

		if (dbmail_mailbox_build_imap_search(mb, self->args, &(self->args_idx), order) < 0) {
			dbmail_imap_session_buff_printf(self, "%s BAD invalid arguments to %s\r\n",
				self->tag, cmd);
			D->status = 1;
			SESSION_RETURN;
		}
		dbmail_mailbox_search(mb);
		/* ok, display results */
		switch(order) {
			case SEARCH_SORTED:
				dbmail_mailbox_sort(mb);
				s = dbmail_mailbox_sorted_as_string(mb);
			break;
			case SEARCH_UNORDERED:
				s = dbmail_mailbox_ids_as_string(mb, FALSE, " ");
				imap_modseq = dbmail_mailbox_imap_modseq_as_string(mb, FALSE);
				s = g_strconcat(s, imap_modseq, NULL);
				g_free(imap_modseq);
			break;
			case SEARCH_THREAD_ORDEREDSUBJECT:
				s = dbmail_mailbox_orderedsubject(mb);
			break;
			case SEARCH_THREAD_REFERENCES:
				s = NULL; // TODO: unsupported
			break;
		}
	} else {
		TRACE(TRACE_DEBUG, "empty mailbox?");
	}

	if (s) {
		dbmail_imap_session_buff_printf(self, "* %s %s\r\n", cmd, s);
		g_free(s);
	} else {
		dbmail_imap_session_buff_printf(self, "* %s\r\n", cmd);
	}

	SESSION_OK;
	SESSION_RETURN;
}

static int sorted_search(ImapSession *self, search_order order)
{
	if (!check_state_and_args(self, 1, 0, CLIENTSTATE_SELECTED)) return 1;
	self->order = order;
	dm_thread_data_push((gpointer)self, sorted_search_enter, _ic_cb_leave, NULL);
	return 0;
}

int _ic_search(ImapSession *self)
{
	return sorted_search(self,SEARCH_UNORDERED);
}

int _ic_sort(ImapSession *self)
{
	return sorted_search(self,SEARCH_SORTED);
}

int _ic_thread(ImapSession *self)
{
	if (MATCH(p_string_str(self->args[self->args_idx]),"ORDEREDSUBJECT"))
		return sorted_search(self,SEARCH_THREAD_ORDEREDSUBJECT);
	if (MATCH(p_string_str(self->args[self->args_idx]),"REFERENCES"))
		dbmail_imap_session_buff_printf(self, "%s BAD THREAD=REFERENCES not supported\r\n",self->tag);
		//return sorted_search(self,SEARCH_THREAD_REFERENCES);

	return 1;
}

int _dm_imapsession_get_ids(ImapSession *self, const char *set)
{
	gboolean found = FALSE;
	
	dbmail_mailbox_set_uid(self->mailbox,self->use_uid);

	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}

	self->ids = dbmail_mailbox_get_set(self->mailbox, set, self->use_uid);

	found = ( self->ids && (g_tree_nnodes(self->ids) > 0) );

	if ( (! self->use_uid) && (! found)) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid sequence in msn set [%s]\r\n", self->tag, set);
		return DM_EGENERAL;
	}
	
	if (self->use_uid && (! self->ids)) { // empty tree IS valid
		dbmail_imap_session_buff_printf(self, "%s BAD invalid sequence in uid set [%s]\r\n", self->tag, set);
		return DM_EGENERAL;
	}

	return DM_SUCCESS;
}



/*
 * _ic_fetch()
 *
 * fetch message(s) from the selected mailbox
 */
	

static void _ic_fetch_enter(dm_thread_data *D)
{
	SESSION_GET;
	int result, state, setidx;

	self->fi->bodyfetch = p_list_new(self->pool);
	self->fi->getUID = self->use_uid;

	setidx = self->args_idx;
	TRACE(TRACE_DEBUG, "id-set: [%s]", p_string_str(self->args[self->args_idx]));
	self->args_idx++; //skip on past this for the fetch_parse_args coming next...

	state = 1;
	do {
		if ((state=dbmail_imap_session_fetch_parse_args(self)) == -2) {
			dbmail_imap_session_buff_printf(self, "%s BAD invalid argument list to fetch\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
		self->args_idx++;
	} while (state > 0);

	if (self->fi->vanished && (! self->fi->changedsince)) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid argument list to fetch\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	if (self->fi->vanished) {
		self->qresync.modseq = self->fi->changedsince;
		char *out = NULL;
		GTree *vanished = MailboxState_getExpunged(self->mailbox->mbstate, &self->qresync, &out);
		if (g_tree_nnodes(vanished)) {
			dbmail_imap_session_buff_printf(self,
				       	"* VANISHED (EARLIER) %s\r\n", out);
		}
		g_free(out);
		g_tree_destroy(vanished);
	}

	dbmail_imap_session_mailbox_status(self, FALSE);

	if ((result = _dm_imapsession_get_ids(self, p_string_str(self->args[setidx]))) == DM_SUCCESS) {
		self->ids_list = g_tree_keys(self->ids);
		result = dbmail_imap_session_fetch_get_items(self);
	}

	dbmail_imap_session_fetch_free(self, FALSE);
	dbmail_imap_session_args_free(self, FALSE);

	MailboxState_flush_recent(self->mailbox->mbstate);

	if (result) {
		D->status = result;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_fetch(ImapSession *self)
{
	if (!check_state_and_args (self, 2, 0, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_fetch_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_store()
 *
 * alter message-associated data in selected mailbox
 */

void _fetch_update(ImapSession *self, MessageInfo *msginfo, gboolean showmodseq, gboolean showflags)
{
	gboolean needspace = false;

	uint64_t *msn = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate), &msginfo->uid);

	dbmail_imap_session_buff_printf(self,"* %" PRIu64 " FETCH (", *msn);
	if (self->use_uid) {
		dbmail_imap_session_buff_printf(self, "UID %" PRIu64 , msginfo->uid);
		needspace = true;
	}

	if (showflags) {
		GList *sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		char *s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);
		if (needspace) dbmail_imap_session_buff_printf(self, " ");
		dbmail_imap_session_buff_printf(self, "FLAGS %s", s);
		g_free(s);
		needspace = true;
	}
	if (showmodseq) {
		if (needspace) dbmail_imap_session_buff_printf(self, " ");
		dbmail_imap_session_buff_printf(self, "MODSEQ (%" PRIu64 ")", msginfo->seq);
	}

	dbmail_imap_session_buff_printf(self, ")\r\n");
}

static gboolean _do_store(uint64_t *id, gpointer UNUSED value, dm_thread_data *D)
{
	ImapSession *self = D->session;
	struct cmd_t *cmd = self->cmd;

	MessageInfo *msginfo = NULL;
	int i;
	int changed = 0;

	if (self->mailbox && MailboxState_getMsginfo(self->mailbox->mbstate))
		msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), id);

	if (! msginfo)
		return TRUE;


	if (MailboxState_getPermission(self->mailbox->mbstate) == IMAPPERM_READWRITE) {
		changed = db_set_msgflag(*id, cmd->flaglist, cmd->keywords, cmd->action, cmd->unchangedsince, msginfo);
		if (changed < 0) {
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
			D->status = TRUE;
			return TRUE;
		} else if (changed) {
			db_message_set_seq(*id, cmd->seq);
			msginfo->seq = cmd->seq;
		} else {
			self->ids_list = g_list_prepend(self->ids_list, id);
		}
	}

	// Set the system flags
	for (i = 0; i < IMAP_NFLAGS; i++) {
		
		if (i == IMAP_FLAG_RECENT) // Skip recent_flag
			continue;

		switch (cmd->action) {
			case IMAPFA_ADD:
				if (cmd->flaglist[i])
					msginfo->flags[i] = 1;
			break;
			case IMAPFA_REMOVE:
				if (cmd->flaglist[i]) 
					msginfo->flags[i] = 0;
			break;
			case IMAPFA_REPLACE:
				if (cmd->flaglist[i]) 
					msginfo->flags[i] = 1;
				else
					msginfo->flags[i] = 0;
			break;
		}
	}

	// Set the user keywords as labels
	g_list_merge(&(msginfo->keywords), cmd->keywords, cmd->action, (GCompareFunc)g_ascii_strcasecmp);

	// reporting callback
	if ((! cmd->silent) || changed > 0) {
		gboolean showmodseq = (changed && (cmd->unchangedsince || self->mailbox->condstore));
		gboolean showflags = (! cmd->silent);
		_fetch_update(self, msginfo, showmodseq, showflags);
	}

	return FALSE;
}

static void _ic_store_enter(dm_thread_data *D)
{
	SESSION_GET;
	int result, j, k;
	struct cmd_t cmd;
	gboolean update = FALSE;
	const char *token = NULL;
	gboolean needflags = false;
	int startflags = 0, endflags = 0;
	String_T buffer = NULL;

	k = self->args_idx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.silent = FALSE;

	/* retrieve action type */
	for (k = self->args_idx+1; self->args[k]; k++) {

		token = p_string_str(self->args[k]);
		if (cmd.action == IMAPFA_NONE) {
			if (MATCH(token, "flags")) {
				cmd.action = IMAPFA_REPLACE;
			} else if (MATCH(token, "flags.silent")) {
				cmd.action = IMAPFA_REPLACE;
				cmd.silent = TRUE;
			} else if (MATCH(token, "+flags")) {
				cmd.action = IMAPFA_ADD;
			} else if (MATCH(token, "+flags.silent")) {
				cmd.action = IMAPFA_ADD;
				cmd.silent = TRUE;
			} else if (MATCH(token, "-flags")) {
				cmd.action = IMAPFA_REMOVE;
			} else if (MATCH(token, "-flags.silent")) {
				cmd.action = IMAPFA_REMOVE;
				cmd.silent = TRUE;
			}
			if (cmd.action != IMAPFA_NONE) {
				needflags = true;
				continue;
			}
		}
		if (needflags) {
			if ( (! startflags) && (MATCH(token, "("))) {
				startflags = k+1;
				continue;
			} else if (! startflags) {
				startflags = k;
				continue;
			} else if (startflags && (MATCH(token, ")") || MATCH(token, "("))) {
				needflags = false;
				endflags = k-1;
			}
		} 
		if (! needflags) {
			if (MATCH(token, "(")) {
				char *end;
				if (self->args[k+1] && self->args[k+2]) {
					if ((! Capa_match(self->capa, "CONDSTORE")) || (! MATCH(p_string_str(self->args[k+1]), "UNCHANGEDSINCE"))) {
						cmd.action = IMAPFA_NONE;
						break;
					}
					errno = 0;
					cmd.unchangedsince = dm_strtoull(p_string_str(self->args[k+2]), &end, 10);
					if (p_string_str(self->args[k+2]) == end) {
						cmd.action = IMAPFA_NONE;
						break;
					}
					k += 2;
					self->mailbox->condstore = true;
				}
			}
		}
	}
	if (! endflags)
		endflags = k-1;


	if (cmd.action == IMAPFA_NONE) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid STORE action specified\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* multiple flags should be parenthesed */
	if (needflags && (!startflags)) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid argument(s) to STORE\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}


	/* now fetch flag list */
	for (k = startflags; k <= endflags; k++) {
		for (j = 0; j < IMAP_NFLAGS; j++) {
			/* storing the recent flag explicitely is not allowed */
			if (MATCH(p_string_str(self->args[k]),"\\Recent")) {
				dbmail_imap_session_buff_printf(self, "%s BAD invalid flag list to STORE command\r\n", self->tag);
				D->status = 1;
				SESSION_RETURN;
			}
				
			if (MATCH(p_string_str(self->args[k]), imap_flag_desc_escaped[j])) {
				cmd.flaglist[j] = 1;
				break;
			}
		}

		if (j == IMAP_NFLAGS) {
			const char *kw = p_string_str(self->args[k]);
			cmd.keywords = g_list_append(cmd.keywords,g_strdup(kw));
			if (! MailboxState_hasKeyword(self->mailbox->mbstate, kw)) {
				MailboxState_addKeyword(self->mailbox->mbstate, kw);
				update = TRUE;
			}
		}
	}

	/** check ACL's for STORE */
	if (cmd.flaglist[IMAP_FLAG_SEEN] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_SEEN))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd.keywords);
			SESSION_RETURN;
		}
	}
	if (cmd.flaglist[IMAP_FLAG_DELETED] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_DELETED))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd.keywords);
			SESSION_RETURN;
		}
	}
	if (cmd.flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    cmd.flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    cmd.flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    g_list_length(cmd.keywords) > 0 ) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_WRITE))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd.keywords);
			SESSION_RETURN;
		}
	}
	/* end of ACL checking. If we get here without returning, the user has
	   the right to store the flags */

	self->cmd = &cmd;

	if ( update ) {
		/* flags need and seq update of the mailbox in order to trigger updates to other connected clients */
		db_mailbox_seq_update(MailboxState_getId(self->mailbox->mbstate), 0);
		char *flags = MailboxState_flags(self->mailbox->mbstate);
		dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
		dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)] Flags allowed.\r\n", flags);
		g_free(flags);
	}

	if ((result = _dm_imapsession_get_ids(self, p_string_str(self->args[self->args_idx]))) == DM_SUCCESS) {
		if (self->ids) {
			uint64_t seq = db_mailbox_seq_update(MailboxState_getId(self->mailbox->mbstate), 0);
			cmd.seq = seq;
			g_tree_foreach(self->ids, (GTraverseFunc) _do_store, D);
		}
	}

	g_list_destroy(cmd.keywords);

	if (result || D->status) {
		if (result) D->status = result;
		SESSION_RETURN;
	}

	if (self->ids_list) {
		GString *failed_ids = g_list_join_u64(self->ids_list, ",");
		buffer = p_string_new(self->pool, "");
		//according to RFC7162 section 3.1.3.0 MODIFIED keyword should be used as respnse like 
		//"... OK [MODIFIED 7,9] ..." not "...OK [MODIFIED [7,9]]..."
		p_string_printf(buffer, "MODIFIED %s", failed_ids->str);
		g_string_free(failed_ids, TRUE);
		g_list_free(g_list_first(self->ids_list));
		self->ids_list = NULL;
		SESSION_OK_WITH_RESP_CODE(p_string_str(buffer));
		p_string_free(buffer, TRUE);
	} else {
		SESSION_OK;
	}

	SESSION_RETURN;
}

int _ic_store(ImapSession *self)
{
	if (!check_state_and_args (self, 2, 0, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_store_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_copy()
 *
 * copy a message to another mailbox
 */

static gboolean _do_copy(uint64_t *id, gpointer UNUSED value, ImapSession *self)
{
	struct cmd_t *cmd = self->cmd;
	uint64_t newid = 0;
	int result;
	uint64_t *new_ids_element = NULL;
	if (!g_tree_lookup(self->mailbox->mbstate->msginfo, id)){
		TRACE(TRACE_WARNING,"Copy message [%ld] failed security issue, trying to copy message that are not in this mailbox",*id);
		//dbmail_imap_session_buff_printf(self, "%s NO security issue, trying to copy message that are not in this mailbox\r\n",self->tag);
		return FALSE;
	}
	result = db_copymsg(*id, cmd->mailbox_id, self->userid, &newid, TRUE);
	db_message_set_seq(*id, cmd->seq);
	if (result == -1) {
		/* uid not found, according to RFC 3501 section 6.4.8, should continue  */
		TRACE(TRACE_WARNING,"Copy message [%ld] failed due to missing in database. continue.",*id);
		/* continue operation, do not close or send various info on connection */
		//dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		//return TRUE;
		return FALSE;
	}
	if (result == -2) {
		TRACE(TRACE_WARNING,"Copy message [%ld] failed due to `%s NO quotum would exceed`",*id,self->tag);
		dbmail_imap_session_buff_printf(self, "%s NO quotum would exceed\r\n", self->tag);
		return TRUE;
	}
	// insert the new uid to the new_ids collection
	// reserve memory for the new element in new_ids
	new_ids_element = g_new0(uint64_t,1);
	*new_ids_element = newid;
	TRACE(TRACE_DEBUG, "copied uid %" PRIu64 " -> %" PRIu64, *id, *new_ids_element);
	// prepending is faster then appending
	self->new_ids = g_list_prepend(self->new_ids, new_ids_element);
	return FALSE;
}


static void _ic_copy_enter(dm_thread_data *D)
{
	SESSION_GET;
	uint64_t destmboxid;
	int result;
	MailboxState_T S;
	struct cmd_t cmd;
	const char *src, *dst;

	src = p_string_str(self->args[self->args_idx]);
	dst = p_string_str(self->args[self->args_idx+1]);

	GList *old_ids;
	GString *old_ids_buff;
	GString *new_ids_buff;

	String_T buffer;

	/* check if destination mailbox exists */
	if (! db_findmailbox(dst, self->userid, &destmboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO [TRYCREATE] specified mailbox does not exist\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}
	// check if user has right to COPY from source mailbox
	if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_READ))) {
		D->status = result;
		SESSION_RETURN;
	}

	// check if user has right to COPY to destination mailbox
	S = dbmail_imap_session_mbxinfo_lookup(self, destmboxid);
	if ((result = mailbox_check_acl(self, S, ACL_RIGHT_INSERT))) {
		D->status = result;
		SESSION_RETURN;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.mailbox_id = destmboxid;
	self->cmd = &cmd;
	if ((result = _dm_imapsession_get_ids(self, src)) == DM_SUCCESS) {
		if (self->ids) {
			cmd.seq = db_mailbox_seq_update(destmboxid, 0);
			g_tree_foreach(self->ids, (GTraverseFunc) _do_copy, self);
		}
	}
	self->cmd = NULL;

	if (result) {
		D->status = result;
		SESSION_RETURN;
	}

	if (MailboxState_getId(self->mailbox->mbstate) == destmboxid)
		dbmail_imap_session_mailbox_status(self, TRUE);

	self->new_ids = g_list_reverse(self->new_ids);

	old_ids = g_tree_keys(self->ids);
	old_ids_buff = g_list_join_u64(old_ids,",");
	g_list_free(g_list_first(old_ids));

	new_ids_buff = g_list_join_u64(self->new_ids,",");

	buffer = p_string_new(self->pool, "");
	p_string_printf(buffer, "COPYUID %" PRIu64 " %s %s", destmboxid, old_ids_buff->str, new_ids_buff->str);

	g_string_free(new_ids_buff,TRUE);
	g_string_free(old_ids_buff,TRUE);

	SESSION_OK_WITH_RESP_CODE(p_string_str(buffer));
	p_string_free(buffer, TRUE);

	g_list_destroy(self->new_ids);
	self->new_ids = NULL;

	SESSION_RETURN;
}

int _ic_copy(ImapSession *self) 
{
	if (!check_state_and_args(self, 2, 2, CLIENTSTATE_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_copy_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_uid()
 *
 * fetch/store/copy/search message UID's
 */
int _ic_uid(ImapSession *self)
{
	int result;
	const char *command;

	if (self->state != CLIENTSTATE_SELECTED) {
		dbmail_imap_session_buff_printf(self, "%s BAD UID command received in invalid state\r\n", self->tag);
		return 1;
	}

	if (! (self->args[0] && self->args[self->args_idx]) ) {
		dbmail_imap_session_buff_printf(self, "%s BAD missing argument(s) to UID\r\n", self->tag);
		return 1;
	}

	self->use_uid = 1;	/* set global var to make clear we will be using UID's */
	
	/* ACL rights for UID are handled by the other functions called below */
	command = p_string_str(self->args[self->args_idx]);
	if (MATCH(command, "fetch")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_fetch(self);
	} else if (MATCH(command, "copy")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_copy(self);
	} else if (MATCH(command, "store")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_store(self);
	} else if (MATCH(command, "search")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_search(self);
	} else if (MATCH(command, "sort")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_sort(self);
	} else if (MATCH(command, "thread")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_thread(self);
	} else if (MATCH(command, "expunge")) {
		dbmail_imap_session_set_command(self, command);
		self->args_idx++;
		result = _ic_expunge(self);
	} else {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid UID command\r\n", self->tag);
		result = 1;
		self->use_uid = 0;
	}

	return result;
}


/* Helper function for _ic_getquotaroot() and _ic_getquota().
 * Send all resource limits in `quota'.
 */
void send_quota(ImapSession *self, Quota_T quota)
{
	uint64_t usage, limit;

	limit = quota_get_limit(quota);
	usage = quota_get_usage(quota);

	if (limit > 0) {
		usage = usage / 1024;
		limit = limit / 1024;
		dbmail_imap_session_buff_printf(self,
				"* QUOTA \"\" (STORAGE %" PRIu64 " %" PRIu64 ")\r\n",
				usage, limit);
	}
}

/*
 * _ic_getquotaroot()
 *
 * get quota root and send quota
 */
static void _ic_getquotaroot_enter(dm_thread_data *D)
{
	Quota_T quota;
	char *errormsg;
	SESSION_GET;

	if (! (quota = quota_get_quota(self->userid, "", &errormsg))) {
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		D->status=1;
		SESSION_RETURN;
	}

	dbmail_imap_session_buff_printf(self, "* QUOTAROOT \"%s\" \"%s\"\r\n", 
			p_string_str(self->args[self->args_idx]), 
			quota_get_root(quota));

	send_quota(self, quota);
	quota_free(&quota);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_getquotaroot(ImapSession *self)
{
	if (! check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_getquotaroot_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_getquot()
 *
 * get quota
 */
static void _ic_getquota_enter(dm_thread_data *D)
{
	Quota_T quota;
	char *errormsg;
	SESSION_GET;

	if (! (quota = quota_get_quota(self->userid, p_string_str(self->args[self->args_idx]), &errormsg))) {
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		D->status=1;
		SESSION_RETURN;
	}

	send_quota(self, quota);
	quota_free(&quota);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_getquota(ImapSession *self)
{
	if (! check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_getquota_enter, _ic_cb_leave, NULL);
	return 0;
}

/* returns -1 on error, 0 if user or mailbox not found and 1 otherwise */
static int imap_acl_pre_administer(const char *mailboxname,
				   const char *username,
				   uint64_t executing_userid,
				   uint64_t * mboxid, uint64_t * target_userid)
{
	if (! db_findmailbox(mailboxname, executing_userid, mboxid))
		return FALSE;
	return auth_user_exists(username, target_userid);
}

static void _ic_setacl_enter(dm_thread_data *D)
{
	/* SETACL mailboxname identifier mod_rights */
	int result;
	uint64_t mboxid;
	uint64_t targetuserid;
	MailboxState_T S;
	SESSION_GET;

	const char *mailbox = p_string_str(self->args[self->args_idx]);
	const char *usernam = p_string_str(self->args[self->args_idx+1]);
	const char *rights = p_string_str(self->args[self->args_idx+2]);
	result = imap_acl_pre_administer(mailbox, usernam, self->userid, &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	} else if (result == 0) {
		dbmail_imap_session_buff_printf(self, "%s NO SETACL failure: can't set acl\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}
	// has the rights to 'administer' this mailbox? 
	S = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	if ((result = mailbox_check_acl(self, S, ACL_RIGHT_ADMINISTER))) {
		D->status = result;
		SESSION_RETURN;
	}

	// set the new acl
	if (acl_set_rights(targetuserid, mboxid, rights) < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_setacl(ImapSession *self)
{
	if (!check_state_and_args(self, 3, 3, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_setacl_enter, _ic_cb_leave, NULL);
	return 0;
}
	

static void _ic_deleteacl_enter(dm_thread_data *D)
{
	// DELETEACL mailboxname identifier
	uint64_t mboxid, targetuserid;
	MailboxState_T S;
	int result;
	SESSION_GET;
	const char *mailbox = p_string_str(self->args[self->args_idx]);
	const char *usernam = p_string_str(self->args[self->args_idx+1]);

	if (imap_acl_pre_administer(mailbox, usernam, self->userid, &mboxid, &targetuserid) == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}
	
	S = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	if ((result = mailbox_check_acl(self, S, ACL_RIGHT_ADMINISTER))) {
		D->status = result;
		SESSION_RETURN;
	}

	// set the new acl
	if (acl_delete_acl(targetuserid, mboxid) < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_deleteacl(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_deleteacl_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_getacl_enter(dm_thread_data *D)
{
	/* GETACL mailboxname */
	uint64_t mboxid;
	char *acl_string;
	SESSION_GET;
	const char *mailbox = p_string_str(self->args[self->args_idx]);

	if (! db_findmailbox(mailbox, self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO GETACL failure: can't get acl\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}
	// get acl string (string of identifier-rights pairs)
	if (!(acl_string = acl_get_acl(mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}

	dbmail_imap_session_buff_printf(self, "* ACL \"%s\" %s\r\n", mailbox, acl_string);
	g_free(acl_string);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_getacl(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_getacl_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_listrights_enter(dm_thread_data *D)
{
	/* LISTRIGHTS mailboxname identifier */
	int result;
	uint64_t mboxid;
	uint64_t targetuserid;
	const char *listrights_string;
	MailboxState_T S;
	SESSION_GET;

	const char *mailbox = p_string_str(self->args[self->args_idx]);
	const char *usernam = p_string_str(self->args[self->args_idx+1]);
	result = imap_acl_pre_administer(mailbox, usernam, self->userid, &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	} else if (result == 0) {
		dbmail_imap_session_buff_printf(self, "%s, NO LISTRIGHTS failure: can't set acl\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}
	// has the rights to 'administer' this mailbox? 
	S = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	if ((result = mailbox_check_acl(self, S, ACL_RIGHT_ADMINISTER))) {
		D->status=1;
		SESSION_RETURN;	
	}

	// set the new acl
	if (!(listrights_string = acl_listrights(targetuserid, mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status=-1;
		SESSION_RETURN;
	}

	dbmail_imap_session_buff_printf(self, "* LISTRIGHTS \"%s\" %s %s\r\n",
		mailbox, usernam, listrights_string);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_listrights(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_listrights_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_myrights_enter(dm_thread_data *D)
{
	/* MYRIGHTS mailboxname */
	uint64_t mboxid;
	char *myrights_string;
	SESSION_GET;

	if (! db_findmailbox(p_string_str(self->args[self->args_idx]), self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO MYRIGHTS failure: unknown mailbox\r\n", self->tag);
		D->status=1;
		SESSION_RETURN;
	}

	if (!(myrights_string = acl_myrights(self->userid, mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}

	dbmail_imap_session_buff_printf(self, "* MYRIGHTS \"%s\" %s\r\n",
		       	p_string_str(self->args[self->args_idx]), myrights_string);
	g_free(myrights_string);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_myrights(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_myrights_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_namespace_enter(dm_thread_data *D)
{
	SESSION_GET;
	dbmail_imap_session_buff_printf(self, "* NAMESPACE ((\"\" \"%s\")) ((\"%s\" \"%s\")) "
		"((\"%s\" \"%s\"))\r\n",
		MAILBOX_SEPARATOR, NAMESPACE_USER,
		MAILBOX_SEPARATOR, NAMESPACE_PUBLIC, MAILBOX_SEPARATOR);

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_namespace(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_namespace_enter, _ic_cb_leave, NULL);
	return 0;
}


