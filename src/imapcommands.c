/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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
};

cmd_t cmd_new(void)
{
	return (cmd_t)g_malloc0(sizeof(cmd_t));
}

void cmd_free(cmd_t *cmd)
{
	assert(cmd && *cmd);
	if ((*cmd)->keywords)
		g_list_destroy((*cmd)->keywords);
	(*cmd)->keywords = NULL;
	g_free((*cmd));	
}

/* 
 * push a message onto the queue and notify the
 * event-loop by sending a char into the selfpipe
 */

#define SESSION_GET \
	ImapSession *self = D->session

#define SESSION_RETURN \
	D->session->command_state = TRUE; \
	g_async_queue_push(queue, (gpointer)D); \
	if (selfpipe[1] > -1) { \
		if (write(selfpipe[1], "Q", 1) != 1) { /* ignore */; } \
	} \
	return;

#define SESSION_OK \
	if (self->state != CLIENTSTATE_ERROR) \
		dbmail_imap_session_buff_printf(self, \
				"%s OK %s%s completed\r\n", \
				self->tag, self->use_uid ? "UID " : "", \
				self->command)


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
	if (self->ci->ssl_state) {
		ci_write(self->ci, "%s NO TLS already active\r\n", self->tag);
		return 1;
	}
	ci_write(self->ci, "%s OK Begin TLS now\r\n", self->tag);
	i = ci_starttls(self->ci);
	
	Capa_remove(self->capa, "STARTTLS");
	
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
	TRACE(TRACE_NOTICE, "[%p] userid:[%lu]", self, self->userid);
	return 2;
}

void _ic_id_enter(dm_thread_data *D)
{
	SESSION_GET;
	struct utsname buf;
	memset(&buf, 0, sizeof(buf));
	uname(&buf);
	dbmail_imap_session_buff_printf(self, "* ID (\"name\" \"dbmail\" \"version\" \"%s\""
		" \"os\" \"%s\" \"os-version\" \"%s\")\r\n", VERSION, &buf.sysname, &buf.release);
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
	const char *username = (const char *)self->args[self->args_idx];
	const char *password = (const char *)self->args[self->args_idx+1];

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
		if ( (! MATCH(self->args[self->args_idx], "login")) && \
			(! MATCH(self->args[self->args_idx], "cram-md5")) ) {
			dbmail_imap_session_buff_printf(self, "%s NO Invalid authentication mechanism specified\r\n", self->tag);
			return 1;
		}
		self->args_idx++;
	} else {
		if (!check_state_and_args(self, 2, 2, CLIENTSTATE_NON_AUTHENTICATED)) return 1;
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
	if (msginfo->flags[IMAPFLAG_SEEN]) return FALSE; 
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
	db_findmailbox(mailbox, self->userid, &mailbox_idnr);
	
	/* create missing INBOX for this authenticated user */
	if ((! mailbox_idnr ) && (strcasecmp(mailbox, "INBOX")==0)) {
		int err = db_createmailbox("INBOX", self->userid, &mailbox_idnr);
		TRACE(TRACE_INFO, "[%p] [%d] Auto-creating INBOX for user id [%lu]", 
				self, err, self->userid);
	}
	
	if (! mailbox_idnr) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1; /* error */
	}

	/* new mailbox structure */
	self->mailbox = dbmail_mailbox_new(mailbox_idnr);

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

	return 0;
}

static void _ic_select_enter(dm_thread_data *D)
{
	int err;
	char *flags;
	const char *okarg;
	MailboxState_T S;
	SESSION_GET;

	/* close the currently opened mailbox */
	imap_session_mailbox_close(self);

	if ((err = imap_session_mailbox_open(self, self->args[self->args_idx]))) {
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
	dbmail_imap_session_buff_printf(self, "* OK [UIDNEXT %lu] Predicted next UID\r\n",
		MailboxState_getUidnext(S));
	
	/* UID */
	dbmail_imap_session_buff_printf(self, "* OK [UIDVALIDITY %lu] UID value\r\n",
		MailboxState_getId(S));

	if (MailboxState_getExists(S)) { 
		/* show msn of first unseen msg (if present) */
		GTree *uids = MailboxState_getIds(S);
		GTree *info = MailboxState_getMsginfo(S);
		uint64_t key = 0, *msn = NULL;
		g_tree_foreach(info, (GTraverseFunc)mailbox_first_unseen, &key);
		if ( (key > 0) && (msn = g_tree_lookup(uids, &key)))
			dbmail_imap_session_buff_printf(self, "* OK [UNSEEN %lu] first unseen message\r\n", *msn);
	}

	if (self->command_type == IMAP_COMM_SELECT) {
		okarg = "READ-WRITE";
		MailboxState_flush_recent(S);
	} else {
		okarg = "READ-ONLY";
	}

	dbmail_imap_session_buff_printf(self, "%s OK [%s] %s completed\r\n", self->tag, okarg, self->command);

	SESSION_RETURN;
}

int _ic_select(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 1, CLIENTSTATE_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_select_enter, _ic_cb_leave, NULL);
	return 0;
}


int _ic_examine(ImapSession *self)
{
	return _ic_select(self);
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

	result = db_mailbox_create_with_parents(self->args[self->args_idx], BOX_COMMANDLINE, self->userid, &mboxid, &message);

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
	char *mailbox = self->args[0];
	unsigned nchildren = 0;
	
	/* check if there is an attempt to delete inbox */
	if (MATCH(self->args[0], "INBOX")) {
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
			C c; volatile int t = DM_SUCCESS;
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
				db_exec(c, "UPDATE %smessages SET status=%d WHERE mailbox_idnr = %lu", DBPFX, MESSAGE_STATUS_PURGE, mailbox_idnr);
				db_exec(c, "UPDATE %smailboxes SET no_select = 1 WHERE mailbox_idnr = %lu", DBPFX, mailbox_idnr);
				db_commit_transaction(c);
			CATCH(SQLException)
				LOG_SQLERROR;
				t = DM_EQUERY;
			FINALLY
				db_con_close(c);
			END_TRY;

			if (t == DM_EQUERY) {
				D->status=t;
				SESSION_RETURN;
			}

			MailboxState_setNoSelect(S, TRUE);
			db_mailbox_seq_update(mailbox_idnr);
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
	char newname[IMAP_MAX_MAILBOX_NAMELEN];
	MailboxState_T M;


	if (! (db_findmailbox(self->args[0], self->userid, &mboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check if new name is valid */
        if (!checkmailboxname(self->args[1])) {
	        dbmail_imap_session_buff_printf(self, "%s NO new mailbox name contains invalid characters\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
        }

	if ((db_findmailbox(self->args[1], self->userid, &newmboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO new mailbox already exists\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	oldnamelen = strlen(self->args[0]);

	/* check if new name would invade structure as in
	 * test (exists)
	 * rename test test/testing
	 * would create test/testing but delete test
	 */
	if (strncasecmp(self->args[0], self->args[1], (int) oldnamelen) == 0 &&
	    strlen(self->args[1]) > oldnamelen && self->args[1][oldnamelen] == '/') {
		dbmail_imap_session_buff_printf(self, "%s NO new mailbox would invade mailbox structure\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check if structure of new name is valid */
	/* i.e. only last part (after last '/' can be nonexistent) */
	for (i = strlen(self->args[1]) - 1; i >= 0 && self->args[1][i] != '/'; i--);
	if (i >= 0) {
		self->args[1][i] = '\0';	/* note: original char was '/' */

		if (! db_findmailbox(self->args[1], self->userid, &parentmboxid)) {
			/* parent mailbox does not exist */
			dbmail_imap_session_buff_printf(self, "%s NO new mailbox would invade mailbox structure\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}

		/* ok, reset arg */
		self->args[1][i] = '/';
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
		TRACE(TRACE_DEBUG, "[%p] Checking right to CREATE under [%lu]", self, parentmboxid);
		if ((result = imap_session_mailbox_check_acl(self, parentmboxid, ACL_RIGHT_CREATE))) {
			D->status = result;
			SESSION_RETURN;
		}

		TRACE(TRACE_DEBUG, "[%p] We have the right to CREATE under [%lu]", self, parentmboxid);
	}

	/* check if it is INBOX to be renamed */
	if (MATCH(self->args[0], "INBOX")) {
		/* ok, renaming inbox */
		/* this means creating a new mailbox and moving all the INBOX msgs to the new mailbox */
		/* inferior names of INBOX are left unchanged */
		result = db_createmailbox(self->args[1], self->userid, &newmboxid);
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
		uint64_t childid = *(uint64_t *)children->data;
		const char *tname;
		M = dbmail_imap_session_mbxinfo_lookup(self, childid);
		tname = MailboxState_getName(M);

		g_snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s", self->args[1], &tname[oldnamelen]);
		if ((mailbox_rename(M, newname)) != DM_SUCCESS) {
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
	if ((mailbox_rename(M, self->args[1])) != DM_SUCCESS) {
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
	
	if (! (db_findmailbox(self->args[0], self->userid, &mboxid))) {
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

/*
 * _ic_list()
 *
 * executes a list command
 */
void _ic_list_enter(dm_thread_data *D)
{
	SESSION_GET;
	int list_is_lsub = 0;
	GList *plist = NULL, *children = NULL;
	GTree *shown = NULL;
	char *pstring = NULL;
	MailboxState_T M = NULL;
	unsigned i;
	char *pattern;
	char mailbox[IMAP_MAX_MAILBOX_NAMELEN];

	/* check if self->args are both empty strings, i.e. A001 LIST "" "" 
	   this has special meaning; show root & delimiter */
	if (strlen(self->args[0]) == 0 && strlen(self->args[1]) == 0) {
		dbmail_imap_session_buff_printf(self, "* %s (\\NoSelect) \"/\" \"\"\r\n", self->command);
		SESSION_OK;
		SESSION_RETURN;
	}

	/* check the reference name, should contain only accepted mailboxname chars */
	for (i = 0; self->args[0][i]; i++) {
		if (index(AcceptedMailboxnameChars, self->args[0][i]) == NULL) {
			dbmail_imap_session_buff_printf(self, "%s BAD reference name contains invalid characters\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}
	pattern = g_strdup_printf("%s%s", self->args[0], self->args[1]);

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

	shown = g_tree_new_full((GCompareDataFunc)dm_strcmpdata,NULL,(GDestroyNotify)g_free,NULL);

	while ((! D->status) && children) {
		gboolean show = FALSE;

		memset(&mailbox, 0, IMAP_MAX_MAILBOX_NAMELEN);

		uint64_t mailbox_id = *(uint64_t *)children->data;
		if ( (D->status = db_getmailboxname(mailbox_id, self->userid, mailbox)) != DM_SUCCESS) {
			break;
		}

		// avoid fully loading mailbox here
		M = MailboxState_new(0);
		MailboxState_setId(M, mailbox_id);
		MailboxState_info(M);
		MailboxState_setName(M, mailbox);

		/* Enforce match of mailbox to pattern. */
		TRACE(TRACE_DEBUG,"test if [%s] matches [%s]", mailbox, pattern);
		if (! listex_match(pattern, mailbox, MAILBOX_SEPARATOR, 0)) {
			if (g_str_has_suffix(pattern,"%")) {
				/*
				   If the "%" wildcard is the last character of a mailbox name argument, matching levels
				   of hierarchy are also returned.  If these levels of hierarchy are not also selectable 
				   mailboxes, they are returned with the \Noselect mailbox name attribute
				   */

				TRACE(TRACE_DEBUG, "mailbox [%s] doesn't match pattern [%s]", mailbox, pattern);
				char *m = NULL, **p = g_strsplit(mailbox,MAILBOX_SEPARATOR,0);
				int l = g_strv_length(p);
				while (l > 1) {
					if (p[l]) {
						g_free(p[l]);
						p[l] = NULL;
					}
					m = g_strjoinv(MAILBOX_SEPARATOR,p);
					if (listex_match(pattern, m, MAILBOX_SEPARATOR, 0)) {
						TRACE(TRACE_DEBUG,"[%s] matches [%s]", m, pattern);

						MailboxState_setName(M, m);
						MailboxState_setNoSelect(M, TRUE);
						MailboxState_setNoChildren(M, FALSE);
						show = TRUE;
						break;
					}
					g_free(m);
					l--;
				}
				g_strfreev(p);
			}
		} else {
			show = TRUE;
		}

		if (show && MailboxState_getName(M) && (! g_tree_lookup(shown, MailboxState_getName(M)))) {
			char *s = g_strdup(MailboxState_getName(M));
			TRACE(TRACE_DEBUG,"[%s]", s);
			g_tree_insert(shown, s, s);

			plist = NULL;
			if (MailboxState_noSelect(M))
				plist = g_list_append(plist, g_strdup("\\noselect"));
			if (MailboxState_noInferiors(M))
				plist = g_list_append(plist, g_strdup("\\noinferiors"));
			if (MailboxState_noChildren(M))
				plist = g_list_append(plist, g_strdup("\\hasnochildren"));
			else
				plist = g_list_append(plist, g_strdup("\\haschildren"));

			/* show */
			pstring = dbmail_imap_plist_as_string(plist);
			dbmail_imap_session_buff_printf(self, "* %s %s \"%s\" \"%s\"\r\n", self->command, 
					pstring, MAILBOX_SEPARATOR, MailboxState_getName(M));

			g_list_destroy(plist);
			g_free(pstring);
		}

		MailboxState_free(&M);

		if (! g_list_next(children)) break;
		children = g_list_next(children);
	}

	if (shown) g_tree_destroy(shown);
	if (children) g_list_destroy(children);
	g_free(pattern);

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
	
	if (self->args[1][0] != '(') {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list should be parenthesed\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	/* check final arg: should be ')' and no new '(' in between */
	for (i = 2, endfound = 0; self->args[i]; i++) {
		if (self->args[i][0] == ')') {
			endfound = i;
			break;
		} else if (self->args[i][0] == '(') {
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
	if (! db_findmailbox(self->args[0], self->userid, &id)) {
		/* create missing INBOX for this authenticated user */
		if ((! id ) && (MATCH(self->args[0], "INBOX"))) {
			TRACE(TRACE_INFO, "[%p] Auto-creating INBOX for user id [%lu]", self, self->userid);
			db_createmailbox("INBOX", self->userid, &id);
		}
		if (! id) {
			dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
	}

	// avoid fully loading mailbox here
	M = MailboxState_new(0);
	MailboxState_setId(M, id);
	if (MailboxState_info(M)) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		D->status = 1;
		MailboxState_free(&M);
		SESSION_RETURN;
	}

	MailboxState_setName(M, self->args[0]);
	MailboxState_count(M);

	if ((result = mailbox_check_acl(self, M, ACL_RIGHT_READ))) {
		D->status = result;
		MailboxState_free(&M);
		SESSION_RETURN;
	}

	for (i = 2; self->args[i]; i++) {
		if (MATCH(self->args[i], "messages"))
			plst = g_list_append_printf(plst,"MESSAGES %u", MailboxState_getExists(M));
		else if (MATCH(self->args[i], "recent"))
			plst = g_list_append_printf(plst,"RECENT %u", MailboxState_getRecent(M));
		else if (MATCH(self->args[i], "unseen"))
			plst = g_list_append_printf(plst,"UNSEEN %u", MailboxState_getUnseen(M));
		else if (MATCH(self->args[i], "uidnext"))
			plst = g_list_append_printf(plst,"UIDNEXT %lu", MailboxState_getUidnext(M));
		else if (MATCH(self->args[i], "uidvalidity"))
			plst = g_list_append_printf(plst,"UIDVALIDITY %lu", MailboxState_getId(M));
		else if (MATCH(self->args[i], ")"))
			break;
		else {
			dbmail_imap_session_buff_printf(self, "\r\n%s BAD option '%s' specified\r\n",
				self->tag, self->args[i]);
			D->status = 1;
			MailboxState_free(&M);
			SESSION_RETURN;
		}
	}
	astring = dbmail_imap_astring_as_string(self->args[0]);
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
	self->ci->timeout->tv_sec = idle_timeout;
	self->command_state = IDLE;
	dbmail_imap_session_buff_printf(self, "+ idling\r\n");
	dbmail_imap_session_mailbox_status(self,TRUE);
	dbmail_imap_session_buff_flush(self);
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
	char *internal_date = NULL;
	int flaglist[IMAP_NFLAGS], flagcount = 0;
	GList *keywords = NULL;
	MailboxState_T M;
	SESSION_GET;
	char *message;
	gboolean recent = TRUE;
	MessageInfo *info;

	memset(flaglist,0,sizeof(flaglist));

	/* find the mailbox to place the message */
	if (! db_findmailbox(self->args[0], self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO [TRYCREATE]\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	M = dbmail_imap_session_mbxinfo_lookup(self, mboxid);

	/* check if user has right to append to  mailbox */
	if ((result = imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_INSERT))) {
		D->status = result;
		SESSION_RETURN;
	}

	i = 1;

	/* check if a flag list has been specified */
	if (self->args[i][0] == '(') {
		/* ok fetch the flags specified */
		TRACE(TRACE_DEBUG, "[%p] flag list found:", self);

		i++;
		while (self->args[i] && self->args[i][0] != ')') {
			TRACE(TRACE_DEBUG, "[%p] [%s]", self, self->args[i]);
			for (j = 0; j < IMAP_NFLAGS; j++) {
				if (MATCH(self->args[i], imap_flag_desc_escaped[j])) {
					flaglist[j] = 1;
					flagcount++;
					break;
				}
			}
			if (j == IMAP_NFLAGS) {
				TRACE(TRACE_DEBUG,"[%p] found keyword [%s]", self, self->args[i]);
				keywords = g_list_append(keywords,g_strdup(self->args[i]));
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
		internal_date = self->args[i];
		i++;
		TRACE(TRACE_DEBUG, "[%p] internal date [%s] found, next arg [%s]", self, internal_date, self->args[i]);
	}

	if (self->state == CLIENTSTATE_SELECTED && self->mailbox->id == mboxid) {
		recent = FALSE;
	}
	
	message = self->args[i];

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
			if (db_set_msgflag(message_id, flaglist, keywords, IMAPFA_ADD, NULL) < 0)
				TRACE(TRACE_ERR, "[%p] error setting flags for message [%lu]", self, message_id);
			db_mailbox_seq_update(mboxid);
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
	strncpy(info->internaldate, internal_date?internal_date:"01-Jan-1970 00:00:01 +0100", IMAP_INTERNALDATE_LEN);
	info->rfcsize = strlen(message);
	info->keywords = keywords;

	M = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	MailboxState_addMsginfo(M, message_id, info);

	SESSION_OK;
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
	if (result < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE Internal database error\r\n");
		D->status=result;
		SESSION_RETURN;
	}
	/* only perform the expunge if the user has the right to do it */
	if (result == 1) {
		if (MailboxState_getPermission(self->mailbox->mbstate) == IMAPPERM_READWRITE)
			dbmail_imap_session_mailbox_expunge(self);
		imap_session_mailbox_close(self);
	}

	dbmail_imap_session_set_state(self, CLIENTSTATE_AUTHENTICATED);

	SESSION_OK;
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
	int result;
	SESSION_GET;

	if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_EXPUNGE))) {
		D->status = result;
		SESSION_RETURN;
	}
	
	if (dbmail_imap_session_mailbox_expunge(self) != DM_SUCCESS) {
		dbmail_imap_session_buff_printf(self, "* BYE expunge failed\r\n");
		D->status = DM_EQUERY;
		SESSION_RETURN;
	}

	SESSION_OK;
	SESSION_RETURN;
}

int _ic_expunge(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, CLIENTSTATE_SELECTED)) return 1;

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
	const gchar *cmd;

	search_order order = *(search_order *)D->data;

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
	search_order *data = g_new0(search_order,1);
	*data = order;
	dm_thread_data_push((gpointer)self, sorted_search_enter, _ic_cb_leave, (gpointer)data);
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
	if (MATCH(self->args[self->args_idx],"ORDEREDSUBJECT"))
		return sorted_search(self,SEARCH_THREAD_ORDEREDSUBJECT);
	if (MATCH(self->args[self->args_idx],"REFERENCES"))
		dbmail_imap_session_buff_printf(self, "%s BAD THREAD=REFERENCES not supported\r\n",self->tag);
		//return sorted_search(self,SEARCH_THREAD_REFERENCES);

	return 1;
}

int _dm_imapsession_get_ids(ImapSession *self, const char *set)
{
	dbmail_mailbox_set_uid(self->mailbox,self->use_uid);

	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}

	self->ids = dbmail_mailbox_get_set(self->mailbox, set, self->use_uid);

	if ( (! self->use_uid) && ((!self->ids) || (g_tree_nnodes(self->ids)==0)) ) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid sequence\r\n", self->tag);
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

	if (self->fi) {
		dbmail_imap_session_bodyfetch_free(self);
		g_free(self->fi);
		self->fi = NULL;
	}
	self->fi = g_new0(fetch_items,1);
	self->fi->getUID = self->use_uid;

	setidx = self->args_idx;
	TRACE(TRACE_DEBUG, "id-set: [%s]", self->args[self->args_idx]);
	self->args_idx++; //skip on past this for the fetch_parse_args coming next...

	state = 1;
	do {
		if ( (state = dbmail_imap_session_fetch_parse_args(self)) == -2) {
			dbmail_imap_session_buff_printf(self, "%s BAD invalid argument list to fetch\r\n", self->tag);
			D->status = 1;
			SESSION_RETURN;
		}
		TRACE(TRACE_DEBUG,"[%p] dbmail_imap_session_fetch_parse_args loop idx %lu state %d ", self, self->args_idx, state);
		self->args_idx++;
	} while (state > 0);

	dbmail_imap_session_mailbox_status(self, FALSE);

	if ((result = _dm_imapsession_get_ids(self, self->args[setidx])) == DM_SUCCESS) {
		self->ids_list = g_tree_keys(self->ids);
		result = dbmail_imap_session_fetch_get_items(self);
	}

	dbmail_imap_session_fetch_free(self);
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

static gboolean _do_store(uint64_t *id, gpointer UNUSED value, dm_thread_data *D)
{
	ImapSession *self = D->session;
	cmd_t cmd = self->cmd;

	uint64_t *msn;
	MessageInfo *msginfo = NULL;
	char *s;
	int i;

	if (self->mailbox && MailboxState_getMsginfo(self->mailbox->mbstate))
		msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), id);

	if (! msginfo) {
		TRACE(TRACE_WARNING, "[%p] unable to lookup msginfo struct for [%lu]", self, *id);
		return TRUE;
	}

	msn = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate), id);

	if (MailboxState_getPermission(self->mailbox->mbstate) == IMAPPERM_READWRITE) {
		if (db_set_msgflag(*id, cmd->flaglist, cmd->keywords, cmd->action, msginfo) < 0) {
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
			D->status = TRUE;
			return TRUE;
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
	if (! cmd->silent) {
		GList *sublist = NULL;
		char *uid = NULL;
		if (self->use_uid)
			uid = g_strdup_printf("UID %lu ", *id);
		sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);
		dbmail_imap_session_buff_printf(self,"* %lu FETCH (%sFLAGS %s)\r\n", *msn, uid?uid:"", s);
		if (uid) g_free(uid);
		g_free(s);
	}

	return FALSE;
}

static void _ic_store_enter(dm_thread_data *D)
{
	SESSION_GET;
	int result, i, j, k;
	cmd_t cmd;
	gboolean update = FALSE;

	k = self->args_idx;
	/* multiple flags should be parenthesed */
	if (self->args[k+3] && strcmp(self->args[k+2], "(") != 0) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid argument(s) to STORE\r\n", self->tag);
		D->status = 1;
		SESSION_RETURN;
	}

	cmd = g_malloc0(sizeof(*cmd));
	cmd->silent = FALSE;

	/* retrieve action type */
	if (MATCH(self->args[k+1], "flags"))
		cmd->action = IMAPFA_REPLACE;
	else if (MATCH(self->args[k+1], "flags.silent")) {
		cmd->action = IMAPFA_REPLACE;
		cmd->silent = TRUE;
	} else if (MATCH(self->args[k+1], "+flags"))
		cmd->action = IMAPFA_ADD;
	else if (MATCH(self->args[k+1], "+flags.silent")) {
		cmd->action = IMAPFA_ADD;
		cmd->silent = TRUE;
	} else if (MATCH(self->args[k+1], "-flags"))
		cmd->action = IMAPFA_REMOVE;
	else if (MATCH(self->args[k+1], "-flags.silent")) {
		cmd->action = IMAPFA_REMOVE;
		cmd->silent = TRUE;
	}

	if (cmd->action == IMAPFA_NONE) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid STORE action specified\r\n", self->tag);
		D->status = 1;
		g_free(cmd);
		SESSION_RETURN;
	}

	/* now fetch flag list */
	i = (strcmp(self->args[k+2], "(") == 0) ? 3 : 2;

	for (; self->args[k+i] && strcmp(self->args[k+i], ")") != 0; i++) {
		for (j = 0; j < IMAP_NFLAGS; j++) {
			/* storing the recent flag explicitely is not allowed */
			if (MATCH(self->args[k+i],"\\Recent")) {
				dbmail_imap_session_buff_printf(self, "%s BAD invalid flag list to STORE command\r\n", self->tag);
				D->status = 1;
				g_free(cmd);
				SESSION_RETURN;
			}
				
			if (MATCH(self->args[k+i], imap_flag_desc_escaped[j])) {
				cmd->flaglist[j] = 1;
				break;
			}
		}

		if (j == IMAP_NFLAGS) {
			char *kw = self->args[k+i];
			cmd->keywords = g_list_append(cmd->keywords,g_strdup(kw));
			if (! MailboxState_hasKeyword(self->mailbox->mbstate, kw)) {
				MailboxState_addKeyword(self->mailbox->mbstate, kw);
				update = TRUE;
			}
		}
	}

	/** check ACL's for STORE */
	if (cmd->flaglist[IMAP_FLAG_SEEN] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_SEEN))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd->keywords);
			g_free(cmd);
			SESSION_RETURN;
		}
	}
	if (cmd->flaglist[IMAP_FLAG_DELETED] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_DELETED))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd->keywords);
			g_free(cmd);
			SESSION_RETURN;
		}
	}
	if (cmd->flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    cmd->flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    cmd->flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    g_list_length(cmd->keywords) > 0 ) {
		if ((result = mailbox_check_acl(self, self->mailbox->mbstate, ACL_RIGHT_WRITE))) {
			dbmail_imap_session_buff_printf(self, "%s NO access denied\r\n", self->tag);
			D->status = result;
			g_list_destroy(cmd->keywords);
			g_free(cmd);
			SESSION_RETURN;
		}
	}
	/* end of ACL checking. If we get here without returning, the user has
	   the right to store the flags */

	self->cmd = cmd;

	if ( update ) {
		char *flags = MailboxState_flags(self->mailbox->mbstate);
		dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
		dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)] Flags allowed.\r\n", flags);
		g_free(flags);
	}

	if ((result = _dm_imapsession_get_ids(self, self->args[k])) == DM_SUCCESS) {
		g_tree_foreach(self->ids, (GTraverseFunc) _do_store, D);
		if (self->ids) {
			db_mailbox_seq_update(MailboxState_getId(self->mailbox->mbstate));
		}
	}

	g_list_destroy(cmd->keywords);
	g_free(cmd);

	if (result || D->status) {
		if (result) D->status = result;
		SESSION_RETURN;
	}

	SESSION_OK;
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
	cmd_t cmd = self->cmd;
	uint64_t newid;
	int result;

	result = db_copymsg(*id, cmd->mailbox_id, self->userid, &newid, TRUE);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		return TRUE;
	}
	if (result == -2) {
		dbmail_imap_session_buff_printf(self, "%s NO quotum would exceed\r\n", self->tag);
		return TRUE;
	}
	return FALSE;
}


static void _ic_copy_enter(dm_thread_data *D)
{
	SESSION_GET;
	uint64_t destmboxid;
	int result;
	MailboxState_T S;
	cmd_t cmd;

	/* check if destination mailbox exists */
	if (! db_findmailbox(self->args[self->args_idx+1], self->userid, &destmboxid)) {
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

	cmd = g_malloc0(sizeof(*cmd));
	cmd->mailbox_id = destmboxid;
	self->cmd = cmd;

	if ((result = _dm_imapsession_get_ids(self, self->args[self->args_idx])) == DM_SUCCESS) {
		g_tree_foreach(self->ids, (GTraverseFunc) _do_copy, self);
		if (self->ids)
			db_mailbox_seq_update(destmboxid);
	}
  	
	g_free(self->cmd);
	self->cmd = NULL;

	if (result) {
		D->status = result;
		SESSION_RETURN;
	}

	if (MailboxState_getId(self->mailbox->mbstate) == destmboxid)
		dbmail_imap_session_mailbox_status(self, TRUE);

	SESSION_OK;
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
	if (MATCH(self->args[self->args_idx], "fetch")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_fetch(self);
	} else if (MATCH(self->args[self->args_idx], "copy")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_copy(self);
	} else if (MATCH(self->args[self->args_idx], "store")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_store(self);
	} else if (MATCH(self->args[self->args_idx], "search")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_search(self);
	} else if (MATCH(self->args[self->args_idx], "sort")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_sort(self);
	} else if (MATCH(self->args[self->args_idx], "thread")) {
		dbmail_imap_session_set_command(self, self->args[self->args_idx++]);
		result = _ic_thread(self);
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
				"* QUOTA \"\" (STORAGE %lu %lu)\r\n",
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
			self->args[self->args_idx], 
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

	if (! (quota = quota_get_quota(self->userid, self->args[self->args_idx], &errormsg))) {
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

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid);
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
	if (acl_set_rights(targetuserid, mboxid, self->args[self->args_idx+2]) < 0) {
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

	if (imap_acl_pre_administer(self->args[self->args_idx], 
				self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid) == -1) {
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

	if (! db_findmailbox(self->args[self->args_idx], self->userid, &mboxid)) {
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

	dbmail_imap_session_buff_printf(self, "* ACL \"%s\" %s\r\n", self->args[self->args_idx], acl_string);
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
	char *listrights_string;
	MailboxState_T S;
	SESSION_GET;

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid);
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
		self->args[self->args_idx], self->args[self->args_idx+1], listrights_string);
	g_free(listrights_string);

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

	if (! db_findmailbox(self->args[self->args_idx], self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO MYRIGHTS failure: unknown mailbox\r\n", self->tag);
		D->status=1;
		SESSION_RETURN;
	}

	if (!(myrights_string = acl_myrights(self->userid, mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		SESSION_RETURN;
	}

	dbmail_imap_session_buff_printf(self, "* MYRIGHTS \"%s\" %s\r\n", self->args[self->args_idx], myrights_string);
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


