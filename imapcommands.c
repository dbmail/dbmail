/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

/* $Id$
 *
 * imapcommands.c
 * 
 * IMAP server command implementations
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* dbmail.h is included first */
#include "dbmail.h"
#include "acl.h"
#include "auth.h"
#include "db.h"

#include "dbmsgbuf.h"
#include "debug.h"
#include "imapcommands.h"
#include "imaputil.h"
#include "memblock.h"
#include "misc.h"
#include "quota.h"
#include "rfcmsg.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <time.h>
#include <unistd.h>

#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

#ifndef MAX_LINESIZE
#define MAX_LINESIZE 1024
#endif

#ifndef MAX_RETRIES
#define MAX_RETRIES 12
#endif

const char *imap_flag_desc[IMAP_NFLAGS] = {
	"Seen", "Answered", "Deleted", "Flagged", "Draft", "Recent"
};

const char *imap_flag_desc_escaped[IMAP_NFLAGS] = {
	"\\Seen", "\\Answered", "\\Deleted", "\\Flagged", "\\Draft",
	"\\Recent"
};

int imapcommands_use_uid = 0;
int list_is_lsub = 0;

extern cache_t cached_msg;

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
int _ic_capability(char *tag, char **args, ClientInfo * ci)
{
	if (!check_state_and_args("CAPABILITY", tag, args, 0, -1, ci))
		return 1;	/* error, return */

	ci_write(ci->tx, "* CAPABILITY %s\r\n", IMAP_CAPABILITY_STRING);
	ci_write(ci->tx, "%s OK CAPABILITY completed\r\n", tag);

	return 0;
}


/*
 * _ic_noop()
 *
 * performs No operation
 */
int _ic_noop(char *tag, char **args, ClientInfo * ci)
{
	if (!check_state_and_args("NOOP", tag, args, 0, -1, ci))
		return 1;	/* error, return */

	ci_write(ci->tx, "%s OK NOOP completed\r\n", tag);
	return 0;
}


/*
 * _ic_logout()
 *
 * prepares logout from IMAP-server
 */
int _ic_logout(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	timestring_t timestring;

	create_current_timestring(&timestring);
	if (!check_state_and_args("LOGOUT", tag, args, 0, -1, ci))
		return 1;	/* error, return */

	/* change status */
	ud->state = IMAPCS_LOGOUT;

	trace(TRACE_MESSAGE,
	      "_ic_logout(): user (id:%llu) logging out @ [%s]",
	      ud->userid, timestring);

	ci_write(ci->tx, "* BYE dbmail imap server kisses you goodbye\r\n");

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
int _ic_login(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t userid = 0;
	timestring_t timestring;
	int validate_result;

	create_current_timestring(&timestring);

	if (!check_state_and_args
	    ("LOGIN", tag, args, 2, IMAPCS_NON_AUTHENTICATED, ci))
		return 1;	/* error, return */

	trace(TRACE_DEBUG, "_ic_login(): trying to validate user");
	validate_result = auth_validate(args[0], args[1], &userid);
	trace(TRACE_MESSAGE,
	      "_ic_login(): user (id:%llu, name %s) tries login",
	      userid, args[0]);

	if (validate_result == -1) {
		/* a db-error occurred */
		ci_write(ci->tx,
			"* BYE internal db error validating user\r\n");
		trace(TRACE_ERROR,
		      "_ic_login(): db-validate error while validating user %s (pass %s).",
		      args[0], args[1]);
		return -1;
	}

	if (validate_result == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		trace(TRACE_MESSAGE,
		      "IMAPD [PID %d]: user (name %s) login rejected @ %s",
		      (int) getpid(), args[0], timestring);
		ci_write(ci->tx, "%s NO login rejected\r\n", tag);

		return 1;
	}

	/* login ok */
	trace(TRACE_MESSAGE,
	      "_ic_login(): user (id %llu, name %s) login accepted @ %s",
	      userid, args[0], timestring);
#ifdef PROC_TITLES
	set_proc_title("USER %s [%s]", args[0], ci->ip);
#endif

	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	if (imap_before_smtp)
		db_log_ip(ci->ip);

	ci_write(ci->tx, "%s OK LOGIN completed\r\n", tag);
	return 0;
}


/*
 * _ic_authenticate()
 * 
 * performs authentication using LOGIN mechanism:
 *
 *
 */
int _ic_authenticate(char *tag, char **args, ClientInfo * ci)
{
	u64_t userid;
	char username[MAX_LINESIZE], buf[MAX_LINESIZE], pass[MAX_LINESIZE];
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int validate_result;
	timestring_t timestring;

	create_current_timestring(&timestring);
	if (!check_state_and_args
	    ("AUTHENTICATE", tag, args, 1, IMAPCS_NON_AUTHENTICATED, ci))
		return 1;	/* error, return */

	/* check authentication method */
	if (strcasecmp(args[0], "login") != 0) {
		ci_write(ci->tx,
			"%s NO Invalid authentication mechanism specified\r\n",
			tag);
		return 1;
	}

	/* ask for username (base64 encoded) */
	memset(buf, 0, MAX_LINESIZE);
	base64encode("username\r\n", buf);
	ci_write(ci->tx, "+ %s\r\n", buf);
	fflush(ci->tx);

	alarm(ci->timeout);
	if (fgets(buf, MAX_LINESIZE, ci->rx) == NULL) {
		trace(TRACE_ERROR, "%s,%s: error reading from client",
		      __FILE__, __func__);
		ci_write(ci->tx, "* BYE Error reading input\r\n");
		return -1;
	}
	alarm(0);

	base64decode(buf, username);

	/* ask for password */
	memset(buf, 0, MAX_LINESIZE);
	base64encode("password\r\n", buf);
	ci_write(ci->tx, "+ %s\r\n", buf);
	fflush(ci->tx);

	alarm(ci->timeout);
	if (fgets(buf, MAX_LINESIZE, ci->rx) == NULL) {
		trace(TRACE_ERROR, "%s,%s: error reading from client",
		      __FILE__, __func__);
		ci_write(ci->tx, "* BYE Error reading input\r\n");
		return -1;
	}
	alarm(0);

	base64decode(buf, pass);


	/* try to validate user */
	validate_result = auth_validate(username, pass, &userid);

	if (validate_result == -1) {
		/* a db-error occurred */
		ci_write(ci->tx,
			"* BYE internal db error validating user\r\n");
		trace(TRACE_ERROR,
		      "IMAPD: authenticate(): db-validate error while validating user %s "
		      "(pass %s).", username, pass);
		return -1;
	}

	if (validate_result == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		ci_write(ci->tx, "%s NO login rejected\r\n", tag);

		/* validation failed: invalid user/pass combination */
		trace(TRACE_MESSAGE,
		      "IMAPD [PID %d]: user (name %s) login rejected @ %s",
		      (int) getpid(), username, timestring);

		return 1;
	}

	/* login ok */
	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	if (imap_before_smtp)
		db_log_ip(ci->ip);

	trace(TRACE_MESSAGE,
	      "IMAPD [PID %d]: user (id %llu, name %s) login accepted @ %s",
	      (int) getpid(), userid, username, timestring);
#ifdef PROC_TITLES
	set_proc_title("USER %s [%s]", args[0], ci->ip);
#endif

	ci_write(ci->tx, "%s OK AUTHENTICATE completed\r\n", tag);
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
int _ic_select(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid, key;
	int result;
	unsigned idx = 0;
#define PERMSTRING_SIZE 80
	char permstring[PERMSTRING_SIZE];

	if (!check_state_and_args
	    ("SELECT", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (mboxid == 0) {
		ci_write(ci->tx,
			"%s NO Could not find specified mailbox\r\n", tag);

		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));

		return 1;
	}

	/* check if user has right to select mailbox */
	result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to select mailbox\r\n", tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	/* check if mailbox is selectable */
	result = db_isselectable(mboxid);
	if (result == 0) {
		/* error: cannot select mailbox */
		ci_write(ci->tx,
			"%s NO specified mailbox is not selectable\r\n",
			tag);

		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));

		return 1;
	}
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}

	ud->mailbox.uid = mboxid;

	/* read info from mailbox */ result = db_getmailbox(&ud->mailbox);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal  */
	}

	/* show mailbox info */
	/* msg counts */
	ci_write(ci->tx, "* %u EXISTS\r\n", ud->mailbox.exists);
	ci_write(ci->tx, "* %u RECENT\r\n", ud->mailbox.recent);

	/* flags */
	ci_write(ci->tx, "* FLAGS (");

	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		ci_write(ci->tx, "\\Seen ");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		ci_write(ci->tx, "\\Answered ");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		ci_write(ci->tx, "\\Deleted ");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		ci_write(ci->tx, "\\Flagged ");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		ci_write(ci->tx, "\\Draft ");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		ci_write(ci->tx, "\\Recent ");

	ci_write(ci->tx, ")\r\n");

	/* permanent flags */
	ci_write(ci->tx, "* OK [PERMANENTFLAGS (");
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		ci_write(ci->tx, "\\Seen ");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		ci_write(ci->tx, "\\Answered ");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		ci_write(ci->tx, "\\Deleted ");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		ci_write(ci->tx, "\\Flagged ");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		ci_write(ci->tx, "\\Draft ");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		ci_write(ci->tx, "\\Recent ");

	ci_write(ci->tx, ")]\r\n");

	/* UID */
	ci_write(ci->tx, "* OK [UIDVALIDITY %llu] UID value\r\n",
		ud->mailbox.uid);

	/* show idx of first unseen msg (if present) */
	key = db_first_unseen(ud->mailbox.uid);
	if (key == (u64_t) (-1)) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (binary_search(ud->mailbox.seq_list, ud->mailbox.exists,
			  key, &idx) != -1)
		ci_write(ci->tx,
			"* OK [UNSEEN %u] first unseen message\r\n",
			idx + 1);

	/* permission */
	switch (ud->mailbox.permission) {
	case IMAPPERM_READ:
		snprintf(permstring, PERMSTRING_SIZE, "READ-ONLY");
		break;
	case IMAPPERM_READWRITE:
		snprintf(permstring, PERMSTRING_SIZE, "READ-WRITE");
		break;
	default:
		/* invalid permission --> fatal */
		trace(TRACE_ERROR,
		      "IMAPD: select(): detected invalid permission mode for mailbox %llu ('%s')",
		      ud->mailbox.uid, args[0]);

		ci_write(ci->tx,
			"* BYE fatal: detected invalid mailbox settings\r\n");
		return -1;
	}

	/* ok, update state */
	ud->state = IMAPCS_SELECTED;

	ci_write(ci->tx, "%s OK [%s] SELECT completed\r\n", tag,
		permstring);
	return 0;
}


/*
 * _ic_examine()
 * 
 * examines a specified mailbox 
 */
int _ic_examine(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid;
	int result;

	if (!check_state_and_args
	    ("EXAMINE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (mboxid == 0) {
		ci_write(ci->tx,
			"%s NO Could not find specified mailbox\r\n", tag);
		return 1;
	}

	/* check if user has right to examine mailbox */
	result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to examine mailbox\r\n", tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	/* check if mailbox is selectable */
	result = db_isselectable(mboxid);
	if (result == 0) {
		/* error: cannot select mailbox */
		ci_write(ci->tx,
			"%s NO specified mailbox is not selectable\r\n",
			tag);
		return 1;	/* fatal */
	}
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}

	ud->mailbox.uid = mboxid;

	/* read info from mailbox */
	result = db_getmailbox(&ud->mailbox);

	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}

	/* show mailbox info */
	/* msg counts */
	ci_write(ci->tx, "* %u EXISTS\r\n", ud->mailbox.exists);
	ci_write(ci->tx, "* %u RECENT\r\n", ud->mailbox.recent);

	/* flags */
	ci_write(ci->tx, "* FLAGS (");

	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		ci_write(ci->tx, "\\Seen ");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		ci_write(ci->tx, "\\Answered ");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		ci_write(ci->tx, "\\Deleted ");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		ci_write(ci->tx, "\\Flagged ");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		ci_write(ci->tx, "\\Draft ");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		ci_write(ci->tx, "\\Recent ");

	ci_write(ci->tx, ")\r\n");

	/* permanent flags */
	ci_write(ci->tx, "* OK [PERMANENTFLAGS (");
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		ci_write(ci->tx, "\\Seen ");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		ci_write(ci->tx, "\\Answered ");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		ci_write(ci->tx, "\\Deleted ");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		ci_write(ci->tx, "\\Flagged ");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		ci_write(ci->tx, "\\Draft ");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		ci_write(ci->tx, "\\Recent ");

	ci_write(ci->tx, ")]\r\n");

	/* UID */
	ci_write(ci->tx, "* OK [UIDVALIDITY %llu] UID value\r\n",
		ud->mailbox.uid);

	/* update permission: examine forces read-only */
	ud->mailbox.permission = IMAPPERM_READ;

	/* ok, update state */
	ud->state = IMAPCS_SELECTED;

	ci_write(ci->tx, "%s OK [READ-ONLY] EXAMINE completed\r\n", tag);
	return 0;
}


/*
 * _ic_create()
 *
 * create a mailbox
 */
int _ic_create(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result, i;
	u64_t mboxid, tmp_mboxid;
	u64_t parent_mboxid = 0;	/* id of parent mailbox (if applicable) */
	char **chunks, *cpy;
	int other_namespace = 0;

	if (!check_state_and_args
	    ("CREATE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	/* remove trailing '/' if present */
	while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
		args[0][strlen(args[0]) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; args[0][i] && args[0][i] == '/'; i++);
	memmove(&args[0][0], &args[0][i],
		(strlen(args[0]) - i) * sizeof(char));

	/* check if this mailbox already exists */
	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		/* dbase failure */
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}

	if (mboxid != 0) {
		/* mailbox already exists */
		ci_write(ci->tx, "%s NO mailbox already exists\r\n", tag);
		return 1;
	}

	/* check if new name is valid */
	if (!checkmailboxname(args[0])) {
		ci_write(ci->tx,
			"%s BAD new mailbox name contains invalid characters\r\n",
			tag);
		return 1;
	}

	/* alloc a ptr which can contain up to the full name */
	cpy = (char *) my_malloc(sizeof(char) * (strlen(args[0]) + 1));
	if (!cpy) {
		/* out of mem */
		trace(TRACE_ERROR, "IMAPD: create(): not enough memory");
		ci_write(ci->tx, "* BYE server ran out of memory\r\n");
		return -1;
	}

	/* split up the name & create parent folders as necessary */
	chunks = give_chunks(args[0], '/');

	if (chunks == NULL) {
		/* serious error while making chunks */
		trace(TRACE_ERROR,
		      "IMAPD: create(): could not create chunks");
		ci_write(ci->tx, "* BYE server ran out of memory\r\n");
		my_free(cpy);
		return -1;
	}

	if (chunks[0] == NULL) {
		/* wrong argument */
		ci_write(ci->tx, "%s NO invalid mailbox name specified\r\n",
			tag);
		free_chunks(chunks);
		my_free(cpy);
		return 1;
	}

	/* now go create */
	strcpy(cpy, "");

	for (i = 0; chunks[i]; i++) {
		if (strlen(chunks[i]) == 0) {
			/* no can do */
			ci_write(ci->tx,
				"%s NO invalid mailbox name specified\r\n",
				tag);
			free_chunks(chunks);
			my_free(cpy);
			return 1;
		}

		if (i == 0) {
			if (strcasecmp(chunks[0], "inbox") == 0)
				strcpy(chunks[0], "INBOX");	/* make inbox uppercase */

			strcat(cpy, chunks[i]);
			/* check to see if this is a folder in Other Users/ or Public
			   namespace */
			if (strcmp(cpy, NAMESPACE_USER) == 0 ||
			    strcmp(cpy, NAMESPACE_PUBLIC) == 0) {
				other_namespace = 1;
				continue;
			}
		} else {
			strcat(cpy, "/");
			strcat(cpy, chunks[i]);
			/* if this is in Other Users namespace, continue to the next chunk */
			if (i == 1
			    && strncmp(cpy, NAMESPACE_USER,
				       strlen(NAMESPACE_USER)) == 0)
				continue;

		}

		trace(TRACE_DEBUG, "checking for '%s'...", cpy);

		/* check if this mailbox already exists */
		if (db_findmailbox(cpy, ud->userid, &mboxid) == -1) {
			/* dbase failure */
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			free_chunks(chunks);
			my_free(cpy);
			return -1;	/* fatal */
		}

		if (mboxid == 0) {
			/* mailbox does not exist */
			/* check if we have the right to create mailboxes in this
			   hierarchy */
			trace(TRACE_DEBUG,
			      "%s,%s: Checking if we have the right to "
			      "create mailboxes under mailbox [%llu]",
			      __FILE__, __func__, parent_mboxid);
			if (parent_mboxid != 0) {
				result =
				    acl_has_right(ud->userid,
						  parent_mboxid,
						  ACL_RIGHT_CREATE);
				if (result < 0) {
					ci_write(ci->tx,
						"* BYE internal database "
						"error\r\n");
					free_chunks(chunks);
					my_free(cpy);
					return -1;	/* fatal */
				}
				if (result == 0) {
					ci_write(ci->tx,
						"%s NO no permission to create "
						"mailbox here\r\n", tag);
					free_chunks(chunks);
					my_free(cpy);
					return 1;
				}
			} else {
				if (other_namespace) {
					/* if we want to create a new mailbox in 
					   another namespace, but we don't specify 
					   the parent's mailbox, we should not be
					   allowed to do so */
					ci_write(ci->tx,
						"%s NO no permission to create "
						"mailbox here\r\n", tag);
					free_chunks(chunks);
					my_free(cpy);
					return 1;
				}
			}
			result =
			    db_createmailbox(cpy, ud->userid, &tmp_mboxid);


			if (result == -1) {
				ci_write(ci->tx,
					"* BYE internal dbase error\r\n");
				free_chunks(chunks);
				my_free(cpy);
				return -1;	/* fatal */
			}
		} else {
			/* this might be the parent of our new mailbox. store it's id */
			parent_mboxid = mboxid;
			/* mailbox does exist, failure if no_inferiors flag set */
			result = db_noinferiors(mboxid);
			if (result == 1) {
				ci_write(ci->tx,
					"%s NO mailbox cannot have inferior names\r\n",
					tag);
				free_chunks(chunks);
				my_free(cpy);
				return 1;
			}


			if (result == -1) {
				/* dbase failure */
				ci_write(ci->tx,
					"* BYE internal dbase error\r\n");
				free_chunks(chunks);
				my_free(cpy);
				return -1;	/* fatal */
			}
		}
	}

	/* creation complete */
	free_chunks(chunks);
	my_free(cpy);

	ci_write(ci->tx, "%s OK CREATE completed\r\n", tag);
	return 0;
}


/*
 * _ic_delete()
 *
 * deletes a specified mailbox
 */
int _ic_delete(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result, nchildren = 0;
	u64_t *children = NULL, mboxid;

	if (!check_state_and_args
	    ("DELETE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	/* remove trailing '/' if present */
	while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
		args[0][strlen(args[0]) - 1] = '\0';

	/* check if this mailbox exists */
	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		/* dbase failure */
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}
	if (mboxid == 0) {
		/* mailbox does not exist */
		ci_write(ci->tx, "%s NO mailbox does not exist\r\n", tag);
		return 1;
	}

	/* check if the user is the owner of this mailbox. If so, then
	   the user has the right to delete it. */
	result = db_user_is_mailbox_owner(ud->userid, mboxid);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to delete mailbox\r\n", tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	/* check if there is an attempt to delete inbox */
	if (strcasecmp(args[0], "inbox") == 0) {
		ci_write(ci->tx,
			"%s NO cannot delete special mailbox INBOX\r\n",
			tag);
		return 1;
	}

	/* check for children of this mailbox */
	result =
	    db_listmailboxchildren(mboxid, ud->userid, &children,
				   &nchildren, "%");
	if (result == -1) {
		/* error */
		trace(TRACE_ERROR,
		      "IMAPD: delete(): cannot retrieve list of mailbox children");
		ci_write(ci->tx, "* BYE dbase/memory error\r\n");
		return -1;
	}

	if (nchildren != 0) {
		/* mailbox has inferior names; error if \noselect specified */
		result = db_isselectable(mboxid);
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO mailbox is non-selectable\r\n",
				tag);
			my_free(children);
			return 1;
		}
		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			my_free(children);
			return -1;	/* fatal */
		}

		/* mailbox has inferior names; remove all msgs and set noselect flag */
		result = db_removemsg(ud->userid, mboxid);
		if (result != -1)
			result = db_setselectable(mboxid, 0);	/* set non-selectable flag */

		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			my_free(children);
			return -1;	/* fatal */
		}

		/* check if this was the currently selected mailbox */
		if (mboxid == ud->mailbox.uid) {
			my_free(ud->mailbox.seq_list);
			memset(&ud->mailbox, 0, sizeof(ud->mailbox));
			ud->state = IMAPCS_AUTHENTICATED;
		}

		/* ok done */
		ci_write(ci->tx, "%s OK DELETE completed\r\n", tag);
		my_free(children);
		return 0;
	}

	/* ok remove mailbox */
	db_delete_mailbox(mboxid, 0, 1);

	/* check if this was the currently selected mailbox */
	if (mboxid == ud->mailbox.uid) {
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		ud->state = IMAPCS_AUTHENTICATED;
	}

	ci_write(ci->tx, "%s OK DELETE completed\r\n", tag);
	return 0;
}


/*
 * _ic_rename()
 *
 * renames a specified mailbox
 */
int _ic_rename(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid, newmboxid, *children, parentmboxid;
	size_t oldnamelen;
	int nchildren, i, result;
	char newname[IMAP_MAX_MAILBOX_NAMELEN],
	    name[IMAP_MAX_MAILBOX_NAMELEN];

	if (!check_state_and_args
	    ("RENAME", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	/* remove trailing '/' if present */
	while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
		args[0][strlen(args[0]) - 1] = '\0';
	while (strlen(args[1]) > 0 && args[1][strlen(args[1]) - 1] == '/')
		args[1][strlen(args[1]) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; args[1][i] && args[1][i] == '/'; i++);
	memmove(&args[1][0], &args[1][i],
		(strlen(args[1]) - i) * sizeof(char));

	for (i = 0; args[0][i] && args[0][i] == '/'; i++);
	memmove(&args[0][0], &args[0][i],
		(strlen(args[0]) - i) * sizeof(char));


	/* check if new mailbox exists */
	if (db_findmailbox(args[1], ud->userid, &mboxid) == -1) {
		/* dbase failure */
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}
	if (mboxid != 0) {
		/* mailbox exists */
		ci_write(ci->tx, "%s NO new mailbox already exists\r\n",
			tag);
		return 1;
	}

	/* check if original mailbox exists */
	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		/* dbase failure */
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}
	if (mboxid == 0) {
		/* mailbox does not exist */
		ci_write(ci->tx, "%s NO mailbox does not exist\r\n", tag);
		return 1;
	}

	/* check if the user is the owner of this mailbox. If so, then
	   the user has the right to delete it. */
	result = db_user_is_mailbox_owner(ud->userid, mboxid);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to rename mailbox\r\n", tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	/* check if new name is valid */
	if (!checkmailboxname(args[1])) {
		ci_write(ci->tx,
			"%s NO new mailbox name contains invalid characters\r\n",
			tag);
		return 1;
	}

	oldnamelen = strlen(args[0]);

	/* check if new name would invade structure as in
	 * test (exists)
	 * rename test test/testing
	 * would create test/testing but delete test
	 */
	if (strncasecmp(args[0], args[1], (int) oldnamelen) == 0 &&
	    strlen(args[1]) > oldnamelen && args[1][oldnamelen] == '/') {
		ci_write(ci->tx,
			"%s NO new mailbox would invade mailbox structure\r\n",
			tag);
		return 1;
	}


	/* check if structure of new name is valid */
	/* i.e. only last part (after last '/' can be nonexistent) */
	for (i = strlen(args[1]) - 1; i >= 0 && args[1][i] != '/'; i--);
	if (i >= 0) {
		args[1][i] = '\0';	/* note: original char was '/' */

		if (db_findmailbox(args[1], ud->userid, &parentmboxid) ==
		    -1) {
			/* dbase failure */
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			return -1;	/* fatal */
		}
		if (parentmboxid == 0) {
			/* parent mailbox does not exist */
			ci_write(ci->tx,
				"%s NO new mailbox would invade mailbox structure\r\n",
				tag);
			return 1;
		}

		/* ok, reset arg */
		args[1][i] = '/';
	}

	/* check if it is INBOX to be renamed */
	if (strcasecmp(args[0], "inbox") == 0) {
		/* ok, renaming inbox */
		/* this means creating a new mailbox and moving all the INBOX msgs to the new mailbox */
		/* inferior names of INBOX are left unchanged */
		result = db_createmailbox(args[1], ud->userid, &newmboxid);
		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			return -1;
		}

		result = db_movemsg(newmboxid, mboxid);
		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			return -1;
		}

		/* ok done */
		ci_write(ci->tx, "%s OK RENAME completed\r\n", tag);
		return 0;
	}

	/* check for inferior names */
	result =
	    db_listmailboxchildren(mboxid, ud->userid, &children,
				   &nchildren, "%");
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}

	/* replace name for each child */
	for (i = 0; i < nchildren; i++) {
		result = db_getmailboxname(children[i], ud->userid, name);
		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			my_free(children);
			return -1;
		}

		if (oldnamelen >= strlen(name)) {
			/* strange error, let's say its fatal */
			trace(TRACE_ERROR,
			      "IMAPD: rename(): mailbox names appear to be corrupted");
			ci_write(ci->tx,
				"* BYE internal error regarding mailbox names\r\n");
			my_free(children);
			return -1;
		}

		snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s",
			 args[1], &name[oldnamelen]);

		result = db_setmailboxname(children[i], newname);
		if (result == -1) {
			ci_write(ci->tx, "* BYE internal dbase error\r\n");
			my_free(children);
			return -1;
		}
	}
	if (children)
		my_free(children);

	/* now replace name */
	result = db_setmailboxname(mboxid, args[1]);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}

	ci_write(ci->tx, "%s OK RENAME completed\r\n", tag);
	return 0;
}


/*
 * _ic_subscribe()
 *
 * subscribe to a specified mailbox
 */
int _ic_subscribe(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid;
	int result;


	if (!check_state_and_args
	    ("SUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (mboxid == 0) {
		/* mailbox does not exist */
		ci_write(ci->tx, "%s NO mailbox does not exist\r\n", tag);
		return 0;
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */
	result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_LOOKUP);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to subscribe to  mailbox\r\n",
			tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	if (db_subscribe(mboxid, ud->userid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}

	ci_write(ci->tx, "%s OK SUBSCRIBE completed\r\n", tag);
	return 0;
}


/*
 * _ic_unsubscribe()
 *
 * removes a mailbox from the users' subscription list
 */
int _ic_unsubscribe(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid;
	int result;


	if (!check_state_and_args
	    ("UNSUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (mboxid == 0) {
		/* mailbox does not exist */
		ci_write(ci->tx, "%s NO mailbox does not exist\r\n", tag);
		return 0;
	}

	/* check for the lookup-right. RFC is unclear about which right to
	   use, so I guessed it should be lookup */
	result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_LOOKUP);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to unsubscribe from mailbox\r\n",
			tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}

	if (db_unsubscribe(mboxid, ud->userid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}

	ci_write(ci->tx, "%s OK UNSUBSCRIBE completed\r\n", tag);
	return 0;
}


/*
 * _ic_list()
 *
 * executes a list command
 */
int _ic_list(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t *children = NULL;
	int result;
	size_t slen, plen;
	unsigned i, j;
	unsigned nchildren;
	char *pattern;
	char *thisname = list_is_lsub ? "LSUB" : "LIST";
	
	mailbox_t *mb = (mailbox_t *)my_malloc(sizeof(mailbox_t));
	memset(mb,0,sizeof(mailbox_t));
	GList * plist = NULL;

	if (!check_state_and_args
	    (thisname, tag, args, 2, IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */


	/* check if args are both empty strings */
	if (strlen(args[0]) == 0 && strlen(args[1]) == 0) {
		/* this has special meaning; show root & delimiter */
		trace(TRACE_ERROR,
		      "_ic_list(): showing delimiter [(\\NoSelect) \"/\" \"\"]");
		ci_write(ci->tx, "* %s (\\NoSelect) \"/\" \"\"\r\n",
			thisname);
		ci_write(ci->tx, "%s OK %s completed\r\n", tag, thisname);
		return 0;
	}

	/* check the reference name, should contain only accepted mailboxname chars */
	for (i = 0, slen = strlen(args[0]); args[0][i]; i++) {
		if (stridx(AcceptedMailboxnameChars, args[0][i]) == slen) {
			/* wrong char found */
			ci_write(ci->tx,
				"%s BAD reference name contains invalid characters\r\n",
				tag);
			return 1;
		}
	}

	plen = strlen(args[1]) * 6;
	/* FIXME: We need to allocated a sane amount of memory for this, 
	   instead of adding an extra 20 places for just for having extra
	   space. This is bound to fail sometime!! */
	pattern = (char *) my_malloc(sizeof(char) * (plen + slen + 20));
	if (!pattern) {
		ci_write(ci->tx, "* BYE out of memory\r\n");
		return -1;
	}

	memset(pattern, '\0', plen + slen + 12);
	pattern[0] = '^';
	strcpy(&pattern[1], args[0]);

	i = slen + 1;
	for (j = 0; args[1][j] && i < (plen + slen + 1); j++) {
		if (args[1][j] == '*') {
			pattern[i++] = '.';
			pattern[i++] = '*';
		} else if (args[1][j] == '%') {
			pattern[i++] = '[';
			pattern[i++] = '^';
			pattern[i++] = '\\';
			pattern[i++] = '/';
			pattern[i++] = ']';
			pattern[i++] = '*';
		} else
			pattern[i++] = args[1][j];
	}

	/* small addition by Ilja */
	if (pattern[i - 1] != '*') {
		pattern[i++] = '.';
		pattern[i++] = '*';
	}
	pattern[i] = '$';

	trace(TRACE_INFO, "ic_list(): build the pattern: [%s]", pattern);

	result =
	    db_findmailbox_by_regex(ud->userid, pattern, &children,
				    &nchildren, list_is_lsub);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		my_free(children);
		my_free(pattern);
		return -1;
	}

	if (result == 1) {
		ci_write(ci->tx, "%s BAD invalid pattern specified\r\n",
			tag);
		my_free(children);
		my_free(pattern);
		return 1;
	}

	for (i = 0; i < nchildren; i++) {
		result = db_getmailbox_list_result(children[i], ud->userid, mb);
		plist = NULL;
		if (mb->no_select)
			plist = g_list_append(plist, "\\noselect");
		if (mb->no_inferiors)
			plist = g_list_append(plist, "\\noinferiors");
		
		/* show */
		ci_write(ci->tx, "* %s %s \"/\" \"%s\"\r\n", thisname, dbmail_imap_plist_as_string(plist), mb->name);
	}

	g_list_foreach(plist,(GFunc)g_free,NULL);
	g_list_free(plist);

	if (children)
		my_free(children);

	my_free(pattern);

	ci_write(ci->tx, "%s OK %s completed\r\n", tag, thisname);
	return 0;
}


/*
 * _ic_lsub()
 *
 * list subscribed mailboxes
 */
int _ic_lsub(char *tag, char **args, ClientInfo * ci)
{
	int result;

	list_is_lsub = 1;
	result = _ic_list(tag, args, ci);
	list_is_lsub = 0;
	return result;
}


/*
 * _ic_status()
 *
 * inquire the status of a mailbox
 */
int _ic_status(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	mailbox_t mb;
	int i, endfound, result;
	GString *response = g_string_new("");
	GList *plst = NULL;
	
	if (ud->state != IMAPCS_AUTHENTICATED
	    && ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx, "%s BAD STATUS command received in invalid state\r\n", tag);
		return 1;
	}

	if (!args[0] || !args[1] || !args[2]) {
		ci_write(ci->tx, "%s BAD missing argument(s) to STATUS\r\n", tag);
		return 1;
	}

	if (strcmp(args[1], "(") != 0) {
		ci_write(ci->tx, "%s BAD argument list should be parenthesed\r\n", tag);
		return 1;
	}

	/* check final arg: should be ')' and no new '(' in between */
	for (i = 2, endfound = 0; args[i]; i++) {
		if (strcmp(args[i], ")") == 0) {
			endfound = i;
			break;
		}

		if (strcmp(args[i], "(") == 0) {
			ci_write(ci->tx, "%s BAD too many parentheses specified\r\n", tag);
			return 1;
		}
	}

	if (endfound == 2) {
		ci_write(ci->tx, "%s BAD argument list empty\r\n", tag);
		return 1;
	}

	if (args[endfound + 1]) {
		ci_write(ci->tx, "%s BAD argument list too long\r\n", tag);
		return 1;
	}


	/* zero init */
	memset(&mb, 0, sizeof(mb));

	/* check if mailbox exists */
	if (db_findmailbox(args[0], ud->userid, &(mb.uid)) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}

	if (mb.uid == 0) {
		/* mailbox does not exist */
		ci_write(ci->tx, "%s NO specified mailbox does not exist\r\n", tag);
		return 1;
	}

	result = acl_has_right(ud->userid, mb.uid, ACL_RIGHT_READ);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx, "%s NO no rights to get status for mailbox\r\n", tag);
		return 1;
	}

	/* retrieve mailbox data */
	result = db_getmailbox(&mb);

	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal  */
	}
	for (i = 2; args[i]; i++) {
		if (strcasecmp(args[i], "messages") == 0)
			plst = g_list_append_printf(plst,"MESSAGES %u", mb.exists);
		else if (strcasecmp(args[i], "recent") == 0)
			plst = g_list_append_printf(plst,"RECENT %u", mb.recent);
		else if (strcasecmp(args[i], "unseen") == 0)
			plst = g_list_append_printf(plst,"UNSEEN %u", mb.unseen);
		else if (strcasecmp(args[i], "uidnext") == 0) {
			plst = g_list_append_printf(plst,"UIDNEXT %llu", mb.msguidnext);
		} else if (strcasecmp(args[i], "uidvalidity") == 0) {
			plst = g_list_append_printf(plst,"UIDVALIDITY %llu", mb.uid);
		} else if (strcasecmp(args[i], ")") == 0)
			break;
		else {
			ci_write(ci->tx,
				"\r\n%s BAD unrecognized option '%s' specified\r\n",
				tag, args[i]);
			my_free(mb.seq_list);
			return 1;
		}
	}
	g_string_printf(response, "* STATUS %s %s\r\n", 
		dbmail_imap_astring_as_string(args[0]),
		dbmail_imap_plist_as_string(plst));	
	trace(TRACE_DEBUG,"%s,%s: RESPONSE [ %s ]", __FILE__, __func__, response->str);
	ci_write(ci->tx, "%s", response->str);
	ci_write(ci->tx, "%s OK STATUS completed\r\n", tag);

	my_free(mb.seq_list);
	g_list_foreach(plst,(GFunc)g_free,NULL);
	g_list_free(plst);
	g_string_free(response,1);
	return 0;
}


/*
 * _ic_append()
 *
 * append a message to a mailbox
 */
int _ic_append(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid;
	u64_t msg_idnr;
	int i, j, result;
	timestring_t sqldate;
	int flaglist[IMAP_NFLAGS];
	int flagcount = 0;

	for (i = 0; i < IMAP_NFLAGS; i++)
		flaglist[i] = 0;

	if (!args[0] || !args[1]) {
		ci_write(ci->tx,
			"%s BAD invalid arguments specified to APPEND\r\n",
			tag);
		return 1;
	}

	/* find the mailbox to place the message */
	if (db_findmailbox(args[0], ud->userid, &mboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error");
		return -1;
	}

	if (mboxid == 0) {
		ci_write(ci->tx,
			"%s NO [TRYCREATE] could not find specified mailbox\r\n",
			tag);
		return 1;
	}

	trace(TRACE_DEBUG, "ic_append(): mailbox [%s] found, id: %llu",
	      args[0], mboxid);
	/* check if user has right to append to  mailbox */
	result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_INSERT);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to append to mailbox\r\n",
			tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		return 1;
	}


	i = 1;

	/* check if a flag list has been specified */
	/* FIXME: We need to take of care of the Flags that are set here. They
	   should be set to the new message!
	 */
	if (args[i][0] == '(') {
		/* ok fetch the flags specified */
		trace(TRACE_DEBUG, "ic_append(): flag list found:");

		while (args[i] && args[i][0] != ')') {
			trace(TRACE_DEBUG, "%s ", args[i]);
			for (j = 0; j < IMAP_NFLAGS; j++) {
				if (strcasecmp
				    (args[i],
				     imap_flag_desc_escaped[j]) == 0) {
					flaglist[j] = 1;
					flagcount++;
					break;
				}
			}
			i++;
		}

		i++;
		trace(TRACE_DEBUG, ")");
	}

	if (!args[i]) {
		trace(TRACE_INFO,
		      "ic_append(): unexpected end of arguments");
		ci_write(ci->tx,
			"%s BAD invalid arguments specified to APPEND\r\n",
			tag);
		return 1;
	}

	for (j = 0; j < IMAP_NFLAGS; j++)
		if (flaglist[j] == 1)
			trace(TRACE_DEBUG, "%s,%s: %s set",
			      __FILE__, __func__, imap_flag_desc[j]);

	/** check ACL's for STORE */
	if (flaglist[IMAP_STORE_FLAG_SEEN] == 1) {
		result = acl_has_right(ud->userid, mboxid, ACL_RIGHT_SEEN);
		if (result < 0) {
			ci_write(ci->tx,
				"* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO no right to store \\SEEN flag\r\n",
				tag);
			return 1;
		}
	}
	if (flaglist[IMAP_STORE_FLAG_DELETED] == 1) {
		result =
		    acl_has_right(ud->userid, mboxid, ACL_RIGHT_DELETE);
		if (result < 0) {
			ci_write(ci->tx,
				"* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO no right to store \\DELETED flag\r\n",
				tag);
			return 1;
		}
	}
	if (flaglist[IMAP_STORE_FLAG_ANSWERED] == 1 ||
	    flaglist[IMAP_STORE_FLAG_FLAGGED] == 1 ||
	    flaglist[IMAP_STORE_FLAG_DRAFT] == 1 ||
	    flaglist[IMAP_STORE_FLAG_RECENT] == 1) {
		result =
		    acl_has_right(ud->userid, mboxid, ACL_RIGHT_WRITE);
		if (result < 0) {
			ci_write(ci->tx,
				"*BYE internal database error\r\n");
			return -1;
		}
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO no right to store flags\r\n", tag);
			return 1;
		}
	}


	/* there could be a literal date here, check if the next argument exists
	 * if so, assume this is the literal date.
	 */
	if (args[i + 1]) {
		struct tm tm;

		if (strptime(args[i], "%d-%b-%Y %T", &tm) != NULL)
			strftime(sqldate,
				 sizeof(sqldate), "%Y-%m-%d %H:%M:%S",
				 &tm);
		else
			sqldate[0] = '\0';
		/* internal date specified */

		i++;
		trace(TRACE_DEBUG,
		      "ic_append(): internal date [%s] found, next arg [%s]",
		      sqldate, args[i]);
	} else {
		sqldate[0] = '\0';
	}

	/* ok literal msg should be in args[i] */
	/* insert this msg */

	result =
	    db_imap_append_msg(args[i], strlen(args[i]), mboxid,
			       ud->userid, sqldate, &msg_idnr);
	switch (result) {
	case -1:
		trace(TRACE_ERROR, "ic_append(): error appending msg");
		ci_write(ci->tx,
			"* BYE internal dbase error storing message\r\n");
		break;

	case 1:
		trace(TRACE_ERROR, "ic_append(): faulty msg");
		ci_write(ci->tx, "%s NO invalid message specified\r\n",
			tag);
		break;

	case 2:
		trace(TRACE_INFO, "ic_append(): quotum would exceed");
		ci_write(ci->tx, "%s NO not enough quotum left\r\n", tag);
		break;

	case 0:
		ci_write(ci->tx, "%s OK APPEND completed\r\n", tag);
		break;
	}

	if (result == 0 && flagcount > 0) {
		if (db_set_msgflag(msg_idnr, mboxid, flaglist, IMAPFA_ADD)
		    < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: error setting flags for message "
			      "[%llu]", __FILE__, __func__, msg_idnr);
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
 * _ic_sort()
 * 
 * sort and return sorted IDS for the selected mailbox
 */
int _ic_sort(char *tag, char **args, ClientInfo *ci)
{
	imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
	unsigned *result_set;
	unsigned i;
	int result=0,only_ascii=0,idx=0;
	search_key_t sk;

	if (ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx,"%s BAD SORT command received in invalid state\r\n", tag);
		return 1;
	}
  
	memset(&sk, 0, sizeof(sk));
	list_init(&sk.sub_search);

	if (!args[0]) {
		ci_write(ci->tx,"%s BAD invalid arguments to SORT\r\n",tag);
		return 1;
	}

	if (strcasecmp(args[0], "charset") == 0) {
	/* charset specified */
	if (!args[1]) {
		ci_write(ci->tx,"%s BAD invalid argument list\r\n",tag);
		return 1;
	}

	if (strcasecmp(args[1], "us-ascii") != 0) {
		ci_write(ci->tx,"%s NO specified charset is not supported\r\n",tag);
		return 0;
	}

	only_ascii = 1;
	idx = 2;
}

	/* parse the search keys */
	while ( args[idx] && (result = build_imap_search(args, &sk.sub_search, &idx,1)) >= 0);

	if (result == -2) {
		free_searchlist(&sk.sub_search);
		ci_write(ci->tx, "* BYE server ran out of memory\r\n");
		return -1;
	}

	if (result == -1) {
		free_searchlist(&sk.sub_search);
		ci_write(ci->tx, "%s BAD syntax error in sort keys\r\n",tag);
		return 1;
	}

	/* check if user has the right to search in this mailbox */
	result = acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx,"* BYE internal database error\r\n");
		free_searchlist(&sk.sub_search);
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,"%s NO no permission to sort mailbox\r\n", tag);
		free_searchlist(&sk.sub_search);
		return 1;
	}
  
	/* make it a top-level search key */
	sk.type = IST_SUBSEARCH_AND;

	/* allocate memory for result set */
	result_set = (unsigned*)my_malloc(sizeof(unsigned) * ud->mailbox.exists);
	if (!result_set) {
		free_searchlist(&sk.sub_search);
		ci_write(ci->tx,"* NO server ran out of memory\r\n");
		return -1;
	}
   
	/* do i need this? */ 
	for (i=0; i<ud->mailbox.exists; i++)
		result_set[i] = (i+1);

	/* now perform the search operations */
	result = perform_imap_search((int *)result_set, ud->mailbox.exists, &sk, &ud->mailbox,1);
    
	if (result < 0) {
		free_searchlist(&sk.sub_search);
		my_free(result_set);
		ci_write(ci->tx,"%s", (result == -1) ? 
			"* NO internal dbase error\r\n" :
			"* NO server ran out of memory\r\n");

		trace(TRACE_ERROR, "ic_sort(): fatal error [%d] from perform_imap_search()\n",result);
		return -1;
	}

	free_searchlist(&sk.sub_search);

	if (result == 1) {
		ci_write(ci->tx,"* NO error synchronizing dbase\r\n");
		return -1;
	}
      
	/* ok, display results */
	ci_write(ci->tx, "* SORT");

	for (i=0; i<ud->mailbox.exists; i++) {
		/*trace(TRACE_INFO, "ic_sort(): i: %d, rs[i]: %d, mbuid[i]: %llu ", i, result_set[i], ud->mailbox.seq_list[i]); */
		if (result_set[i])
			ci_write(ci->tx, " %llu", imapcommands_use_uid ? ud->mailbox.seq_list[i] : (u64_t)result_set[i]);
	}

	ci_write(ci->tx,"\r\n");
	my_free(result_set);
      
	ci_write(ci->tx,"%s OK SORT completed\r\n",tag);
	return 0;
}

/*
 * _ic_check()
 * 
 * request a checkpoint for the selected mailbox
 * (equivalent to NOOP)
 */
int _ic_check(char *tag, char **args, ClientInfo * ci)
{
	int result;
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;

	if (!check_state_and_args
	    ("CHECK", tag, args, 0, IMAPCS_SELECTED, ci))

		return 1;	/* error, return */

	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE Internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx, "%s NO no permission to do check on "
			"mailbox\r\n", tag);
		return 1;
	}

	ci_write(ci->tx, "%s OK CHECK completed\r\n", tag);
	return 0;
}


/*
 * _ic_close()
 *
 * expunge deleted messages from selected mailbox & return to AUTH state
 * do not show expunge-output
 */
int _ic_close(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;

	if (!check_state_and_args
	    ("CLOSE", tag, args, 0, IMAPCS_SELECTED, ci))
		return 1;	/* error, return */

	/* check if the user has to right to expunge all messages from the
	   mailbox. */
	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_DELETE);
	if (result < 0) {
		ci_write(ci->tx, "* BYE Internal database error\r\n");
		return -1;
	}
	/* only perform the expunge if the user has the right to do it */
	if (result == 1)
		if (ud->mailbox.permission == IMAPPERM_READWRITE)
			db_expunge(ud->mailbox.uid, ud->userid, NULL,
				   NULL);


	/* ok, update state (always go to IMAPCS_AUTHENTICATED) */
	ud->state = IMAPCS_AUTHENTICATED;

	my_free(ud->mailbox.seq_list);
	memset(&ud->mailbox, 0, sizeof(ud->mailbox));

	ci_write(ci->tx, "%s OK CLOSE completed\r\n", tag);
	return 0;
}


/*
 * _ic_expunge()
 *
 * expunge deleted messages from selected mailbox
 * show expunge output per message
 */
int _ic_expunge(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	mailbox_t newmailbox;
	u64_t *msgids;
	u64_t nmsgs, i;
	unsigned idx;
	int result;

	if (!check_state_and_args
	    ("EXPUNGE", tag, args, 0, IMAPCS_SELECTED, ci))
		return 1;	/* error, return */

	if (ud->mailbox.permission != IMAPPERM_READWRITE) {
		ci_write(ci->tx,
			"%s NO you do not have write permission on this folder\r\n",
			tag);
		return 1;
	}

	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_DELETE);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO you do not have delete rights on this "
			"mailbox\r\n", tag);
		return -1;
	}

	/* delete messages */
	result = db_expunge(ud->mailbox.uid, ud->userid, &msgids, &nmsgs);
	if (result == -1) {
		ci_write(ci->tx, "* BYE dbase/memory error\r\n");
		return -1;
	}

	/* show expunge info */
	for (i = 0; i < nmsgs; i++) {
		/* find the message sequence number */
		binary_search(ud->mailbox.seq_list, ud->mailbox.exists,
			      msgids[i], &idx);

		ci_write(ci->tx, "* %u EXPUNGE\r\n", idx + 1);	/* add one: IMAP MSN starts at 1 not zero */
	}
	my_free(msgids);
	msgids = NULL;

	/* update mailbox info */

	memset(&newmailbox, 0, sizeof(newmailbox));
	newmailbox.uid = ud->mailbox.uid;

	result = db_getmailbox(&newmailbox);

	if (result == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		my_free(newmailbox.seq_list);
		return -1;	/* fatal  */
	}

	if (newmailbox.exists != ud->mailbox.exists)
		ci_write(ci->tx, "* %u EXISTS\r\n", newmailbox.exists);

	if (newmailbox.recent != ud->mailbox.recent)
		ci_write(ci->tx, "* %u RECENT\r\n", newmailbox.recent);

	my_free(ud->mailbox.seq_list);
	memcpy((void *) &ud->mailbox, (void *) &newmailbox, 
	       sizeof(newmailbox));

	ci_write(ci->tx, "%s OK EXPUNGE completed\r\n", tag);
	return 0;
}


/*
 * _ic_search()
 *
 * search the selected mailbox for messages
 *
 */
int _ic_search(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	unsigned *result_set;
	unsigned i;
	int result = 0, only_ascii = 0, idx = 0;
	search_key_t sk;

	if (ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx,
			"%s BAD SEARCH command received in invalid state\r\n",
			tag);
		return 1;
	}

	memset(&sk, 0, sizeof(sk));
	list_init(&sk.sub_search);

	if (!args[0]) {
		ci_write(ci->tx, "%s BAD invalid arguments to SEARCH\r\n",
			tag);
		return 1;
	}

	if (strcasecmp(args[0], "charset") == 0) {
		/* charset specified */
		if (!args[1]) {
			ci_write(ci->tx, "%s BAD invalid argument list\r\n",
				tag);
			return 1;
		}

		if (strcasecmp(args[1], "us-ascii") != 0) {
			ci_write(ci->tx,
				"%s NO specified charset is not supported\r\n",
				tag);
			return 0;
		}

		only_ascii = 1;
		idx = 2;
	}

	/* parse the search keys */
	while (args[idx]
	       && (result =
		   build_imap_search(args, &sk.sub_search, &idx, 0)) >= 0);

	if (result == -2) {
		free_searchlist(&sk.sub_search);
		ci_write(ci->tx, "* BYE server ran out of memory\r\n");
		return -1;
	}

	if (result == -1) {
		free_searchlist(&sk.sub_search);
		ci_write(ci->tx, "%s BAD syntax error in search keys\r\n",
			tag);
		return 1;
	}

	/* check if user has the right to search in this mailbox */
	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		free_searchlist(&sk.sub_search);
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to search mailbox\r\n", tag);
		free_searchlist(&sk.sub_search);
		return 1;
	}

	/* make it a top-level search key */
	sk.type = IST_SUBSEARCH_AND;

	i = 0;
	do {
		/* update mailbox info */
		/* commented out: the search should be on the mailbox 
		   as the client thinks it is (!) */
/* result = db_getmailbox(&ud->mailbox);
   
if (result == -1)
{
free_searchlist(&sk.sub_search);
ci_write(ci->tx,"* BYE internal dbase error\r\n");
	  return -1;
	  }
*/

		/* allocate memory for result set */
		result_set =
		    (unsigned *) my_malloc(sizeof(unsigned) *
					   ud->mailbox.exists);
		if (!result_set) {
			free_searchlist(&sk.sub_search);
			ci_write(ci->tx,
				"* BYE server ran out of memory\r\n");
			return -1;
		}

		/* init set: select every message, this way the first search key 
		 * will be copied entirely (it is ANDed with this initial set), as it should
		 */

		for (i = 0; i < ud->mailbox.exists; i++)
			result_set[i] = 1;

		/* now perform the search operations */
		result =
		    perform_imap_search((int *)result_set, ud->mailbox.exists,
					&sk, &ud->mailbox,0);

		if (result < 0) {
			free_searchlist(&sk.sub_search);
			my_free(result_set);
			ci_write(ci->tx, "%s", (result == -1) ?
				"* BYE internal dbase error\r\n" :
				"* BYE server ran out of memory\r\n");

			trace(TRACE_ERROR,
			      "ic_search(): fatal error [%d] from perform_imap_search()",
			      result);
			return -1;
		}

		if (result == 1) {
			my_free(result_set);
			result_set = NULL;
		}

	} while (result == 1 && ++i < MAX_RETRIES);

	free_searchlist(&sk.sub_search);

	if (result == 1) {
		ci_write(ci->tx, "* BYE error synchronizing dbase\r\n");
		my_free(result_set);
		return -1;
	}

	/* ok, display results */
	ci_write(ci->tx, "* SEARCH");

	for (i = 0; i < ud->mailbox.exists; i++) {
		if (result_set[i])
			ci_write(ci->tx, " %llu",
				imapcommands_use_uid ? ud->mailbox.
				seq_list[i] : (u64_t) (i + 1));
	}

	ci_write(ci->tx, "\r\n");
	my_free(result_set);

	ci_write(ci->tx, "%s OK SEARCH completed\r\n", tag);
	return 0;
}


/*
 * _ic_fetch()
 *
 * fetch message(s) from the selected mailbox
 */
int _ic_fetch(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t i, fetch_start, fetch_end;
	unsigned fn;
	int result, setseen, idx, j, k;
	int only_main_header_parsing = 1, insert_rfcsize;
	int isfirstout, isfirstfetchout, uid_will_be_fetched;
	int partspeclen, only_text_from_msgpart = 0;
	int bad_response_send = 0;
	u64_t actual_cnt;
	fetch_items_t *fi, fetchitem;
	mime_message_t *msgpart;
	char date[IMAP_INTERNALDATE_LEN], *endptr;
	u64_t thisnum;
	u64_t tmpdumpsize, rfcsize = 0;
	long long cnt;
	int msgflags[IMAP_NFLAGS];
	struct list fetch_list;
	mime_message_t headermsg;	/* for main-header & rfcsize parsing */
	struct element *curr;
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	msginfo_t *msginfo;
	u64_t lo, hi;
	unsigned nmatching;
	int no_parsing_at_all = 1, getrfcsize = 0, getinternaldate =
	    0, getflags = 0;
	char *lastchar = NULL;
	memset(&fetch_list, 0, sizeof(fetch_list));
	memset(&headermsg, 0, sizeof(headermsg));

	if (ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx,
			"%s BAD FETCH command received in invalid state\r\n",
			tag);
		return 1;
	}

	if (!args[0] || !args[1]) {
		ci_write(ci->tx, "%s BAD missing argument(s) to FETCH\r\n",
			tag);
		return 1;
	}

	/* check if the user has the right to fetch messages in this mailbox */
	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to fetch from mailbox\r\n",
			tag);
		return 1;
	}

	/* fetch fetch items */
	list_init(&fetch_list);
	idx = 1;
	uid_will_be_fetched = 0;
	do {
		idx = next_fetch_item(args, idx, &fetchitem);
		if (idx == -2) {
			list_freelist(&fetch_list.start);
			ci_write(ci->tx,
				"%s BAD invalid argument list to fetch\r\n",
				tag);
			return 1;
		}

		if (idx > 0
		    && !list_nodeadd(&fetch_list, &fetchitem,
				     sizeof(fetchitem))) {
			list_freelist(&fetch_list.start);
			ci_write(ci->tx, "* BYE out of memory\r\n");
			return 1;
		}

		if (fetchitem.msgparse_needed) {
			no_parsing_at_all = 0;
		} else if (no_parsing_at_all) {
			if (fetchitem.getFlags)
				getflags = 1;
			if (fetchitem.getSize)
				getrfcsize = 1;
			if (fetchitem.getInternalDate)
				getinternaldate = 1;
			/* the msgUID will be retrieved anyway so if it is requested, it will be fetched */
		}

		if (fetchitem.msgparse_needed && only_main_header_parsing) {
			/* check to see wether all the information can be retrieved from the
			 * main message header (not the entire message has to be parsed then)
			 *
			 * this is the case when:
			 * FETCH RFC822.HEADER
			 *       BODY[HEADER]
			 *       BODY[HEADER.FIELDS ]
			 *       BODY[HEADER.FIELDS.NOT ]
			 *       BODY.PEEK[HEADER]
			 *       BODY.PEEK[HEADER.FIELDS ]
			 *       BODY.PEEK[HEADER.FIELDS.NOT ]
			 *
			 */

			if (!(fetchitem.getRFC822Header ||
			      ((fetchitem.bodyfetch.itemtype == BFIT_HEADER
				|| fetchitem.bodyfetch.itemtype ==
				BFIT_HEADER_FIELDS
				|| fetchitem.bodyfetch.itemtype ==
				BFIT_HEADER_FIELDS_NOT)
			       && fetchitem.bodyfetch.partspec[0] == 0)
			    )
			    ) {
				only_main_header_parsing = 0;
			}
		}

		if (fetchitem.getUID)
			uid_will_be_fetched = 1;

	} while (idx > 0);

	if (!uid_will_be_fetched && imapcommands_use_uid) {
		/* make sure UID will be on the fetch-item list */
		memset(&fetchitem, 0, sizeof(fetchitem));
		fetchitem.getUID = 1;
		fetchitem.bodyfetch.itemtype = -1;

		if (!list_nodeadd
		    (&fetch_list, &fetchitem, sizeof(fetchitem))) {
			list_freelist(&fetch_list.start);
			ci_write(ci->tx, "* BYE out of memory\r\n");
			return -1;
		}
	}

	fetch_list.start = dbmail_list_reverse(fetch_list.start);

	/* now fetch results for each msg */
	endptr = args[0];
	while (*endptr) {
		if (endptr != args[0])
			endptr++;	/* skip delimiter */

		fetch_start = strtoull(endptr, &endptr, 10);

		if (fetch_start == 0 || fetch_start >
		    (imapcommands_use_uid ? (ud->mailbox.msguidnext - 1) :
		     ud->mailbox.exists)) {
			if (imapcommands_use_uid)
				ci_write(ci->tx,
					"%s OK FETCH completed\r\n", tag);
			else
				ci_write(ci->tx,
					"%s BAD invalid message range specified\r\n",
					tag);

			list_freelist(&fetch_list.start);
			return !imapcommands_use_uid;
		}

		switch (*endptr) {
		case ':':
			fetch_end = strtoull(++endptr, &lastchar, 10);
			endptr = lastchar;

			if (*endptr == '*') {
				fetch_end = (imapcommands_use_uid ?
					     (ud->mailbox.msguidnext -
					      1) : ud->mailbox.exists);
				endptr++;
				break;
			}

			if (fetch_end == 0 || fetch_end >
			    (imapcommands_use_uid
			     ? (ud->mailbox.msguidnext -
				1) : ud->mailbox.exists)) {
				if (!imapcommands_use_uid) {
					ci_write(ci->tx,
						"%s BAD invalid message range specified\r\n",
						tag);

					list_freelist(&fetch_list.start);
					return 1;
				}
			}

			if (fetch_end < fetch_start) {
				i = fetch_start;
				fetch_start = fetch_end;
				fetch_end = i;
			}
			break;

		case ',':
		case 0:
			fetch_end = fetch_start;
			break;

		default:
			ci_write(ci->tx,
				"%s BAD invalid character in message range\r\n",
				tag);
			list_freelist(&fetch_list.start);
			return 1;
		}

		if (!imapcommands_use_uid) {
			fetch_start--;
			fetch_end--;
		}

		if (no_parsing_at_all) {
			trace(TRACE_DEBUG,
			      "ic_fetch(): no parsing at all");

			/* all the info we need can be retrieved by a single
			 * call to db_get_msginfo_range()
			 */
			if (!imapcommands_use_uid) {
				/* find the msgUID's to use */
				lo = ud->mailbox.seq_list[fetch_start];
				hi = ud->mailbox.seq_list[fetch_end];

			} else {
				lo = fetch_start;
				hi = fetch_end;
			}

			/* (always retrieve uid) */
			result =
			    db_get_msginfo_range(lo, hi, ud->mailbox.uid,
						 getflags, getinternaldate,
						 getrfcsize, 1, &msginfo,
						 &nmatching);

			if (result == -1) {
				list_freelist(&fetch_list.start);
				ci_write(ci->tx,
					"* BYE internal dbase error\r\n");
				return -1;
			}

			if (result == -2) {
				list_freelist(&fetch_list.start);
				ci_write(ci->tx, "* BYE out of memory\r\n");
				return -1;
			}

			for (i = 0; i < nmatching; i++) {
				if (getrfcsize && msginfo[i].rfcsize == 0) {
					/* parse the message to calc the size */
					result =
					    db_fetch_headers(msginfo[i].
							     uid,
							     &headermsg);
					if (result == -2) {
						ci_write(ci->tx,
							"\r\n* BYE internal dbase error\r\n");
						list_freelist(&fetch_list.
							      start);
						my_free(msginfo);
						return -1;
					}
					if (result == -3) {
						ci_write(ci->tx,
							"\r\n* BYE out of memory\r\n");
						list_freelist(&fetch_list.
							      start);
						my_free(msginfo);
						return -1;
					}

					msginfo[i].rfcsize =
					    (headermsg.rfcheadersize +
					     headermsg.bodysize +
					     headermsg.bodylines);

					db_set_rfcsize(msginfo[i].rfcsize,
						       msginfo[i].uid,
						       ud->mailbox.uid);
					db_free_msg(&headermsg);
				}

				if (binary_search
				    (ud->mailbox.seq_list,
				     ud->mailbox.exists, msginfo[i].uid,
				     &fn) == -1) {
					/* this is probably some sync error:
					 * the msgUID belongs to this mailbox but was not found
					 * when building the mailbox info
					 * let's call it fatal and let the client re-connect :)
					 */
					ci_write(ci->tx,
						"* BYE internal syncing error\r\n");

					list_freelist(&fetch_list.start);
					my_free(msginfo);
					return -1;
				}

				ci_write(ci->tx, "* %u FETCH (", (fn + 1));

				curr = list_getstart(&fetch_list);
				isfirstfetchout = 1;

				while (curr) {
					trace(TRACE_DEBUG,
					      "_ic_fetch(): no parsing, into fetch loop");

					fi = (fetch_items_t *) curr->data;

					if (fi->getInternalDate) {
						if (isfirstfetchout)
							isfirstfetchout =
							    0;
						else
							ci_write(ci->tx,
								" ");

						ci_write(ci->tx,
							"INTERNALDATE \"%s\"",
							date_sql2imap
							(msginfo[i].
							 internaldate));
					}

					if (fi->getUID) {
						if (isfirstfetchout)
							isfirstfetchout =
							    0;
						else
							ci_write(ci->tx,
								" ");

						ci_write(ci->tx, "UID %llu",
							msginfo[i].uid);
					}

					if (fi->getSize) {
						if (isfirstfetchout)
							isfirstfetchout =
							    0;
						else
							ci_write(ci->tx,
								" ");

						ci_write(ci->tx,
							"RFC822.SIZE %llu",
							msginfo[i].
							rfcsize);
					}

					if (fi->getFlags) {
						isfirstout = 1;

						if (isfirstfetchout)
							isfirstfetchout =
							    0;
						else
							ci_write(ci->tx,
								" ");

						ci_write(ci->tx, "FLAGS (");
						for (j = 0;
						     j < IMAP_NFLAGS;
						     j++) {
							if (msginfo[i].
							    flags[j]) {
								fprintf
								    (ci->
								     tx,
								     "%s%s",
								     isfirstout
								     ? "" :
								     " ",
								     imap_flag_desc_escaped
								     [j]);
								if (isfirstout)
									isfirstout
									    =
									    0;
							}
						}
						ci_write(ci->tx, ")");
					}

					curr = curr->nextnode;
				}

				ci_write(ci->tx, ")\r\n");
			}

			my_free(msginfo);
		}

		/* if there is no parsing at all, this loop is not needed */
		for (i = fetch_start; i <= fetch_end && !no_parsing_at_all;
		     i++) {
			thisnum =
			    (imapcommands_use_uid ? i : ud->mailbox.
			     seq_list[i]);
			insert_rfcsize = 0;

			if (imapcommands_use_uid) {
				if (i > ud->mailbox.msguidnext - 1) {
					/* passed the last one */
					ci_write(ci->tx,
						"%s OK FETCH completed\r\n",
						tag);
					list_freelist(&fetch_list.start);
					return 0;
				}

				/* check if the message with this UID belongs to this mailbox */
				if (binary_search
				    (ud->mailbox.seq_list,
				     ud->mailbox.exists, i, &fn) == -1) {
					continue;
				}

				ci_write(ci->tx, "* %u FETCH (", fn + 1);

			} else
				ci_write(ci->tx, "* %llu FETCH (", i + 1);

			trace(TRACE_DEBUG,
			      "Fetching msgID %llu (fetch num %llu)",
			      thisnum, i + 1);

			curr = list_getstart(&fetch_list);
			setseen = 0;
			bad_response_send = 0;

			isfirstfetchout = 1;

			while (curr && !bad_response_send) {
				fi = (fetch_items_t *) curr->data;
				fflush(ci->tx);

				only_text_from_msgpart = 0;

				/* check RFC822.SIZE request */
				if (fi->getSize) {
					/* ok, try to fetch size from dbase */
					if (db_get_rfcsize
					    (thisnum, ud->mailbox.uid,
					     &rfcsize) == -1) {
						ci_write(ci->tx,
							"\r\n* BYE internal dbase error\r\n");
						list_freelist(&fetch_list.
							      start);
						return -1;
					}

					if (rfcsize == 0) {
						/* field is empty in dbase, message needs 
						   to be parsed */
						fi->msgparse_needed = 1;
						only_main_header_parsing =
						    0;
						insert_rfcsize = 1;
					}
				}


				/* update cache */
				if (fi->msgparse_needed
				    && thisnum != cached_msg.num) {
					if (only_main_header_parsing) {
						/* don't update cache if only the main header is needed 
						 * but do retrieve this main header
						 */

						result = db_get_main_header(thisnum, &headermsg.rfcheader);

						if (result == -1) {
							ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
							list_freelist(&fetch_list.start);
							db_free_msg(&headermsg);
							return -1;
						}

						if (result == -2) {
							ci_write(ci->tx, "\r\n* BYE out of memory\r\n");
							list_freelist(&fetch_list.start);
							db_free_msg(&headermsg);
							return -1;
						}

					} else {
						/* parse message structure */
						if (cached_msg.msg_parsed)
							db_free_msg(&cached_msg.msg);

						memset(&cached_msg.msg, 0, sizeof(cached_msg.msg));

						cached_msg.msg_parsed = 0;
						cached_msg.num = -1;
						cached_msg.file_dumped = 0;
						mreset(cached_msg.memdump);

						result = db_fetch_headers(thisnum, &cached_msg.msg);
						if (result == -2) {
							ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
							list_freelist (&fetch_list.  start);
							db_free_msg (&headermsg);
							return -1;
						}
						if (result == -3) {
							ci_write(ci->tx, "\r\n* BYE out of memory\r\n");
							list_freelist (&fetch_list.start);
							db_free_msg (&headermsg);
							return -1;
						}

						cached_msg.msg_parsed = 1;
						cached_msg.num = thisnum;

						rfcsize = (cached_msg.msg.rfcheadersize + cached_msg.msg.bodysize + cached_msg.msg.bodylines);

						if (insert_rfcsize) {
							/* insert the rfc822 size into the dbase */
							if (db_set_rfcsize
							    (rfcsize,
							     thisnum,
							     ud->mailbox.
							     uid) == -1) {
								fprintf
								    (ci->
								     tx,
								     "\r\n* BYE internal dbase error\r\n");
								list_freelist
								    (&fetch_list.
								     start);
								db_free_msg
								    (&headermsg);
								return -1;
							}

							insert_rfcsize = 0;
						}

					}
				}

				if (fi->getInternalDate) {
					result =
					    db_get_msgdate(ud->mailbox.uid,
							   thisnum, date);
					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE internal dbase error\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}

					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx,
						"INTERNALDATE \"%s\"",
						date_sql2imap(date));
				}

				if (fi->getUID) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "UID %llu",
						thisnum);
				}

				if (fi->getMIME_IMB) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "BODYSTRUCTURE ");
					result =
					    retrieve_structure(ci->tx,
							       &cached_msg.
							       msg, 1);
					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE error fetching body structure\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}
				}

				if (fi->getMIME_IMB_noextension) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "BODY ");
					result =
					    retrieve_structure(ci->tx,
							       &cached_msg.
							       msg, 0);
					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE error fetching body\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}
				}

				if (fi->getEnvelope) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "ENVELOPE ");
					result =
					    retrieve_envelope(ci->tx,
							      &cached_msg.
							      msg.
							      rfcheader);

					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE error fetching envelope structure\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}
				}

				if (fi->getRFC822 || fi->getRFC822Peek) {
					if (cached_msg.file_dumped == 0
					    || cached_msg.num != thisnum) {
						mreset(cached_msg.memdump);

						cached_msg.dumpsize =
						    rfcheader_dump
						    (cached_msg.memdump,
						     &cached_msg.msg.rfcheader, args, 0,
						     0);

						cached_msg.dumpsize +=
						    db_dump_range(cached_msg.memdump, 
								    cached_msg.msg.bodystart, 
								    cached_msg.msg.bodyend, 
								    thisnum);

						cached_msg.file_dumped = 1;

						if (cached_msg.num !=
						    thisnum) {
							/* if there is a parsed msg in the cache it will be invalid now */
							if (cached_msg.
							    msg_parsed) {
								cached_msg.
								    msg_parsed
								    = 0;
								db_free_msg
								    (&cached_msg.
								     msg);
							}
							cached_msg.num =
							    thisnum;
						}
					}

					mseek(cached_msg.memdump, 0,
					      SEEK_SET);

					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx,
						"RFC822 {%llu}\r\n",
						cached_msg.dumpsize);
					send_data(ci->tx,
						  cached_msg.memdump,
						  cached_msg.dumpsize);

					if (fi->getRFC822)
						setseen = 1;

				}

				if (fi->getSize) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "RFC822.SIZE %llu",
						rfcsize);
				}

				if (fi->getBodyTotal
				    || fi->getBodyTotalPeek) {
					if (cached_msg.file_dumped == 0
					    || cached_msg.num != thisnum) {
						cached_msg.dumpsize =
						    rfcheader_dump
						    (cached_msg.memdump,
						     &cached_msg.msg.
						     rfcheader, args, 0,
						     0);

						cached_msg.dumpsize +=
						    db_dump_range
						    (cached_msg.memdump,
						     cached_msg.msg.
						     bodystart,
						     cached_msg.msg.
						     bodyend, thisnum);


						if (cached_msg.num !=
						    thisnum) {
							/* if there is a parsed msg in the cache it will be invalid now */
							if (cached_msg.
							    msg_parsed) {
								cached_msg.
								    msg_parsed
								    = 0;
								db_free_msg
								    (&cached_msg.
								     msg);
							}
							cached_msg.num =
							    thisnum;
						}

						cached_msg.file_dumped = 1;
					}

					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					if (fi->bodyfetch.octetstart == -1) {
						mseek(cached_msg.memdump,
						      0, SEEK_SET);

						ci_write(ci->tx,
							"BODY[] {%llu}\r\n",
							cached_msg.
							dumpsize);
						send_data(ci->tx,
							  cached_msg.
							  memdump,
							  cached_msg.
							  dumpsize);
					} else {
						mseek(cached_msg.memdump,
						      fi->bodyfetch.
						      octetstart,
						      SEEK_SET);

		      /** \todo this next statement is ugly because of the
			  casts to 'long long'. Probably, octetcnt should be
			  changed to be a u64_t instead of a long long, because
			  it should never be negative anyway */
						actual_cnt =
						    (fi->bodyfetch.
						     octetcnt >
						     (((long long)
						       cached_msg.
						       dumpsize) -
						      fi->bodyfetch.
						      octetstart))
						    ? (((long long)
							cached_msg.
							dumpsize)
						       -
						       fi->bodyfetch.
						       octetstart) : fi->
						    bodyfetch.octetcnt;

						ci_write(ci->tx,
							"BODY[]<%llu> {%llu}\r\n",
							fi->bodyfetch.
							octetstart,
							actual_cnt);

						send_data(ci->tx,
							  cached_msg.
							  memdump,
							  actual_cnt);

					}

					if (fi->getBodyTotal)
						setseen = 1;

				}

				if (fi->getRFC822Header) {
					/* here: msgparse_needed == 1
					 * if this msg is in cache, retrieve it from there
					 * otherwise only_main_header_parsing == 1 so retrieve direct
					 * from the dbase
					 */
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					if (cached_msg.num == thisnum) {
						mrewind(cached_msg.
							tmpdump);
						tmpdumpsize =
						    rfcheader_dump
						    (cached_msg.tmpdump,
						     &cached_msg.msg.
						     rfcheader, args, 0,
						     0);

						mseek(cached_msg.tmpdump,
						      0, SEEK_SET);

						ci_write(ci->tx,
							"RFC822.HEADER {%llu}\r\n",
							tmpdumpsize);
						send_data(ci->tx,
							  cached_msg.
							  tmpdump,
							  tmpdumpsize);
					} else {
						/* remember only_main_header_parsing == 1 here ! */

						/* use cached_msg.tmpdump as temporary storage */
						mrewind(cached_msg.
							tmpdump);
						tmpdumpsize =
						    rfcheader_dump
						    (cached_msg.tmpdump,
						     &headermsg.rfcheader,
						     args, 0, 0);

						mseek(cached_msg.tmpdump,
						      0, SEEK_SET);

						ci_write(ci->tx,
							"RFC822.HEADER {%llu}\r\n",
							tmpdumpsize);
						send_data(ci->tx,
							  cached_msg.
							  tmpdump,
							  tmpdumpsize);
					}
				}

				if (fi->getRFC822Text) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					mrewind(cached_msg.tmpdump);
					tmpdumpsize =
					    db_dump_range(cached_msg.
							  tmpdump,
							  cached_msg.msg.
							  bodystart,
							  cached_msg.msg.
							  bodyend,
							  thisnum);

					mseek(cached_msg.tmpdump, 0,
					      SEEK_SET);

					ci_write(ci->tx,
						"RFC822.TEXT {%llu}\r\n",
						tmpdumpsize);
					send_data(ci->tx,
						  cached_msg.tmpdump,
						  tmpdumpsize);

					setseen = 1;
				}

				if (fi->bodyfetch.itemtype >= 0) {
					mrewind(cached_msg.tmpdump);

					if (fi->bodyfetch.partspec[0]) {
						if (fi->bodyfetch.
						    partspec[0] == '0') {
							ci_write(ci->tx,
								"\r\n%s BAD protocol error\r\n",
								tag);
							trace(TRACE_DEBUG,
							      "PROTOCOL ERROR");
							list_freelist
							    (&fetch_list.
							     start);
							db_free_msg
							    (&headermsg);
							return 1;
						}

						msgpart =
						    get_part_by_num
						    (&cached_msg.msg,
						     fi->bodyfetch.
						     partspec);

						if (!msgpart) {
							/* if the partspec ends on "1" or "1." the msg body
							 * of the parent message is to be retrieved
							 */

							partspeclen =
							    strlen(fi->
								   bodyfetch.
								   partspec);

							if ((fi->bodyfetch.
							     partspec
							     [partspeclen -
							      1] == '1'
							     &&
							     (partspeclen
							      == 1
							      || fi->
							      bodyfetch.
							      partspec
							      [partspeclen
							       - 2] ==
							      '.'))
							    ||
							    ((fi->
							      bodyfetch.
							      partspec
							      [partspeclen
							       - 1] == '.'
							      && fi->
							      bodyfetch.
							      partspec
							      [partspeclen
							       - 2] == '1')
							     &&
							     (partspeclen
							      == 2
							      || fi->
							      bodyfetch.
							      partspec
							      [partspeclen
							       - 3] ==
							      '.'))
							    ) {
								/* ok find the parent of this message */
								/* start value of k is partspeclen-2 'cause we could
								   have partspec[partspeclen-1] == '.' right at the start
								 */

								for (k =
								     partspeclen
								     - 2;
								     k >=
								     0;
								     k--)
									if (fi->bodyfetch.partspec[k] == '.')
										break;

								if (k > 0) {
									fi->bodyfetch.partspec[k] = '\0';
									msgpart
									    =
									    get_part_by_num
									    (&cached_msg.
									     msg,
									     fi->
									     bodyfetch.
									     partspec);
									fi->bodyfetch.partspec[k] = '.';
								} else
									msgpart
									    =
									    &cached_msg.
									    msg;

								only_text_from_msgpart
								    = 1;
							}
						} else {
							only_text_from_msgpart
							    = 0;
						}
					} else {
						if (cached_msg.num ==
						    thisnum)
							msgpart =
							    &cached_msg.
							    msg;
						else {
							/* this will be only the case when only_main_header_parsing == 1 */
							msgpart =
							    &headermsg;
						}
					}

					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					if (fi->bodyfetch.noseen)
						ci_write(ci->tx, "BODY[%s",
							fi->bodyfetch.
							partspec);
					else {
						ci_write(ci->tx, "BODY[%s",
							fi->bodyfetch.
							partspec);
						setseen = 1;
					}

					switch (fi->bodyfetch.itemtype) {
					case BFIT_TEXT_SILENT:
						if (!msgpart)
							ci_write(ci->tx,
								"] NIL ");
						else {
							tmpdumpsize = 0;

							if (!only_text_from_msgpart)
								tmpdumpsize
								    =
								    rfcheader_dump
								    (cached_msg.
								     tmpdump,
								     &msgpart->
								     rfcheader,
								     args,
								     0, 0);

							tmpdumpsize +=
							    db_dump_range
							    (cached_msg.
							     tmpdump,
							     msgpart->
							     bodystart,
							     msgpart->
							     bodyend,
							     thisnum);

							if (fi->bodyfetch.
							    octetstart >=
							    0) {
								cnt =
								    tmpdumpsize
								    -
								    fi->
								    bodyfetch.
								    octetstart;
								if (cnt <
								    0)
									cnt = 0;
								if (cnt >
								    fi->
								    bodyfetch.
								    octetcnt)
									cnt = fi->bodyfetch.octetcnt;

								fprintf
								    (ci->
								     tx,
								     "]<%llu> {%llu}\r\n",
								     fi->
								     bodyfetch.
								     octetstart,
								     cnt);

								mseek
								    (cached_msg.
								     tmpdump,
								     fi->
								     bodyfetch.
								     octetstart,
								     SEEK_SET);
							} else {
								cnt =
								    tmpdumpsize;
								fprintf
								    (ci->
								     tx,
								     "] {%llu}\r\n",
								     tmpdumpsize);
								mseek
								    (cached_msg.
								     tmpdump,
								     0,
								     SEEK_SET);
							}

							/* output data */
							send_data(ci->tx,
								  cached_msg.
								  tmpdump,
								  cnt);

						}
						break;


					case BFIT_TEXT:
						/* dump body text */
						ci_write(ci->tx, "TEXT");
						if (!msgpart)
							ci_write(ci->tx,
								"] NIL ");
						else {
							tmpdumpsize =
							    db_dump_range
							    (cached_msg.
							     tmpdump,
							     msgpart->
							     bodystart,
							     msgpart->
							     bodyend,
							     thisnum);

							if (fi->bodyfetch.
							    octetstart >=
							    0) {
								cnt =
								    tmpdumpsize
								    -
								    fi->
								    bodyfetch.
								    octetstart;
								if (cnt <
								    0)
									cnt = 0;
								if (cnt >
								    fi->
								    bodyfetch.
								    octetcnt)
									cnt = fi->bodyfetch.octetcnt;

								fprintf
								    (ci->
								     tx,
								     "]<%llu> {%llu}\r\n",
								     fi->
								     bodyfetch.
								     octetstart,
								     cnt);

								mseek
								    (cached_msg.
								     tmpdump,
								     fi->
								     bodyfetch.
								     octetstart,
								     SEEK_SET);
							} else {
								cnt =
								    tmpdumpsize;
								fprintf
								    (ci->
								     tx,
								     "] {%llu}\r\n",
								     tmpdumpsize);
								mseek
								    (cached_msg.
								     tmpdump,
								     0,
								     SEEK_SET);
							}

							/* output data */
							send_data(ci->tx,
								  cached_msg.
								  tmpdump,
								  cnt);
						}
						break;

					case BFIT_HEADER:
						ci_write(ci->tx, "HEADER");
						if (!msgpart
						    ||
						    only_text_from_msgpart)
							ci_write(ci->tx,
								"] NIL\r\n");
						else {
							tmpdumpsize =
							    rfcheader_dump
							    (cached_msg.
							     tmpdump,
							     &msgpart->
							     rfcheader,
							     args, 0, 0);

							if (!tmpdumpsize) {
								fprintf
								    (ci->
								     tx,
								     "] NIL\r\n");
							} else {
								if (fi->
								    bodyfetch.
								    octetstart
								    >= 0) {
									cnt = tmpdumpsize - fi->bodyfetch.octetstart;
									if (cnt < 0)
										cnt = 0;
									if (cnt > fi->bodyfetch.octetcnt)
										cnt = fi->bodyfetch.octetcnt;

									fprintf
									    (ci->
									     tx,
									     "]<%llu> {%llu}\r\n",
									     fi->
									     bodyfetch.
									     octetstart,
									     cnt);

									mseek
									    (cached_msg.
									     tmpdump,
									     fi->
									     bodyfetch.
									     octetstart,
									     SEEK_SET);
								} else {
									cnt = tmpdumpsize;
									fprintf
									    (ci->
									     tx,
									     "] {%llu}\r\n",
									     tmpdumpsize);
									mseek
									    (cached_msg.
									     tmpdump,
									     0,
									     SEEK_SET);
								}

								/* output data */
								send_data
								    (ci->
								     tx,
								     cached_msg.
								     tmpdump,
								     cnt);

							}
						}
						break;

					case BFIT_HEADER_FIELDS:
						ci_write(ci->tx,
							"HEADER.FIELDS (");

						isfirstout = 1;
						for (k = 0;
						     k <
						     fi->bodyfetch.argcnt;
						     k++) {
							if (isfirstout) {
								fprintf
								    (ci->
								     tx,
								     "%s",
								     args[k
									  +
									  fi->
									  bodyfetch.
									  argstart]);
								isfirstout
								    = 0;
							} else
								fprintf
								    (ci->
								     tx,
								     " %s",
								     args[k
									  +
									  fi->
									  bodyfetch.
									  argstart]);
						}

						ci_write(ci->tx, ")] ");

						if (!msgpart
						    ||
						    only_text_from_msgpart)
							ci_write(ci->tx,
								"NIL\r\n");
						else {
							tmpdumpsize =
							    rfcheader_dump
							    (cached_msg.
							     tmpdump,
							     &msgpart->
							     rfcheader,
							     &args[fi->
								   bodyfetch.
								   argstart],
							     fi->bodyfetch.
							     argcnt, 1);

							if (!tmpdumpsize) {
								fprintf
								    (ci->
								     tx,
								     "NIL\r\n");
							} else {
								if (fi->
								    bodyfetch.
								    octetstart
								    >= 0) {
									cnt = tmpdumpsize - fi->bodyfetch.octetstart;
									if (cnt < 0)
										cnt = 0;
									if (cnt > fi->bodyfetch.octetcnt)
										cnt = fi->bodyfetch.octetcnt;

									fprintf
									    (ci->
									     tx,
									     "<%llu> {%llu}\r\n",
									     fi->
									     bodyfetch.
									     octetstart,
									     cnt);

									mseek
									    (cached_msg.
									     tmpdump,
									     fi->
									     bodyfetch.
									     octetstart,
									     SEEK_SET);
								} else {
									cnt = tmpdumpsize;
									fprintf
									    (ci->
									     tx,
									     "{%llu}\r\n",
									     tmpdumpsize);
									mseek
									    (cached_msg.
									     tmpdump,
									     0,
									     SEEK_SET);
								}

								/* output data */
								send_data
								    (ci->
								     tx,
								     cached_msg.
								     tmpdump,
								     cnt);

							}
						}
						break;
					case BFIT_HEADER_FIELDS_NOT:
						ci_write(ci->tx,
							"HEADER.FIELDS.NOT (");

						isfirstout = 1;
						for (k = 0;
						     k <
						     fi->bodyfetch.argcnt;
						     k++) {
							if (isfirstout) {
								fprintf
								    (ci->
								     tx,
								     "%s",
								     args[k
									  +
									  fi->
									  bodyfetch.
									  argstart]);
								isfirstout
								    = 0;
							} else
								fprintf
								    (ci->
								     tx,
								     " %s",
								     args[k
									  +
									  fi->
									  bodyfetch.
									  argstart]);
						}

						ci_write(ci->tx, ")] ");

						if (!msgpart
						    ||
						    only_text_from_msgpart)
							ci_write(ci->tx,
								"NIL\r\n");
						else {
							tmpdumpsize =
							    rfcheader_dump
							    (cached_msg.
							     tmpdump,
							     &msgpart->
							     rfcheader,
							     &args[fi->
								   bodyfetch.
								   argstart],
							     fi->bodyfetch.
							     argcnt, 0);

							if (!tmpdumpsize) {
								fprintf
								    (ci->
								     tx,
								     "NIL\r\n");
							} else {
								if (fi->
								    bodyfetch.
								    octetstart
								    >= 0) {
									cnt = tmpdumpsize - fi->bodyfetch.octetstart;
									if (cnt < 0)
										cnt = 0;
									if (cnt > fi->bodyfetch.octetcnt)
										cnt = fi->bodyfetch.octetcnt;

									fprintf
									    (ci->
									     tx,
									     "<%llu> {%llu}\r\n",
									     fi->
									     bodyfetch.
									     octetstart,
									     cnt);

									mseek
									    (cached_msg.
									     tmpdump,
									     fi->
									     bodyfetch.
									     octetstart,
									     SEEK_SET);
								} else {
									cnt = tmpdumpsize;
									fprintf
									    (ci->
									     tx,
									     "{%llu}\r\n",
									     tmpdumpsize);
									mseek
									    (cached_msg.
									     tmpdump,
									     0,
									     SEEK_SET);
								}

								/* output data */
								send_data
								    (ci->
								     tx,
								     cached_msg.
								     tmpdump,
								     cnt);

							}
						}
						break;
					case BFIT_MIME:
						ci_write(ci->tx, "MIME] ");

						if (!msgpart)
							ci_write(ci->tx,
								"NIL\r\n");
						else {
							tmpdumpsize =
							    mimeheader_dump
							    (cached_msg.
							     tmpdump,
							     &msgpart->
							     mimeheader);

							if (!tmpdumpsize) {
								fprintf
								    (ci->
								     tx,
								     "NIL\r\n");
							} else {
								if (fi->
								    bodyfetch.
								    octetstart
								    >= 0) {
									cnt = tmpdumpsize - fi->bodyfetch.octetstart;
									if (cnt < 0)
										cnt = 0;
									if (cnt > fi->bodyfetch.octetcnt)
										cnt = fi->bodyfetch.octetcnt;

									fprintf
									    (ci->
									     tx,
									     "<%llu> {%llu}\r\n",
									     fi->
									     bodyfetch.
									     octetstart,
									     cnt);

									mseek
									    (cached_msg.
									     tmpdump,
									     fi->
									     bodyfetch.
									     octetstart,
									     SEEK_SET);
								} else {
									cnt = tmpdumpsize;
									fprintf
									    (ci->
									     tx,
									     "{%llu}\r\n",
									     tmpdumpsize);
									mseek
									    (cached_msg.
									     tmpdump,
									     0,
									     SEEK_SET);
								}

								/* output data */
								send_data
								    (ci->
								     tx,
								     cached_msg.
								     tmpdump,
								     cnt);

							}
						}

						break;
					default:
						ci_write(ci->tx,
							"\r\n* BYE internal server error\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}
				}


				/* set \Seen flag if necessary; note the absence of an error-check 
				 * for db_get_msgflag()!
				 */
				if (setseen && db_get_msgflag("seen", thisnum, ud->mailbox.uid) != 1) {
					/* only if the user has an ACL which grants
					   him rights to set the flag should the
					   flag be set! */
					result = acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_SEEN);
					if (result == -1) {
						ci_write(ci->tx, "\r\n *BYE internal dbase error\r\n");
						list_freelist(&fetch_list.start);
						db_free_msg(&headermsg);
						return -1;
					}
					
					if (result == 1 && ud->mailbox.permission == IMAPPERM_READWRITE) {
						result = db_set_msgflag(thisnum, ud->mailbox.uid, setSeenSet, IMAPFA_ADD);
						if (result == -1) {
							ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
							list_freelist(&fetch_list.  start);
							db_free_msg(&headermsg);
							return -1;
						}
					}

					fi->getFlags = 1;
					ci_write(ci->tx, " ");
				}

				/* FLAGS ? */
				if (fi->getFlags) {
					if (isfirstfetchout)
						isfirstfetchout = 0;
					else
						ci_write(ci->tx, " ");

					ci_write(ci->tx, "FLAGS (");

					isfirstout = 1;

					result =
					    db_get_msgflag_all(thisnum,
							       ud->mailbox.
							       uid,
							       msgflags);
					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE internal dbase error\r\n");
						list_freelist(&fetch_list.
							      start);
						db_free_msg(&headermsg);
						return -1;
					}

					for (j = 0; j < IMAP_NFLAGS; j++) {
						if (msgflags[j]) {
							if (isfirstout) {
								fprintf
								    (ci->
								     tx,
								     "\\%s",
								     imap_flag_desc
								     [j]);
								isfirstout
								    = 0;
							} else
								fprintf
								    (ci->
								     tx,
								     " \\%s",
								     imap_flag_desc
								     [j]);
						}
					}
					ci_write(ci->tx, ")");
				}

				curr = curr->nextnode;
			}

			if (!bad_response_send)
				ci_write(ci->tx, ")\r\n");

		}
	}

	mreset(cached_msg.tmpdump);
	list_freelist(&fetch_list.start);
	db_free_msg(&headermsg);

	ci_write(ci->tx, "%s OK %sFETCH completed\r\n", tag,
		imapcommands_use_uid ? "UID " : "");
	return 0;
}


/*
 * _ic_store()
 *
 * alter message-associated data in selected mailbox
 */
int _ic_store(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	char *endptr, *lastchar = NULL;
	u64_t i, store_start, store_end;
	unsigned fn = 0;
	int result, j, isfirstout = 0;
	int be_silent = 0, action = IMAPFA_NONE;
	int flaglist[IMAP_NFLAGS], msgflags[IMAP_NFLAGS];
	u64_t thisnum, lo, hi;

	memset(flaglist, 0, sizeof(int) * IMAP_NFLAGS);

	if (ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx,
			"%s BAD STORE command received in invalid state\r\n",
			tag);
		return 1;
	}

	if (!args[0] || !args[1] || !args[2]) {
		ci_write(ci->tx, "%s BAD missing argument(s) to STORE\r\n",
			tag);
		return 1;
	}

	/* multiple flags should be parenthesed */
	if (args[3] && strcmp(args[2], "(") != 0) {
		ci_write(ci->tx, "%s BAD invalid argument(s) to STORE\r\n",
			tag);
		return 1;
	}


	/* retrieve action type */
	if (strcasecmp(args[1], "flags") == 0)
		action = IMAPFA_REPLACE;
	else if (strcasecmp(args[1], "flags.silent") == 0) {
		action = IMAPFA_REPLACE;
		be_silent = 1;
	} else if (strcasecmp(args[1], "+flags") == 0)
		action = IMAPFA_ADD;
	else if (strcasecmp(args[1], "+flags.silent") == 0) {
		action = IMAPFA_ADD;
		be_silent = 1;
	} else if (strcasecmp(args[1], "-flags") == 0)
		action = IMAPFA_REMOVE;
	else if (strcasecmp(args[1], "-flags.silent") == 0) {
		action = IMAPFA_REMOVE;
		be_silent = 1;
	}

	if (action == IMAPFA_NONE) {
		ci_write(ci->tx,
			"%s BAD invalid STORE action specified\r\n", tag);
		return 1;
	}

	/* now fetch flag list */
	i = (strcmp(args[2], "(") == 0) ? 3 : 2;

	for (; args[i] && strcmp(args[i], ")") != 0; i++) {
		for (j = 0; j < IMAP_NFLAGS; j++)
			if (strcasecmp(args[i], imap_flag_desc_escaped[j])
			    == 0) {
				flaglist[j] = 1;
				break;
			}

		if (j == IMAP_NFLAGS) {
			ci_write(ci->tx,
				"%s BAD invalid flag list to STORE command\r\n",
				tag);
			return 1;
		}
	}

  /** check ACL's for STORE */
	if (flaglist[IMAP_STORE_FLAG_SEEN] == 1) {
		result =
		    acl_has_right(ud->userid, ud->mailbox.uid,
				  ACL_RIGHT_SEEN);
		if (result < 0) {
			ci_write(ci->tx, "* BYE internal database error");
			return -1;	/* fatal */
		}
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO no right to store \\SEEN flag\r\n",
				tag);
			return 1;
		}
	}
	if (flaglist[IMAP_STORE_FLAG_DELETED] == 1) {
		result =
		    acl_has_right(ud->userid, ud->mailbox.uid,
				  ACL_RIGHT_DELETE);
		if (result < 0) {
			ci_write(ci->tx, "* BYE internal database error\r\n");
			return -1;	/* fatal */
		}
		if (result == 0) {
			ci_write(ci->tx,
				"%s NO no right to store \\DELETED flag\r\n",
				tag);
			return 1;
		}
	}
	if (flaglist[IMAP_STORE_FLAG_ANSWERED] == 1 ||
	    flaglist[IMAP_STORE_FLAG_FLAGGED] == 1 ||
	    flaglist[IMAP_STORE_FLAG_DRAFT] == 1 ||
	    flaglist[IMAP_STORE_FLAG_RECENT] == 1) {
		result =
		    acl_has_right(ud->userid, ud->mailbox.uid,
				  ACL_RIGHT_WRITE);
		if (result < 0) {
			ci_write(ci->tx, "*BYE internal database error");
			return -1;
		}
		if (result == 0) {
			ci_write(ci->tx, "%s NO no right to store flags",
				tag);
			return 1;
		}
	}
	/* end of ACL checking. If we get here without returning, the user has
	   the right to store the flags */

	/* set flags & show if needed */
	endptr = args[0];
	while (*endptr) {
		if (endptr != args[0])
			endptr++;	/* skip delimiter */

		store_start = strtoull(endptr, &endptr, 10);

		if (store_start == 0 || store_start >
		    (imapcommands_use_uid ? (ud->mailbox.msguidnext - 1) :
		     ud->mailbox.exists)) {
			ci_write(ci->tx,
				"%s BAD invalid message range specified\r\n",
				tag);
			return 1;
		}

		switch (*endptr) {
		case ':':
			store_end = strtoull(++endptr, &lastchar, 10);
			endptr = lastchar;

			if (*endptr == '*') {
				store_end = (imapcommands_use_uid ?
					     (ud->mailbox.msguidnext -
					      1) : ud->mailbox.exists);
				endptr++;
				break;
			}

			if (store_end == 0 || store_end >
			    (imapcommands_use_uid
			     ? (ud->mailbox.msguidnext -
				1) : ud->mailbox.exists)) {
				ci_write(ci->tx,
					"%s BAD invalid message range specified\r\n",
					tag);
				return 1;
			}

			if (store_end < store_start) {
				i = store_start;
				store_start = store_end;
				store_end = i;
			}
			break;

		case ',':
		case 0:
			store_end = store_start;
			break;

		default:
			ci_write(ci->tx,
				"%s BAD invalid character in message range\r\n",
				tag);
			return 1;
		}

		if (!imapcommands_use_uid) {
			store_start--;
			store_end--;
		}

		if (store_start == store_end) {
			thisnum =
			    (imapcommands_use_uid ? store_start : ud->
			     mailbox.seq_list[store_start]);

			if (imapcommands_use_uid) {
				/* check if the message with this UID belongs to this mailbox */
				if (binary_search
				    (ud->mailbox.seq_list,
				     ud->mailbox.exists, store_start,
				     &fn) == -1)
					continue;
			}
			if (ud->mailbox.permission == IMAPPERM_READWRITE) {
				result = db_set_msgflag(thisnum, ud->mailbox.uid, flaglist, action);
				if (result == -1) {
					ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
					return -1;
				}
			}
			if (!be_silent) {
				result = db_get_msgflag_all(thisnum, ud->mailbox.uid, msgflags);
				if (result == -1) {
					ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
					return -1;
				}

				ci_write(ci->tx, "* %llu FETCH (FLAGS (",
					imapcommands_use_uid ? (u64_t) (fn + 1) : store_start + 1);

				for (j = 0, isfirstout = 1;
				     j < IMAP_NFLAGS; j++) {
					if (msgflags[j]) {
						ci_write(ci->tx, "%s%s",
							isfirstout ? "" :
							" ",
							imap_flag_desc_escaped
							[j]);
						if (isfirstout)
							isfirstout = 0;
					}
				}

				ci_write(ci->tx, "))\r\n");
			}
		} else {
			if (!imapcommands_use_uid) {
				/* find the msgUID's to use */
				lo = ud->mailbox.seq_list[store_start];
				hi = ud->mailbox.seq_list[store_end];

			} else {
				lo = store_start;
				hi = store_end;
			}

			if (ud->mailbox.permission == IMAPPERM_READWRITE) {
				result = db_set_msgflag_range(lo, hi, ud->mailbox.uid, flaglist, action);
				if (result == -1) {
					ci_write(ci->tx, "\r\n* BYE internal dbase error\r\n");
					return -1;
				}
			}
			if (!be_silent) {
				for (i = store_start; i <= store_end; i++) {
					thisnum =
					    (imapcommands_use_uid ? i :
					     ud->mailbox.seq_list[i]);

					if (imapcommands_use_uid) {
						/* check if the message with this UID belongs
						   to this mailbox */
						if (binary_search
						    (ud->mailbox.seq_list,
						     ud->mailbox.exists, i,
						     &fn) == -1)
							continue;
					}

					result =
					    db_get_msgflag_all(thisnum,
							       ud->mailbox.
							       uid,
							       msgflags);
					if (result == -1) {
						ci_write(ci->tx,
							"\r\n* BYE internal dbase error\r\n");
						return -1;
					}

					ci_write(ci->tx,
						"* %llu FETCH (FLAGS (",
						imapcommands_use_uid
						? (u64_t) (fn + 1) : i +
						1);

					for (j = 0, isfirstout = 1;
					     j < IMAP_NFLAGS; j++) {
						if (msgflags[j]) {
							ci_write(ci->tx,
								"%s%s",
								isfirstout
								? "" : " ",
								imap_flag_desc_escaped
								[j]);
							if (isfirstout)
								isfirstout
								    = 0;
						}
					}

					ci_write(ci->tx, "))\r\n");
				}
			}
		}
	}

	ci_write(ci->tx, "%s OK %sSTORE completed\r\n", tag,
		imapcommands_use_uid ? "UID " : "");
	return 0;
}


/*
 * _ic_copy()
 *
 * copy a message to another mailbox
 */
int _ic_copy(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t i, copy_start, copy_end;
	unsigned fn;
	u64_t destmboxid, thisnum;
	int result;
	u64_t new_msgid;
	char *endptr, *lastchar = NULL;

	if (!check_state_and_args
	    ("COPY", tag, args, 2, IMAPCS_SELECTED, ci))
		return 1;	/* error, return */

	/* check if destination mailbox exists */
	if (db_findmailbox(args[1], ud->userid, &destmboxid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;	/* fatal */
	}
	if (destmboxid == 0) {
		/* error: cannot select mailbox */
		ci_write(ci->tx,
			"%s NO [TRYCREATE] specified mailbox does not exist\r\n",
			tag);
		return 1;
	}
	// check if user has right to COPY from source mailbox
	result =
	    acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_READ);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;	/* fatal */
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to copy from mailbox\r\n",
			tag);
		return 1;
	}
	// check if user has right to COPY to destination mailbox
	result = acl_has_right(ud->userid, destmboxid, ACL_RIGHT_INSERT);
	if (result < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;	/* fatal */
	}
	if (result == 0) {
		ci_write(ci->tx,
			"%s NO no permission to copy to mailbox\r\n", tag);
		return 1;
	}

	/* ok copy msgs */
	endptr = args[0];
	while (*endptr) {
		if (endptr != args[0])
			endptr++;	/* skip delimiter */

		copy_start = strtoull(endptr, &lastchar, 10);
		endptr = lastchar;

		if (copy_start == 0 || copy_start >
		    (imapcommands_use_uid ? (ud->mailbox.msguidnext - 1) :
		     ud->mailbox.exists)) {
			ci_write(ci->tx,
				"%s BAD invalid message range specified\r\n",
				tag);
			return 1;
		}

		switch (*endptr) {
		case ':':
			copy_end = strtoull(++endptr, &lastchar, 10);
			endptr = lastchar;

			if (*endptr == '*') {
				copy_end = (imapcommands_use_uid ?
					    (ud->mailbox.msguidnext -
					     1) : ud->mailbox.exists);
				endptr++;
				break;
			}

			if (copy_end == 0 || copy_end >
			    (imapcommands_use_uid
			     ? (ud->mailbox.msguidnext -
				1) : ud->mailbox.exists)) {
				ci_write(ci->tx,
					"%s BAD invalid message range specified\r\n",
					tag);
				return 1;
			}

			if (copy_end < copy_start) {
				i = copy_start;
				copy_start = copy_end;
				copy_end = i;
			}
			break;

		case ',':
		case 0:
			copy_end = copy_start;
			break;

		default:
			ci_write(ci->tx,
				"%s BAD invalid character in message range\r\n",
				tag);
			return 1;
		}

		if (!imapcommands_use_uid) {
			copy_start--;
			copy_end--;
		}

		for (i = copy_start; i <= copy_end; i++) {
			thisnum =
			    (imapcommands_use_uid ? i : ud->mailbox.
			     seq_list[i]);

			if (imapcommands_use_uid) {
				/* check if the message with this UID belongs to this mailbox */
				if (binary_search
				    (ud->mailbox.seq_list,
				     ud->mailbox.exists, i, &fn) == -1)
					continue;
			}

			result =
			    db_copymsg(thisnum, destmboxid, ud->userid,
				       &new_msgid);
			if (result == -1) {
				ci_write(ci->tx,
					"* BYE internal dbase error\r\n");
				return -1;
			}
			if (result == -2) {
				ci_write(ci->tx,
					"%s NO quotum would exceed\r\n",
					tag);
				return 1;
			}
		}
	}

	ci_write(ci->tx, "%s OK %sCOPY completed\r\n", tag,
		imapcommands_use_uid ? "UID " : "");
	return 0;
}


/*
 * _ic_uid()
 *
 * fetch/store/copy/search message UID's
 */
int _ic_uid(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;

	if (ud->state != IMAPCS_SELECTED) {
		ci_write(ci->tx,
			"%s BAD UID command received in invalid state\r\n",
			tag);
		return 1;
	}

	if (!args[0]) {
		ci_write(ci->tx, "%s BAD missing argument(s) to UID\r\n",
			tag);
		return 1;
	}

	imapcommands_use_uid = 1;	/* set global var to make clear we will be using UID's */
	/* ACL rights for UID are handled by the other functions called below */
	if (strcasecmp(args[0], "fetch") == 0)
		result = _ic_fetch(tag, &args[1], ci);
	else if (strcasecmp(args[0], "copy") == 0)
		result = _ic_copy(tag, &args[1], ci);
	else if (strcasecmp(args[0], "store") == 0)
		result = _ic_store(tag, &args[1], ci);
	else if (strcasecmp(args[0], "search") == 0)
		result = _ic_search(tag, &args[1], ci);
	else if (strcasecmp(args[0], "sort") == 0)
		result = _ic_sort(tag, &args[1], ci);
	else {
		ci_write(ci->tx, "%s BAD invalid UID command\r\n", tag);
		result = 1;
	}

	imapcommands_use_uid = 0;

	return result;
}


/* Helper function for _ic_getquotaroot() and _ic_getquota().
 * Send all resource limits in `quota'.
 */
void send_quota(quota_t * quota, ClientInfo * ci)
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
			ci_write(ci->tx,
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
int _ic_getquotaroot(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	quota_t *quota;
	char *root, *errormsg;

	if (!check_state_and_args("GETQUOTAROOT", tag, args, 1,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	root = quota_get_quotaroot(ud->userid, args[0], &errormsg);
	if (root == NULL) {
		ci_write(ci->tx, "%s NO %s\r\n", tag, errormsg);
		return 1;
	}

	quota = quota_get_quota(ud->userid, root, &errormsg);
	if (quota == NULL) {
		ci_write(ci->tx, "%s NO %s\r\n", tag, errormsg);
		return 1;
	}

	ci_write(ci->tx, "* QUOTAROOT \"%s\" \"%s\"\r\n", args[0],
		quota->root);
	send_quota(quota, ci);
	quota_free(quota);

	ci_write(ci->tx, "%s OK GETQUOTAROOT completed\r\n", tag);
	return 0;
}

/*
 * _ic_getquot()
 *
 * get quota
 */
int _ic_getquota(char *tag, char **args, ClientInfo * ci)
{
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	quota_t *quota;
	char *errormsg;

	if (!check_state_and_args("GETQUOTA", tag, args, 1,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;	/* error, return */

	quota = quota_get_quota(ud->userid, args[0], &errormsg);
	if (quota == NULL) {
		ci_write(ci->tx, "%s NO %s\r\n", tag, errormsg);
		return 1;
	}

	send_quota(quota, ci);
	quota_free(quota);

	ci_write(ci->tx, "%s OK GETQUOTA completed\r\n", tag);
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

int _ic_setacl(char *tag, char **args, ClientInfo * ci)
{
	/* SETACL mailboxname identifier mod_rights */
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;
	u64_t mboxid;
	u64_t targetuserid;

	if (!check_state_and_args("SETACL", tag, args, 3,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	result = imap_acl_pre_administer(args[0], args[1], ud->userid,
					 &mboxid, &targetuserid);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		ci_write(ci->tx, "%s NO SETACL failure: can't set acl\r\n",
			tag);
		return 1;
	}
	// has the rights to 'administer' this mailbox? 
	if (acl_has_right(ud->userid, mboxid, ACL_RIGHT_ADMINISTER) != 1) {
		ci_write(ci->tx, "%s NO SETACL failure: can't set acl, "
			"you don't have the proper rights\r\n", tag);
		return 1;
	}
	// set the new acl
	if (acl_set_rights(targetuserid, mboxid, args[2]) < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}

	ci_write(ci->tx, "%s OK SETACL completed\r\n", tag);
	return 0;
}


int _ic_deleteacl(char *tag, char **args, ClientInfo * ci)
{
	// DELETEACL mailboxname identifier
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	u64_t mboxid;
	u64_t targetuserid;

	if (!check_state_and_args("DELETEACL", tag, args, 2,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	if (imap_acl_pre_administer(args[0], args[1], ud->userid,
				    &mboxid, &targetuserid) == -1) {
		ci_write(ci->tx, "* BYE internal dbase error\r\n");
		return -1;
	}
	// has the rights to 'administer' this mailbox? 
	if (acl_has_right(ud->userid, mboxid, ACL_RIGHT_ADMINISTER) != 1) {
		ci_write(ci->tx, "%s NO DELETEACL failure: can't delete "
			"acl\r\n", tag);
		return 1;
	}
	// set the new acl
	if (acl_delete_acl(targetuserid, mboxid) < 0) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}

	ci_write(ci->tx, "%s OK DELETEACL completed\r\n", tag);
	return 0;
}

int _ic_getacl(char *tag, char **args, ClientInfo * ci)
{
	/* GETACL mailboxname */
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;
	u64_t mboxid;
	char *acl_string;

	if (!check_state_and_args("GETACL", tag, args, 1,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	result = db_findmailbox(args[0], ud->userid, &mboxid);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		ci_write(ci->tx, "%s NO GETACL failure: can't get acl\r\n",
			tag);
		return 1;
	}
	// get acl string (string of identifier-rights pairs)
	if (!(acl_string = acl_get_acl(mboxid))) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}

	ci_write(ci->tx, "* ACL \"%s\" %s\r\n", args[0], acl_string);
	my_free(acl_string);
	ci_write(ci->tx, "%s OK GETACL completed\r\n", tag);
	return 0;
}

int _ic_listrights(char *tag, char **args, ClientInfo * ci)
{
	/* LISTRIGHTS mailboxname identifier */
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;
	u64_t mboxid;
	u64_t targetuserid;
	char *listrights_string;


	if (!check_state_and_args("LISTRIGHTS", tag, args, 2,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	result = imap_acl_pre_administer(args[0], args[1], ud->userid,
					 &mboxid, &targetuserid);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		ci_write(ci->tx,
			"%s, NO LISTRIGHTS failure: can't set acl\r\n",
			tag);
		return 1;
	}
	// has the rights to 'administer' this mailbox? 
	if (acl_has_right(ud->userid, mboxid, ACL_RIGHT_ADMINISTER) != 1) {
		ci_write(ci->tx,
			"%s NO LISTRIGHTS failure: can't set acl\r\n",
			tag);
		return 1;
	}
	// set the new acl
	if (!(listrights_string = acl_listrights(targetuserid, mboxid))) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}

	ci_write(ci->tx, "* LISTRIGHTS \"%s\" %s %s\r\n",
		args[0], args[1], listrights_string);
	ci_write(ci->tx, "%s OK LISTRIGHTS completed\r\n", tag);
	my_free(listrights_string);
	return 0;
}

int _ic_myrights(char *tag, char **args, ClientInfo * ci)
{
	/* MYRIGHTS mailboxname */
	imap_userdata_t *ud = (imap_userdata_t *) ci->userData;
	int result;
	u64_t mboxid;
	char *myrights_string;

	if (!check_state_and_args("LISTRIGHTS", tag, args, 1,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	result = db_findmailbox(args[0], ud->userid, &mboxid);
	if (result == -1) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	} else if (result == 0) {
		ci_write(ci->tx,
			"%s NO MYRIGHTS failure: unknown mailbox\r\n",
			tag);
		return 1;
	}

	if (!(myrights_string = acl_myrights(ud->userid, mboxid))) {
		ci_write(ci->tx, "* BYE internal database error\r\n");
		return -1;
	}

	ci_write(ci->tx, "* MYRIGHTS \"%s\" %s\r\n", args[0],
		myrights_string);
	my_free(myrights_string);
	ci_write(ci->tx, "%s OK MYRIGHTS complete\r\n", tag);
	return 0;
}

int _ic_namespace(char *tag, char **args, ClientInfo * ci)
{
	/* NAMESPACE command */
	if (!check_state_and_args("NAMESPACE", tag, args, 0,
				  IMAPCS_AUTHENTICATED, ci))
		return 1;

	ci_write(ci->tx, "* NAMESPACE ((\"\" \"%s\")) ((\"%s\" \"%s\")) "
		"((\"%s\" \"%s\"))\r\n",
		MAILBOX_SEPERATOR, NAMESPACE_USER,
		MAILBOX_SEPERATOR, NAMESPACE_PUBLIC, MAILBOX_SEPERATOR);
	ci_write(ci->tx, "%s OK NAMESPACE complete\r\n", tag);
	return 0;
}
