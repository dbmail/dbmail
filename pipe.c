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
 * Functions for reading the pipe from the MTA */

#include "dbmail.h"

#define THIS_MODULE "delivery"

#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
#define MAX_U64_STRINGSIZE 40
#define MAX_COMM_SIZE 512
#define RING_SIZE 6

static int valid_sender(const char *addr) 
{
	int ret = 1;
	char *testaddr;
	testaddr = g_ascii_strdown(addr, -1);
	if (strstr(testaddr, "mailer-daemon@"))
		ret = 0;
	if (strstr(testaddr, "daemon@"))
		ret = 0;
	if (strstr(testaddr, "postmaster@"))
		ret = 0;
	g_free(testaddr);
	return ret;
}

static int parse_and_escape(const char *in, char **out)
{
	InternetAddressList *ialist;
	InternetAddress *ia;

	TRACE(TRACE_DEBUG, "parsing address [%s]", in);
	ialist = internet_address_parse_string(in);
	ia = ialist->address;
	if (ia->type != INTERNET_ADDRESS_NAME) {
		TRACE(TRACE_MESSAGE, "unable to parse email address [%s]", in);
		internet_address_list_destroy(ialist);
		return -1;
	}

	if (! (*out = dm_shellesc(ia->value.addr))) {
		TRACE(TRACE_ERROR, "out of memory calling dm_shellesc");
		internet_address_list_destroy(ialist);
		return -1;
	}

	internet_address_list_destroy(ialist);

	return 0;
}

// Either convert the message struct to a
// string, or send the database rows raw.
enum sendwhat {
	SENDMESSAGE     = 0,
	SENDRAW         = 1
};

// Use the system sendmail binary.
#define SENDMAIL        NULL

/* Sends a message. */
static int send_mail(struct DbmailMessage *message,
		const char *to, const char *from,
		const char *preoutput,
		enum sendwhat sendwhat, char *sendmail_external)
{
	FILE *mailpipe = NULL;
	char *escaped_to = NULL;
	char *escaped_from = NULL;
	char *message_string = NULL;
	char *sendmail_command = NULL;
	field_t sendmail, postmaster;
	int result;

	if (!from || strlen(from) < 1) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
		}
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}

	if (config_get_value("SENDMAIL", "DBMAIL", sendmail) < 0) {
		TRACE(TRACE_ERROR, "error getting value for SENDMAIL in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (strlen(sendmail) < 1) {
		TRACE(TRACE_ERROR, "SENDMAIL not set in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (!sendmail_external) {
		parse_and_escape(to, &escaped_to);
		parse_and_escape(from, &escaped_from);
		sendmail_command = g_strconcat(sendmail, " -f ", escaped_from, " ", escaped_to, NULL);
		dm_free(escaped_to);
		dm_free(escaped_from);
		if (!sendmail_command) {
			TRACE(TRACE_ERROR, "out of memory calling g_strconcat");
			return -1;
		}
	} else {
		sendmail_command = sendmail_external;
	}

	TRACE(TRACE_INFO, "opening pipe to [%s]", sendmail_command);

	if (!(mailpipe = popen(sendmail_command, "w"))) {
		TRACE(TRACE_ERROR, "could not open pipe to sendmail");
		g_free(sendmail_command);
		return 1;
	}

	TRACE(TRACE_DEBUG, "pipe opened");

	switch (sendwhat) {
	case SENDRAW:
		// This is a hack so forwards can give a From line.
		if (preoutput)
			fprintf(mailpipe, "%s\n", preoutput);
		// This function will dot-stuff the message.
		db_send_message_lines(mailpipe, message->id, -2, 1);
		break;
	case SENDMESSAGE:
		message_string = dbmail_message_to_string(message);
		fprintf(mailpipe, "%s", message_string);
		g_free(message_string);
		break;
	default:
		TRACE(TRACE_ERROR, "invalid sendwhat in call to send_mail: [%d]", sendwhat);
		break;
	}

	result = pclose(mailpipe);
	TRACE(TRACE_DEBUG, "pipe closed");

	/* Adapted from the Linux waitpid 2 man page. */
	if (WIFEXITED(result)) {
		result = WEXITSTATUS(result);
		TRACE(TRACE_INFO, "sendmail exited normally");
	} else if (WIFSIGNALED(result)) {
		result = WTERMSIG(result);
		TRACE(TRACE_INFO, "sendmail was terminated by signal");
	} else if (WIFSTOPPED(result)) {
		result = WSTOPSIG(result);
		TRACE(TRACE_INFO, "sendmail was stopped by signal");
	}

	if (result != 0) {
		TRACE(TRACE_ERROR, "sendmail error return value was [%d]", result);

		if (!sendmail_external)
			g_free(sendmail_command);
		return 1;
	}

	if (!sendmail_external)
		g_free(sendmail_command);
	return 0;
} 

int send_redirect(struct DbmailMessage *message, const char *to, const char *from)
{
	if (!to || !from) {
		TRACE(TRACE_ERROR, "both To and From addresses must be specified");
		return -1;
	}

	return send_mail(message, to, from, NULL, SENDRAW, SENDMAIL);
}

int send_forward_list(struct DbmailMessage *message,
		struct dm_list *targets, const char *from)
{
	int result = 0;
	struct element *target;
	field_t postmaster;

	TRACE(TRACE_INFO, "delivering to [%ld] external addresses", dm_list_length(targets));

	if (!from) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
		}
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}

	target = dm_list_getstart(targets);
	while (target != NULL) {
		char *to = (char *)target->data;

		if (!to || strlen(to) < 1) {
			TRACE(TRACE_ERROR, "forwarding address is zero length, message not forwarded.");
		} else {
			if (to[0] == '!') {
				// The forward is a command to execute.
				// Prepend an mbox From line.
				char timestr[50];
				time_t td;
				struct tm tm;
				char *fromline;
                        
				time(&td);		/* get time */
				tm = *localtime(&td);	/* get components */
				strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y", &tm);
                        
				TRACE(TRACE_DEBUG, "prepending mbox style From header to pipe returnpath: %s", from);
                        
				/* Format: From<space>address<space><space>Date */
				fromline = g_strconcat("From ", from, "  ", timestr, NULL);

				result |= send_mail(message, "", "", fromline, SENDRAW, to+1);
				g_free(fromline);
			} else if (to[0] == '|') {
				// The forward is a command to execute.
				result |= send_mail(message, "", "", NULL, SENDRAW, to+1);

			} else {
				// The forward is an email address.
				result |= send_mail(message, to, from, NULL, SENDRAW, SENDMAIL);
			}
		}

		target = target->nextnode;
	}

	return result;
}

/* 
 * Send an automatic notification.
 */
static int send_notification(struct DbmailMessage *message UNUSED, const char *to)
{
	field_t from = "";
	field_t subject = "";
	int result;

	if (config_get_value("POSTMASTER", "DBMAIL", from) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
	}

	if (config_get_value("AUTO_NOTIFY_SENDER", "DELIVERY", from) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for AUTO_NOTIFY_SENDER");
	}

	if (config_get_value("AUTO_NOTIFY_SUBJECT", "DELIVERY", subject) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for AUTO_NOTIFY_SUBJECT");
	}

	if (strlen(from) < 1)
		g_strlcpy(from, AUTO_NOTIFY_SENDER, FIELDSIZE);

	if (strlen(subject) < 1)
		g_strlcpy(subject, AUTO_NOTIFY_SUBJECT, FIELDSIZE);

	struct DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, subject, "");

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	dbmail_message_free(new_message);

	return result;
}

/*
 * Send a vacation message. FIXME: this should provide
 * MIME support, to comply with the Sieve-Vacation spec.
 */
int send_vacation(struct DbmailMessage *message,
		const char *to, const char *from,
		const char *subject, const char *body, const char *handle)
{
	int result;
	const char *x_dbmail_vacation = dbmail_message_get_header(message, "X-Dbmail-Vacation");

	if (x_dbmail_vacation) {
		TRACE(TRACE_MESSAGE, "vacation loop detected [%s]", x_dbmail_vacation);
		return 0;
	}

	struct DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, subject, body);
	dbmail_message_set_header(new_message, "X-DBMail-Vacation", handle);

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	dbmail_message_free(new_message);

	return result;
}
	
/*
 * Send an automatic reply.
 */
#define REPLY_DAYS 7
static int send_reply(struct DbmailMessage *message, const char *body)
{
	const char *from, *to, *replyto, *subject;
	const char *x_dbmail_reply;
	char *escaped_send_address;
	int result;

	InternetAddressList *ialist;
	InternetAddress *ia;

	x_dbmail_reply = dbmail_message_get_header(message, "X-Dbmail-Reply");
	if (x_dbmail_reply) {
		TRACE(TRACE_MESSAGE, "reply loop detected [%s]", x_dbmail_reply);
		return 0;
	}
	
	from = dbmail_message_get_header(message, "From");
	subject = dbmail_message_get_header(message, "Subject");
	replyto = dbmail_message_get_header(message, "Reply-To");

	/* The To header is not usable as a backup because it
	 * is likely to have other addresses listed. */
	to = dbmail_message_get_header(message, "Delivered-To");

	if (!from && !replyto) {
		TRACE(TRACE_ERROR, "no address to send to");
		return 0;
	}

	if (!valid_sender(from)) {
		TRACE(TRACE_DEBUG, "sender invalid. skip auto-reply.");
		return 0;
	}

	ialist = internet_address_parse_string(replyto ? replyto : from);
	ia = ialist->address;
	escaped_send_address = internet_address_to_string(ia, TRUE);
	internet_address_list_destroy(ialist);

	if (db_replycache_validate(to, escaped_send_address,
		"replycache", REPLY_DAYS) != DM_SUCCESS) {
		TRACE(TRACE_DEBUG, "skip auto-reply");
		return 0;
	}

	char *newsubject = g_strconcat("Re: ", subject, NULL);

	struct DbmailMessage *new_message = dbmail_message_new();
	/* Reversed 'to' and 'from' because this is a reply. */
	new_message = dbmail_message_construct(new_message, escaped_send_address, to, newsubject, body);
	dbmail_message_set_header(new_message, "X-DBMail-Reply", escaped_send_address);

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	/* Reversed 'to' and 'from' because this is a reply. */
	if (!send_mail(new_message, escaped_send_address, to, NULL, SENDMESSAGE, SENDMAIL)) {
		db_replycache_register(to, escaped_send_address, "replycache");
	}

	dm_free(newsubject);
	dbmail_message_free(new_message);

	return 0;
}


/* Yeah, RAN. That's Reply And Notify ;-) */
static int execute_auto_ran(struct DbmailMessage *message, u64_t useridnr)
{
	field_t val;
	int do_auto_notify = 0, do_auto_reply = 0;
	char *reply_body = NULL;
	char *notify_address = NULL;

	/* message has been succesfully inserted, perform auto-notification & auto-reply */
	if (config_get_value("AUTO_NOTIFY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERROR, "error getting config value for AUTO_NOTIFY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (config_get_value("AUTO_REPLY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERROR, "error getting config value for AUTO_REPLY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	if (do_auto_notify != 0) {
		TRACE(TRACE_DEBUG, "starting auto-notification procedure");

		if (db_get_notify_address(useridnr, &notify_address) != 0)
			TRACE(TRACE_ERROR, "error fetching notification address");
		else {
			if (notify_address == NULL)
				TRACE(TRACE_DEBUG, "no notification address specified, skipping");
			else {
				TRACE(TRACE_DEBUG, "sending notifcation to [%s]", notify_address);
				if (send_notification(message, notify_address) < 0) {
					TRACE(TRACE_ERROR, "error in call to send_notification.");
					dm_free(notify_address);
					return -1;
				}
				dm_free(notify_address);
			}
		}
	}

	if (do_auto_reply != 0) {
		TRACE(TRACE_DEBUG, "starting auto-reply procedure");

		if (db_get_reply_body(useridnr, &reply_body) != 0)
			TRACE(TRACE_ERROR, "error fetching reply body");
		else {
			if (reply_body == NULL || reply_body[0] == '\0')
				TRACE(TRACE_DEBUG, "no reply body specified, skipping");
			else {
				if (send_reply(message, reply_body) < 0) {
					TRACE(TRACE_ERROR, "error in call to send_reply");
					dm_free(reply_body);
					return -1;
				}
				dm_free(reply_body);
				
			}
		}
	}

	return 0;
}


int store_message_in_blocks(const char *message, u64_t message_size,
			    u64_t msgidnr) 
{
	u64_t tmp_messageblk_idnr;
	u64_t rest_size = message_size;
	u64_t block_size = 0;
	unsigned block_nr = 0;
	size_t offset;

	while (rest_size > 0) {
		offset = block_nr * READ_BLOCK_SIZE;
		block_size = (rest_size < READ_BLOCK_SIZE ? 
			      rest_size : READ_BLOCK_SIZE);
		rest_size = (rest_size < READ_BLOCK_SIZE ?
			     0 : rest_size - READ_BLOCK_SIZE);
		TRACE(TRACE_DEBUG, "inserting message [%s]", &message[offset]);
		if (db_insert_message_block(&message[offset],
					    block_size, msgidnr,
					    &tmp_messageblk_idnr,0) < 0) {
			TRACE(TRACE_ERROR, "db_insert_message_block() failed");
			return -1;
		}
		
			
		block_nr += 1;
	}

	return 1;
}

/* Here's the real *meat* of this source file!
 *
 * Function: insert_messages()
 * What we get:
 *   - A pointer to the incoming message stream
 *   - The header of the message 
 *   - A list of destination addresses / useridnr's
 *   - The default mailbox to delivery to
 *
 * What we do:
 *   - Read in the rest of the message
 *   - Store the message to the DBMAIL user
 *   - Process the destination addresses into lists:
 *     - Local useridnr's
 *     - External forwards
 *     - No such user bounces
 *   - Store the local useridnr's
 *     - Run the message through each user's sorting rules
 *     - Potentially alter the delivery:
 *       - Different mailbox
 *       - Bounce
 *       - Reply with vacation message
 *       - Forward to another address
 *     - Check the user's quota before delivering
 *       - Do this *after* their sorting rules, since the
 *         sorting rules might not store the message anyways
 *   - Send out the no such user bounces
 *   - Send out the external forwards
 *   - Delete the temporary message from the database
 * What we return:
 *   - 0 on success
 *   - -1 on full failure
 */
int insert_messages(struct DbmailMessage *message, 
		struct dm_list *dsnusers)
{
	u64_t bodysize, rfcsize;
	u64_t tmpid;
	struct element *element;
	u64_t msgsize;

 	delivery_status_t final_dsn;

	/* first start a new database transaction */
	if (db_begin_transaction() < 0) {
		TRACE(TRACE_ERROR, "error executing db_begin_transaction(). aborting delivery...");
		return -1;
	}

	switch (dbmail_message_store(message)) {
	case -1:
		TRACE(TRACE_ERROR, "failed to store temporary message.");
		db_rollback_transaction();
		return -1;
	default:
		TRACE(TRACE_DEBUG, "temporary msgidnr is [%llu]", message->id);
		break;
	}

	tmpid = message->id; // for later removal

	bodysize = (u64_t)dbmail_message_get_body_size(message, FALSE);
	rfcsize = (u64_t)dbmail_message_get_rfcsize(message);
	msgsize = (u64_t)dbmail_message_get_size(message, FALSE);


	/* Loop through the users list. */
	for (element = dm_list_getstart(dsnusers); element != NULL; element = element->nextnode) {
		
		struct element *userid_elem;
		int has_2 = 0, has_4 = 0, has_5 = 0, has_5_2 = 0;
		
		deliver_to_user_t *delivery = (deliver_to_user_t *) element->data;
		
		/* Each user may have a list of user_idnr's for local
		 * delivery. */
		for (userid_elem = dm_list_getstart(delivery->userids); userid_elem != NULL; userid_elem = userid_elem->nextnode) {
			u64_t useridnr = *(u64_t *) userid_elem->data;
			TRACE(TRACE_DEBUG, "calling sort_and_deliver for useridnr [%llu]", useridnr);

			switch (sort_and_deliver(message,
					delivery->address, useridnr,
					delivery->mailbox, delivery->source)) {
			case DSN_CLASS_OK:
				/* Indicate success. */
				TRACE(TRACE_INFO, "successful sort_and_deliver for useridnr [%llu]", useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				/* Indicate permanent failure. */
				TRACE(TRACE_ERROR, "permanent failure sort_and_deliver for useridnr [%llu]", useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_QUOTA:
			/* Indicate over quota. */
				TRACE(TRACE_MESSAGE, "mailbox over quota, message rejected for useridnr [%llu]", useridnr);
				has_5_2 = 1;
				break;
			case DSN_CLASS_TEMP:
			default:
				/* Assume a temporary failure */
				TRACE(TRACE_ERROR, "unknown temporary failure in sort_and_deliver for useridnr [%llu]", useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			if (execute_auto_ran(message, useridnr) < 0) {
				TRACE(TRACE_ERROR, "error in execute_auto_ran(), but continuing delivery normally.");
			}
		} /* from: the useridnr for loop */

		final_dsn = dsnuser_worstcase_int(has_2, has_4, has_5, has_5_2);
		switch (final_dsn.class) {
		case DSN_CLASS_OK:
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			break;
		case DSN_CLASS_TEMP:
			/* sort_and_deliver returns TEMP is useridnr is 0, aka,
			 * if nothing was delivered at all, or for any other failures. */	

			/* If there's a problem with the delivery address, but
			 * there are proper forwarding addresses, we're OK. */
			if (dm_list_length(delivery->forwards) > 0) {
				/* Success. Address related. Valid. */
				set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
				break;
			}
			/* Fall through to FAIL. */
		case DSN_CLASS_FAIL:
			/* Permanent failure. Address related. Does not exist. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			break;
		case DSN_CLASS_QUOTA:
			/* Permanent failure. Mailbox related. Over quota limit. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 2, 2);
			break;
		case DSN_CLASS_NONE:
			/* Leave the DSN status at whatever dsnuser_resolve set it at. */
			break;
		}

		TRACE(TRACE_DEBUG, "deliver [%ld] messages to external addresses",
			dm_list_length(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (dm_list_length(delivery->forwards) > 0) {

			TRACE(TRACE_DEBUG, "delivering to external addresses");

			/* Forward using the temporary stored message. */
			if (send_forward_list(message, delivery->forwards,
					dbmail_message_get_header(message, "Return-Path")) < 0)
				/* FIXME: if forward fails, we should do something 
				 * sensible. Currently, the message is just black-
				 * holed! */
				TRACE(TRACE_ERROR, "forward failed message lost");
		}
	}			/* from: the delivery for loop */

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	if (db_delete_message(tmpid) < 0) 
		TRACE(TRACE_ERROR, "failed to delete temporary message [%llu]", message->id);
	TRACE(TRACE_DEBUG, "temporary message deleted from database. Done.");

	/* if committing the transaction fails, a rollback is performed */
	if (db_commit_transaction() < 0) 
		return -1;

	return 0;
}

int send_alert(u64_t user_idnr, char *subject, char *body)
{
	struct DbmailMessage *new_message;
	field_t postmaster;
	char *from;
	int msgflags[IMAP_NFLAGS];

	// Only send each unique alert once a day.
	char *tmp = g_strconcat(subject, body, NULL);
	char *handle = dm_md5((unsigned char *)tmp);
	char *userchar = g_strdup_printf("%llu", user_idnr);
	if (db_replycache_validate(userchar, "send_alert", handle, 1) != DM_SUCCESS) {
		TRACE(TRACE_INFO, "Already sent alert [%s] to user [%llu] today", subject, user_idnr);
		g_free(userchar);
		g_free(handle);
		g_free(tmp);
		return 0;
	} else {
		TRACE(TRACE_INFO, "Sending alert [%s] to user [%llu]", subject, user_idnr);
		db_replycache_register(userchar, "send_alert", handle);
		g_free(userchar);
		g_free(handle);
		g_free(tmp);
	}

	// From the Postmaster.
	if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
		TRACE(TRACE_MESSAGE, "no config value for POSTMASTER");
	}
	if (strlen(postmaster))
		from = postmaster;
	else
		from = DEFAULT_POSTMASTER;

	// Set the \Flagged flag.
	memset(msgflags, 0, IMAP_NFLAGS);
	msgflags[IMAP_FLAG_FLAGGED] = 1;

	// Get the user's login name.
	char *to = auth_get_userid(user_idnr);

	new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, subject, body);

	// Pre-insert the message and get a new_message->id
	dbmail_message_store(new_message);
	u64_t tmpid = new_message->id;

	if (sort_deliver_to_mailbox(new_message, user_idnr,
			"INBOX", BOX_BRUTEFORCE, msgflags) != DSN_CLASS_OK) {
		TRACE(TRACE_ERROR, "Unable to deliver alert [%s] to user [%llu]", subject, user_idnr);
	}

	g_free(to);
	db_delete_message(tmpid);
	dbmail_message_free(new_message);

	return 0;
}

