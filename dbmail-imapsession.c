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
 * imaputil.c
 *
 * IMAP-server utility functions implementations
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "dbmail.h"
#include "imaputil.h"
#include "imap4.h"
#include "debug.h"
#include "db.h"
#include "memblock.h"
#include "dbsearch.h"
#include "dbmsgbuf.h"
#include "rfcmsg.h"
#include "dbmail-imapsession.h"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE (10*1024)
#endif

#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

/* cache */
extern cache_t cached_msg;

extern const char AcceptedChars[];
extern const char AcceptedTagChars[];
extern const char AcceptedMailboxnameChars[];
extern const char *month_desc[];
extern char base64encodestring[];
/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "03-Nov-1979 00:00:00 +0000"
extern char _imapdate[IMAP_INTERNALDATE_LEN];

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"
extern char _sqldate[SQL_INTERNALDATE_LEN + 1];
extern const int month_len[];
extern const char *item_desc[];
extern const char *envelope_items[];
extern const char *imap_flag_desc[IMAP_NFLAGS];
extern const char *imap_flag_desc_escaped[IMAP_NFLAGS];

static int dbmail_imap_fetch_parse_partspec(struct ImapSession *self, int idx);
static int dbmail_imap_fetch_parse_octet_range(struct ImapSession *self, int idx);




static int dbmail_imap_fetch_parse_partspec(struct ImapSession *self, int idx)
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
		strncpy(self->fi.bodyfetch.partspec, token, j);
	}
	self->fi.bodyfetch.partspec[j] = '\0';

	char *partspec = &token[j];

	int shouldclose = 0;
	if (MATCH(partspec, "text")) {
		self->fi.bodyfetch.itemtype = BFIT_TEXT;
		shouldclose = 1;
	} else if (MATCH(partspec, "header")) {
		self->fi.bodyfetch.itemtype = BFIT_HEADER;
		shouldclose = 1;
	} else if (MATCH(partspec, "mime")) {
		if (j == 0)
			return -2;	/* error DONE */

		self->fi.bodyfetch.itemtype = BFIT_MIME;
		shouldclose = 1;
	} else if (MATCH(partspec, "header.fields")) {
		self->fi.bodyfetch.itemtype = BFIT_HEADER_FIELDS;
	} else if (MATCH(partspec, "header.fields.not")) {
		self->fi.bodyfetch.itemtype = BFIT_HEADER_FIELDS_NOT;
	} else if (token[j] == '\0') {
		self->fi.bodyfetch.itemtype = BFIT_TEXT_SILENT;
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
		self->fi.bodyfetch.argstart = idx;

		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(self->args[idx],")") )
			idx++;

		token = self->args[idx];
		nexttoken = self->args[idx+1];
		
		self->fi.bodyfetch.argcnt = idx - self->fi.bodyfetch.argstart;

		if (self->fi.bodyfetch.argcnt == 0 || ! MATCH(nexttoken,"]") )
			return -2;	/* error DONE */
	}
	return idx + 1;
}

static int dbmail_imap_fetch_parse_octet_range(struct ImapSession *self, int idx) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[idx];
	
	if (token && token[0] == '<') {

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
		self->fi.bodyfetch.octetstart = strtoll(&token[1], NULL, 10);
		self->fi.bodyfetch.octetcnt = strtoll(&token [delimpos + 1], NULL, 10);

		/* restore argument */
		token[delimpos] = '.';
		token[strlen(token) - 1] = '>';
	} else {
		self->fi.bodyfetch.octetstart = -1;
		self->fi.bodyfetch.octetcnt = -1;
	}

	return idx + 1;	/* DONE */
}

/*
 * dbmail_imap_fetch_parse_args()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int dbmail_imap_fetch_parse_args(struct ImapSession * self, int idx)
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
		self->fi.getFlags = 1;
	} else if (MATCH(token,"internaldate")) {
		self->fi.getInternalDate=1;
	} else if (MATCH(token,"uid")) {
		self->fi.getUID=1;
	} else if (MATCH(token,"rfc822")) {
		self->fi.getRFC822=1;
	
	/* from here on message parsing will be necessary */
	
	} else if (MATCH(token,"rfc822.header")) {
		self->fi.msgparse_needed=1;
		self->fi.getRFC822Header = 1;
	} else if (MATCH(token,"rfc822.peek")) {
		self->fi.msgparse_needed=1;
		self->fi.getRFC822Peek = 1;
	} else if (MATCH(token,"rfc822.size")) {
		self->fi.msgparse_needed=1;
		self->fi.getSize = 1;
	} else if (MATCH(token,"rfc822.text")) {
		self->fi.msgparse_needed=1;
		self->fi.getRFC822Text = 1;
	
	} else if (MATCH(token,"body") || MATCH(token,"body.peek")) {
		self->fi.msgparse_needed=1;
		if (MATCH(token,"body.peek"))
			ispeek=1;
		
		nexttoken = (char *)self->args[idx+1];
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek)
				return -2;	/* error DONE */
			self->fi.getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			/* now read the argument list to body */
			idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			idx++;	/* now pointing at what should be the item type */

			token = (char *)self->args[idx];
			nexttoken = (char *)self->args[idx+1];

			if (MATCH(token,"]")) {
				if (ispeek)
					self->fi.getBodyTotalPeek = 1;
				else
					self->fi.getBodyTotal = 1;
				return dbmail_imap_fetch_parse_octet_range(self,idx);
			}
			
			if (ispeek)
				self->fi.bodyfetch.noseen = 1;

			
			idx = dbmail_imap_fetch_parse_partspec(self,idx);
			if (idx < 0)
				return -2;
			idx++;	/* points to ']' now */
			return dbmail_imap_fetch_parse_octet_range(self,idx);
		}
	} else if (MATCH(token,"all")) {		
		self->fi.msgparse_needed=1;
		self->fi.getFlags = 1;
		self->fi.getInternalDate = 1;
		self->fi.getSize = 1;
		self->fi.getEnvelope = 1;
	} else if (MATCH(token,"fast")) {
		self->fi.getFlags = 1;
		self->fi.getInternalDate = 1;
		self->fi.getSize = 1;
	} else if (MATCH(token,"full")) {
		self->fi.msgparse_needed=1;
		self->fi.getFlags = 1;
		self->fi.getInternalDate = 1;
		self->fi.getSize = 1;
		self->fi.getEnvelope = 1;
		self->fi.getMIME_IMB = 1;
	} else if (MATCH(token,"bodystructure")) {
		self->fi.msgparse_needed=1;
		self->fi.getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		self->fi.msgparse_needed=1;
		self->fi.getEnvelope = 1;
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
			if (!
			    (state == IMAPCS_AUTHENTICATED
			     && ud->state == IMAPCS_SELECTED)) {
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


struct ImapSession * dbmail_imap_session_new(void)
{
	struct ImapSession * self;
	fetch_items_t fi;
	msginfo_t * msginfo;
	
	self = (struct ImapSession *)my_malloc(sizeof(struct ImapSession));
	if (! self)
		trace(TRACE_ERROR,"%s,%s: OOM error", __FILE__, __func__);
	
	self->use_uid = 0;
	self->msg_idnr = 0;

	self->ci = (ClientInfo *)my_malloc(sizeof(ClientInfo));
	self->tag = (char *)my_malloc(sizeof(char));
	self->command = (char *)my_malloc(sizeof(char));
	self->args = (char **)my_malloc(sizeof(char **));
	
	if (! (self->ci && self->tag && self->command && self->args))
		trace(TRACE_ERROR,"%s,%s: OOM error", __FILE__, __func__);
	
	memset(&fi,0,sizeof(fetch_items_t));
	dbmail_imap_session_setFi(self,fi);
   
	msginfo = (msginfo_t *)my_malloc(sizeof(msginfo_t));
	memset(msginfo,0,sizeof(msginfo));
	dbmail_imap_session_setMsginfo(self,msginfo);
	
	return self;
}

struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self)
{
	self->fi.msgparse_needed = 0;	/* by default no body parsing required */	
	self->fi.hdrparse_needed = 1;	/* by default header parsing is required */
	self->fi.bodyfetch.itemtype = -1;	/* expect no body fetches (a priori) */
	self->fi.getBodyTotal = 0;
	self->fi.getBodyTotalPeek = 0;
	self->fi.getInternalDate = 0;
	self->fi.getFlags = 0;
	self->fi.getUID = 0;
	self->fi.getMIME_IMB = 0;
	self->fi.getEnvelope = 0;
	self->fi.getSize = 0;
	self->fi.getMIME_IMB_noextension = 0;
	self->fi.getRFC822Header = 0;
	self->fi.getRFC822Text = 0;
	self->fi.getRFC822 = 0;
	self->fi.getRFC822Peek = 0;
	return self;
}
     
struct ImapSession * dbmail_imap_session_setClientInfo(struct ImapSession * self, ClientInfo *ci)
{
	self->ci = ci;
	return self;
}
struct ImapSession * dbmail_imap_session_setTag(struct ImapSession * self, char * tag)
{
	GString *s = g_string_new(tag);
	self->tag = s->str;
	g_string_free(s,FALSE);
	return self;
}
struct ImapSession * dbmail_imap_session_setCommand(struct ImapSession * self, char * command)
{
	GString *s = g_string_new(command);
	self->command = s->str;
	g_string_free(s,FALSE);
	return self;
}
struct ImapSession * dbmail_imap_session_setArgs(struct ImapSession * self, char ** args)
{
	self->args = args;
	return self;
}
struct ImapSession * dbmail_imap_session_setFi(struct ImapSession * self, fetch_items_t fi)
{
	self->fi = fi;
	return self;
}
struct ImapSession * dbmail_imap_session_setMsginfo(struct ImapSession * self, msginfo_t * msginfo)
{
	self->msginfo = msginfo;
	return self;
}



void dbmail_imap_session_delete(struct ImapSession * self)
{
	my_free(self);
}

int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...)
{
	va_list ap;
	va_start(ap, message);
	FILE * fd = self->ci->tx;
	int len;	
	if (feof(fd) || (len = vfprintf(fd,message,ap)) < 0 || fflush(fd) < 0) {
		va_end(ap);
		return -1;
	}
	va_end(ap);
	return len;
}
	
int dbmail_imap_session_readln(struct ImapSession * self, char * buffer)
{
	memset(buffer, 0, MAX_LINESIZE);
	alarm(self->ci->timeout);
	if (fgets(buffer, MAX_LINESIZE, self->ci->rx) == NULL) {
		trace(TRACE_ERROR, "%s,%s: error reading from client", __FILE__, __func__);
		dbmail_imap_session_printf(self, "* BYE Error reading input\r\n");
		return -1;
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
	trace(TRACE_DEBUG, "_ic_login(): trying to validate user");
	int valid = auth_validate(username, password, &userid);
	trace(TRACE_MESSAGE, "_ic_login(): user (id:%llu, name %s) tries login",
			userid, username);

	if (valid == -1) {
		/* a db-error occurred */
		dbmail_imap_session_printf(self, "* BYE internal db error validating user\r\n");
		trace(TRACE_ERROR,
		      "_ic_login(): db-validate error while validating user %s (pass %s).",
		      username, password);
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
	trace(TRACE_MESSAGE,
	      "_ic_login(): user (id %llu, name %s) login accepted @ %s",
	      userid, username, timestring);
#ifdef PROC_TITLES
	set_proc_title("USER %s [%s]", username, ci->ip);
#endif

	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	return 0;

}


int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value )
{
	char *buf;
	GString *tmp;
	tmp = g_string_new(prompt);
	
	if (! (  buf = (char *)my_malloc(sizeof(char) * MAX_LINESIZE ))) {
		trace(TRACE_ERROR, "%s,%s: malloc failure", __FILE__, __func__);
		return -1;
	}
	
	tmp = g_string_append(tmp, "\r\n");
	base64encode(tmp->str, buf);

	dbmail_imap_session_printf(self, "+ %s\r\n", buf);
	fflush(self->ci->tx);
	
	if ( (dbmail_imap_session_readln(self, buf) < 0) )
		return -1;

	tmp = g_string_new(buf);
	memset(buf,0,sizeof(buf));

	base64decode(tmp->str, buf);
	
	value = strdup(buf);

	g_string_free(tmp,1);
	my_free(buf);
	
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
	access = acl_has_right(ud->userid, idnr, acl);
	if (access < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (access == 0) {
		dbmail_imap_session_printf(self, "%s NO no permission to select mailbox\r\n", self->tag);
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
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
	dbmail_imap_session_printf(self, "* OK [PERMANENTFLAGS (%s)]\r\n", string->str);

	/* UID */
	dbmail_imap_session_printf(self, "* OK [UIDVALIDITY %llu] UID value\r\n",
		ud->mailbox.uid);

	return 0;
}
	
int dbmail_imap_session_mailbox_open(struct ImapSession * self, char * mailbox)
{
	int result;
	u64_t mailbox_idnr;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	
	/* get the mailbox_idnr */
	if (! (mailbox_idnr = dbmail_imap_session_mailbox_get_idnr(self, mailbox))) {
		ud->state = IMAPCS_AUTHENTICATED;
		my_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
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

