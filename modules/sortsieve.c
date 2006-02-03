/* $Id: sortsieve.c 1912 2005-11-19 02:29:41Z aaron $

 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

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

 *
 * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * 
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"
#include "dbmail.h"

#include "sort.h"
#include <sieve2.h>

/* Used by us to keep track of libSieve. */
// FIXME: This is a mess right now. Clean it up.
struct my_context {
	char *s_buf;
	char *script;
	const char *mailbox;
	GString *errormsg;
	u64_t user_idnr;
	int error_runtime;
	int error_parse;
	int cancelkeep;
	struct DbmailMessage *message;
};

sieve2_context_t *sieve2_context;
struct my_context *my_context;

/* SIEVE CALLBACKS */

/*
From http://www.ietf.org/internet-drafts/draft-ietf-sieve-vacation-05.txt

   Usage:   vacation [":days" number] [":subject" string]
                     [":from" string] [":addresses" string-list]
                     [":mime"] [":handle" string] <reason: string>

   The parameters that an implementation needs to know about are:
   days, subject, from, mime, handle, reason. Addresses is used
   internally by libSieve implementation to comply with RFCs.

We need to make sure to respect the implementation requirements.
*/
int my_vacation(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	int days = 1, mime = 0;
	const char *message, *subject, *fromaddr, *handle;
	char *md5_handle = NULL, *rc_to, *rc_from, *rc_handle;

	days = sieve2_getvalue_int(s, "days"); // days: min 1, max 30, default 7.
	mime = sieve2_getvalue_int(s, "mime"); // mime: 1 if message is mime coded. FIXME.
	message = sieve2_getvalue_string(s, "message");
	subject = sieve2_getvalue_string(s, "subject");
	fromaddr = sieve2_getvalue_string(s, "fromaddr"); // From: specified by the script.
	handle = sieve2_getvalue_string(s, "handle");

	if (days < 1) days = 1;
	if (days > 30) days = 30;

	if (handle) {
		rc_handle = handle;
	} else {
		GString *tmp = g_string_new("");
		g_string_append(tmp, subject);
		g_string_append(tmp, message);
		rc_handle = md5_handle = makemd5(tmp->str);
		g_string_free(tmp, TRUE);
	}

	if (fromaddr) {
		// FIXME: should be validated as a user might try to forge an address.
		rc_from = fromaddr;
	} else {
		rc_from = "";// FIXME: What's the user's from address!?
	}

	rc_to = dbmail_message_get_envelope(m->message);

	if (db_replycache_validate(rc_to, rc_from, rc_handle, days)) {
		db_replycache_register(rc_to, rc_from, rc_handle);
		send_vacation(rc_to, rc_from, subject, message);
	}

	if (md5_handle)
		dm_free(md5_handle);

	m->cancelkeep = 1;
	return SIEVE2_OK;
}

int my_redirect(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	struct dm_list targets;
	const char *address;

	address = sieve2_getvalue_string(s, "address");

	trace(TRACE_INFO, "Action is REDIRECT: "
		"REDIRECT destination is [%s].", address);

	dm_list_nodeadd(&targets, address, strlen(address+1));

	if (forward(m->message->id, &targets,
			dbmail_message_get_envelope(m->message),
			NULL, 0) != 0) {
		dm_list_free(&targets.start);
		return SIEVE2_ERROR_FAIL;
	}

	dm_list_free(&targets.start);
	m->cancelkeep = 1;
	return SIEVE2_OK;
}

int my_reject(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	trace(TRACE_INFO, "Action is REJECT: "
		"REJECT message is [%s].",
		sieve2_getvalue_string(s, "message"));

//	FIXME: how do we do this?
//	send_bounce(my->messageidnr, message);

	m->cancelkeep = 1;
	return SIEVE2_OK;
}

int my_discard(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	trace(TRACE_INFO, "Action is DISCARD.");

	m->cancelkeep = 1;
	return SIEVE2_OK;
}

// TODO: support the imapflags extension.
int my_fileinto(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	// const char * const * flags;
	const char * mailbox;

	mailbox = sieve2_getvalue_string(s, "mailbox");
	// flags = sieve2_getvalue_stringlist(s, "imapflags");

	trace(TRACE_INFO, "Action is FILEINTO: mailbox is [%s]", mailbox);

	m->mailbox = mailbox;

	m->cancelkeep = 0;
	return SIEVE2_OK;
}

int my_keep(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	trace(TRACE_INFO, "Action is KEEP.");

	m->cancelkeep = 0;
	return SIEVE2_OK;
}

int my_errparse(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char *message;
	int lineno;

	lineno = sieve2_getvalue_int(s, "lineno");
	message = sieve2_getvalue_string(s, "message");

	trace(TRACE_INFO, "Error is PARSE:"
		"Line is [%d], Message is [%s]", lineno, message);

	g_string_append_printf(m->errormsg, "Parse error on line [%d]: %s", lineno, message);

//	FIXME: generate a message and put it into the INBOX
//	of the script's owner, probably with an Urgent flag.

	m->error_parse = 1;
	return SIEVE2_OK;
}

int my_errexec(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char *message;

	message = sieve2_getvalue_string(s, "message");

	trace(TRACE_INFO, "Error is EXEC: "
		"Message is [%s]", message);

	g_string_append_printf(m->errormsg, "Execution error: %s", message);

//	FIXME: generate a message and put it into the INBOX
//	of the script's owner, probably with an Urgent flag.

	m->error_runtime = 1;
	return SIEVE2_OK;
}

int my_getscript(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char * path, * name;
	int res;

	/* Path could be :general, :personal, or empty. */
	path = sieve2_getvalue_string(s, "path");

	/* If no file is named, we're looking for the main file. */
	name = sieve2_getvalue_string(s, "name");

	if (path == NULL || name == NULL)
		return SIEVE2_ERROR_BADARGS;

	if (strlen(path) && strlen(name)) {
		/* TODO: handle included files. */
		trace(TRACE_INFO, "Include requested from [%s] named [%s]",
			path, name);
	} else
	if (!strlen(path) && !strlen(name)) {
		/* Read the script file given as an argument. */
		res = db_get_sievescript_byname(m->user_idnr, m->script, &m->s_buf);
		if (res != SIEVE2_OK) {
			trace(TRACE_ERROR, "my_getscript: read_file() returns %d\n", res);
			return SIEVE2_ERROR_FAIL;
		}
		sieve2_setvalue_string(s, "script", m->s_buf);
	} else {
		return SIEVE2_ERROR_BADARGS;
	}

	return SIEVE2_OK;
}

int my_getheader(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	char *header, *value = "";

	header = sieve2_getvalue_string(s, "header");

	// FIXME: Use GMIME to get a header.

	sieve2_setvalue_string(s, "value", value);

	return SIEVE2_OK;
}

int my_getenvelope(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_string(s, "envelope",
		dbmail_message_get_envelope(m->message));

	return SIEVE2_OK;
}

int my_getbody(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int my_getsize(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_int(s, "size",
		dbmail_message_get_rfcsize(m->message));

	return SIEVE2_OK;
}

/* END OF CALLBACKS */


sieve2_callback_t my_callbacks[] = {
	{ SIEVE2_ERRCALL_RUNTIME,       my_errexec     },
	{ SIEVE2_ERRCALL_PARSE,         my_errparse    },

	{ SIEVE2_ACTION_VACATION,       my_vacation    },
	{ SIEVE2_ACTION_FILEINTO,       my_fileinto    },
	{ SIEVE2_ACTION_REDIRECT,       my_redirect    },
	{ SIEVE2_ACTION_DISCARD,        my_discard     },
	{ SIEVE2_ACTION_REJECT,         my_reject      },
	{ SIEVE2_ACTION_KEEP,           my_keep        },

	{ SIEVE2_SCRIPT_GETSCRIPT,      my_getscript   },
	{ SIEVE2_MESSAGE_GETHEADER,     my_getheader    },
	{ SIEVE2_MESSAGE_GETENVELOPE,   my_getenvelope },
	{ SIEVE2_MESSAGE_GETBODY,       my_getbody     },
	{ SIEVE2_MESSAGE_GETSIZE,       my_getsize     },
	{ 0, 0 } };


/* Return 0 on script OK, 1 on script error, 2 on misc error. */
int sort_validate(u64_t user_idnr, char *scriptname, char **errormsg)
{
	int res, exitcode = 0;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	my_context->script = scriptname;
	my_context->user_idnr = user_idnr;

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_alloc: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_callbacks(sieve2_context, my_callbacks);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_callbacks: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_validate(sieve2_context, my_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_validate: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;
	*errormsg = my_context->errormsg->str;

freesieve:
	if (my_context->s_buf)
		dm_free(my_context->s_buf);

	return exitcode;
}

int sort_connect(void)
{
	int res;

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_alloc: %s\n",
			res, sieve2_errstr(res));
		sort_disconnect();
		return DM_EGENERAL;
	}

	res = sieve2_callbacks(sieve2_context, my_callbacks);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_callbacks: %s\n",
			res, sieve2_errstr(res));
		sort_disconnect();
		return DM_EGENERAL;
	}

	my_context = dm_malloc(sizeof(struct my_context));
	if (!my_context) {
		sort_disconnect();
		return DM_EGENERAL;
	}
	memset(my_context, 0, sizeof(struct my_context));

	my_context->errormsg = g_string_new("");

	return DM_SUCCESS;
}

int sort_disconnect(void)
{
	int res;

	if (my_context)
		dm_free(my_context);

	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		return DM_EGENERAL;
	}

	return DM_SUCCESS;
}

/* Pull up the relevant sieve scripts for this
 * user and begin running them against the header
 * and possibly the body of the message.
 *
 * Returns 0 on success, -1 on failure,
 * and +1 on success but with memory leaking.
 * In the +1 case, if called from a daemon
 * such as dbmail-lmtpd, the daemon should
 * finish storing the message and restart.
 * */
int sort_process(u64_t user_idnr, struct DbmailMessage *message)
{
	int res, exitcode = 0;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	my_context->message = message;
	my_context->user_idnr = user_idnr;

	res = db_get_sievescript_active(user_idnr, &my_context->script);
	if (res != 0) {
		trace(TRACE_ERROR, "Error %d when calling db_getactive_sievescript\n", res);
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_execute(sieve2_context, my_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_execute: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}
	if (!my_context->cancelkeep) {
		trace(TRACE_INFO, "  no actions taken; keeping message.\n");
		my_keep(NULL, my_context);
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;

freesieve:
	if (my_context->s_buf)
		dm_free(my_context->s_buf);

	return exitcode;
}

int sort_get_cancelkeep(void)
{
	return my_context->cancelkeep;
}

const char * sort_get_mailbox(void)
{
	return my_context->mailbox;
}

