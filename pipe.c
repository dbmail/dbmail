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

/* $Id: pipe.c 2126 2006-05-22 10:21:59Z paul $
 *
 * Functions for reading the pipe from the MTA */

#include "dbmail.h"


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

// Send only certain parts of the message.
#define SENDNOTHING     0
#define SENDHEADERS     1
#define SENDBODY        2
#define SENDRAW         4
// Use the system sendmail binary.
#define SENDMAIL        NULL

/* Sends a message. */
static int send_mail(struct DbmailMessage *message,
		const char *to, const char *from, const char *subject,
		const char *headers, const char *body,
		int sendwhat, char *sendmail_external)
{
	FILE *mailpipe = NULL;
	char *escaped_to = NULL;
	char *escaped_from = NULL;
	char *sendmail_command = NULL;
	field_t sendmail;
	int result;

	if (config_get_value("SENDMAIL", "DBMAIL", sendmail) < 0) {
		trace(TRACE_ERROR,
			"%s, %s: error getting value for SENDMAIL in DBMAIL section of dbmail.conf.",
			__FILE__, __func__);
		return -1;
	}

	if (strlen(sendmail) < 1) {
		trace(TRACE_ERROR, "%s, %s: SENDMAIL not set in DBMAIL section of dbmail.conf.",
			__FILE__, __func__);
		return -1;
	}

	trace(TRACE_DEBUG, "%s, %s: sendmail command is [%s]",
		__FILE__, __func__, sendmail);
	
	if (! (escaped_to = dm_shellesc(to))) {
		trace(TRACE_ERROR, "%s, %s: out of memory calling dm_shellesc",
				__FILE__, __func__);
		return -1;
	}

	if (! (escaped_from = dm_shellesc(from))) {
		trace(TRACE_ERROR, "%s, %s: out of memory calling dm_shellesc",
				__FILE__, __func__);
		return -1;
	}

	if (!sendmail_external) {
		sendmail_command = g_strconcat(sendmail, " -f ", escaped_from, " ", escaped_to, NULL);
		dm_free(escaped_to);
		if (!sendmail_command) {
			trace(TRACE_ERROR, "%s, %s: out of memory calling g_strconcat",
					__FILE__, __func__);
			return -1;
		}
	} else {
		sendmail_command = sendmail_external;
	}

	trace(TRACE_INFO, "%s, %s: opening pipe to [%s]",
		__FILE__, __func__, sendmail_command);

	if (!(mailpipe = popen(sendmail_command, "w"))) {
		trace(TRACE_ERROR, "%s, %s: could not open pipe to sendmail",
			__FILE__, __func__);
		g_free(sendmail_command);
		return 1;
	}

	trace(TRACE_DEBUG, "%s, %s: pipe opened", __FILE__, __func__);

	if (sendwhat != SENDRAW) {
		fprintf(mailpipe, "To: %s\n", to);
		fprintf(mailpipe, "From: %s\n", from);
		fprintf(mailpipe, "Subject: %s\n", subject);
		if (strlen(headers))
			fprintf(mailpipe, "%s\n", headers);
		fprintf(mailpipe, "\n");
		if (strlen(body))
			fprintf(mailpipe, "%s\n\n", body);
	}

	switch (sendwhat) {
	case SENDRAW:
		// This is a hack so forwards can give a From line.
		if (strlen(headers))
			fprintf(mailpipe, "%s\n", headers);
		db_send_message_lines(mailpipe, message->id, -2, 1);
		break;
	case SENDBODY:
		// Get the message body from message.
		// FIXME: This will break mime messages,
		// so before anybody starts consuming this
		// part of the function, please realize this!
		fprintf(mailpipe, "%s\n",
			dbmail_message_body_to_string(message));
		break;
	case SENDHEADERS:
		// Get the headers from message.
		fprintf(mailpipe, "%s\n",
			dbmail_message_hdrs_to_string(message));
		break;
	case SENDNOTHING:
	default:
		// Just like it says: nothing.
		break;
	}

	result = pclose(mailpipe);
	trace(TRACE_DEBUG, "%s, %s: pipe closed", __FILE__, __func__);

	if (result != 0) {
		trace(TRACE_ERROR, "%s, %s: sendmail error [%d]",
			__FILE__, __func__, result);

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
		trace(TRACE_ERROR, "%s, %s: both To and From addresses must be specified",
			__FILE__, __func__);
		return -1;
	}

	return send_mail(message, to, from, "", "", "", SENDRAW, SENDMAIL);
}

int send_forward_list(struct DbmailMessage *message,
		struct dm_list *targets, const char *from)
{
	int result = 0;
	struct element *target;
	field_t postmaster;

	trace(TRACE_INFO, "%s, %s: delivering to [%ld] external addresses",
	      __FILE__, __func__, dm_list_length(targets));

	if (!from) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			trace(TRACE_MESSAGE, "%s, %s: no config value for POSTMASTER",
			      __FILE__, __func__);
		}
		if (strlen(postmaster))
			from = dm_strdup(postmaster);
		else
			from = dm_strdup(DEFAULT_POSTMASTER);
	}

	target = dm_list_getstart(targets);
	while (target != NULL) {
		char *to = (char *)target->data;

		if (!to || strlen(to) < 1) {
			trace(TRACE_ERROR, "%s, %s: forwarding address is zero length,"
					" message not forwarded.",
					__FILE__, __func__);
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
                        
				trace(TRACE_DEBUG, "%s, %s: prepending mbox style From "
				      "header to pipe returnpath: %s",
				      __FILE__, __func__, from);
                        
				/* Format: From<space>address<space><space>Date */
				fromline = g_strconcat("From ", from, "  ", timestr, NULL);

				result |= send_mail(message, "", "", "", fromline, "", SENDRAW, to+1);
				g_free(fromline);
			} else if (to[0] == '|') {
				// The forward is a command to execute.
				result |= send_mail(message, "", "", "", "", "", SENDRAW, to+1);

			} else {
				// The forward is an email address.
				result |= send_mail(message, to, from, "", "", "", SENDRAW, SENDMAIL);
			}
		}

		target = target->nextnode;
	}

	return result;
}

/* 
 * Send an automatic notification.
 */
static int send_notification(struct DbmailMessage *message, const char *to)
{
	field_t from = "";
	field_t subject = "";

	if (config_get_value("POSTMASTER", "DBMAIL", from) < 0) {
		trace(TRACE_MESSAGE, "%s, %s: no config value for POSTMASTER",
		      __FILE__, __func__);
	}

	if (config_get_value("AUTO_NOTIFY_SENDER", "DELIVERY", from) < 0) {
		trace(TRACE_MESSAGE, "%s, %s: no config value for AUTO_NOTIFY_SENDER",
		      __FILE__, __func__);
	}

	if (config_get_value("AUTO_NOTIFY_SUBJECT", "DELIVERY", subject) < 0) {
		trace(TRACE_MESSAGE, "%s, %s: no config value for AUTO_NOTIFY_SUBJECT",
		      __FILE__, __func__);
	}

	if (strlen(from) < 1)
		g_strlcpy(from, AUTO_NOTIFY_SENDER, FIELDSIZE);

	if (strlen(subject) < 1)
		g_strlcpy(subject, AUTO_NOTIFY_SUBJECT, FIELDSIZE);

	return send_mail(message, to, from, subject,
			"", "", SENDNOTHING, SENDMAIL);
}

/*
 * Send a vacation message. This should provide MIME
 * support, to comply with the Sieve-Vacation spec.
 */
int send_vacation(struct DbmailMessage *message,
		const char *to, const char *from,
		const char *subject, const char *body)
{
	return send_mail(message, to, from, subject, 
			"", body, SENDNOTHING, SENDMAIL);
	return 0;
}
	
/*
 * Send an automatic reply.
 */
#define REPLY_DAYS 7
static int send_reply(struct DbmailMessage *message, const char *body)
{
	char *from = NULL, *to = NULL, *replyto = NULL, *subject = NULL;
	char *escaped_send_address;
	char *x_dbmail_reply;
	field_t postmaster;

	InternetAddressList *ialist;
	InternetAddress *ia;

	x_dbmail_reply = dbmail_message_get_header(message, "X-Dbmail-Reply");
	if (x_dbmail_reply) {
		trace(TRACE_ERROR, "%s, %s: reply loop detected [%s]",
				__FILE__, __func__, x_dbmail_reply);
		dm_free(x_dbmail_reply);
		return 0;
	}
	
	from = dbmail_message_get_header(message, "From");
	subject = dbmail_message_get_header(message, "Subject");
	replyto = dbmail_message_get_header(message, "Reply-To");

	/* We prefer the actual Delivered-To, rather than To
	 * because that probably came to us over the wire. */
	to = dbmail_message_get_header(message, "Delivered-To");
	if (!to)
		to = dbmail_message_get_header(message, "To");
	if (!to) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			trace(TRACE_MESSAGE, "%s, %s: no config value for POSTMASTER",
			      __FILE__, __func__);
		}
		if (strlen(postmaster))
			to = dm_strdup(postmaster);
		else
			to = dm_strdup(DEFAULT_POSTMASTER);
	}
	

	if (!from && !replyto) {
		trace(TRACE_ERROR, "%s, %s: no address to send to", __FILE__, __func__);
		return 0;
	}

	if (!valid_sender(from)) {
		trace(TRACE_DEBUG, "%s, %s: sender invalid. skip auto-reply.",
				__FILE__, __func__);
		dm_free(from);
		return 0;
	}

	ialist = internet_address_parse_string(replyto ? replyto : from);
	ia = ialist->address;
	escaped_send_address = internet_address_to_string(ia, TRUE);
	internet_address_list_destroy(ialist);

	if (db_replycache_validate(to, escaped_send_address,
		"replycache", REPLY_DAYS) != DM_SUCCESS) {
		trace(TRACE_DEBUG, "%s, %s: skip auto-reply", 
				__FILE__, __func__);
		dm_free(to);
		dm_free(from);
		dm_free(subject);
		dm_free(replyto);
		return 0;
	}

	char *newsubject = g_strconcat("Re: ", subject, NULL);
	char *headers = g_strconcat("X-Dbmail-Reply: ", escaped_send_address, NULL);

	/* Our 'to' is in the 'from' arg because it's a reply. */
	if (!send_mail(message, escaped_send_address,
			to, subject, headers, body, SENDNOTHING, SENDMAIL)) {
		db_replycache_register(to, escaped_send_address, "replycache");
	}

	dm_free(to);
	dm_free(from);
	dm_free(replyto);
	dm_free(subject);
	dm_free(newsubject);

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
		trace(TRACE_ERROR, "%s, %s: error getting config value for AUTO_NOTIFY",
		      __FILE__, __func__);
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (config_get_value("AUTO_REPLY", "DELIVERY", val) < 0) {
		trace(TRACE_ERROR, "%s, %s: error getting config value for AUTO_REPLY",
		      __FILE__, __func__);
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	if (do_auto_notify != 0) {
		trace(TRACE_DEBUG,
		      "execute_auto_ran(): starting auto-notification procedure");

		if (db_get_notify_address(useridnr, &notify_address) != 0)
			trace(TRACE_ERROR,
			      "execute_auto_ran(): error fetching notification address");
		else {
			if (notify_address == NULL)
				trace(TRACE_DEBUG,
				      "execute_auto_ran(): no notification address specified, skipping");
			else {
				trace(TRACE_DEBUG,
				      "execute_auto_ran(): sending notifcation to [%s]",
				      notify_address);
				if (send_notification(message, notify_address) < 0) {
					trace(TRACE_ERROR, "%s, %s: error in call to send_notification.",
					      __FILE__, __func__);
					dm_free(notify_address);
					return -1;
				}
				dm_free(notify_address);
			}
		}
	}

	if (do_auto_reply != 0) {
		trace(TRACE_DEBUG,
		      "execute_auto_ran(): starting auto-reply procedure");

		if (db_get_reply_body(useridnr, &reply_body) != 0)
			trace(TRACE_ERROR,
			      "execute_auto_ran(): error fetching reply body");
		else {
			if (reply_body == NULL || reply_body[0] == '\0')
				trace(TRACE_DEBUG,
				      "execute_auto_ran(): no reply body specified, skipping");
			else {
				if (send_reply(message, reply_body) < 0) {
					trace(TRACE_ERROR, "%s, %s: error in call to send_reply",
					      __FILE__, __func__);
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
		trace(TRACE_DEBUG, "%s, %s: inserting message: %s",
		      __FILE__, __func__, &message[offset]);
		if (db_insert_message_block(&message[offset],
					    block_size, msgidnr,
					    &tmp_messageblk_idnr,0) < 0) {
			trace(TRACE_ERROR, "%s, %s: "
			      "db_insert_message_block() failed",
			      __FILE__, __func__);
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
		trace(TRACE_ERROR, "%s, %s: error executing "
		      "db_begin_transaction(). aborting delivery...",
		      __FILE__, __func__);
		return -1;
	}

	switch (dbmail_message_store(message)) {
	case -1:
		trace(TRACE_ERROR, "%s, %s: failed to store temporary message.",
		      __FILE__, __func__);
		db_rollback_transaction();
		return -1;
	default:
		trace(TRACE_DEBUG, "%s, %s: temporary msgidnr is [%llu]",
		      __FILE__, __func__, message->id);
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
			trace(TRACE_DEBUG, "%s, %s: calling sort_and_deliver for useridnr [%llu]",
			      __FILE__, __func__, useridnr);

			switch (sort_and_deliver(message,
					delivery->address, useridnr,
					delivery->mailbox, delivery->source)) {
			case DSN_CLASS_OK:
				/* Indicate success. */
				trace(TRACE_DEBUG, "%s, %s: successful sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				/* Indicate permanent failure. */
				trace(TRACE_ERROR, "%s, %s: permanent failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_QUOTA:
			/* Indicate over quota. */
				trace(TRACE_ERROR, "%s, %s: temporary failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_5_2 = 1;
				break;
			case DSN_CLASS_TEMP:
			default:
				/* Assume a temporary failure */
				trace(TRACE_ERROR, "%s, %s: temporary failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			if (execute_auto_ran(message, useridnr) < 0) {
				trace(TRACE_ERROR, "%s, %s: error in execute_auto_ran(),"
					" but continuing delivery normally.",
				      __FILE__, __func__);
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

		trace(TRACE_DEBUG, "%s, %s: deliver [%ld] messages to external addresses", 
				__FILE__, __func__, dm_list_length(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (dm_list_length(delivery->forwards) > 0) {

			trace(TRACE_DEBUG, "%s, %s: delivering to external addresses",
					__FILE__, __func__);

			/* Forward using the temporary stored message. */
			if (send_forward_list(message, delivery->forwards,
					dbmail_message_get_header(message, "Return-Path")) < 0)
				/* FIXME: if forward fails, we should do something 
				 * sensible. Currently, the message is just black-
				 * holed! */
				trace(TRACE_ERROR, "%s, %s: forward failed message lost", 
						__FILE__, __func__);
		}
	}			/* from: the delivery for loop */

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	if (db_delete_message(tmpid) < 0) 
		trace(TRACE_ERROR, "%s, %s: failed to delete temporary message [%llu]",
				__FILE__, __func__, message->id);
	trace(TRACE_DEBUG, "%s, %s: temporary message deleted from database. Done.",
			__FILE__, __func__);

	/* if committing the transaction fails, a rollback is performed */
	if (db_commit_transaction() < 0) 
		return -1;

	return 0;
}

