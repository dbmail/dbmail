/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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
 * imapcommands.c
 * 
 * IMAP server command implementations
 */

#include "dbmail.h"
#define THIS_MODULE "imap"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


#ifndef MAX_RETRIES
#define MAX_RETRIES 12
#endif

extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];

int list_is_lsub = 0;

extern const char AcceptedMailboxnameChars[];

extern int imap_before_smtp;

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
int _ic_capability(struct ImapSession *self)
{
	if (!check_state_and_args(self, "CAPABILITY", 0, 0, -1))
		return 1;	/* error, return */

	dbmail_imap_session_printf(self, "* CAPABILITY %s\r\n", IMAP_CAPABILITY_STRING);
	dbmail_imap_session_printf(self, "%s OK CAPABILITY completed\r\n", self->tag);

	return 0;
}


/*
 * _ic_noop()
 *
 * performs No operation
 */
int _ic_noop(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	if (!check_state_and_args(self, "NOOP", 0, 0, -1))
		return 1;	/* error, return */

	if (ud->state == IMAPCS_SELECTED)
		dbmail_imap_session_mailbox_status(self, TRUE);
	else
		TRACE(TRACE_DEBUG,"state: %d", ud->state);
	
	dbmail_imap_session_printf(self, "%s OK NOOP completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_logout()
 *
 * prepares logout from IMAP-server
 */
int _ic_logout(struct ImapSession *self)
{
	timestring_t timestring;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	// flush recent messages from previous select
	dbmail_imap_session_mailbox_update_recent(self);

	if (!check_state_and_args(self, "LOGOUT", 0, 0, -1))
		return 1;	/* error, return */

	create_current_timestring(&timestring);
	dbmail_imap_session_set_state(self,IMAPCS_LOGOUT);
	TRACE(TRACE_MESSAGE, "user (id:%llu) logging out @ [%s]", ud->userid, timestring);

	dbmail_imap_session_printf(self, "* BYE dbmail imap server kisses you goodbye\r\n");

	return 0;
}

/*
 * PRE-AUTHENTICATED STATE COMMANDS
 * login, authenticate
 */
/*
 * _ic_login()
 *
 * Performs login-request handling.
 */
int _ic_login(struct ImapSession *self)
{
	int result;
	timestring_t timestring;
	
	if (!check_state_and_args(self, "LOGIN", 2, 2, IMAPCS_NON_AUTHENTICATED))
		return 1;
	
	create_current_timestring(&timestring);
	if ((result = dbmail_imap_session_handle_auth(self, self->args[self->args_idx], self->args[self->args_idx+1])))
		return result;
	if (imap_before_smtp)
		db_log_ip(self->ci->ip_src);

	child_reg_connected_user(self->args[self->args_idx]);

	dbmail_imap_session_printf(self, "%s OK LOGIN completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_authenticate()
 * 
 * performs authentication using LOGIN mechanism:
 *
 *
 */
int _ic_authenticate(struct ImapSession *self)
{
	int result;
	char *username;
	char *password;

	timestring_t timestring;
	
	if (!check_state_and_args(self, "AUTHENTICATE", 1, 1, IMAPCS_NON_AUTHENTICATED))
		return 1;

	create_current_timestring(&timestring);

	/* check authentication method */
	if (strcasecmp(self->args[self->args_idx], "login") != 0) {
		dbmail_imap_session_printf(self,
			"%s NO Invalid authentication mechanism specified\r\n",
			self->tag);
		return 1;
	}

	/* ask for username (base64 encoded) */
	username = g_new0(char,MAX_LINESIZE);
	if (dbmail_imap_session_prompt(self,"username", username)) {
		dbmail_imap_session_printf(self, "* BYE error reading username\r\n");
		g_free(username);
		return -1;
	}
	/* ask for password */
	password = g_new0(char,MAX_LINESIZE);
	if (dbmail_imap_session_prompt(self,"password", password)) {
		dbmail_imap_session_printf(self, "* BYE error reading password\r\n");
		g_free(username);
		g_free(password);
		return -1;
	}

	/* try to validate user */
	if ((result = dbmail_imap_session_handle_auth(self,username,password))) {
		g_free(username);
		g_free(password);
		return result;
	}

	if (imap_before_smtp)
		db_log_ip(self->ci->ip_src);

	dbmail_imap_session_printf(self, "%s OK AUTHENTICATE completed\r\n", self->tag);
	
	g_free(username);
	g_free(password);
	return 0;
}


/* 
 * AUTHENTICATED STATE COMMANDS 
 * select, examine, create, delete, rename, subscribe, 
 * unsubscribe, list, lsub, status, append
 */

/*
 * _ic_select()
 * 
 * select a specified mailbox
 */
#define PERMSTRING_SIZE 80
int _ic_select(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t key = 0;
	u64_t *msn = NULL;
	int result;
	char *mailbox;
	char permstring[PERMSTRING_SIZE];

	if (!check_state_and_args(self, "SELECT", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;	/* error, return */

	mailbox = self->args[self->args_idx];
	
	if ((result = dbmail_imap_session_mailbox_open(self, mailbox))) 
		return result;

	if ((result = dbmail_imap_session_mailbox_show_info(self)))
		return result;

	/* show idx of first unseen msg (if present) */
	if (ud->mailbox.exists) {
		key = db_first_unseen(ud->mailbox.uid);
		if ( (key > 0) && (msn = g_tree_lookup(self->mailbox->ids, &key))) {
			dbmail_imap_session_printf(self,
				"* OK [UNSEEN %llu] first unseen message\r\n", *msn);
		}
	}
	/* permission */
	switch (ud->mailbox.permission) {
	case IMAPPERM_READ:
		g_snprintf(permstring, PERMSTRING_SIZE, "READ-ONLY");
		break;
	case IMAPPERM_READWRITE:
		g_snprintf(permstring, PERMSTRING_SIZE, "READ-WRITE");
		break;
	default:
		TRACE(TRACE_ERROR, "detected invalid permission mode for mailbox %llu ('%s')",
		      ud->mailbox.uid, self->args[self->args_idx]);

		dbmail_imap_session_printf(self,
			"* BYE fatal: detected invalid mailbox settings\r\n");
		return -1;
	}

	dbmail_imap_session_set_state(self,IMAPCS_SELECTED);
	dbmail_imap_session_printf(self, "%s OK [%s] SELECT completed\r\n", self->tag,
		permstring);
	return 0;
}


/*
 * _ic_examine()
 * 
 * examines a specified mailbox 
 */
int _ic_examine(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	char *mailbox;

	if (!check_state_and_args(self, "EXAMINE", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	mailbox = self->args[self->args_idx];

	if ((result = dbmail_imap_session_mailbox_open(self, mailbox)))
		return result;

	if ((result = dbmail_imap_session_mailbox_show_info(self)))
		return result;
	
	/* update permission: examine forces read-only */
	ud->mailbox.permission = IMAPPERM_READ;
	
	dbmail_imap_session_set_state(self,IMAPCS_SELECTED);

	dbmail_imap_session_printf(self, "%s OK [READ-ONLY] EXAMINE completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_create()
 *
 * create a mailbox
 */
int _ic_create(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	const char *message;
	u64_t mboxid;
	
	if (!check_state_and_args(self, "CREATE", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	/* Create the mailbox and its parents. */
	result = db_mailbox_create_with_parents(self->args[self->args_idx], BOX_COMMANDLINE, ud->userid, &mboxid, &message);

	if (result > 0) {
		dbmail_imap_session_printf(self, "%s NO %s\r\n", self->tag, message);
		return DM_EGENERAL;
	} else if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return DM_EQUERY;
	}

	dbmail_imap_session_printf(self, "%s OK CREATE completed\r\n", self->tag);
	return DM_SUCCESS;
}


/*
 * _ic_delete()
 *
 * deletes a specified mailbox
 */
int _ic_delete(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result, nchildren = 0;
	u64_t *children = NULL, mboxid;
	char *mailbox = self->args[0];

	if (!check_state_and_args(self, "DELETE", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;	/* error, return */

	if (! (mboxid = dbmail_imap_session_mailbox_get_idnr(self, mailbox)) ) {
		dbmail_imap_session_printf(self, "%s NO mailbox doesn't exists\r\n", self->tag);
		return 1;
	}

	/* Check if the user has ACL delete rights to this mailbox;
	 * this also returns true is the user owns the mailbox. */
	result = dbmail_imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_DELETE);
	if (result != 0)
		return result;
	
	/* check if there is an attempt to delete inbox */
	if (strcasecmp(self->args[0], "inbox") == 0) {
		dbmail_imap_session_printf(self, "%s NO cannot delete special mailbox INBOX\r\n", self->tag);
		return 1;
	}

	/* check for children of this mailbox */
	result = db_listmailboxchildren(mboxid, ud->userid, &children, &nchildren);
	if (result == -1) {
		/* error */
		TRACE(TRACE_ERROR, "cannot retrieve list of mailbox children");
		dbmail_imap_session_printf(self, "* BYE dbase/memory error\r\n");
		return -1;
	}

	if (nchildren != 0) {
		/* mailbox has inferior names; error if \noselect specified */
		result = db_isselectable(mboxid);
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO mailbox is non-selectable\r\n", self->tag);
			g_free(children);
			return 1;
		}
		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			g_free(children);
			return -1;	/* fatal */
		}

		/* mailbox has inferior names; remove all msgs and set noselect flag */
		result = db_removemsg(ud->userid, mboxid);
		if (result != -1)
			result = db_setselectable(mboxid, 0);	/* set non-selectable flag */

		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			g_free(children);
			return -1;	/* fatal */
		}

		/* check if this was the currently selected mailbox */
		if (mboxid == ud->mailbox.uid) 
			dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);

		/* ok done */
		dbmail_imap_session_printf(self, "%s OK DELETE completed\r\n", self->tag);
		g_free(children);
		return 0;
	}

	/* ok remove mailbox */
	if (db_delete_mailbox(mboxid, 0, 1)) {
		TRACE(TRACE_DEBUG,"db_delete_mailbox failed");
		dbmail_imap_session_printf(self,"%s NO DELETE failed\r\n", self->tag);
		return DM_EGENERAL;
	}

	/* check if this was the currently selected mailbox */
	if (mboxid == ud->mailbox.uid) 
		dbmail_imap_session_set_state(self, IMAPCS_AUTHENTICATED);

	dbmail_imap_session_printf(self, "%s OK DELETE completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_rename()
 *
 * renames a specified mailbox
 */
int _ic_rename(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t mboxid, newmboxid, *children;
	u64_t parentmboxid = 0;
	size_t oldnamelen;
	int nchildren, i, result;
	char newname[IMAP_MAX_MAILBOX_NAMELEN],
	    name[IMAP_MAX_MAILBOX_NAMELEN];

	if (!check_state_and_args(self, "RENAME", 2, 2, IMAPCS_AUTHENTICATED))
		return 1;

	if ((mboxid = dbmail_imap_session_mailbox_get_idnr(self, self->args[0])) == 0) {
		dbmail_imap_session_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		return 1;
	}

	/* check if new name is valid */
        if (!checkmailboxname(self->args[1])) {
	        dbmail_imap_session_printf(self, "%s NO new mailbox name contains invalid characters\r\n", self->tag);
                return 1;
        }

	if ((newmboxid = dbmail_imap_session_mailbox_get_idnr(self, self->args[1])) != 0) {
		dbmail_imap_session_printf(self, "%s NO new mailbox already exists\r\n", self->tag);
		return 1;
	}

	oldnamelen = strlen(self->args[0]);

	/* check if new name would invade structure as in
	 * test (exists)
	 * rename test test/testing
	 * would create test/testing but delete test
	 */
	if (strncasecmp(self->args[0], self->args[1], (int) oldnamelen) == 0 &&
	    strlen(self->args[1]) > oldnamelen && self->args[1][oldnamelen] == '/') {
		dbmail_imap_session_printf(self,
			"%s NO new mailbox would invade mailbox structure\r\n",
			self->tag);
		return 1;
	}

	/* check if structure of new name is valid */
	/* i.e. only last part (after last '/' can be nonexistent) */
	for (i = strlen(self->args[1]) - 1; i >= 0 && self->args[1][i] != '/'; i--);
	if (i >= 0) {
		self->args[1][i] = '\0';	/* note: original char was '/' */

		if (db_findmailbox(self->args[1], ud->userid, &parentmboxid) == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			return -1;	/* fatal */
		}
		if (parentmboxid == 0) {
			/* parent mailbox does not exist */
			dbmail_imap_session_printf(self,
				"%s NO new mailbox would invade mailbox structure\r\n",
				self->tag);
			return 1;
		}

		/* ok, reset arg */
		self->args[1][i] = '/';
	}

	/* Check if the user has ACL delete rights to old name, 
	 * and create rights to the parent of the new name, or
	 * if the user just owns both mailboxes. */
	TRACE(TRACE_DEBUG, "Checking right to DELETE [%llu]", mboxid);
	result = dbmail_imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_DELETE);
	if (result != 0)
		return result;
	TRACE(TRACE_DEBUG, "We have the right to DELETE [%llu]", mboxid);
	if (!parentmboxid) {
		TRACE(TRACE_DEBUG, "Destination is a top-level mailbox; not checking right to CREATE.");
	} else {
		TRACE(TRACE_DEBUG, "Checking right to CREATE under [%llu]", parentmboxid);
		result = dbmail_imap_session_mailbox_check_acl(self, parentmboxid, ACL_RIGHT_CREATE);
		if (result != 0)
			return result;
		TRACE(TRACE_DEBUG, "We have the right to CREATE under [%llu]", parentmboxid);
	}

	/* check if it is INBOX to be renamed */
	if (strcasecmp(self->args[0], "inbox") == 0) {
		/* ok, renaming inbox */
		/* this means creating a new mailbox and moving all the INBOX msgs to the new mailbox */
		/* inferior names of INBOX are left unchanged */
		result = db_createmailbox(self->args[1], ud->userid, &newmboxid);
		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			return -1;
		}

		result = db_movemsg(newmboxid, mboxid);
		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			return -1;
		}

		/* ok done */
		dbmail_imap_session_printf(self, "%s OK RENAME completed\r\n", self->tag);
		return 0;
	}

	/* check for inferior names */
	result = db_listmailboxchildren(mboxid, ud->userid, &children, &nchildren);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}

	/* replace name for each child */
	for (i = 0; i < nchildren; i++) {
		result = db_getmailboxname(children[i], ud->userid, name);
		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			g_free(children);
			return -1;
		}

		if (oldnamelen >= strlen(name)) {
			/* strange error, let's say its fatal */
			TRACE(TRACE_ERROR, "mailbox names appear to be corrupted");
			dbmail_imap_session_printf(self, "* BYE internal error regarding mailbox names\r\n");
			g_free(children);
			return -1;
		}

		g_snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s", self->args[1], &name[oldnamelen]);

		result = db_setmailboxname(children[i], newname);
		if (result == -1) {
			dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
			g_free(children);
			return -1;
		}
			
	}
	if (children)
		g_free(children);

	/* now replace name */
	result = db_setmailboxname(mboxid, self->args[1]);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "%s OK RENAME completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_subscribe()
 *
 * subscribe to a specified mailbox
 */
int _ic_subscribe(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t mboxid;

	if (!check_state_and_args(self, "SUBSCRIBE", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	if (! (mboxid = dbmail_imap_session_mailbox_get_idnr(self, self->args[0]))) {
		dbmail_imap_session_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		return 0;
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */

	if (dbmail_imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_LOOKUP))
		return 1;

	if (db_subscribe(mboxid, ud->userid) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "%s OK SUBSCRIBE completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_unsubscribe()
 *
 * removes a mailbox from the users' subscription list
 */
int _ic_unsubscribe(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t mboxid;

	if (!check_state_and_args(self, "UNSUBSCRIBE", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	if (! (mboxid = dbmail_imap_session_mailbox_get_idnr(self, self->args[0]))) {
		dbmail_imap_session_printf(self, "%s NO mailbox does not exist\r\n", self->tag);
		return 0;
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */
	
	if (dbmail_imap_session_mailbox_check_acl(self, mboxid, ACL_RIGHT_LOOKUP))
		return 1;

	if (db_unsubscribe(mboxid, ud->userid) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "%s OK UNSUBSCRIBE completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_list()
 *
 * executes a list command
 */
int _ic_list(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t *children = NULL;
	int result;
	size_t slen;
	unsigned i;
	unsigned nchildren;
	char *pattern;
	char *thisname = list_is_lsub ? "LSUB" : "LIST";
	
	mailbox_t *mb = NULL;
	GList * plist = NULL;
	gchar * pstring;


	if (!check_state_and_args(self, thisname, 2, 2, IMAPCS_AUTHENTICATED))
		return 1;

	/* check if self->args are both empty strings, i.e. A001 LIST "" "" 
	   this has special meaning; show root & delimiter */
	if (strlen(self->args[0]) == 0 && strlen(self->args[1]) == 0) {
		dbmail_imap_session_printf(self, "* %s (\\NoSelect) \"/\" \"\"\r\n",
			thisname);
		dbmail_imap_session_printf(self, "%s OK %s completed\r\n", self->tag, thisname);
		return 0;
	}

	/* check the reference name, should contain only accepted mailboxname chars */
	for (i = 0, slen = strlen(AcceptedMailboxnameChars); self->args[0][i]; i++) {
		if (stridx(AcceptedMailboxnameChars, self->args[0][i]) == slen) {
			/* wrong char found */
			dbmail_imap_session_printf(self,
				"%s BAD reference name contains invalid characters\r\n",
				self->tag);
			return 1;
		}
	}
	pattern = g_strdup_printf("%s%s", self->args[0], self->args[1]);

	TRACE(TRACE_INFO, "search with pattern: [%s]",pattern);
	
	result = db_findmailbox_by_regex(ud->userid, pattern, &children, &nchildren, list_is_lsub);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		g_free(children);
		g_free(pattern);
		return -1;
	}

	if (result == 1) {
		dbmail_imap_session_printf(self, "%s BAD invalid pattern specified\r\n",
			self->tag);
		g_free(children);
		g_free(pattern);
		return 1;
	}

	mb = g_new0(mailbox_t,1);

	for (i = 0; i < nchildren; i++) {
		if ((db_getmailbox_list_result(children[i], ud->userid, mb) != 0))
			continue;
		
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
		dbmail_imap_session_printf(self, "* %s %s \"%s\" \"%s\"\r\n", thisname, 
				pstring, MAILBOX_SEPARATOR, mb->name);
		
		g_list_destroy(plist);
		g_free(pstring);
	}


	if (children)
		g_free(children);

	g_free(pattern);
	g_free(mb);
	dbmail_imap_session_printf(self, "%s OK %s completed\r\n", self->tag, thisname);

	return 0;
}


/*
 * _ic_lsub()
 *
 * list subscribed mailboxes
 */
int _ic_lsub(struct ImapSession *self)
{
	int result;

	list_is_lsub = 1;
	result = _ic_list(self);
	list_is_lsub = 0;
	return result;
}


/*
 * _ic_status()
 *
 * inquire the status of a mailbox
 */
int _ic_status(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	mailbox_t mb;
	int i, endfound, result;
	GString *response;
	GList *plst = NULL;
	gchar *pstring, *astring;
	
	
	if (!check_state_and_args(self, "STATUS", 3, 0, IMAPCS_AUTHENTICATED))
		return 1;

	if (strcmp(self->args[1], "(") != 0) {
		dbmail_imap_session_printf(self, "%s BAD argument list should be parenthesed\r\n", self->tag);
		return 1;
	}

	/* check final arg: should be ')' and no new '(' in between */
	for (i = 2, endfound = 0; self->args[i]; i++) {
		if (strcmp(self->args[i], ")") == 0) {
			endfound = i;
			break;
		}

		if (strcmp(self->args[i], "(") == 0) {
			dbmail_imap_session_printf(self, "%s BAD too many parentheses specified\r\n", self->tag);
			return 1;
		}
	}

	if (endfound == 2) {
		dbmail_imap_session_printf(self, "%s BAD argument list empty\r\n", self->tag);
		return 1;
	}

	if (self->args[endfound + 1]) {
		dbmail_imap_session_printf(self, "%s BAD argument list too long\r\n", self->tag);
		return 1;
	}


	/* zero init */
	memset(&mb, 0, sizeof(mb));

	/* check if mailbox exists */
	if (db_findmailbox(self->args[0], ud->userid, &(mb.uid)) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}

	if (mb.uid == 0) {
		/* mailbox does not exist */
		dbmail_imap_session_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1;
	}

	result = acl_has_right(&mb, ud->userid, ACL_RIGHT_READ);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO no rights to get status for mailbox\r\n", self->tag);
		return 1;
	}

	/* retrieve mailbox data */
	result = db_getmailbox(&mb);

	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;	/* fatal  */
	}
	for (i = 2; self->args[i]; i++) {
		if (strcasecmp(self->args[i], "messages") == 0)
			plst = g_list_append_printf(plst,"MESSAGES %u", mb.exists);
		else if (strcasecmp(self->args[i], "recent") == 0)
			plst = g_list_append_printf(plst,"RECENT %u", mb.recent);
		else if (strcasecmp(self->args[i], "unseen") == 0)
			plst = g_list_append_printf(plst,"UNSEEN %u", mb.unseen);
		else if (strcasecmp(self->args[i], "uidnext") == 0) {
			plst = g_list_append_printf(plst,"UIDNEXT %llu", mb.msguidnext);
		} else if (strcasecmp(self->args[i], "uidvalidity") == 0) {
			plst = g_list_append_printf(plst,"UIDVALIDITY %llu", mb.uid);
		} else if (strcasecmp(self->args[i], ")") == 0)
			break;
		else {
			dbmail_imap_session_printf(self,
				"\r\n%s BAD unrecognized option '%s' specified\r\n",
				self->tag, self->args[i]);
			return 1;
		}
	}
	astring = dbmail_imap_astring_as_string(self->args[0]);
	pstring = dbmail_imap_plist_as_string(plst); 

	response = g_string_new("");
	g_string_printf(response, "* STATUS %s %s", astring, pstring);	
	dbmail_imap_session_printf(self, "%s\r\n", response->str);
	dbmail_imap_session_printf(self, "%s OK STATUS completed\r\n", self->tag);

	g_list_destroy(plst);
	g_string_free(response,TRUE);
	g_free(astring);
	g_free(pstring);

	return 0;
}


/*
 * _ic_append()
 *
 * append a message to a mailbox
 */
int _ic_append(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t mboxid;
	u64_t msg_idnr;
	int i, j, result;
	timestring_t sqldate;
	int flaglist[IMAP_NFLAGS];
	int flagcount = 0;
	mailbox_t mailbox;

	bzero(&mailbox, sizeof(mailbox_t));
	
	for (i = 0; i < IMAP_NFLAGS; i++)
		flaglist[i] = 0;

	if (!self->args[0] || !self->args[1]) {
		dbmail_imap_session_printf(self, "%s BAD invalid arguments specified to APPEND\r\n",
			self->tag);
		return 1;
	}

	/* find the mailbox to place the message */
	if (db_findmailbox(self->args[0], ud->userid, &mboxid) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error");
		return -1;
	}

	if (mboxid == 0) {
		dbmail_imap_session_printf(self, "%s NO [TRYCREATE] could not find specified mailbox\r\n",
			self->tag);
		return 1;
	}

	TRACE(TRACE_DEBUG, "mailbox [%s] found, id: %llu", self->args[0], mboxid);
	/* check if user has right to append to  mailbox */
	mailbox.uid = mboxid;
	result = acl_has_right(&mailbox, ud->userid, ACL_RIGHT_INSERT);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO no permission to append to mailbox\r\n",
				self->tag);
		dbmail_imap_session_set_state(self, IMAPCS_AUTHENTICATED);
		return 1;
	}


	i = 1;

	/* check if a flag list has been specified */
	/* FIXME: We need to take of care of the Flags that are set here. They
	   should be set to the new message!
	 */
	if (self->args[i][0] == '(') {
		/* ok fetch the flags specified */
		TRACE(TRACE_DEBUG, "flag list found:");

		while (self->args[i] && self->args[i][0] != ')') {
			TRACE(TRACE_DEBUG, "%s ", self->args[i]);
			for (j = 0; j < IMAP_NFLAGS; j++) {
				if (strcasecmp (self->args[i], imap_flag_desc_escaped[j]) == 0) {
					flaglist[j] = 1;
					flagcount++;
					break;
				}
			}
			i++;
		}

		i++;
		TRACE(TRACE_DEBUG, ")");
	}

	if (!self->args[i]) {
		TRACE(TRACE_INFO, "unexpected end of arguments");
		dbmail_imap_session_printf(self, 
				"%s BAD invalid arguments specified to APPEND\r\n", 
				self->tag);
		return 1;
	}

	for (j = 0; j < IMAP_NFLAGS; j++)
		if (flaglist[j] == 1)
			TRACE(TRACE_DEBUG, "%s set", imap_flag_desc[j]);

	/** check ACL's for STORE */
	mailbox.uid = mboxid;
	if (flaglist[IMAP_FLAG_SEEN] == 1) {
		result = acl_has_right(&mailbox, ud->userid, ACL_RIGHT_SEEN);
		if (result < 0) {
			dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store \\SEEN flag\r\n", self->tag);
			return 1;
		}
	}
	if (flaglist[IMAP_FLAG_DELETED] == 1) {
		result = acl_has_right(&mailbox, ud->userid, ACL_RIGHT_DELETE);
		if (result < 0) {
			dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store \\DELETED flag\r\n", self->tag);
			return 1;
		}
	}
	if (flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    flaglist[IMAP_FLAG_DRAFT] == 1 ||
	    flaglist[IMAP_FLAG_RECENT] == 1) {
		result = acl_has_right(&mailbox, ud->userid, ACL_RIGHT_WRITE);
		if (result < 0) {
			dbmail_imap_session_printf(self, "*BYE internal database error\r\n");
			return -1;
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store flags\r\n", self->tag);
			return 1;
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
		TRACE(TRACE_DEBUG, "internal date [%s] found, next arg [%s]", sqldate, self->args[i]);
	} else {
		sqldate[0] = '\0';
	}

	/* ok literal msg should be in self->args[i] */
	/* insert this msg */

	result = db_imap_append_msg(self->args[i], strlen(self->args[i]), mboxid, ud->userid, sqldate, &msg_idnr);
	switch (result) {
	case -1:
		TRACE(TRACE_ERROR, "error appending msg");
		dbmail_imap_session_printf(self, "* BYE internal dbase error storing message\r\n");
		break;

	case 1:
		TRACE(TRACE_ERROR, "faulty msg");
		dbmail_imap_session_printf(self, "%s NO invalid message specified\r\n", self->tag);
		break;

	case 2:
		TRACE(TRACE_INFO, "quotum would exceed");
		dbmail_imap_session_printf(self, "%s NO not enough quotum left\r\n", self->tag);
		break;

	case 0:
		dbmail_imap_session_printf(self, "%s OK APPEND completed\r\n", self->tag);
		break;
	}

	if (result == 0 && flagcount > 0) {
		if (db_set_msgflag(msg_idnr, mboxid, flaglist, IMAPFA_ADD) < 0) {
			TRACE(TRACE_ERROR, "error setting flags for message [%llu]", msg_idnr);
			return -1;
		}
	}

	return result;
}



/* 
 * SELECTED-STATE COMMANDS 
 * sort, check, close, expunge, search, fetch, store, copy, uid
 */

/*
 * _ic_check()
 * 
 * request a checkpoint for the selected mailbox
 * (equivalent to NOOP)
 */
int _ic_check(struct ImapSession *self)
{
	int result;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	if (!check_state_and_args(self, "CHECK", 0, 0, IMAPCS_SELECTED))
		return 1;	/* error, return */

	result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_READ);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE Internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO no permission to do check on "
			"mailbox\r\n", self->tag);
		return 1;
	}

	dbmail_imap_session_mailbox_status(self, TRUE);

	dbmail_imap_session_printf(self, "%s OK CHECK completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_close()
 *
 * expunge deleted messages from selected mailbox & return to AUTH state
 * do not show expunge-output
 */
int _ic_close(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;

	if (!check_state_and_args(self, "CLOSE", 0, 0, IMAPCS_SELECTED))
		return 1;	/* error, return */

	/* check if the user has to right to expunge all messages from the
	   mailbox. */
	result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_DELETE);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE Internal database error\r\n");
		return -1;
	}
	/* only perform the expunge if the user has the right to do it */
	if (result == 1)
		if (ud->mailbox.permission == IMAPPERM_READWRITE)
			db_expunge(ud->mailbox.uid, ud->userid, NULL,
				   NULL);


	/* ok, update state (always go to IMAPCS_AUTHENTICATED) */
	dbmail_imap_session_set_state(self, IMAPCS_AUTHENTICATED);
	dbmail_imap_session_printf(self, "%s OK CLOSE completed\r\n", self->tag);
	return 0;
}

/*
 * _ic_unselect
 *
 * non-expunging close for select mailbox and return to AUTH state
 *
 */

int _ic_unselect(struct ImapSession *self)
{
	if (!check_state_and_args(self, "UNSELECT", 0, 0, IMAPCS_SELECTED))
		return 1;	/* error, return */

	dbmail_imap_session_mailbox_close(self);
	dbmail_imap_session_printf(self, "%s OK UNSELECT completed\r\n", self->tag);
	return 0;
}

/*
 * _ic_expunge()
 *
 * expunge deleted messages from selected mailbox
 * show expunge output per message
 */
int _ic_expunge(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t *msgids, *msn;
	u64_t nmsgs, i, uid;
	int result;
	

	if (!check_state_and_args(self, "EXPUNGE", 0, 0, IMAPCS_SELECTED))
		return 1; /* error, return */

	if (ud->mailbox.permission != IMAPPERM_READWRITE) {
		dbmail_imap_session_printf(self,
			"%s NO you do not have write permission on this folder\r\n",
			self->tag);
		return 1;
	}

	result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_DELETE);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO you do not have delete rights on this mailbox\r\n", 
		self->tag);
		return 1;
	}

	/* delete messages */
	result = db_expunge(ud->mailbox.uid, ud->userid, &msgids, &nmsgs);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE db error\r\n");
		return -1;
	}
	if (result == 1) {
		dbmail_imap_session_printf(self, "%s OK EXPUNGE completed\r\n", self->tag);
		return 1;
	}

	for (i=0; i<nmsgs; i++) {
		uid = msgids[i];
		msn = g_tree_lookup(self->mailbox->ids, &uid);

		if (! msn) {
			TRACE(TRACE_DEBUG,"can't find uid [%llu]", uid);
			break;
		}
		dbmail_imap_session_printf(self, "* %llu EXPUNGE\r\n", *msn);
		dbmail_mailbox_remove_uid(self->mailbox, &uid);
	}
		
	if (msgids)
		g_free(msgids);
	msgids = NULL;

	dbmail_imap_session_printf(self, "%s OK EXPUNGE completed\r\n", self->tag);
	return 0;
}


/*
 * _ic_search()
 *
 * search the selected mailbox for messages
 *
 */

static int sorted_search(struct ImapSession *self, search_order_t order)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	struct DbmailMailbox *mb;
	int result = 0;
	gchar *s = NULL, *cmd = NULL;
	gboolean sorted;

	if (order == SEARCH_SORTED)
		sorted = 1;
	
	if (ud->state != IMAPCS_SELECTED) {
		dbmail_imap_session_printf(self,
			"%s BAD %s command received in invalid state\r\n",
			self->tag, self->command);
		return 1;
	}
	
	if (!self->args[0]) {
		dbmail_imap_session_printf(self, "%s BAD invalid arguments to %s\r\n",
			self->tag, self->command);
		return 1;
	}
	
	/* check ACL */
	if (! (result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_READ))) {
		dbmail_imap_session_printf(self, "%s NO no permission to search mailbox\r\n", self->tag);
		return 1;
	}
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	mb = dbmail_mailbox_new(ud->mailbox.uid);

	if (g_tree_nnodes(mb->ids) > 0) {
		dbmail_mailbox_set_uid(mb,self->use_uid);
		switch(order) {
			case SEARCH_SORTED:
				cmd = g_strdup("SORT");
			break;
			case SEARCH_UNORDERED:
				cmd = g_strdup("SEARCH");
			break;
			case SEARCH_THREAD_ORDEREDSUBJECT:
				cmd = g_strdup("THREAD");
			break;
			case SEARCH_THREAD_REFERENCES:
				cmd = g_strdup("THREAD");
			break;
		}


		if (dbmail_mailbox_build_imap_search(mb, self->args, &(self->args_idx), order) < 0) {
			dbmail_imap_session_printf(self, "%s BAD invalid arguments to %s\r\n",
				self->tag, cmd);
			return 1;
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

		dbmail_imap_session_printf(self, "* %s %s\r\n", cmd, s?s:"");
		if (s)
			g_free(s);
	}

	dbmail_imap_session_printf(self, "%s OK %s completed\r\n", self->tag, cmd);
	if (cmd)
		g_free(cmd);
	
	dbmail_mailbox_free(mb);
	
	return 0;
}

int _ic_search(struct ImapSession *self)
{
	return sorted_search(self,SEARCH_UNORDERED);
}

int _ic_sort(struct ImapSession *self)
{
	return sorted_search(self,SEARCH_SORTED);
}

int _ic_thread(struct ImapSession *self)
{
	if (MATCH(self->args[0],"ORDEREDSUBJECT"))
		return sorted_search(self,SEARCH_THREAD_ORDEREDSUBJECT);
	if (MATCH(self->args[0],"REFERENCES"))
		return sorted_search(self,SEARCH_THREAD_REFERENCES);

	return 1;
}

/*
 * _ic_fetch()
 *
 * fetch message(s) from the selected mailbox
 */
	
int _ic_fetch(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result, state, setidx;

	if (!check_state_and_args (self, "FETCH", 2, 0, IMAPCS_SELECTED))
		return 1;

	/* check if the user has the right to fetch messages in this mailbox */
	result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_READ);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO no permission to fetch from mailbox\r\n", self->tag);
		return 1;
	}
	
	dbmail_imap_session_resetFi(self);

	self->fi->getUID = self->use_uid;

	setidx = self->args_idx;
	self->args_idx++; //skip on past this for the fetch_parse_args coming next...

	state = 1;
	do {
		if ( (state = dbmail_imap_session_fetch_parse_args(self)) == -2) {
			dbmail_imap_session_printf(self, "%s BAD invalid argument list to fetch\r\n", self->tag);
			return 1;
		}
		TRACE(TRACE_DEBUG,"dbmail_imap_session_fetch_parse_args loop idx %llu state %d ", self->args_idx, state);
		self->args_idx++;
	} while (state > 0);

	if (g_tree_nnodes(self->mailbox->ids) > 0) {

		dbmail_mailbox_set_uid(self->mailbox,self->use_uid);

		if (self->ids) {
			g_tree_destroy(self->ids);
			self->ids = NULL;
		}
	
		self->ids = dbmail_mailbox_get_set(self->mailbox,self->args[setidx],self->use_uid);
		self->ids_list = g_tree_keys(self->ids);
		
		if (g_tree_nnodes(self->ids)==0) {
			dbmail_imap_session_printf(self, "%s BAD invalid message range specified\r\n", self->tag);
			return DM_EGENERAL;
		}
			
		if (dbmail_imap_session_fetch_get_items(self) < 0)
			return -1;
		
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
	}	
	
	dbmail_imap_session_printf(self, "%s OK %sFETCH completed\r\n", self->tag, self->use_uid ? "UID " : "");
	return 0;
}



/*
 * _ic_store()
 *
 * alter message-associated data in selected mailbox
 */

static gboolean _do_store(u64_t *id, gpointer UNUSED value, struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	cmd_store_t *cmd = (cmd_store_t *)self->cmd;

	u64_t *msn;
	msginfo_t *msginfo;
	char *s;
	int i;

	msginfo = g_tree_lookup(self->msginfo, id);
	if (! msginfo) {
		TRACE(TRACE_WARNING, "unable to lookup msginfo struct for [%llu]", *id);
		return TRUE;
	}

	msn = g_tree_lookup(self->mailbox->ids, id);

	if (ud->mailbox.permission == IMAPPERM_READWRITE) {
		if (db_set_msgflag(*id, ud->mailbox.uid, cmd->flaglist, cmd->action) < 0) {
			dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
			return TRUE;
		}
	}

	for (i = 0; i < IMAP_NFLAGS; i++) {
		// Skip recent_flag because it is part of the query.
		if (i == IMAP_FLAG_RECENT)
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

	if (! cmd->silent) {
		s = imap_flags_as_string(msginfo);
		dbmail_imap_session_printf(self,"* %llu FETCH (FLAGS %s)\r\n", *msn, s);
		g_free(s);
	}

	return FALSE;
}

int _ic_store(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	cmd_store_t cmd;
	int result, i, j, k;

	memset(cmd.flaglist, 0, sizeof(int) * IMAP_NFLAGS);

	if (!check_state_and_args (self, "STORE", 2, 0, IMAPCS_SELECTED))
		return 1;

	k = self->args_idx;
	/* multiple flags should be parenthesed */
	if (self->args[k+3] && strcmp(self->args[k+2], "(") != 0) {
		dbmail_imap_session_printf(self, "%s BAD invalid argument(s) to STORE\r\n",
			self->tag);
		return 1;
	}

	cmd.silent = FALSE;

	/* retrieve action type */
	if (MATCH(self->args[k+1], "flags"))
		cmd.action = IMAPFA_REPLACE;
	else if (MATCH(self->args[k+1], "flags.silent")) {
		cmd.action = IMAPFA_REPLACE;
		cmd.silent = TRUE;
	} else if (MATCH(self->args[k+1], "+flags"))
		cmd.action = IMAPFA_ADD;
	else if (MATCH(self->args[k+1], "+flags.silent")) {
		cmd.action = IMAPFA_ADD;
		cmd.silent = TRUE;
	} else if (MATCH(self->args[k+1], "-flags"))
		cmd.action = IMAPFA_REMOVE;
	else if (MATCH(self->args[k+1], "-flags.silent")) {
		cmd.action = IMAPFA_REMOVE;
		cmd.silent = TRUE;
	}

	if (cmd.action == IMAPFA_NONE) {
		dbmail_imap_session_printf(self, "%s BAD invalid STORE action specified\r\n", self->tag);
		return 1;
	}

	/* now fetch flag list */
	i = (strcmp(self->args[k+2], "(") == 0) ? 3 : 2;

	for (; self->args[k+i] && strcmp(self->args[k+i], ")") != 0; i++) {
		for (j = 0; j < IMAP_NFLAGS; j++) {
			/* storing the recent flag explicitely is not allowed */
			if (MATCH(self->args[k+i],"\\Recent")) {
				j = IMAP_NFLAGS;
				break;
			}
				
			if (MATCH(self->args[k+i], imap_flag_desc_escaped[j])) {
				cmd.flaglist[j] = 1;
				break;
			}
		}

		if (j == IMAP_NFLAGS) {
			dbmail_imap_session_printf(self, "%s BAD invalid flag list to STORE command\r\n", self->tag);
			return 1;
		}
	}

	/** check ACL's for STORE */
	if (cmd.flaglist[IMAP_FLAG_SEEN] == 1) {
		result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_SEEN);
		if (result < 0) {
			dbmail_imap_session_printf(self, "* BYE internal database error");
			return -1;	/* fatal */
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store \\SEEN flag\r\n", self->tag);
			return 1;
		}
	}
	if (cmd.flaglist[IMAP_FLAG_DELETED] == 1) {
		result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_DELETE);
		if (result < 0) {
			dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store \\DELETED flag\r\n", self->tag);
			return 1;
		}
	}
	if (cmd.flaglist[IMAP_FLAG_ANSWERED] == 1 ||
	    cmd.flaglist[IMAP_FLAG_FLAGGED] == 1 ||
	    cmd.flaglist[IMAP_FLAG_DRAFT] == 1) {
		result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_WRITE);
		if (result < 0) {
			dbmail_imap_session_printf(self, "* BYE internal database error");
			return -1;
		}
		if (result == 0) {
			dbmail_imap_session_printf(self, "%s NO no right to store flags", self->tag);
			return 1;
		}
	}
	/* end of ACL checking. If we get here without returning, the user has
	   the right to store the flags */

	self->cmd = &cmd;

	if (g_tree_nnodes(self->mailbox->ids) > 0) {
		GTree *t;

		dbmail_mailbox_set_uid(self->mailbox,self->use_uid);
		
		if (self->ids) {
			g_tree_destroy(self->ids);
			self->ids = NULL;
		}

		self->ids = dbmail_mailbox_get_set(self->mailbox,self->args[k],self->use_uid);
		
		if (g_tree_nnodes(self->ids)==0) {
			dbmail_imap_session_printf(self, "%s BAD invalid message range specified\r\n", self->tag);
			return DM_EGENERAL;
		}

		t = self->msginfo;
		if ((self->msginfo = dbmail_imap_session_get_msginfo(self, self->mailbox->ids)) == NULL)
			TRACE(TRACE_DEBUG, "unable to retrieve msginfo");
		if(t)
			g_tree_destroy(t);


		g_tree_foreach(self->ids, (GTraverseFunc) _do_store, self);

	}	

	dbmail_imap_session_printf(self, "%s OK %sSTORE completed\r\n", self->tag, self->use_uid ? "UID " : "");
	return 0;
}


/*
 * _ic_copy()
 *
 * copy a message to another mailbox
 */

static gboolean _do_copy(u64_t *id, gpointer UNUSED value, struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	cmd_copy_t *cmd = (cmd_copy_t *)self->cmd;
	u64_t newid;
	int result;

	result = db_copymsg(*id, cmd->mailbox_id, ud->userid, &newid);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		db_rollback_transaction();
		return TRUE;
	}
	if (result == -2) {
		dbmail_imap_session_printf(self, "%s NO quotum would exceed\r\n", self->tag);
		db_rollback_transaction();
		return TRUE;
	}
	return FALSE;
}


int _ic_copy(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t destmboxid;
	int result;
	mailbox_t destmbox;
	cmd_copy_t cmd;
	
	memset(&destmbox, 0, sizeof(destmbox));

	if (!check_state_and_args(self, "COPY", 2, 2, IMAPCS_SELECTED))
		return 1;	/* error, return */

	/* check if destination mailbox exists */
	if (db_findmailbox(self->args[self->args_idx+1], ud->userid, &destmboxid) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}
	if (destmboxid == 0) {
		/* error: cannot select mailbox */
		dbmail_imap_session_printf(self,
			"%s NO [TRYCREATE] specified mailbox does not exist\r\n",
			self->tag);
		return 1;
	}
	// check if user has right to COPY from source mailbox
	result = acl_has_right(&ud->mailbox, ud->userid, ACL_RIGHT_READ);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;	/* fatal */
	}
	if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO no permission to copy from mailbox\r\n",
			self->tag);
		return 1;
	}
	// check if user has right to COPY to destination mailbox
	destmbox.uid = destmboxid;
	result = acl_has_right(&destmbox, ud->userid, ACL_RIGHT_INSERT);
	if (result < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;	/* fatal */
	}
	if (result == 0) {
		dbmail_imap_session_printf(self,
			"%s NO no permission to copy to mailbox\r\n", self->tag);
		return 1;
	}

	cmd.mailbox_id = destmboxid;
	self->cmd = &cmd;

	if (db_begin_transaction() < 0)
		return -1;
	
	if (g_tree_nnodes(self->mailbox->ids) > 0) {

		dbmail_mailbox_set_uid(self->mailbox,self->use_uid);
		
		if (self->ids) {
			g_tree_destroy(self->ids);
			self->ids = NULL;
		}

		self->ids = dbmail_mailbox_get_set(self->mailbox,self->args[self->args_idx],self->use_uid);
		
		if (g_tree_nnodes(self->ids)==0) {
			dbmail_imap_session_printf(self, "%s BAD invalid message range specified\r\n", self->tag);
			return DM_EGENERAL;
		}

		TRACE(TRACE_DEBUG,"copy [%d] messages", g_tree_nnodes(self->ids));

		g_tree_foreach(self->ids, (GTraverseFunc) _do_copy, self);
	}	

	if (db_commit_transaction() < 0)
		return -1;

	dbmail_imap_session_printf(self, "%s OK %sCOPY completed\r\n", self->tag,
		self->use_uid ? "UID " : "");
	return 0;
}


/*
 * _ic_uid()
 *
 * fetch/store/copy/search message UID's
 */
int _ic_uid(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;

	if (ud->state != IMAPCS_SELECTED) {
		dbmail_imap_session_printf(self,
			"%s BAD UID command received in invalid state\r\n",
			self->tag);
		return 1;
	}

	if (!self->args[0]) {
		dbmail_imap_session_printf(self, "%s BAD missing argument(s) to UID\r\n",
			self->tag);
		return 1;
	}

	self->use_uid = 1;	/* set global var to make clear we will be using UID's */
	
	/* ACL rights for UID are handled by the other functions called below */
	if (MATCH(self->args[self->args_idx], "fetch")) {
		self->args_idx++; 
		result = _ic_fetch(self);
	} else if (MATCH(self->args[self->args_idx], "copy")) {
		self->args_idx++;
		result = _ic_copy(self);
	} else if (MATCH(self->args[self->args_idx], "store")) {
		self->args_idx++;
		result = _ic_store(self);
	} else if (MATCH(self->args[self->args_idx], "search")) {
		self->args_idx++;
		result = _ic_search(self);
	} else if (MATCH(self->args[self->args_idx], "sort")) {
		self->args_idx++;
		result = _ic_sort(self);
	} else if (MATCH(self->args[self->args_idx], "thread")) {
		self->args_idx++;
		result = _ic_thread(self);
	} else {
		dbmail_imap_session_printf(self, "%s BAD invalid UID command\r\n", self->tag);
		result = 1;
	}

	self->use_uid = 0;

	return result;
}


/* Helper function for _ic_getquotaroot() and _ic_getquota().
 * Send all resource limits in `quota'.
 */
void send_quota(struct ImapSession *self, quota_t * quota)
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
			dbmail_imap_session_printf(self,
				"* QUOTA \"%s\" (%s %llu %llu)\r\n",
				quota->root, name, usage, limit);
		}
	}
}

/*
 * _ic_getquotaroot()
 *
 * get quota root and send quota
 */
int _ic_getquotaroot(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	quota_t *quota;
	char *root, *errormsg;

	if (!check_state_and_args(self, "GETQUOTAROOT", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;	/* error, return */

	root = quota_get_quotaroot(ud->userid, self->args[self->args_idx], &errormsg);
	if (root == NULL) {
		dbmail_imap_session_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		return 1;
	}

	quota = quota_get_quota(ud->userid, root, &errormsg);
	if (quota == NULL) {
		dbmail_imap_session_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		return 1;
	}

	dbmail_imap_session_printf(self, "* QUOTAROOT \"%s\" \"%s\"\r\n", self->args[self->args_idx],
		quota->root);
	send_quota(self, quota);
	quota_free(quota);

	dbmail_imap_session_printf(self, "%s OK GETQUOTAROOT completed\r\n", self->tag);
	return 0;
}

/*
 * _ic_getquot()
 *
 * get quota
 */
int _ic_getquota(struct ImapSession *self)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	quota_t *quota;
	char *errormsg;

	if (!check_state_and_args(self, "GETQUOTA", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;	/* error, return */

	quota = quota_get_quota(ud->userid, self->args[self->args_idx], &errormsg);
	if (quota == NULL) {
		dbmail_imap_session_printf(self, "%s NO %s\r\n", self->tag, errormsg);
		return 1;
	}

	send_quota(self, quota);
	quota_free(quota);

	dbmail_imap_session_printf(self, "%s OK GETQUOTA completed\r\n", self->tag);
	return 0;
}

/* returns -1 on error, 0 if user or mailbox not found and 1 otherwise */
static int imap_acl_pre_administer(const char *mailboxname,
				   const char *username,
				   u64_t executing_userid,
				   u64_t * mboxid, u64_t * target_userid)
{
	int result;
	result = db_findmailbox(mailboxname, executing_userid, mboxid);
	if (result < 1)
		return result;

	result = auth_user_exists(username, target_userid);
	if (result < 1)
		return result;

	return 1;
}

int _ic_setacl(struct ImapSession *self)
{
	/* SETACL mailboxname identifier mod_rights */
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	u64_t mboxid;
	u64_t targetuserid;
	mailbox_t mailbox;

	bzero(&mailbox, sizeof(mailbox_t));

	if (!check_state_and_args(self, "SETACL", 3, 3, IMAPCS_AUTHENTICATED))
		return 1;

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], ud->userid,
					 &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO SETACL failure: can't set acl\r\n",
			self->tag);
		return 1;
	}
	// has the rights to 'administer' this mailbox? 
	mailbox.uid = mboxid;
	if (acl_has_right(&mailbox, ud->userid, ACL_RIGHT_ADMINISTER) != 1) {
		dbmail_imap_session_printf(self, "%s NO SETACL failure: can't set acl, "
			"you don't have the proper rights\r\n", self->tag);
		return 1;
	}
	// set the new acl
	if (acl_set_rights(targetuserid, mboxid, self->args[self->args_idx+2]) < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "%s OK SETACL completed\r\n", self->tag);
	return 0;
}


int _ic_deleteacl(struct ImapSession *self)
{
	// DELETEACL mailboxname identifier
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t mboxid;
	u64_t targetuserid;
	mailbox_t mailbox;

	if (!check_state_and_args(self, "DELETEACL", 2, 2, IMAPCS_AUTHENTICATED))
		return 1;

	if (imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], ud->userid,
				    &mboxid, &targetuserid) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	
	bzero(&mailbox, sizeof(mailbox_t));
	mailbox.uid = mboxid;
	// has the rights to 'administer' this mailbox? 
	if (acl_has_right(&mailbox, ud->userid, ACL_RIGHT_ADMINISTER) != 1) {
		dbmail_imap_session_printf(self, "%s NO DELETEACL failure: can't delete "
			"acl\r\n", self->tag);
		return 1;
	}
	// set the new acl
	if (acl_delete_acl(targetuserid, mboxid) < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "%s OK DELETEACL completed\r\n", self->tag);
	return 0;
}

int _ic_getacl(struct ImapSession *self)
{
	/* GETACL mailboxname */
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	u64_t mboxid;
	char *acl_string;

	if (!check_state_and_args(self, "GETACL", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	result = db_findmailbox(self->args[self->args_idx], ud->userid, &mboxid);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		dbmail_imap_session_printf(self, "%s NO GETACL failure: can't get acl\r\n",
			self->tag);
		return 1;
	}
	// get acl string (string of identifier-rights pairs)
	if (!(acl_string = acl_get_acl(mboxid))) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "* ACL \"%s\" %s\r\n", self->args[self->args_idx], acl_string);
	g_free(acl_string);
	dbmail_imap_session_printf(self, "%s OK GETACL completed\r\n", self->tag);
	return 0;
}

int _ic_listrights(struct ImapSession *self)
{
	/* LISTRIGHTS mailboxname identifier */
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	u64_t mboxid;
	u64_t targetuserid;
	char *listrights_string;
	mailbox_t mailbox;

	if (!check_state_and_args(self, "LISTRIGHTS", 2, 2, IMAPCS_AUTHENTICATED))
		return 1;

	result = imap_acl_pre_administer(self->args[self->args_idx], self->args[self->args_idx+1], ud->userid,
					 &mboxid, &targetuserid);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		dbmail_imap_session_printf(self,
			"%s, NO LISTRIGHTS failure: can't set acl\r\n",
			self->tag);
		return 1;
	}
	// has the rights to 'administer' this mailbox? 
	bzero(&mailbox, sizeof(mailbox_t));
	mailbox.uid = mboxid;
	if (acl_has_right(&mailbox, ud->userid, ACL_RIGHT_ADMINISTER) != 1) {
		dbmail_imap_session_printf(self,
			"%s NO LISTRIGHTS failure: can't set acl\r\n",
			self->tag);
		return 1;
	}
	// set the new acl
	if (!(listrights_string = acl_listrights(targetuserid, mboxid))) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "* LISTRIGHTS \"%s\" %s %s\r\n",
		self->args[self->args_idx], self->args[self->args_idx+1], listrights_string);
	dbmail_imap_session_printf(self, "%s OK LISTRIGHTS completed\r\n", self->tag);
	g_free(listrights_string);
	return 0;
}

int _ic_myrights(struct ImapSession *self)
{
	/* MYRIGHTS mailboxname */
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result;
	u64_t mboxid;
	char *myrights_string;

	if (!check_state_and_args(self, "LISTRIGHTS", 1, 1, IMAPCS_AUTHENTICATED))
		return 1;

	result = db_findmailbox(self->args[self->args_idx], ud->userid, &mboxid);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		dbmail_imap_session_printf(self,
			"%s NO MYRIGHTS failure: unknown mailbox\r\n",
			self->tag);
		return 1;
	}

	if (!(myrights_string = acl_myrights(ud->userid, mboxid))) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}

	dbmail_imap_session_printf(self, "* MYRIGHTS \"%s\" %s\r\n", self->args[self->args_idx],
		myrights_string);
	g_free(myrights_string);
	dbmail_imap_session_printf(self, "%s OK MYRIGHTS complete\r\n", self->tag);
	return 0;
}

int _ic_namespace(struct ImapSession *self)
{
	/* NAMESPACE command */
	if (!check_state_and_args(self, "NAMESPACE", 0, 0, IMAPCS_AUTHENTICATED))
		return 1;

	dbmail_imap_session_printf(self, "* NAMESPACE ((\"\" \"%s\")) ((\"%s\" \"%s\")) "
		"((\"%s\" \"%s\"))\r\n",
		MAILBOX_SEPARATOR, NAMESPACE_USER,
		MAILBOX_SEPARATOR, NAMESPACE_PUBLIC, MAILBOX_SEPARATOR);
	dbmail_imap_session_printf(self, "%s OK NAMESPACE complete\r\n", self->tag);
	return 0;
}
