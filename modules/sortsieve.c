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

#include "dbmail.h"

/* Used by us to keep track of libSieve. */
struct sort_context {
	char *s_buf;
	char *script;
	u64_t user_idnr;
	struct DbmailMessage *message;
	struct sort_result *result;
	struct dm_list freelist;
};

/* Returned opaquely as type sort_result_t. */
struct sort_result {
	int cancelkeep;
	int reject;
	GString *errormsg;
	const char *mailbox;
	int error_runtime;
	int error_parse;
};

struct sort_sieve_config {
	int vacation;
	int notify;
} sort_sieve_config;


/* SIEVE CALLBACKS */

/*
From http://www.ietf.org/internet-drafts/draft-ietf-sieve-vacation-06.txt

   Usage:   vacation [":days" number] [":subject" string]
                     [":from" string] [":addresses" string-list]
                     [":mime"] [":handle" string] <reason: string>

   The parameters that an implementation needs to know about are:
   days, subject, from, mime, handle, reason. Addresses is used
   internally by libSieve implementation to comply with RFCs.

We need to make sure to respect the implementation requirements.
*/
int sort_vacation(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	int days = 1, mime = 0;
	const char *message, *subject, *fromaddr, *handle;
	const char *rc_to, *rc_from, *rc_handle;
	char *md5_handle = NULL;

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
		char *tmp;
		tmp = g_strconcat(subject, message, NULL);
		rc_handle = md5_handle = dm_md5((const unsigned char * const) tmp);
		g_free(tmp);
	}

	// FIXME: should be validated as a user might try
	// to forge an address from their script.
	rc_from = fromaddr;
	if (!rc_from)
		rc_from = dbmail_message_get_header(m->message, "Delivered-To");

	// Or maybe should it be the Reply-To or From header?
	rc_to = dbmail_message_get_header(m->message, "Reply-To");
	if (!rc_to)
		rc_to = dbmail_message_get_header(m->message, "Return-Path");

	if (db_replycache_validate(rc_to, rc_from, rc_handle, days) == DM_SUCCESS) {
		if (send_vacation(m->message, rc_to, rc_from, subject, message, rc_handle) == 0)
			db_replycache_register(rc_to, rc_from, rc_handle);
		trace(TRACE_INFO, "%s, %s: Sending vacation to [%s] from [%s] handle [%s] repeat days [%d]",
				__FILE__, __func__, rc_to, rc_from, rc_handle, days);
	} else {
		trace(TRACE_INFO, "%s, %s: Vacation suppressed to [%s] from [%s] handle [%s] repeat days [%d]",
				__FILE__, __func__, rc_to, rc_from, rc_handle, days);
	}

	if (md5_handle)
		dm_free(md5_handle);

	m->result->cancelkeep = 0;
	return SIEVE2_OK;
}

int sort_redirect(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *to;
	const char *from;

	to = sieve2_getvalue_string(s, "address");

	trace(TRACE_INFO, "Action is REDIRECT: "
		"REDIRECT destination is [%s].", to);

	from = dbmail_message_get_header(m->message, "Delivered-To");

	if (send_redirect(m->message, to, from) != 0) {
		return SIEVE2_ERROR_FAIL;
	}

	m->result->cancelkeep = 1;
	return SIEVE2_OK;
}

int sort_reject(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;

	trace(TRACE_INFO, "Action is REJECT: "
		"REJECT message is [%s].",
		sieve2_getvalue_string(s, "message"));

	/* FIXME: Pass the rejection message out to smtp/lmtp. */

	/* Reject also discards. */
	m->result->cancelkeep = 1;
	m->result->reject = 1;
	return SIEVE2_OK;
}

int sort_discard(sieve2_context_t *s UNUSED, void *my)
{
	struct sort_context *m = (struct sort_context *)my;

	trace(TRACE_INFO, "Action is DISCARD.");

	m->result->cancelkeep = 1;
	return SIEVE2_OK;
}

// TODO: support the imapflags extension.
int sort_fileinto(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	// const char * const * flags;
	const char * mailbox;

	mailbox = sieve2_getvalue_string(s, "mailbox");
	// flags = sieve2_getvalue_stringlist(s, "imapflags");

	trace(TRACE_INFO, "Action is FILEINTO: mailbox is [%s]", mailbox);

	/* Don't cancel the keep if there's a problem storing the message. */
	if (sort_deliver_to_mailbox(m->message, m->user_idnr,
				mailbox, BOX_SORTING) != DSN_CLASS_OK)
		m->result->cancelkeep = 0;
	else
		m->result->cancelkeep = 1;

	return SIEVE2_OK;
}

int sort_keep(sieve2_context_t *s UNUSED, void *my)
{
	struct sort_context *m = (struct sort_context *)my;

	trace(TRACE_INFO, "Action is KEEP.");

	m->result->cancelkeep = 0;
	return SIEVE2_OK;
}

int sort_errparse(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;
	int lineno;

	lineno = sieve2_getvalue_int(s, "lineno");
	message = sieve2_getvalue_string(s, "message");

	trace(TRACE_INFO, "Error is PARSE:"
		"Line is [%d], Message is [%s]", lineno, message);

	g_string_append_printf(m->result->errormsg, "Parse error on line [%d]: %s", lineno, message);

//	FIXME: generate a message and put it into the INBOX
//	of the script's owner, probably with an Urgent flag.

	m->result->error_parse = 1;
	return SIEVE2_OK;
}

int sort_errexec(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;

	message = sieve2_getvalue_string(s, "message");

	trace(TRACE_INFO, "Error is EXEC: "
		"Message is [%s]", message);

	g_string_append_printf(m->result->errormsg, "Execution error: %s", message);

//	FIXME: generate a message and put it into the INBOX
//	of the script's owner, probably with an Urgent flag.

	m->result->error_runtime = 1;
	return SIEVE2_OK;
}

int sort_getscript(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
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
		trace(TRACE_INFO, "%s, %s: Getting default script named [%s]",
			__FILE__, __func__, m->script);
		res = db_get_sievescript_byname(m->user_idnr, m->script, &m->s_buf);
		if (res != SIEVE2_OK) {
			trace(TRACE_ERROR, "sort_getscript: read_file() returns %d\n", res);
			return SIEVE2_ERROR_FAIL;
		}
		sieve2_setvalue_string(s, "script", m->s_buf);
	} else {
		return SIEVE2_ERROR_BADARGS;
	}

	return SIEVE2_OK;
}

int sort_getheader(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	char *header;
	char **bodylist;
	GTuples *headers;
	unsigned i;

	header = (char *)sieve2_getvalue_string(s, "header");
	
	headers = dbmail_message_get_header_repeated(m->message, header);
	
	bodylist = g_new0(char *,headers->len+1);
	for (i=0; i<headers->len; i++)
		bodylist[i] = (char *)g_tuples_index(headers,i,1);
	g_tuples_destroy(headers);

	/* We have to free the header array, but not its contents. */
	dm_list_nodeadd(&m->freelist, &bodylist, sizeof(char **));

	for (i = 0; bodylist[i] != NULL; i++) {
		trace(TRACE_INFO, "%s, %s: Getting header [%s] returning value [%s]",
			__FILE__, __func__, header, bodylist[i]);
	}

	sieve2_setvalue_stringlist(s, "body", bodylist);

	return SIEVE2_OK;
}

/* Return both the to and from headers. */
int sort_getenvelope(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;

	sieve2_setvalue_string(s, "to",
		m->message->envelope_recipient->str);
	sieve2_setvalue_string(s, "from",
		(char *)dbmail_message_get_header(m->message, "Return-Path"));

	return SIEVE2_OK;
}

int sort_getbody(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int sort_getsize(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	int rfcsize;

	rfcsize = dbmail_message_get_rfcsize(m->message);

	trace(TRACE_INFO, "%s, %s: Getting message size [%d]",
		__FILE__, __func__, rfcsize);

	sieve2_setvalue_int(s, "size", rfcsize);

	return SIEVE2_OK;
}

/* END OF CALLBACKS */


sieve2_callback_t sort_callbacks[] = {
	{ SIEVE2_ERRCALL_RUNTIME,       sort_errexec     },
	{ SIEVE2_ERRCALL_PARSE,         sort_errparse    },

	{ SIEVE2_ACTION_VACATION,       sort_vacation    },
	{ SIEVE2_ACTION_FILEINTO,       sort_fileinto    },
	{ SIEVE2_ACTION_REDIRECT,       sort_redirect    },
	{ SIEVE2_ACTION_DISCARD,        sort_discard     },
	{ SIEVE2_ACTION_REJECT,         sort_reject      },
	{ SIEVE2_ACTION_KEEP,           sort_keep        },

	{ SIEVE2_SCRIPT_GETSCRIPT,      sort_getscript   },
	{ SIEVE2_MESSAGE_GETHEADER,     sort_getheader   },
	{ SIEVE2_MESSAGE_GETENVELOPE,   sort_getenvelope },
	{ SIEVE2_MESSAGE_GETBODY,       sort_getbody     },
	{ SIEVE2_MESSAGE_GETSIZE,       sort_getsize     },
	{ 0, 0 } };


static int sort_teardown(sieve2_context_t **s2c,
		struct sort_context **sc)
{
	assert(s2c != NULL);
	assert(sc != NULL);

	sieve2_context_t *sieve2_context = *s2c;
	struct sort_context *sort_context = *sc;
	int res;

	dm_list_free(&sort_context->freelist.start);

	if (sort_context) {
		dm_free(sort_context);
	}

	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		return DM_EGENERAL;
	}

	*s2c = NULL;
	*sc = NULL;

	return DM_SUCCESS;
}

static int sort_startup(sieve2_context_t **s2c,
		struct sort_context **sc)
{
	assert(s2c != NULL);
	assert(sc != NULL);

	sieve2_context_t *sieve2_context = NULL;
	struct sort_context *sort_context = NULL;
	int res;

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_alloc: %s\n",
			res, sieve2_errstr(res));
		return DM_EGENERAL;
	}

	res = sieve2_callbacks(sieve2_context, sort_callbacks);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_callbacks: %s\n",
			res, sieve2_errstr(res));
		sort_teardown(&sieve2_context, &sort_context);
		return DM_EGENERAL;
	}

	sort_context = dm_malloc(sizeof(struct sort_context));
	if (!sort_context) {
		sort_teardown(&sieve2_context, &sort_context);
		return DM_EGENERAL;
	}
	memset(sort_context, 0, sizeof(struct sort_context));

	dm_list_init(&sort_context->freelist);

	*s2c = sieve2_context;
	*sc = sort_context;

	return DM_SUCCESS;
}

/* The caller is responsible for freeing memory here. */
// FIXME: Support SIEVE_{extension} settings in dbmail.conf
const char * sort_listextensions(void)
{
	sieve2_context_t *sieve2_context;
	const char * extensions;

	if (sieve2_alloc(&sieve2_context) != SIEVE2_OK) 
		return NULL;

	if (sieve2_callbacks(sieve2_context, sort_callbacks))
			return NULL;

	/* This will be freed by sieve2_free. */
	extensions = sieve2_listextensions(sieve2_context);

	/* So we'll make our own copy. */
	if (extensions)
		extensions = dm_strdup(extensions);

	/* If this fails, then we don't care about the
	 * memory leak, because the program has to bomb out.
	 * It will not be possible to start libSieve up again
	 * if it does not free properly. */
	if (sieve2_free(&sieve2_context) != SIEVE2_OK)
		return NULL;

	return extensions;
}

/* Return 0 on script OK, 1 on script error, 2 on misc error. */
sort_result_t *sort_validate(u64_t user_idnr, char *scriptname)
{
	int res, exitnull = 0;
	struct sort_result *result = NULL;
	sieve2_context_t *sieve2_context;
	struct sort_context *sort_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	if (sort_startup(&sieve2_context, &sort_context) != DM_SUCCESS) {
		return NULL;
	}

	sort_context->script = scriptname;
	sort_context->user_idnr = user_idnr;
	sort_context->result = g_new0(struct sort_result, 1);
	if (! sort_context->result) {
		return NULL;
	}
	sort_context->result->errormsg = g_string_new("");

	res = sieve2_validate(sieve2_context, sort_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_validate: %s\n",
			res, sieve2_errstr(res));
		exitnull = 1;
		goto freesieve;
	}

	/* At this point the callbacks are called from within libSieve. */

freesieve:
	if (sort_context->s_buf)
		dm_free(sort_context->s_buf);

	if (exitnull)
		result = NULL;
	else
		result = sort_context->result;

	sort_teardown(&sieve2_context, &sort_context);

	return result;
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
sort_result_t *sort_process(u64_t user_idnr, struct DbmailMessage *message)
{
	int res, exitnull = 0;
	struct sort_result *result = NULL;
	sieve2_context_t *sieve2_context;
	struct sort_context *sort_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	if (sort_startup(&sieve2_context, &sort_context) != DM_SUCCESS) {
		return NULL;
	}

	sort_context->message = message;
	sort_context->user_idnr = user_idnr;
	sort_context->result = g_new0(struct sort_result, 1);
	if (! sort_context->result) {
		exitnull = 1;
		goto freesieve;
	}
	sort_context->result->errormsg = g_string_new("");

	res = db_get_sievescript_active(user_idnr, &sort_context->script);
	if (res != 0) {
		trace(TRACE_ERROR, "Error %d when calling db_getactive_sievescript", res);
		exitnull = 1;
		goto freesieve;
	}
	if (sort_context->script == NULL) {
		trace(TRACE_INFO, "User doesn't have any active sieve scripts.");
		exitnull = 1;
		goto freesieve;
	}

	res = sieve2_execute(sieve2_context, sort_context);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "Error %d when calling sieve2_execute: %s",
			res, sieve2_errstr(res));
		exitnull = 1;
	}
	if (! sort_context->result->cancelkeep) {
		trace(TRACE_INFO, "  no actions taken; keeping message.");
		sort_keep(NULL, sort_context);
	}

	/* At this point the callbacks are called from within libSieve. */

freesieve:
	if (sort_context->s_buf)
		dm_free(sort_context->s_buf);
	if (sort_context->script)
		dm_free(sort_context->script);

	if (exitnull)
		result = NULL;
	else
		result = sort_context->result;

	sort_teardown(&sieve2_context, &sort_context);

	return result;
}

/* SORT RESULT INTERFACE */

void sort_free_result(sort_result_t *result)
{
	if (result == NULL) return;
	g_string_free(result->errormsg, TRUE);
	dm_free(result);
}

int sort_get_cancelkeep(sort_result_t *result)
{
	if (result == NULL) return 0;
	return result->cancelkeep;
}

int sort_get_reject(sort_result_t *result)
{
	if (result == NULL) return 0;
	return result->reject;
}

const char * sort_get_mailbox(sort_result_t *result)
{
	assert(result != NULL);
	return result->mailbox;
}

const char * sort_get_errormsg(sort_result_t *result)
{
	assert(result != NULL);
	return result->errormsg->str;
}

int sort_get_error(sort_result_t *result)
{
	assert(result != NULL);
	return result->errormsg->len;
}

