/*
 Copyright (C) 1999-2004 IC & S  dbmail@D-s.nl
 Copyright (c) 2004-2008 NFG Net Facilities Group BV support@nfg.nl

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

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

extern int selfpipe[2];
extern GAsyncQueue *queue;
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];
extern const char AcceptedMailboxnameChars[];

int imap_before_smtp = 0;

/* 
 * push a message onto the queue and notify the
 * event-loop by sending a char into the selfpipe
 */
#define NOTIFY_DONE(D) \
	D->session->command_state=TRUE; \
	g_async_queue_push(queue, (gpointer)D); \
	if (selfpipe[1] > -1) write(selfpipe[1], "Q", 1); \
	return;

#define IC_DONE_OK \
	dbmail_imap_session_buff_printf(self, "%s OK %s%s completed\r\n", self->tag, self->use_uid ? "UID " : "", self->command)
/*
 * RETURN VALUES _ic_ functions:
 *
 * -1 Fatal error, close connection to user
 *  0 Succes
 *  1 Non-fatal error, connection stays alive
 */

/* 
 * ANY-STATE COMMANDS: capability, noop, logout
 */

/*
 * _ic_capability()
 *
 * returns a string to the client containing the server capabilities
 */
// a trivial silly thread example
void _ic_capability_enter(dm_thread_data *D)
{
	field_t val;
	gboolean override = FALSE;
	ImapSession *self = D->session;

	GETCONFIGVALUE("capability", "IMAP", val);
	if (strlen(val) > 0) override = TRUE;

	dbmail_imap_session_buff_printf(D->session, "* %s %s\r\n", D->session->command, override ? val : IMAP_CAPABILITY_STRING);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_capability(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, -1)) return 1;
	dm_thread_data_push((gpointer)self, _ic_capability_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_noop()
 *
 * performs No operation
 */

void _ic_noop_enter(dm_thread_data *D) 
{
	ImapSession *self = D->session;

	if (self->state == IMAPCS_SELECTED)
		dbmail_imap_session_mailbox_status(self, TRUE);
	
	IC_DONE_OK;
	NOTIFY_DONE(D);
}


int _ic_noop(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, -1)) return 1;

	dm_thread_data_push((gpointer)self, _ic_noop_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_logout()
 *
 * prepares logout from IMAP-server
 */
static void _ic_logout_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;
	dbmail_imap_session_mailbox_update_recent(self);
}

int _ic_logout(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, -1)) return 1;
	dbmail_imap_session_set_state(self, IMAPCS_LOGOUT);
	dm_thread_data_push((gpointer)self, _ic_logout_enter, NULL, NULL);
	TRACE(TRACE_MESSAGE, "[%p] userid:[%llu]", self, self->userid);
	return 2;
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
	ImapSession *self = D->session;
	if ((err = dbmail_imap_session_handle_auth(self,self->args[self->args_idx],self->args[self->args_idx+1]))) {
		D->status = err;
		NOTIFY_DONE(D);
	}
	if (imap_before_smtp) 
		db_log_ip(self->ci->ip_src);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_authenticate(ImapSession *self)
{

	if (self->command_type == IMAP_COMM_AUTH) {
		if (!check_state_and_args(self, 3, 3, IMAPCS_NON_AUTHENTICATED)) return 1;
		/* check authentication method */
		if (strcasecmp(self->args[self->args_idx], "login") != 0) {
			dbmail_imap_session_buff_printf(self, "%s NO Invalid authentication mechanism specified\r\n", self->tag);
			return 1;
		}
		self->args_idx++;
	} else {
		if (!check_state_and_args(self, 2, 2, IMAPCS_NON_AUTHENTICATED)) return 1;
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
	*(u64_t *)data = *(u64_t *)key;
	return TRUE;
}

char * mailbox_flags(MailboxInfo *info)
{
	char *s = NULL;
	GString *string = g_string_new("\\Seen \\Answered \\Deleted \\Flagged \\Draft");
	assert(info);

	if (info->keywords) {
		GString *keywords = g_list_join(info->keywords," ");
		g_string_append_printf(string, " %s", keywords->str);
		g_string_free(keywords,TRUE);
	}

	s = string->str;
	g_string_free(string, FALSE);
	return s;
}

static int imap_session_mailbox_close(ImapSession *self)
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

static int mailbox_check_acl(ImapSession *self, MailboxInfo *mailbox, ACLRight_t acl)
{
	int access = acl_has_right(mailbox, self->userid, acl);
	if (access < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (access == 0) {
		dbmail_imap_session_buff_printf(self, "%s NO permission denied\r\n", self->tag);
		return 1;
	}
	return 0;

}
static int imap_session_mailbox_check_acl(ImapSession * self, u64_t idnr,  ACLRight_t acl)
{
	int result;
	MailboxInfo *mailbox = dbmail_imap_session_mbxinfo_lookup(self, idnr);
	if ((result = mailbox_check_acl(self, mailbox, acl)) == 1)
		dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);
	return result;
}

static int imap_session_mailbox_open(ImapSession * self, const char * mailbox)
{
	int err;
	u64_t mailbox_idnr = 0;

	/* get the mailbox_idnr */
	db_findmailbox(mailbox, self->userid, &mailbox_idnr);
	
	/* create missing INBOX for this authenticated user */
	if ((! mailbox_idnr ) && (strcasecmp(mailbox, "INBOX")==0)) {
		TRACE(TRACE_INFO, "[%p] Auto-creating INBOX for user id [%llu]", self, self->userid);
		err = db_createmailbox("INBOX", self->userid, &mailbox_idnr);
	}
	
	if (! mailbox_idnr) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1; /* error */
	}

	/* check if user has right to select mailbox */
	if ((err = imap_session_mailbox_check_acl(self, mailbox_idnr, ACL_RIGHT_READ))) return err;
	
	/* check if mailbox is selectable */
	if ((err = dbmail_imap_session_mailbox_get_selectable(self, mailbox_idnr)))
		return err;

	/* new mailbox structure */
	self->mailbox = dbmail_mailbox_new(mailbox_idnr);

	/* fetch mailbox metadata */
	self->mailbox->info = dbmail_imap_session_mbxinfo_lookup(self, mailbox_idnr);

	/* keep these in sync */
	self->mailbox->info->exists = g_tree_nnodes(self->mailbox->ids);

	return 0;
}

static void _ic_select_enter(dm_thread_data *D)
{
	int err;
	char *flags;
	const char *okarg;
	ImapSession *self = D->session;

	/* close the currently opened mailbox */
	imap_session_mailbox_close(self);

	if ((err = imap_session_mailbox_open(self, self->args[self->args_idx]))) {
		D->status = err;
		NOTIFY_DONE(D);
	}	
	dbmail_imap_session_set_state(self,IMAPCS_SELECTED);

	dbmail_imap_session_buff_printf(self, "* %u EXISTS\r\n", self->mailbox->info->exists);
	dbmail_imap_session_buff_printf(self, "* %u RECENT\r\n", self->mailbox->info->recent);

	/* flags */
	flags = mailbox_flags(self->mailbox->info);
	dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
	dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)]\r\n", flags);
	g_free(flags);

	/* UIDNEXT */
	dbmail_imap_session_buff_printf(self, "* OK [UIDNEXT %llu] Predicted next UID\r\n",
		self->mailbox->info->msguidnext);
	
	/* UID */
	dbmail_imap_session_buff_printf(self, "* OK [UIDVALIDITY %llu] UID value\r\n",
		self->mailbox->info->uid);

	if (self->command_type == IMAP_COMM_SELECT) {
		// SELECT
		if (self->mailbox->info->exists) { 
			/* show msn of first unseen msg (if present) */
			u64_t key = 0, *msn = NULL;
			g_tree_foreach(self->mailbox->msginfo, (GTraverseFunc)mailbox_first_unseen, &key);
			if ( (key > 0) && (msn = g_tree_lookup(self->mailbox->ids, &key)))
				dbmail_imap_session_buff_printf(self, "* OK [UNSEEN %llu] first unseen message\r\n", *msn);
		}
		self->mailbox->info->permission = IMAPPERM_READWRITE;
		okarg = "READ-WRITE";
	} else {
		// EXAMINE
		self->mailbox->info->permission = IMAPPERM_READ;
		okarg = "READ-ONLY";
	}

	dbmail_imap_session_buff_printf(self, "%s OK [%s] %s completed\r\n", self->tag, okarg, self->command);

	NOTIFY_DONE(D);
}

int _ic_select(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
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
	u64_t mboxid;
	const char *message;
	GString *s = g_string_new("");

	result = db_mailbox_create_with_parents(D->session->args[D->session->args_idx], BOX_COMMANDLINE, D->session->userid, &mboxid, &message);

	if (result > 0)
		dbmail_imap_session_buff_printf(D->session, "%s NO %s\r\n", D->session->tag, message);
	else if (result < 0)
		dbmail_imap_session_buff_printf(D->session, "* BYE internal dbase error\r\n");
	else
		dbmail_imap_session_buff_printf(D->session, "%s OK %s completed\r\n", D->session->tag, D->session->command);

	g_string_free(s, FALSE);

	NOTIFY_DONE(D);
}


int _ic_create(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_create_enter, _ic_cb_leave, NULL);
	return DM_SUCCESS;
}


/* _ic_delete()
 *
 * deletes a specified mailbox
 */
void _ic_delete_enter(dm_thread_data *D)
{
	int result;
	u64_t mailbox_idnr;
	GList *children = NULL;
	ImapSession *self = D->session;
	char *mailbox = D->session->args[0];
	unsigned nchildren = 0;
	

	/* check if there is an attempt to delete inbox */
	if (MATCH(self->args[0], "INBOX")) {
		dbmail_imap_session_buff_printf(self, "%s NO cannot delete special mailbox INBOX\r\n", self->tag);
		D->status=1;
		NOTIFY_DONE(D);
	}

	if (! (db_findmailbox(mailbox, self->userid, &mailbox_idnr)) ) {
		dbmail_imap_session_buff_printf(self, "%s NO mailbox doesn't exists\r\n", self->tag);
		D->status=1;
		NOTIFY_DONE(D);
	}

	/* Check if the user has ACL delete rights to this mailbox */
	if ((result = imap_session_mailbox_check_acl(self, mailbox_idnr, ACL_RIGHT_DELETE))) {
		D->status=result;
		NOTIFY_DONE(D);
	}
	
	/* check for children of this mailbox */
	if ((result = db_listmailboxchildren(mailbox_idnr, self->userid, &children)) == DM_EQUERY) {
		TRACE(TRACE_ERROR, "[%p] cannot retrieve list of mailbox children", self);
		dbmail_imap_session_buff_printf(self, "* BYE dbase/memory error\r\n");
		D->status= -1;
		NOTIFY_DONE(D);
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
			NOTIFY_DONE(D);
		}
		if (result == DM_EQUERY) {
			dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
			D->status= -1;	/* fatal */
			NOTIFY_DONE(D);
		}

		/* mailbox has inferior names; remove all msgs and set noselect flag */
		{
			C c; int t = DM_SUCCESS;
			u64_t mailbox_size;

			if (! mailbox_is_writable(mailbox_idnr)) {
				D->status=DM_EQUERY;
				NOTIFY_DONE(D);
			}

			if (db_get_mailbox_size(mailbox_idnr, 0, &mailbox_size) == DM_EQUERY) {
				D->status=DM_EQUERY;
				NOTIFY_DONE(D);
			}

			/* update messages in this mailbox: mark as deleted (status MESSAGE_STATUS_PURGE) */
			c = db_con_get();
			TRY
				db_begin_transaction(c);
				Connection_execute(c, "UPDATE %smessages SET status=%d WHERE mailbox_idnr = %llu", DBPFX, MESSAGE_STATUS_PURGE, mailbox_idnr);
				Connection_execute(c, "UPDATE %smailboxes SET no_select = 1 WHERE mailbox_idnr = %llu", DBPFX, mailbox_idnr);
				db_commit_transaction(c);
			CATCH(SQLException)
				LOG_SQLERROR;
			t = DM_EQUERY;
			FINALLY
				Connection_close(c);
			END_TRY;

			if (t == DM_EQUERY) {
				D->status=t;
				NOTIFY_DONE(D);
			}

			db_mailbox_mtime_update(mailbox_idnr);
			if (! dm_quota_user_dec(self->userid, mailbox_size)) {
				D->status=DM_EQUERY;
				NOTIFY_DONE(D);
			}
		}

		/* check if this was the currently selected mailbox */
		if (self->mailbox && self->mailbox->info && (mailbox_idnr == self->mailbox->info->uid)) 
			dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);

		/* ok done */
		IC_DONE_OK;
		NOTIFY_DONE(D);
	}

	/* ok remove mailbox */
	if (db_delete_mailbox(mailbox_idnr, 0, 1)) {
		dbmail_imap_session_buff_printf(self,"%s NO DELETE failed\r\n", self->tag);
		D->status=DM_EGENERAL;
		NOTIFY_DONE(D);
	}

	/* check if this was the currently selected mailbox */
	if (self->mailbox && self->mailbox->info && (mailbox_idnr == self->mailbox->info->uid)) 
		dbmail_imap_session_set_state(self, IMAPCS_AUTHENTICATED);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_delete(ImapSession *self) 
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_delete_enter, _ic_cb_leave, NULL);
	return 0;
}
/* _ic_rename()
 *
 * renames a specified mailbox
 */
static int mailbox_rename(MailboxInfo *mb, const char *newname)
{
	char *oldname = mb->name;
	if ( (db_setmailboxname(mb->uid, newname)) == DM_EQUERY) return DM_EQUERY;
	mb->name = g_strdup(newname);
	g_free(oldname);
	oldname = NULL;
	return DM_SUCCESS;
}
void _ic_rename_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;
	GList *children = NULL;
	u64_t mboxid, newmboxid;
	u64_t parentmboxid = 0;
	size_t oldnamelen;
	int i, result;
	char newname[IMAP_MAX_MAILBOX_NAMELEN];
	MailboxInfo *mb;


	if (! (db_findmailbox(self->args[0], self->userid, &mboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

	/* check if new name is valid */
        if (!checkmailboxname(self->args[1])) {
	        dbmail_imap_session_buff_printf(self, "%s NO new mailbox name contains invalid characters\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
        }

	if ((db_findmailbox(self->args[1], self->userid, &newmboxid))) {
		dbmail_imap_session_buff_printf(self, "%s NO new mailbox already exists\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
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
		NOTIFY_DONE(D);
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
			NOTIFY_DONE(D);
		}

		/* ok, reset arg */
		self->args[1][i] = '/';
	}

	/* Check if the user has ACL delete rights to old name, 
	 * and create rights to the parent of the new name, or
	 * if the user just owns both mailboxes. */
	if ((result = imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_DELETE))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	if (!parentmboxid) {
		TRACE(TRACE_DEBUG, "[%p] Destination is a top-level mailbox; not checking right to CREATE.", self);
	} else {
		TRACE(TRACE_DEBUG, "[%p] Checking right to CREATE under [%llu]", self, parentmboxid);
		if ((result = imap_session_mailbox_check_acl(self, parentmboxid, ACL_RIGHT_CREATE))) {
			D->status = result;
			NOTIFY_DONE(D);
		}

		TRACE(TRACE_DEBUG, "[%p] We have the right to CREATE under [%llu]", self, parentmboxid);
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
			NOTIFY_DONE(D);
		}

		result = db_movemsg(newmboxid, mboxid);
		if (result == -1) {
			dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
			D->status = result;
			NOTIFY_DONE(D);
		}

		IC_DONE_OK;
		NOTIFY_DONE(D);
	}

	/* check for inferior names */
	result = db_listmailboxchildren(mboxid, self->userid, &children);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = result;
		NOTIFY_DONE(D);
	}

	/* replace name for each child */
	children = g_list_first(children);

	while (children) {
		u64_t childid = *(u64_t *)children->data;
		mb = dbmail_imap_session_mbxinfo_lookup(self, childid);

		g_snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s", self->args[1], &mb->name[oldnamelen]);
		if ((mailbox_rename(mb, newname)) != DM_SUCCESS) {
			dbmail_imap_session_buff_printf(self, "* BYE error renaming mailbox\r\n");
			g_list_destroy(children);
			D->status = DM_EGENERAL;
			NOTIFY_DONE(D);
		}
		if (! g_list_next(children)) break;
		children = g_list_next(children);
	}

	if (children)
		g_list_destroy(children);

	/* now replace name */
	mb = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	if ((mailbox_rename(mb, self->args[1])) != DM_SUCCESS) {
		dbmail_imap_session_buff_printf(self, "* BYE error renaming mailbox\r\n");
		D->status = DM_EGENERAL;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_rename(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, IMAPCS_AUTHENTICATED)) return 1;
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
	ImapSession *self = D->session;
	u64_t mboxid;
	int result = 0;
	
	if (! (db_findmailbox(self->args[0], self->userid, &mboxid))) {
		dbmail_imap_session_buff_printf(self, "%s OK %s on mailbox that does not exist\r\n", self->tag, self->command);
		NOTIFY_DONE(D);
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */

	if (imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_LOOKUP)) {
		D->status = 1;
		NOTIFY_DONE(D);
	}

	if (self->command_type == IMAP_COMM_SUBSCRIBE)
		result = db_subscribe(mboxid, self->userid);
	else
		result = db_unsubscribe(mboxid, self->userid);

	if (result == DM_EQUERY) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = DM_EQUERY;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);

}
int _ic_subscribe(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
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
	ImapSession *self = D->session;
	int list_is_lsub = 0;
	GList *plist = NULL, *children = NULL;
	char *pstring;
	MailboxInfo *mb = NULL;
	size_t slen;
	unsigned i;
	char *pattern;

	/* check if self->args are both empty strings, i.e. A001 LIST "" "" 
	   this has special meaning; show root & delimiter */
	if (strlen(self->args[0]) == 0 && strlen(self->args[1]) == 0) {
		dbmail_imap_session_buff_printf(self, "* %s (\\NoSelect) \"/\" \"\"\r\n", self->command);
		IC_DONE_OK;
		NOTIFY_DONE(D);
	}

	/* check the reference name, should contain only accepted mailboxname chars */
	for (i = 0, slen = strlen(AcceptedMailboxnameChars); self->args[0][i]; i++) {
		if (index(AcceptedMailboxnameChars, self->args[0][i]) == NULL) {
			dbmail_imap_session_buff_printf(self, "%s BAD reference name contains invalid characters\r\n", self->tag);
			D->status = 1;
			NOTIFY_DONE(D);
		}
	}
	pattern = g_strdup_printf("%s%s", self->args[0], self->args[1]);

	TRACE(TRACE_INFO, "[%p] search with pattern: [%s]", self, pattern);

	if (D->session->command_type == IMAP_COMM_LSUB) list_is_lsub = 1;

	D->status = db_findmailbox_by_regex(D->session->userid, pattern, &children, list_is_lsub);
	if (D->status == -1) {
		dbmail_imap_session_buff_printf(D->session, "* BYE internal dbase error\r\n");
		NOTIFY_DONE(D);
	} else if (D->status == 1) {
		dbmail_imap_session_buff_printf(D->session, "%s BAD invalid pattern specified\r\n", D->session->tag);
		NOTIFY_DONE(D);
	}

	while ((! D->status) && children) {
		u64_t mailbox_id = *(u64_t *)children->data;
		mb = dbmail_imap_session_mbxinfo_lookup(self, mailbox_id);
		
		plist = NULL;
		if (mb->no_select)
			plist = g_list_append(plist, g_strdup("\\noselect"));
		if (mb->no_inferiors)
			plist = g_list_append(plist, g_strdup("\\noinferiors"));
		if (mb->no_children)
			plist = g_list_append(plist, g_strdup("\\hasnochildren"));
		else
			plist = g_list_append(plist, g_strdup("\\haschildren"));
		
		/* show */
		pstring = dbmail_imap_plist_as_string(plist);
		dbmail_imap_session_buff_printf(self, "* %s %s \"%s\" \"%s\"\r\n", D->session->command, 
				pstring, MAILBOX_SEPARATOR, mb->name);
		
		g_list_destroy(plist);
		g_free(pstring);

		if (! g_list_next(children)) break;
		children = g_list_next(children);
	}

	if (children) g_list_destroy(children);
	g_free(pattern);

	if (! D->status) dbmail_imap_session_buff_printf(self, "%s OK %s completed\r\n", D->session->tag, D->session->command);

	NOTIFY_DONE(D);
}

int _ic_list(ImapSession *self)
{

	if (!check_state_and_args(self, 2, 2, IMAPCS_AUTHENTICATED)) return 1;
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
	ImapSession *self = D->session;
	MailboxInfo *mb;
	u64_t id;
	int i, endfound, result;
	GList *plst = NULL;
	gchar *pstring, *astring;
	
	if (self->args[1][0] != '(') {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list should be parenthesed\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

	/* check final arg: should be ')' and no new '(' in between */
	for (i = 2, endfound = 0; self->args[i]; i++) {
		if (self->args[i][0] == ')') {
			endfound = i;
			break;
		} else if (self->args[i][0] == '(') {
			dbmail_imap_session_buff_printf(self, "%s BAD too many parentheses specified\r\n", self->tag);
			D->status = 1;
			NOTIFY_DONE(D);
		}
	}

	if (endfound == 2) {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list empty\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}
	if (self->args[endfound + 1]) {
		dbmail_imap_session_buff_printf(self, "%s BAD argument list too long\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

	/* check if mailbox exists */
	if (! db_findmailbox(self->args[0], self->userid, &id)) {
		/* create missing INBOX for this authenticated user */
		if ((! id ) && (MATCH(self->args[0], "INBOX"))) {
			TRACE(TRACE_INFO, "[%p] Auto-creating INBOX for user id [%llu]", self, self->userid);
			db_createmailbox("INBOX", self->userid, &id);
		}
		if (! id) {
			dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
			D->status = 1;
			NOTIFY_DONE(D);
		}
	}

	if (! (mb = dbmail_imap_session_mbxinfo_lookup(self, id))) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

	if ((result = mailbox_check_acl(self, mb, ACL_RIGHT_READ))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	for (i = 2; self->args[i]; i++) {
		if (MATCH(self->args[i], "messages"))
			plst = g_list_append_printf(plst,"MESSAGES %u", mb->exists);
		else if (MATCH(self->args[i], "recent"))
			plst = g_list_append_printf(plst,"RECENT %u", mb->recent);
		else if (MATCH(self->args[i], "unseen"))
			plst = g_list_append_printf(plst,"UNSEEN %u", mb->unseen);
		else if (MATCH(self->args[i], "uidnext"))
			plst = g_list_append_printf(plst,"UIDNEXT %llu", mb->msguidnext);
		else if (MATCH(self->args[i], "uidvalidity"))
			plst = g_list_append_printf(plst,"UIDVALIDITY %llu", mb->uid);
		else if (MATCH(self->args[i], ")"))
			break;
		else {
			dbmail_imap_session_buff_printf(self, "\r\n%s BAD option '%s' specified\r\n",
				self->tag, self->args[i]);
			D->status = 1;
			NOTIFY_DONE(D);
		}
	}
	astring = dbmail_imap_astring_as_string(self->args[0]);
	pstring = dbmail_imap_plist_as_string(plst); 
	g_list_destroy(plst);

	dbmail_imap_session_buff_printf(self, "* STATUS %s %s\r\n", astring, pstring);	
	g_free(astring); g_free(pstring);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_status(ImapSession *self)
{
	if (!check_state_and_args(self, 3, 0, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_status_enter, _ic_cb_leave, NULL);
	return 0;
}

/* _ic_idle
 *
 */

int imap_idle_loop(ImapSession *self, int timeout)
{
	gpointer data;
	char *message;
	GTimeVal end_time;

	TRACE(TRACE_DEBUG,"[%p]", self);
	do {
		g_get_current_time(&end_time);
		g_time_val_add(&end_time, 1000000*timeout);
		data = g_async_queue_timed_pop(self->ci->queue, &end_time);
		if (data) {
			dm_thread_data *D = (gpointer)data;
			message = (char *)D->data;
			if (strlen(message) > 4 && strncasecmp(message,"DONE",4)==0) {
				return 0;
			} else if (strlen(message) > 0) {
				dbmail_imap_session_buff_printf(self,"%s BAD Expecting DONE\r\n", self->tag);
				return 1;
			}
			g_free(D->data);
			g_free(D);
		} else {
			TRACE(TRACE_DEBUG,"[%p]", self);
			if (! (self->loop++ % 10)) dbmail_imap_session_buff_printf(self, "* OK\r\n");
			dbmail_imap_session_mailbox_status(self,TRUE);
			dbmail_imap_session_buff_flush(self);
		}

	} while (TRUE);

	TRACE(TRACE_WARNING, "[%p] uncaught condition", self);
	return 1;
}


#define IDLE_TIMEOUT 30

void _ic_idle_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;
	int t = FALSE, idle_timeout = IDLE_TIMEOUT;
	field_t val;

	GETCONFIGVALUE("idle_timeout", "IMAP", val);
	if ( strlen(val) && (idle_timeout = atoi(val)) <= 0 ) {
		TRACE(TRACE_ERROR, "[%p] illegal value for idle_timeout [%s]", self, val);
		idle_timeout = IDLE_TIMEOUT;	
	}
	
	dbmail_imap_session_mailbox_status(self,TRUE);
	TRACE(TRACE_DEBUG,"[%p] start IDLE [%s]", self, self->tag);
	dbmail_imap_session_buff_printf(self, "+ idling\r\n");
	dbmail_imap_session_buff_flush(self);
	
	if ((t = imap_idle_loop(self, idle_timeout))) {
		D->status = t;
		NOTIFY_DONE(D);
	}
	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_idle(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_idle_enter, _ic_cb_leave, NULL);
	event_add(self->ci->rev, self->timeout);
	return 0;
}

/* _ic_append()
 *
 * append a message to a mailbox
 */
static int imap_append_msg(const char *msgdata,
		       u64_t mailbox_idnr, u64_t user_idnr,
		       timestring_t internal_date, u64_t * msg_idnr)
{
        DbmailMessage *message;
	int result;
	GString *msgdata_string;

	if (! mailbox_is_writable(mailbox_idnr)) return DM_EQUERY;

	msgdata_string = g_string_new("");
	g_string_printf(msgdata_string, "%s", msgdata);

        message = dbmail_message_new();
        message = dbmail_message_init_with_string(message, msgdata_string);
	dbmail_message_set_internal_date(message, (char *)internal_date);
	g_string_free(msgdata_string, TRUE); 
        
	/* 
         * according to the rfc, the recent flag has to be set to '1'.
	 * this also means that the status will be set to '001'
         */

        if (dbmail_message_store(message) < 0) {
		dbmail_message_free(message);
		return DM_EQUERY;
	}

	result = db_copymsg(message->id, mailbox_idnr, user_idnr, msg_idnr);
	db_delete_message(message->id);
        dbmail_message_free(message);
	
        switch (result) {
            case -2:
                    TRACE(TRACE_DEBUG, "error copying message to user [%llu],"
                            "maxmail exceeded", user_idnr);
                    return -2;
            case -1:
                    TRACE(TRACE_ERROR, "error copying message to user [%llu]", 
                            user_idnr);
                    return -1;
        }
                
        TRACE(TRACE_MESSAGE, "message id=%llu is inserted", *msg_idnr);
        
        return db_set_message_status(*msg_idnr, MESSAGE_STATUS_SEEN);
}

void _ic_append_enter(dm_thread_data *D)
{
	u64_t mboxid, message_id = 0;
	int i, j, result;
	timestring_t sqldate;
	int flaglist[IMAP_NFLAGS], flagcount = 0;
	GList *keywords = NULL;
	MailboxInfo *mbx = NULL;
	ImapSession *self = D->session;
	char *message;

	memset(flaglist,0,sizeof(flaglist));

	/* find the mailbox to place the message */
	if (! db_findmailbox(self->args[0], self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO [TRYCREATE]\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

	mbx = dbmail_imap_session_mbxinfo_lookup(self, mboxid);

	/* check if user has right to append to  mailbox */
	if ((result = imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_INSERT))) {
		D->status = result;
		NOTIFY_DONE(D);
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
		NOTIFY_DONE(D);
	}

	/** check ACL's for STORE */
	if (flaglist[IMAP_FLAG_SEEN] == 1) {
		if ((result = mailbox_check_acl(self, mbx, ACL_RIGHT_SEEN))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}
	if (flaglist[IMAP_FLAG_DELETED] == 1) {
		if ((result = mailbox_check_acl(self, mbx, ACL_RIGHT_DELETE))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}
	if (flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    flaglist[IMAP_FLAG_RECENT] == 1 ||
	    g_list_length(keywords) > 0) {
		if ((result = mailbox_check_acl(self, mbx, ACL_RIGHT_WRITE))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}


	/* there could be a literal date here, check if the next argument exists
	 * if so, assume this is the literal date.
	 */
	if (self->args[i + 1]) {
		struct tm tm;
		char *dt = self->args[i];

		memset(&tm, 0, sizeof(struct tm));

		dt = g_strstrip(dt);

		if (strptime(dt, "%d-%b-%Y %T", &tm) != NULL)
			strftime(sqldate, sizeof(sqldate), "%Y-%m-%d %H:%M:%S", &tm);
		else
			sqldate[0] = '\0';
		/* internal date specified */

		i++;
		TRACE(TRACE_DEBUG, "[%p] internal date [%s] found, next arg [%s]", self, sqldate, self->args[i]);
	} else {
		sqldate[0] = '\0';
	}

	message = self->args[i];

	D->status = imap_append_msg(message, mboxid, D->session->userid, sqldate, &message_id);
	switch (D->status) {
	case -1:
		TRACE(TRACE_ERROR, "[%p] error appending msg", D->session);
		dbmail_imap_session_buff_printf(D->session, "* BYE internal dbase error storing message\r\n");
		break;

	case -2:
		TRACE(TRACE_INFO, "[%p] quotum would exceed", D->session);
		dbmail_imap_session_buff_printf(D->session, "%s NO not enough quotum left\r\n", D->session->tag);
		break;

	case FALSE:
		TRACE(TRACE_ERROR, "[%p] faulty msg", D->session);
		dbmail_imap_session_buff_printf(D->session, "%s NO invalid message specified\r\n", D->session->tag);
		break;
	case TRUE:
		if (flagcount > 0) {
			if (db_set_msgflag(message_id, mboxid, flaglist, keywords, IMAPFA_ADD, NULL) < 0) {
				TRACE(TRACE_ERROR, "[%p] error setting flags for message [%llu]", D->session, message_id);
			}
		}

		db_mailbox_mtime_update(mboxid);
		break;
	}

	if (message_id && self->state == IMAPCS_SELECTED) {
		//insert new Messageinfo struct into self->mailbox->msginfo
		//
		int j = 0;
		u64_t *uid;
		MessageInfo *msginfo = g_new0(MessageInfo,1);

		/* id */
		msginfo->id = message_id;

		/* mailbox_id */
		msginfo->mailbox_id = mboxid;

		/* flags */
		for (j = 0; j < IMAP_NFLAGS; j++)
			msginfo->flags[j] = flaglist[j];

		/* keywords */
		msginfo->keywords = keywords;

		/* internal date */
		strncpy(msginfo->internaldate, 
				strlen(sqldate) ? sqldate : "01-Jan-1970 00:00:01 +0100", 
				IMAP_INTERNALDATE_LEN);

		/* rfcsize */
		msginfo->rfcsize = strlen(message);

		uid = g_new0(u64_t,1);
		*uid = message_id;
		g_tree_insert(self->mailbox->msginfo, uid, msginfo); 

		self->mailbox->info->exists++;

		dbmail_mailbox_insert_uid(self->mailbox, message_id);

		dbmail_imap_session_mailbox_status(self, FALSE);
	} else {
		g_list_destroy(keywords);
	}

	dbmail_imap_session_buff_printf(D->session, "%s OK APPEND completed\r\n", D->session->tag);

	NOTIFY_DONE(D);
}

int _ic_append(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 0, IMAPCS_AUTHENTICATED)) return 1;
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
	ImapSession *self = D->session;
	int result;
	if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_READ))) {
		D->status=result;
		NOTIFY_DONE(D);
	}

	dbmail_imap_session_mailbox_status(self, TRUE);
	
	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_check(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_SELECTED)) return 1;
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
	ImapSession *self = D->session;
	int result = acl_has_right(self->mailbox->info, self->userid, ACL_RIGHT_DELETE);
	if (result < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE Internal database error\r\n");
		D->status=result;
		NOTIFY_DONE(D);
	}
	/* only perform the expunge if the user has the right to do it */
	if (result == 1)
		if (self->mailbox->info->permission == IMAPPERM_READWRITE)
			dbmail_imap_session_mailbox_expunge(self);

	dbmail_imap_session_set_state(self, IMAPCS_AUTHENTICATED);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}
	
int _ic_close(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_close_enter, _ic_cb_leave, NULL);
	return 0;
}

/* _ic_unselect
 *
 * non-expunging close for select mailbox and return to AUTH state
 */
static void _ic_unselect_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;
	imap_session_mailbox_close(self);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_unselect(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_SELECTED)) return 1;
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
	ImapSession *self = D->session;

	if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_DELETE))) {
		D->status = result;
		NOTIFY_DONE(D);
	}
	
	if (dbmail_imap_session_mailbox_expunge(self) != DM_SUCCESS) {
		dbmail_imap_session_buff_printf(self, "* BYE expunge failed\r\n");
		D->status = DM_EQUERY;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_expunge(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_SELECTED)) return 1;

	if (self->mailbox->info->permission != IMAPPERM_READWRITE) {
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
	ImapSession *self = D->session;
	DbmailMailbox *mb;
	int result = 0;
	gchar *s = NULL;
	const gchar *cmd;
	gboolean sorted;

	search_order_t order = *(search_order_t *)D->data;

	if (order == SEARCH_SORTED) sorted = 1;
	
	if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_READ))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

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
	if (g_tree_nnodes(mb->ids) > 0) {
		dbmail_mailbox_set_uid(mb,self->use_uid);

		if (dbmail_mailbox_build_imap_search(mb, self->args, &(self->args_idx), order) < 0) {
			dbmail_imap_session_buff_printf(self, "%s BAD invalid arguments to %s\r\n",
				self->tag, cmd);
			D->status = 1;
			NOTIFY_DONE(D);
		}
		dbmail_mailbox_search(mb);
		/* ok, display results */
		switch(order) {
			case SEARCH_SORTED:
				dbmail_mailbox_sort(mb);
				s = dbmail_mailbox_sorted_as_string(mb);
			break;
			case SEARCH_UNORDERED:
				s = dbmail_mailbox_ids_as_string(mb);
			break;
			case SEARCH_THREAD_ORDEREDSUBJECT:
				s = dbmail_mailbox_orderedsubject(mb);
			break;
			case SEARCH_THREAD_REFERENCES:
				s = NULL; // TODO: unsupported
			break;
		}
	}

	if (s) {
		dbmail_imap_session_buff_printf(self, "* %s %s\r\n", cmd, s);
		g_free(s);
	} else {
		dbmail_imap_session_buff_printf(self, "* %s\r\n", cmd);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

static int sorted_search(ImapSession *self, search_order_t order)
{
	if (!check_state_and_args(self, 1, 0, IMAPCS_SELECTED)) return 1;
	search_order_t *data = g_new0(search_order_t,1);
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
	if (MATCH(self->args[0],"ORDEREDSUBJECT"))
		return sorted_search(self,SEARCH_THREAD_ORDEREDSUBJECT);
	if (MATCH(self->args[0],"REFERENCES"))
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

	if ( (!self->ids) || (g_tree_nnodes(self->ids)==0) ) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid message range specified\r\n", self->tag);
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
	ImapSession *self = D->session;
	int result, state, setidx;

	/* check if the user has the right to fetch messages in this mailbox */
	if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_READ))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	if (self->fi) {
		dbmail_imap_session_bodyfetch_free(self);
		g_free(self->fi);
		self->fi = NULL;
	}
	self->fi = g_new0(fetch_items_t,1);
	self->fi->getUID = self->use_uid;

	setidx = self->args_idx;
	TRACE(TRACE_DEBUG, "id-set: [%s]", self->args[self->args_idx]);
	self->args_idx++; //skip on past this for the fetch_parse_args coming next...

	state = 1;
	do {
		if ( (state = dbmail_imap_session_fetch_parse_args(self)) == -2) {
			dbmail_imap_session_buff_printf(self, "%s BAD invalid argument list to fetch\r\n", self->tag);
			D->status = 1;
			NOTIFY_DONE(D);
		}
		TRACE(TRACE_DEBUG,"[%p] dbmail_imap_session_fetch_parse_args loop idx %llu state %d ", self, self->args_idx, state);
		self->args_idx++;
	} while (state > 0);

	result = DM_SUCCESS;

  	if (g_tree_nnodes(self->mailbox->ids) > 0) {
 		if ((result = _dm_imapsession_get_ids(self, self->args[setidx])) == DM_SUCCESS) {
  			self->ids_list = g_tree_keys(self->ids);
  			result = dbmail_imap_session_fetch_get_items(self);
  		}
	}

	dbmail_imap_session_fetch_free(self);
	dbmail_imap_session_args_free(self, FALSE);

	if (result) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_fetch(ImapSession *self)
{
	if (!check_state_and_args (self, 2, 0, IMAPCS_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_fetch_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_store()
 *
 * alter message-associated data in selected mailbox
 */

static gboolean _do_store(u64_t *id, gpointer UNUSED value, ImapSession *self)
{
	cmd_store_t *cmd = (cmd_store_t *)self->cmd;

	u64_t *msn;
	MessageInfo *msginfo = NULL;
	char *s;
	int i;

	if (self->mailbox && self->mailbox->msginfo)
		msginfo = g_tree_lookup(self->mailbox->msginfo, id);

	if (! msginfo) {
		TRACE(TRACE_WARNING, "[%p] unable to lookup msginfo struct for [%llu]", self, *id);
		return TRUE;
	}

	msn = g_tree_lookup(self->mailbox->ids, id);

	if (self->mailbox->info->permission == IMAPPERM_READWRITE) {
		if (db_set_msgflag(*id, self->mailbox->info->uid, cmd->flaglist, cmd->keywords, cmd->action, msginfo) < 0) {
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
			return TRUE;
		}
	}

	// Set the system flags
	for (i = 0; i < IMAP_NFLAGS; i++) {
		
		if (i == IMAP_FLAG_RECENT) // Skip recent_flag because it is already part of the query.
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
		s = imap_flags_as_string(msginfo);
		dbmail_imap_session_buff_printf(self,"* %llu FETCH (FLAGS %s)\r\n", *msn, s);
		g_free(s);
	}

	return FALSE;
}

static void _ic_store_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;
	cmd_store_t *cmd;
	int result, i, j, k;

	cmd = g_new0(cmd_store_t,1);
	k = self->args_idx;
	/* multiple flags should be parenthesed */
	if (self->args[k+3] && strcmp(self->args[k+2], "(") != 0) {
		dbmail_imap_session_buff_printf(self, "%s BAD invalid argument(s) to STORE\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}

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
		NOTIFY_DONE(D);
	}

	/* now fetch flag list */
	i = (strcmp(self->args[k+2], "(") == 0) ? 3 : 2;

	for (; self->args[k+i] && strcmp(self->args[k+i], ")") != 0; i++) {
		for (j = 0; j < IMAP_NFLAGS; j++) {
			/* storing the recent flag explicitely is not allowed */
			if (MATCH(self->args[k+i],"\\Recent")) {
				dbmail_imap_session_buff_printf(self, "%s BAD invalid flag list to STORE command\r\n", self->tag);
				D->status = 1;
				NOTIFY_DONE(D);
			}
				
			if (MATCH(self->args[k+i], imap_flag_desc_escaped[j])) {
				cmd->flaglist[j] = 1;
				break;
			}
		}

		if (j == IMAP_NFLAGS)
			cmd->keywords = g_list_append(cmd->keywords,g_strdup(self->args[k+i]));
	}

	/** check ACL's for STORE */
	if (cmd->flaglist[IMAP_FLAG_SEEN] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_SEEN))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}
	if (cmd->flaglist[IMAP_FLAG_DELETED] == 1) {
		if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_DELETE))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}
	if (cmd->flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    cmd->flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    cmd->flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    g_list_length(cmd->keywords) > 0 ) {
		if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_WRITE))) {
			D->status = result;
			NOTIFY_DONE(D);
		}
	}
	/* end of ACL checking. If we get here without returning, the user has
	   the right to store the flags */

	self->cmd = cmd;
	
	result = DM_SUCCESS;

  	if (g_tree_nnodes(self->mailbox->ids) > 0) {
 		if ((result = _dm_imapsession_get_ids(self, self->args[k])) == DM_SUCCESS)
 			g_tree_foreach(self->ids, (GTraverseFunc) _do_store, self);
  	}	

	if (cmd->action == IMAPFA_ADD) {
		guint l = g_list_length(self->mailbox->info->keywords);
		g_list_merge(&self->mailbox->info->keywords, cmd->keywords, cmd->action, (GCompareFunc)g_ascii_strcasecmp);
		if ( l < g_list_length(self->mailbox->info->keywords) ) {
			char *flags = mailbox_flags(self->mailbox->info);
			dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
			dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)]\r\n", flags);
			g_free(flags);
		}
		
	}

	if (result) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_store(ImapSession *self)
{
	if (!check_state_and_args (self, 2, 0, IMAPCS_SELECTED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_store_enter, _ic_cb_leave, NULL);
	return 0;
}

/*
 * _ic_copy()
 *
 * copy a message to another mailbox
 */

static gboolean _do_copy(u64_t *id, gpointer UNUSED value, ImapSession *self)
{
	cmd_copy_t *cmd = (cmd_copy_t *)self->cmd;
	u64_t newid;
	int result;

	result = db_copymsg(*id, cmd->mailbox_id, self->userid, &newid);
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
	ImapSession *self = D->session;
	u64_t destmboxid;
	int result;
	MailboxInfo *destmbox;
	cmd_copy_t cmd;

	/* check if destination mailbox exists */
	if (! db_findmailbox(self->args[self->args_idx+1], self->userid, &destmboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO [TRYCREATE] specified mailbox does not exist\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}
	// check if user has right to COPY from source mailbox
	if ((result = mailbox_check_acl(self, self->mailbox->info, ACL_RIGHT_READ))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	// check if user has right to COPY to destination mailbox
	destmbox = dbmail_imap_session_mbxinfo_lookup(self, destmboxid);
	if ((result = mailbox_check_acl(self, destmbox, ACL_RIGHT_INSERT))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	cmd.mailbox_id = destmboxid;
	self->cmd = &cmd;

	if (g_tree_nnodes(self->mailbox->ids) > 0) {
 		if ((_dm_imapsession_get_ids(self, self->args[self->args_idx]) == DM_SUCCESS))
 			g_tree_foreach(self->ids, (GTraverseFunc) _do_copy, self);
 		else {
			D->status = DM_EGENERAL;
			NOTIFY_DONE(D);
		}
  	}	
	db_mailbox_mtime_update(destmboxid);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_copy(ImapSession *self) 
{
	if (!check_state_and_args(self, 2, 2, IMAPCS_SELECTED)) return 1;
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

	if (self->state != IMAPCS_SELECTED) {
		dbmail_imap_session_buff_printf(self, "%s BAD UID command received in invalid state\r\n", self->tag);
		return 1;
	}

	if (!self->args[0]) {
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
void send_quota(ImapSession *self, quota_t * quota)
{
	int r;
	u64_t usage, limit;
	char *name;

	for (r = 0; r < quota->n_resources; r++) {
		if (quota->resource[r].limit > 0) {
			switch (quota->resource[r].type) {
			case RT_STORAGE:
				name = "STORAGE";
				usage = quota->resource[r].usage / 1024;
				limit = quota->resource[r].limit / 1024;
				break;
			default:
				continue;
			}
			dbmail_imap_session_buff_printf(self, "* QUOTA \"%s\" (%s %llu %llu)\r\n", quota->root, name, usage, limit);
		}
	}
}

/*
 * _ic_getquotaroot()
 *
 * get quota root and send quota
 */
static void _ic_getquotaroot_enter(dm_thread_data *D)
{
	quota_t *quota;
	char *root, *errormsg;
	ImapSession *self = D->session;

	if (! (root = quota_get_quotaroot(self->userid, self->args[self->args_idx], &errormsg))) {
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		D->status=1;
		NOTIFY_DONE(D);
	}

	if (! (quota = quota_get_quota(self->userid, root, &errormsg))) {
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		D->status=1;
		NOTIFY_DONE(D);
	}

	dbmail_imap_session_buff_printf(self, "* QUOTAROOT \"%s\" \"%s\"\r\n", self->args[self->args_idx], quota->root);

	send_quota(self, quota);
	quota_free(quota);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_getquotaroot(ImapSession *self)
{
	if (! check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
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
	quota_t *quota;
	char *errormsg;
	ImapSession *self = D->session;

	if (! (quota = quota_get_quota(self->userid, self->args[self->args_idx], &errormsg))) {
		dbmail_imap_session_buff_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		D->status=1;
		NOTIFY_DONE(D);
	}

	send_quota(self, quota);
	quota_free(quota);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_getquota(ImapSession *self)
{
	if (! check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_getquota_enter, _ic_cb_leave, NULL);
	return 0;
}

/* returns -1 on error, 0 if user or mailbox not found and 1 otherwise */
static int imap_acl_pre_administer(const char *mailboxname,
				   const char *username,
				   u64_t executing_userid,
				   u64_t * mboxid, u64_t * target_userid)
{
	int result;
	if (! db_findmailbox(mailboxname, executing_userid, mboxid))
		return FALSE;

	result = auth_user_exists(username, target_userid);
	if (result < 1)
		return result;

	return 1;
}

static void _ic_setacl_enter(dm_thread_data *D)
{
	/* SETACL mailboxname identifier mod_rights */
	int result;
	u64_t mboxid;
	u64_t targetuserid;
	MailboxInfo *mailbox;
	ImapSession *self = D->session;

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	} else if (result == 0) {
		dbmail_imap_session_buff_printf(self, "%s NO SETACL failure: can't set acl\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}
	// has the rights to 'administer' this mailbox? 
	mailbox = dbmail_imap_session_mbxinfo_lookup(self, mboxid);

	if ((result = mailbox_check_acl(self, mailbox, ACL_RIGHT_ADMINISTER))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	// set the new acl
	if (acl_set_rights(targetuserid, mboxid, self->args[self->args_idx+2]) < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_setacl(ImapSession *self)
{
	if (!check_state_and_args(self, 3, 3, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_setacl_enter, _ic_cb_leave, NULL);
	return 0;
}
	

static void _ic_deleteacl_enter(dm_thread_data *D)
{
	// DELETEACL mailboxname identifier
	u64_t mboxid, targetuserid;
	MailboxInfo *mailbox;
	int result;
	ImapSession *self = D->session;

	if (imap_acl_pre_administer(self->args[self->args_idx], 
				self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid) == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	}
	
	mailbox = dbmail_imap_session_mbxinfo_lookup(self, mboxid);

	if ((result = mailbox_check_acl(self, mailbox, ACL_RIGHT_ADMINISTER))) {
		D->status = result;
		NOTIFY_DONE(D);
	}

	// set the new acl
	if (acl_delete_acl(targetuserid, mboxid) < 0) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	}

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_deleteacl(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_deleteacl_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_getacl_enter(dm_thread_data *D)
{
	/* GETACL mailboxname */
	u64_t mboxid;
	char *acl_string;
	ImapSession *self = D->session;

	if (! db_findmailbox(self->args[self->args_idx], self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO GETACL failure: can't get acl\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}
	// get acl string (string of identifier-rights pairs)
	if (!(acl_string = acl_get_acl(mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	}

	dbmail_imap_session_buff_printf(self, "* ACL \"%s\" %s\r\n", self->args[self->args_idx], acl_string);
	g_free(acl_string);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_getacl(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_getacl_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_listrights_enter(dm_thread_data *D)
{
	/* LISTRIGHTS mailboxname identifier */
	int result;
	u64_t mboxid;
	u64_t targetuserid;
	char *listrights_string;
	MailboxInfo *mailbox;
	ImapSession *self = D->session;

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], self->userid, &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	} else if (result == 0) {
		dbmail_imap_session_buff_printf(self, "%s, NO LISTRIGHTS failure: can't set acl\r\n", self->tag);
		D->status = 1;
		NOTIFY_DONE(D);
	}
	// has the rights to 'administer' this mailbox? 
	mailbox = dbmail_imap_session_mbxinfo_lookup(self, mboxid);
	
	if ((result = mailbox_check_acl(self, mailbox, ACL_RIGHT_ADMINISTER))) {
		D->status=1;
		NOTIFY_DONE(D);	
	}

	// set the new acl
	if (!(listrights_string = acl_listrights(targetuserid, mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status=-1;
		NOTIFY_DONE(D);
	}

	dbmail_imap_session_buff_printf(self, "* LISTRIGHTS \"%s\" %s %s\r\n",
		self->args[self->args_idx], self->args[self->args_idx+1], listrights_string);
	g_free(listrights_string);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_listrights(ImapSession *self)
{
	if (!check_state_and_args(self, 2, 2, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_listrights_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_myrights_enter(dm_thread_data *D)
{
	/* MYRIGHTS mailboxname */
	u64_t mboxid;
	char *myrights_string;
	ImapSession *self = D->session;

	if (! db_findmailbox(self->args[self->args_idx], self->userid, &mboxid)) {
		dbmail_imap_session_buff_printf(self, "%s NO MYRIGHTS failure: unknown mailbox\r\n", self->tag);
		D->status=1;
		NOTIFY_DONE(D);
	}

	if (!(myrights_string = acl_myrights(self->userid, mboxid))) {
		dbmail_imap_session_buff_printf(self, "* BYE internal database error\r\n");
		D->status = -1;
		NOTIFY_DONE(D);
	}

	dbmail_imap_session_buff_printf(self, "* MYRIGHTS \"%s\" %s\r\n", self->args[self->args_idx], myrights_string);
	g_free(myrights_string);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_myrights(ImapSession *self)
{
	if (!check_state_and_args(self, 1, 1, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_myrights_enter, _ic_cb_leave, NULL);
	return 0;
}

static void _ic_namespace_enter(dm_thread_data *D)
{
	ImapSession *self = D->session;

	dbmail_imap_session_buff_printf(self, "* NAMESPACE ((\"\" \"%s\")) ((\"%s\" \"%s\")) "
		"((\"%s\" \"%s\"))\r\n",
		MAILBOX_SEPARATOR, NAMESPACE_USER,
		MAILBOX_SEPARATOR, NAMESPACE_PUBLIC, MAILBOX_SEPARATOR);

	IC_DONE_OK;
	NOTIFY_DONE(D);
}

int _ic_namespace(ImapSession *self)
{
	if (!check_state_and_args(self, 0, 0, IMAPCS_AUTHENTICATED)) return 1;
	dm_thread_data_push((gpointer)self, _ic_namespace_enter, _ic_cb_leave, NULL);
	return 0;
}
