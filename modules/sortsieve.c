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
	char *header;
	const char *mailbox;
	char *errormsg;
	const char *fromaddr;
	u64_t msg_idnr;
	u64_t user_idnr;
	u64_t headersize;
	u64_t messagesize;
	int error_runtime;
	int error_parse;
	int actiontaken;
	struct DbmailMessage *message;
};

/* SIEVE CALLBACKS */

int my_vacation(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	time_t last_time= 0;
	time_t current_time = 0;
	int interval = 0;
	const char *message, *subject, *address, *name;

	/* TODO: We need a table for Vacation data... */

	interval = sieve2_getvalue_int(s, "interval");
	message = sieve2_getvalue_string(s, "message"),
	subject = sieve2_getvalue_string(s, "subject"),
	address = sieve2_getvalue_string(s, "address"),
	name = sieve2_getvalue_string(s, "name");

	// db_vacation_getlast(my->useridnr, name, &timestamp);

	if (current_time > last_time + interval) {
		send_vacation(address, m->fromaddr, subject, message);
	}

	// db_vacation_setlast(my->useridnr, name, timestamp);

	m->actiontaken = 1;
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

	if (forward(m->msg_idnr, &targets, m->fromaddr, NULL, 0) != 0) {
		dm_list_free(&targets.start);
		return SIEVE2_ERROR_FAIL;
	}

	dm_list_free(&targets.start);
	m->actiontaken = 1;
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

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_discard(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	trace(TRACE_INFO, "Action is DISCARD.");

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_fileinto(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char * const * flags;
	const char * mailbox;

	mailbox = sieve2_getvalue_string(s, "mailbox");
	flags = sieve2_getvalue_stringlist(s, "imapflags");

	trace(TRACE_INFO, "Action is FILEINTO: mailbox is [%s]", mailbox);

	m->mailbox = mailbox;
//	FIXME: how do we do this?
/*	m->flags = 

	if (flags) {
		for (i = 0; flags[i]; i++)
			printf( " %s", flags[i]);
	}
*/
//	We just don't care. Insert with default flags.

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_keep(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	trace(TRACE_INFO, "Action is KEEP.");

//	FIXME: differentiate this from DISCARD, duh.

	m->actiontaken = 1;
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

// TODO: Use GMime to hand pre-parsed headers to libSieve
// on an as-needed basis using the my_getheader callback.
int my_getheaders(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_string(s, "allheaders", m->header);

	return SIEVE2_OK;
}

int my_getenvelope(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_string(s, "envelope", m->fromaddr);

	return SIEVE2_OK;
}

int my_getbody(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int my_getsize(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_int(s, "size", m->messagesize);

	return SIEVE2_OK;
}

/* END OF CALLBACKS */


sieve2_callback_t my_callbacks[] = {
	{ SIEVE2_ERRCALL_RUNTIME,       my_errexec     },
	{ SIEVE2_ERRCALL_PARSE,         my_errparse    },
	{ SIEVE2_ACTION_FILEINTO,       my_fileinto    },
	{ SIEVE2_ACTION_REDIRECT,       my_redirect    },
	{ SIEVE2_ACTION_REJECT,         my_reject      },
	{ SIEVE2_ACTION_VACATION,       my_vacation    },
	{ SIEVE2_ACTION_KEEP,           my_keep        },
	{ SIEVE2_SCRIPT_GETSCRIPT,      my_getscript   },
// TODO: Use GMime to hand pre-parsed headers to libSieve
// on an as-needed basis using the my_getheader callback.
	{ SIEVE2_MESSAGE_GETHEADER,     NULL            },
	{ SIEVE2_MESSAGE_GETALLHEADERS, my_getheaders  },
	{ SIEVE2_MESSAGE_GETENVELOPE,   my_getenvelope },
	{ SIEVE2_MESSAGE_GETBODY,       my_getbody     },
	{ SIEVE2_MESSAGE_GETSIZE,       my_getsize     },
	{ 0, 0 } };


/* Return 0 on script OK, 1 on script error, 2 on misc error. */
int sort_validate(u64_t user_idnr, char *scriptname, char **errormsg)
{
	int res, exitcode = 0;
	struct my_context *my_context;
	sieve2_context_t *sieve2_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	/* This is the locally-defined structure that will be
	 * passed as the user context into the sieve calls.
	 * It will be passed by libSieve into each callback.*/
	my_context = malloc(sizeof(struct my_context));
	if (!my_context) {
		exitcode = 2;
		goto endnofree;
	}
	memset(my_context, 0, sizeof(struct my_context));

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
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;
	*errormsg = my_context->errormsg;

freesieve:
	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}

	if (my_context->s_buf) free(my_context->s_buf);

	if (my_context) free(my_context);

endnofree:
	return exitcode;
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
int sort_process(u64_t user_idnr, struct DbmailMessage *message,
		const char *fromaddr)
{
	int res, exitcode = 0;
	struct my_context *my_context;
	sieve2_context_t *sieve2_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	/* This is the locally-defined structure that will be
	 * passed as the user context into the sieve calls.
	 * It will be passed by libSieve into each callback.*/
	my_context = malloc(sizeof(struct my_context));
	if (!my_context) {
		exitcode = 2;
		goto endnofree;
	}
	memset(my_context, 0, sizeof(struct my_context));

	my_context->user_idnr = user_idnr;
	my_context->headersize = dbmail_message_get_hdrs_size(message, FALSE);
	my_context->messagesize = dbmail_message_get_size(message, FALSE);
	my_context->fromaddr = fromaddr;

	res = db_get_sievescript_active(user_idnr, &my_context->script);
	if (res != 0) {
		trace(TRACE_ERROR, "Error %d when calling db_getactive_sievescript\n", res);
		exitcode = 1;
		goto freesieve;
	}

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

	res = sieve2_execute(sieve2_context, my_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_execute: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}
	if (!my_context->actiontaken) {
		trace(TRACE_INFO, "  no actions taken; keeping message.\n");
		my_keep(NULL, my_context);
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;

freesieve:
	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}

	if (my_context->s_buf) free(my_context->s_buf);

	if (my_context) free(my_context);

endnofree:
	return exitcode;
}

