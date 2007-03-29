/* 

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

#define THIS_MODULE "sort"

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
	const char *mailbox;
	int reject;
	GString *rejectmsg;
	int error_runtime;
	int error_parse;
	GString *errormsg;
};

/* [DELIVERY] SIEVE_* settings in dbmail.conf */
struct sort_sieve_config {
	int vacation;
	int notify;
	int debug;
};

static void sort_sieve_get_config(struct sort_sieve_config *sieve_config)
{
	field_t val;

	assert(sieve_config != NULL);

	sieve_config->vacation = 0;
	sieve_config->notify = 0;
	sieve_config->debug = 0;

	config_get_value("SIEVE_VACATION", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		sieve_config->vacation = 1;
	}

	config_get_value("SIEVE_NOTIFY", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		sieve_config->notify= 1;
	}

	config_get_value("SIEVE_DEBUG", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		sieve_config->debug = 1;
	}
}


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
	if (!rc_from)
		rc_from = m->message->envelope_recipient->str;

	rc_to = dbmail_message_get_header(m->message, "Reply-To");
	if (!rc_to)
		rc_to = dbmail_message_get_header(m->message, "Return-Path");

	if (db_replycache_validate(rc_to, rc_from, rc_handle, days) == DM_SUCCESS) {
		if (send_vacation(m->message, rc_to, rc_from, subject, message, rc_handle) == 0)
			db_replycache_register(rc_to, rc_from, rc_handle);
		TRACE(TRACE_INFO, "Sending vacation to [%s] from [%s] handle [%s] repeat days [%d]",
			rc_to, rc_from, rc_handle, days);
	} else {
		TRACE(TRACE_INFO, "Vacation suppressed to [%s] from [%s] handle [%s] repeat days [%d]",
			rc_to, rc_from, rc_handle, days);
	}

	if (md5_handle)
		dm_free(md5_handle);

	m->result->cancelkeep = 0;
	return SIEVE2_OK;
}

int sort_notify(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int sort_redirect(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *to;
	const char *from;

	to = sieve2_getvalue_string(s, "address");

	TRACE(TRACE_INFO, "Action is REDIRECT: REDIRECT destination is [%s].",
		to);

	/* According to a clarification from the ietf-mta-filter mailing list,
	 * the redirect is supposed to be absolutely transparent: the envelope
	 * sender is the original envelope sender, with only the envelope
	 * recipient changed. As a fallback, we'll use the redirecting user. */
	from = dbmail_message_get_header(m->message, "Return-Path");
	if (!from)
		from = m->message->envelope_recipient->str;

	if (send_redirect(m->message, to, from) != 0) {
		return SIEVE2_ERROR_FAIL;
	}

	m->result->cancelkeep = 1;
	return SIEVE2_OK;
}

int sort_reject(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;

	message = sieve2_getvalue_string(s, "message");

	TRACE(TRACE_INFO, "Action is REJECT: REJECT message is [%s].", message);

	m->result->rejectmsg = g_string_new(message);

	/* Reject also discards. */
	m->result->cancelkeep = 1;
	m->result->reject = 1;
	return SIEVE2_OK;
}

int sort_discard(sieve2_context_t *s UNUSED, void *my)
{
	struct sort_context *m = (struct sort_context *)my;

	TRACE(TRACE_INFO, "Action is DISCARD.");

	m->result->cancelkeep = 1;
	return SIEVE2_OK;
}

int sort_fileinto(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	extern const char * imap_flag_desc[];
	char * const * flags;
	const char * mailbox;
	int *msgflags = NULL;

	mailbox = sieve2_getvalue_string(s, "mailbox");
	flags = sieve2_getvalue_stringlist(s, "imapflags");

	/* This condition exists for the KEEP callback. */
	if (! mailbox) {
		mailbox = "INBOX";
	}

	/* If there were any imapflags, set them. */
	if (flags) {
		int i, j;
		msgflags = g_new0(int, IMAP_NFLAGS);

		for (i = 0; flags[i]; i++) { // Loop through all script/user-specified flags.
			for (j = 0; imap_flag_desc[j]; j++) { // Find the ones we support.
				if (g_strcasestr(imap_flag_desc[j], flags[i])) {
					msgflags[i] = 1;
				}
			}
		}
	}

	TRACE(TRACE_INFO, "Action is FILEINTO: mailbox is [%s]", mailbox);

	/* Don't cancel the keep if there's a problem storing the message. */
	if (sort_deliver_to_mailbox(m->message, m->user_idnr,
			mailbox, BOX_SORTING, msgflags) != DSN_CLASS_OK) {
		TRACE(TRACE_ERROR, "Could not file message into mailbox; not cancelling keep.");
		m->result->cancelkeep = 0;
	} else {
		m->result->cancelkeep = 1;
	}

	if (msgflags)
		g_free(msgflags);

	return SIEVE2_OK;
}

int sort_errparse(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;
	int lineno;

	lineno = sieve2_getvalue_int(s, "lineno");
	message = sieve2_getvalue_string(s, "message");

	TRACE(TRACE_INFO, "Error is PARSE: Line is [%d], Message is [%s]", lineno, message);

	g_string_append_printf(m->result->errormsg, "Parse error on line [%d]: %s", lineno, message);

	if (m->message) {
		char *alertbody = g_strdup_printf(
			"Your Sieve script [%s] failed to parse correctly.\n"
			"Messages will be delivered to your INBOX for now.\n"
			"The error message is:\n"
			"%s\n",
			m->script, message);
		send_alert(m->user_idnr, "Sieve script parse error", alertbody);
		g_free(alertbody);
	}

	m->result->error_parse = 1;
	return SIEVE2_OK;
}

int sort_errexec(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;

	message = sieve2_getvalue_string(s, "message");

	TRACE(TRACE_INFO, "Error is EXEC: Message is [%s]", message);

	g_string_append_printf(m->result->errormsg, "Execution error: %s", message);

	if (m->message) {
		char *alertbody = g_strdup_printf(
			"Your Sieve script [%s] failed to run correctly.\n"
			"Messages will be delivered to your INBOX for now.\n"
			"The error message is:\n"
			"%s\n",
			m->script, message);
		send_alert(m->user_idnr, "Sieve script run error", alertbody);
		g_free(alertbody);
	}

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
		TRACE(TRACE_INFO, "Include requested from [%s] named [%s]", path, name);
	} else
	if (!strlen(path) && !strlen(name)) {
		/* Read the script file given as an argument. */
		TRACE(TRACE_INFO, "Getting default script named [%s]", m->script);
		res = db_get_sievescript_byname(m->user_idnr, m->script, &m->s_buf);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERROR, "sort_getscript: read_file() returns %d\n", res);
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
		TRACE(TRACE_INFO, "Getting header [%s] returning value [%s]",
			header, bodylist[i]);
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

	TRACE(TRACE_INFO, "Getting message size [%d]", rfcsize);

	sieve2_setvalue_int(s, "size", rfcsize);

	return SIEVE2_OK;
}

int sort_getsubaddress(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *address;
	char *user = NULL, *detail = NULL,
	     *localpart = NULL, *domain = NULL;

	address = sieve2_getvalue_string(s, "address");

	/* Simple address parsing. libSieve only shows us
	 * the localpart@domain portion, so we don't need
	 * to handle anything exciting or exotic here. */
	// TODO: Unify with the delivery chain subaddress code.

	localpart = strdup(address);
	domain = strchr(localpart, '@');
	if (domain) {
		*domain = '\0';
		domain++;
	} else {
		// Malformed address.
	}

	user = strdup(localpart);
	detail = strchr(user, '+');
	if (detail) {
		*detail = '\0';
		detail++;
	} else {
		// No detail present.
	}

	sieve2_setvalue_string(s, "user", user);
	sieve2_setvalue_string(s, "detail", detail);
	sieve2_setvalue_string(s, "localpart", localpart);
	sieve2_setvalue_string(s, "domain", domain);

	dm_list_nodeadd(&m->freelist, &user, sizeof(char *));
	dm_list_nodeadd(&m->freelist, &localpart, sizeof(char *));

	return SIEVE2_OK;
}

int sort_debugtrace(sieve2_context_t *s, void *my UNUSED)
{
	int trace_level;

	switch (sieve2_getvalue_int(s, "level")) {
	case 0:
	case 1:
	case 2:
		trace_level = TRACE_INFO;
		break;
	case 3:
	case 4:
	case 5:
	default:
		trace_level = TRACE_DEBUG;
		break;
	}

	TRACE(trace_level, "libSieve: module [%s] file [%s] function [%s] message [%s]\n",
			sieve2_getvalue_string(s, "module"),
			sieve2_getvalue_string(s, "file"),
			sieve2_getvalue_string(s, "function"),
			sieve2_getvalue_string(s, "message"));

	return SIEVE2_OK;
}

/* END OF CALLBACKS */


sieve2_callback_t vacation_callbacks[] = {
	{ SIEVE2_ACTION_VACATION,       sort_vacation      },
	{ 0, 0 } };

sieve2_callback_t notify_callbacks[] = {
	{ SIEVE2_ACTION_NOTIFY,         sort_notify        },
	{ 0, 0 } };

sieve2_callback_t debug_callbacks[] = {
	{ SIEVE2_DEBUG_TRACE,           sort_debugtrace    },
	{ 0, 0 } };

sieve2_callback_t sort_callbacks[] = {
	{ SIEVE2_ERRCALL_RUNTIME,       sort_errexec       },
	{ SIEVE2_ERRCALL_PARSE,         sort_errparse      },

	{ SIEVE2_ACTION_REDIRECT,       sort_redirect      },
	{ SIEVE2_ACTION_DISCARD,        sort_discard       },
	{ SIEVE2_ACTION_REJECT,         sort_reject        },
	{ SIEVE2_ACTION_FILEINTO,       sort_fileinto      },
	{ SIEVE2_ACTION_KEEP,           sort_fileinto      },

	{ SIEVE2_SCRIPT_GETSCRIPT,      sort_getscript     },
	{ SIEVE2_MESSAGE_GETHEADER,     sort_getheader     },
	{ SIEVE2_MESSAGE_GETENVELOPE,   sort_getenvelope   },
	{ SIEVE2_MESSAGE_GETBODY,       sort_getbody       },
	{ SIEVE2_MESSAGE_GETSIZE,       sort_getsize       },
	{ SIEVE2_MESSAGE_GETSUBADDRESS, sort_getsubaddress },
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
		TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_free: [%s]",
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
	struct sort_sieve_config sieve_config;
	int res;

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_alloc: [%s]",
			res, sieve2_errstr(res));
		return DM_EGENERAL;
	}

	sort_sieve_get_config(&sieve_config);

	res = sieve2_callbacks(sieve2_context, sort_callbacks);
	if (res != SIEVE2_OK) {
		TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_callbacks: [%s]",
			res, sieve2_errstr(res));
		sort_teardown(&sieve2_context, &sort_context);
		return DM_EGENERAL;
	}
	if (sieve_config.vacation) {
		TRACE(TRACE_DEBUG, "Sieve vacation enabled.");
		res = sieve2_callbacks(sieve2_context, vacation_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
	}
	if (sieve_config.notify) {
		TRACE(TRACE_DEBUG, "Sieve notify enabled.");
		res = sieve2_callbacks(sieve2_context, notify_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
	}
	if (sieve_config.debug) {
		TRACE(TRACE_DEBUG, "Sieve debugging enabled.");
		res = sieve2_callbacks(sieve2_context, debug_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
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
const char * sort_listextensions(void)
{
	sieve2_context_t *sieve2_context;
	const char * extensions;
	struct sort_sieve_config sieve_config;

	if (sieve2_alloc(&sieve2_context) != SIEVE2_OK) 
		return NULL;

	if (sieve2_callbacks(sieve2_context, sort_callbacks))
		return NULL;

	sort_sieve_get_config(&sieve_config);

	if (sieve_config.vacation) {
		TRACE(TRACE_DEBUG, "Sieve vacation enabled.");
		sieve2_callbacks(sieve2_context, vacation_callbacks);
	}
	if (sieve_config.notify) {
		TRACE(TRACE_DEBUG, "Sieve notify enabled.");
		sieve2_callbacks(sieve2_context, notify_callbacks);
	}
	if (sieve_config.debug) {
		TRACE(TRACE_DEBUG, "Sieve debugging enabled.");
		sieve2_callbacks(sieve2_context, debug_callbacks);
	}

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
		TRACE(TRACE_ERROR, "Error %d when calling sieve2_validate: %s",
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
		TRACE(TRACE_ERROR, "Error [%d] when calling db_getactive_sievescript", res);
		exitnull = 1;
		goto freesieve;
	}
	if (sort_context->script == NULL) {
		TRACE(TRACE_INFO, "User doesn't have any active sieve scripts.");
		exitnull = 1;
		goto freesieve;
	}

	res = sieve2_execute(sieve2_context, sort_context);
	if (res != SIEVE2_OK) {
		TRACE(TRACE_ERROR, "Error [%d] when calling sieve2_execute: [%s]",
			res, sieve2_errstr(res));
		exitnull = 1;
	}
	if (! sort_context->result->cancelkeep) {
		TRACE(TRACE_INFO, "No actions taken; message must be kept.");
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
	if (result->errormsg != NULL) 
		g_string_free(result->errormsg, TRUE);
	if (result->rejectmsg != NULL) 
		g_string_free(result->rejectmsg, TRUE);
	dm_free(result);
}

int sort_get_cancelkeep(sort_result_t *result)
{
	if (result == NULL) return 0;
	return result->cancelkeep;
}

const char * sort_get_mailbox(sort_result_t *result)
{
	if (result == NULL) return NULL;
	return result->mailbox;
}

int sort_get_reject(sort_result_t *result)
{
	if (result == NULL) return 0;
	return result->reject;
}

const char *sort_get_rejectmsg(sort_result_t *result)
{
	if (result == NULL) return NULL;
	return result->rejectmsg->str;
}

int sort_get_error(sort_result_t *result)
{
	if (result == NULL) return 0;
	return result->errormsg->len;
}

const char * sort_get_errormsg(sort_result_t *result)
{
	if (result == NULL) return NULL;
	return result->errormsg->str;
}

