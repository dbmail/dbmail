/* 
 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl
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
	uint64_t user_idnr;
	DbmailMessage *message;
	struct sort_result *result;
	GList *freelist;
};

/* Returned opaquely as type SortResult_T. */
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
	Field_T val;

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

/*
 * Send a vacation message. FIXME: this should provide
 * MIME support, to comply with the Sieve-Vacation spec.
 */
static int send_vacation(DbmailMessage *message,
		const char *to, const char *from,
		const char *subject, const char *body, const char *handle)
{
	int result;
	const char *x_dbmail_vacation = dbmail_message_get_header(message, "X-Dbmail-Vacation");

	if (x_dbmail_vacation) {
		TRACE(TRACE_NOTICE, "vacation loop detected [%s]", x_dbmail_vacation);
		return 0;
	}

	DbmailMessage *new_message = dbmail_message_new(message->pool);
	new_message = dbmail_message_construct(new_message, to, from, subject, body);
	dbmail_message_set_header(new_message, "X-DBMail-Vacation", handle);

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	dbmail_message_free(new_message);

	return result;
}

static int send_redirect(DbmailMessage *message, const char *to, const char *from)
{
	if (!to || !from) {
		TRACE(TRACE_ERR, "both To and From addresses must be specified");
		return -1;
	}

	return send_mail(message, to, from, NULL, SENDRAW, SENDMAIL);
}

int send_alert(uint64_t user_idnr, char *subject, char *body)
{
	DbmailMessage *new_message;
	Field_T postmaster;
	char *from;
	int msgflags[IMAP_NFLAGS];

	// Only send each unique alert once a day.
	char *tmp = g_strconcat(subject, body, NULL);
	char *userchar = g_strdup_printf("%" PRIu64 "", user_idnr);
	char handle[FIELDSIZE];

	memset(handle, 0, sizeof(handle));
       	dm_md5(tmp, handle);
	if (db_replycache_validate(userchar, "send_alert", handle, 1) != DM_SUCCESS) {
		TRACE(TRACE_INFO, "Already sent alert [%s] to user [%" PRIu64 "] today", subject, user_idnr);
		g_free(userchar);
		g_free(tmp);
		return 0;
	} else {
		TRACE(TRACE_INFO, "Sending alert [%s] to user [%" PRIu64 "]", subject, user_idnr);
		db_replycache_register(userchar, "send_alert", handle);
		g_free(userchar);
		g_free(tmp);
	}

	// From the Postmaster.
	if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
		TRACE(TRACE_NOTICE, "no config value for POSTMASTER");
	}
	if (strlen(postmaster))
		from = postmaster;
	else
		from = DEFAULT_POSTMASTER;

	// Set the \Flagged flag.
	memset(msgflags, 0, sizeof(msgflags));
	msgflags[IMAP_FLAG_FLAGGED] = 1;

	// Get the user's login name.
	char *to = auth_get_userid(user_idnr);

	new_message = dbmail_message_new(NULL);
	new_message = dbmail_message_construct(new_message, to, from, subject, body);

	// Pre-insert the message and get a new_message->id
	dbmail_message_store(new_message);
	uint64_t tmpid = new_message->id;

	if (sort_deliver_to_mailbox(new_message, user_idnr,
			"INBOX", BOX_BRUTEFORCE, msgflags, NULL) != DSN_CLASS_OK) {
		TRACE(TRACE_ERR, "Unable to deliver alert [%s] to user [%" PRIu64 "]", subject, user_idnr);
	}

	g_free(to);
	db_delete_message(tmpid);
	dbmail_message_free(new_message);

	return 0;
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
	const char *message, *subject, *fromaddr, *handle;
	const char *rc_to, *rc_from;
	char rc_handle[FIELDSIZE];
	int days;
	//int mime;

	days = sieve2_getvalue_int(s, "days");
	//mime = sieve2_getvalue_int(s, "mime"); // mime: 1 if message is mime coded. FIXME.
	message = sieve2_getvalue_string(s, "message");
	subject = sieve2_getvalue_string(s, "subject");
	fromaddr = sieve2_getvalue_string(s, "fromaddr"); // From: specified by the script.
	handle = sieve2_getvalue_string(s, "hash");

	/* Default to a week, upper limit of a month.
	 * This is our only loop prevention mechanism! The value must be
	 * greater than 0, else the replycache code will always indicate
	 * that we haven't seen anything since 0 days ago... */
	if (days == 0) days = 7;
	if (days < 1) days = 1;
	if (days > 30) days = 30;

	memset(rc_handle, 0, sizeof(rc_handle));
	dm_md5((char * const) handle, rc_handle);

	// FIXME: should be validated as a user might try
	// to forge an address from their script.
	rc_from = fromaddr;
	if (!rc_from)
		rc_from = dbmail_message_get_header(m->message, "Delivered-To");
	if (!rc_from)
		rc_from = p_string_str(m->message->envelope_recipient);

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

	m->result->cancelkeep = 0;
	return SIEVE2_OK;
}

/*
From http://www.ietf.org/internet-drafts/draft-ietf-sieve-notify-07.txt

   Usage:  notify [":from" string] [":importance" <"1" / "2" / "3">]
                  [":options" string-list] [":message" string] <method: string>
*/
int sort_notify(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *fromaddr;
	//const char *message;
	//const char *method;
	const char *rc_to, *rc_from;
	//int importance;
	//char * const * options;

	fromaddr = sieve2_getvalue_string(s, "fromaddr");
	//method = sieve2_getvalue_string(s, "method");
	//message = sieve2_getvalue_string(s, "message");
	//importance = sieve2_getvalue_int(s, "importance");
	//options = sieve2_getvalue_stringlist(s, "options");

	// FIXME: should be validated as a user might try
	// to forge an address from their script.
	rc_from = fromaddr;
	if (!rc_from)
		rc_from = dbmail_message_get_header(m->message, "Delivered-To");
	if (!rc_from)
		rc_from = p_string_str(m->message->envelope_recipient);

	rc_to = dbmail_message_get_header(m->message, "Reply-To");
	if (!rc_to)
		rc_to = dbmail_message_get_header(m->message, "Return-Path");

//	send_notification(m->message, rc_to, rc_from, method, message);

	TRACE(TRACE_INFO, "Notification from [%s] to [%s] was not sent as notify is not supported in this release.", rc_from, rc_to);

	return SIEVE2_OK;
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
		from = p_string_str(m->message->envelope_recipient);

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
	char * const * flaglist;
	const char * mailbox;
	int msgflags[IMAP_NFLAGS];
	int *has_msgflags = NULL;
	GList *keywords = NULL;
	char *allflags = NULL;
	char **flags = NULL;

	mailbox = sieve2_getvalue_string(s, "mailbox");
	flaglist = sieve2_getvalue_stringlist(s, "flags");
	allflags = g_strjoinv(" ", (char **)flaglist);
	flags = g_strsplit(allflags, " ", 0);

	/* This condition exists for the KEEP callback. */
	if (! mailbox) {
		mailbox = "INBOX";
	}

	TRACE(TRACE_INFO, "Action is FILEINTO: mailbox is [%s] flags are [%s]",
			mailbox, allflags);

	/* If there were any imapflags, set them. */
	if (flags) {
		int i, j;
		memset(msgflags, 0, sizeof(msgflags));

		// Loop through all script/user-specified flags.
		for (i = 0; flags[i]; i++) {
			// Find the ones we support.
			int baseflag = FALSE;
			char *flag = strrchr(flags[i], '\\');
			if (flag) 
				flag++;
			else
				flag = flags[i];

			for (j = 0; imap_flag_desc[j] && j < IMAP_NFLAGS; j++) {
				if (g_strcasestr(imap_flag_desc[j], flag)) {
					TRACE(TRACE_DEBUG, "set baseflag [%s]", flag);
					// Flag 'em.
					msgflags[j] = 1;
					baseflag = TRUE;
					// Only pass msgflags if we found something.
					has_msgflags = msgflags;
				}
			}
			if (! baseflag) {
				TRACE(TRACE_DEBUG, "set keyword [%s]", flag);
				keywords = g_list_append(keywords, g_strdup(flag));
			}

		}
		g_strfreev(flags);
	}
	g_free(allflags);


	/* Don't cancel the keep if there's a problem storing the message. */
	if (sort_deliver_to_mailbox(m->message, m->user_idnr,
			mailbox, BOX_SORTING, has_msgflags, keywords) != DSN_CLASS_OK) {
		TRACE(TRACE_ERR, "Could not file message into mailbox; not cancelling keep.");
		m->result->cancelkeep = 0;
	} else {
		m->result->cancelkeep = 1;
	}

	if (keywords)
		g_list_destroy(keywords);

	return SIEVE2_OK;
}

/* This should only happen if the user has uploaded an invalid script.
 * Possible causes are a homebrew script uploader that does not do proper
 * validation, libSieve's removal of a deprecated Sieve language feature,
 * or perhaps some bugginess elsewhere. */
int sort_errparse(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;
	int lineno;

	lineno = sieve2_getvalue_int(s, "lineno");
	message = sieve2_getvalue_string(s, "message");

	TRACE(TRACE_ERR, "SIEVE Error is PARSE: Line is [%d], Message is [%s]", lineno, message);
	/*
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
	*/
	m->result->error_parse = 1;
	return SIEVE2_OK;
}

int sort_errexec(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *message;

	message = sieve2_getvalue_string(s, "message");

	TRACE(TRACE_ERR, "SIEVE Error is EXEC: Message is [%s]", message);

	/* This turns out to be incredibly annoying, as libSieve
	 * throws execution errors on malformed addresses coming
	 * from the wild. As you might guess, that happens with
	 * greater than trivial frequency.

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
	*/

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
		res = dm_sievescript_getbyname(m->user_idnr, m->script, &m->s_buf);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERR, "sort_getscript: read_file() returns %d\n", res);
			return SIEVE2_ERROR_FAIL;
		}
		sieve2_setvalue_string(s, "script", m->s_buf);
		TRACE(TRACE_INFO, "Script\n%s", m->s_buf);
	} else {
		return SIEVE2_ERROR_BADARGS;
	}

	return SIEVE2_OK;
}

int sort_getheader(sieve2_context_t *s, void *my)
{
	struct sort_context *m = (struct sort_context *)my;
	const char *header;
	char **bodylist;
	GList *headers;
	unsigned i;

	header = sieve2_getvalue_string(s, "header");
	
	headers = dbmail_message_get_header_repeated(m->message, header);
	
	bodylist = g_new0(char *,g_list_length(headers)+1);
	i = 0;
	while (headers) {
		char *decoded = dbmail_iconv_decode_text(headers->data);
		bodylist[i++] = decoded;
		/* queue the decoded value for freeing later on */
		m->freelist = g_list_prepend(m->freelist, decoded);

		if (! g_list_next(headers))
			break;
		headers = g_list_next(headers);
	}
	g_list_free(g_list_first(headers));

	/* We have to free the header array. */
	if (m->freelist) {
		m->freelist = g_list_prepend(m->freelist, bodylist);
	}

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
	const char *to, *from;

	to = dbmail_message_get_envelope_recipient(m->message);
	from = dbmail_message_get_header(m->message, "Return-Path");

	TRACE(TRACE_DEBUG, "from [%s], to [%s]", from, to);

	sieve2_setvalue_string(s, "to", (char *)to);
	sieve2_setvalue_string(s, "from", (char *)from);

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

	rfcsize = dbmail_message_get_size(m->message, TRUE);

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

	m->freelist = g_list_prepend(m->freelist, user);
	m->freelist = g_list_prepend(m->freelist, localpart);

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

	TRACE(trace_level, "sieve: [%s,%s,%s: [%s]\n",
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

	struct sort_context *sort_context = *sc;

	g_list_destroy(sort_context->freelist);

	if (sort_context) {
		g_free(sort_context);
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
		TRACE(TRACE_ERR, "Error [%d] when calling sieve2_alloc: [%s]",
			res, sieve2_errstr(res));
		return DM_EGENERAL;
	}

	sort_sieve_get_config(&sieve_config);

	res = sieve2_callbacks(sieve2_context, sort_callbacks);
	if (res != SIEVE2_OK) {
		TRACE(TRACE_ERR, "Error [%d] when calling sieve2_callbacks: [%s]",
			res, sieve2_errstr(res));
		sort_teardown(&sieve2_context, &sort_context);
		return DM_EGENERAL;
	}
	if (sieve_config.vacation) {
		TRACE(TRACE_DEBUG, "Sieve vacation enabled.");
		res = sieve2_callbacks(sieve2_context, vacation_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
	}
	if (sieve_config.notify) {
		TRACE(TRACE_INFO, "Sieve notify is not supported in this release.");
		res = sieve2_callbacks(sieve2_context, notify_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
	}
	if (sieve_config.debug) {
		TRACE(TRACE_DEBUG, "Sieve debugging enabled.");
		res = sieve2_callbacks(sieve2_context, debug_callbacks);
		if (res != SIEVE2_OK) {
			TRACE(TRACE_ERR, "Error [%d] when calling sieve2_callbacks: [%s]",
				res, sieve2_errstr(res));
			sort_teardown(&sieve2_context, &sort_context);
			return DM_EGENERAL;
		}
	}

	sort_context = g_new0(struct sort_context, 1);
	if (!sort_context) {
		sort_teardown(&sieve2_context, &sort_context);
		return DM_EGENERAL;
	}
	memset(sort_context, 0, sizeof(struct sort_context));

	sort_context->freelist = NULL;

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
		TRACE(TRACE_ERR, "Sieve notify is not supported in this release.");
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
		extensions = g_strstrip(g_strdup(extensions));

	/* If this fails, then we don't care about the
	 * memory leak, because the program has to bomb out.
	 * It will not be possible to start libSieve up again
	 * if it does not free properly. */
	if (sieve2_free(&sieve2_context) != SIEVE2_OK)
		return NULL;

	return extensions;
}

/* Return 0 on script OK, 1 on script error, 2 on misc error. */
SortResult_T *sort_validate(uint64_t user_idnr, char *scriptname)
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
		TRACE(TRACE_ERR, "Error %d when calling sieve2_validate: %s",
			res, sieve2_errstr(res));
		exitnull = 1;
		goto freesieve;
	}

	/* At this point the callbacks are called from within libSieve. */

freesieve:
	if (sort_context->s_buf)
		g_free(sort_context->s_buf);

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
SortResult_T *sort_process(uint64_t user_idnr, DbmailMessage *message, const char *mailbox)
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
	if (mailbox)
		sort_context->result->mailbox = mailbox;

	res = dm_sievescript_get(user_idnr, &sort_context->script);
	if (res != 0) {
		TRACE(TRACE_ERR, "Error [%d] when calling db_getactive_sievescript", res);
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
		TRACE(TRACE_ERR, "Error [%d] when calling sieve2_execute: [%s]",
			res, sieve2_errstr(res));
		exitnull = 1;
	}
	if (! sort_context->result->cancelkeep) {
		TRACE(TRACE_INFO, "No actions taken; message must be kept.");
	}

	/* At this point the callbacks are called from within libSieve. */

freesieve:
	if (sort_context->s_buf)
		g_free(sort_context->s_buf);
	if (sort_context->script)
		g_free(sort_context->script);

	if (exitnull)
		result = NULL;
	else
		result = sort_context->result;

	sort_teardown(&sieve2_context, &sort_context);

	return result;
}

/* SORT RESULT INTERFACE */

void sort_free_result(SortResult_T *result)
{
	if (result == NULL) return;
	if (result->errormsg != NULL) 
		g_string_free(result->errormsg, TRUE);
	if (result->rejectmsg != NULL) 
		g_string_free(result->rejectmsg, TRUE);
	g_free(result);
}

int sort_get_cancelkeep(SortResult_T *result)
{
	if (result == NULL) return 0;
	return result->cancelkeep;
}

const char * sort_get_mailbox(SortResult_T *result)
{
	if (result == NULL) return NULL;
	return result->mailbox;
}

int sort_get_reject(SortResult_T *result)
{
	if (result == NULL) return 0;
	return result->reject;
}

const char *sort_get_rejectmsg(SortResult_T *result)
{
	if (result == NULL) return NULL;
	return result->rejectmsg->str;
}

int sort_get_error(SortResult_T *result)
{
	if (result == NULL) return 0;
	return result->errormsg->len;
}

const char * sort_get_errormsg(SortResult_T *result)
{
	if (result == NULL) return NULL;
	return result->errormsg->str;
}

