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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "forward.h"
#include "sort.h"
#include "dbmail.h"
#include "pipe.h"
#include "debug.h"
#include "misc.h"
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dbmd5.h"
#include "misc.h"
#include "dsn.h"
#include "dbmail-message.h"

#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
#define MAX_U64_STRINGSIZE 40
#define MAX_COMM_SIZE 512
#define RING_SIZE 6

#define AUTO_NOTIFY_SENDER "autonotify@dbmail"
#define AUTO_NOTIFY_SUBJECT "NEW MAIL NOTIFICATION"

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024


/* 
 * Send an automatic notification using sendmail
 */
static int send_notification(const char *to, const char *from,
			     const char *subject)
{
	FILE *mailpipe = NULL;
	char *sendmail_command = NULL;
	field_t sendmail;
	int result;
	size_t sendmail_command_maxlen;

	if (config_get_value("SENDMAIL", "SMTP", sendmail) < 0) 
		trace(TRACE_FATAL,
		      "%s,%s: error getting Config Values",
		      __FILE__, __func__);

	if (sendmail[0] == '\0')
		trace(TRACE_FATAL,
		      "send_notification(): SENDMAIL not configured (see config file). Stop.");

	trace(TRACE_DEBUG,
	      "send_notification(): found sendmail command to be [%s]",
	      sendmail);
	
	sendmail_command_maxlen = strlen((char *) to) + strlen(sendmail) + 2;

	sendmail_command = (char *) dm_malloc(sendmail_command_maxlen *
					      sizeof(char));
	if (!sendmail_command) {
		trace(TRACE_ERROR, "send_notification(): out of memory");
		return -1;
	}

	trace(TRACE_DEBUG, "send_notification(): allocated memory for"
	      " external command call");
	(void) snprintf(sendmail_command, sendmail_command_maxlen,
		"%s %s", sendmail, to);

	trace(TRACE_INFO, "send_notification(): opening pipe to command "
	      "%s", sendmail_command);


	if (!(mailpipe = popen(sendmail_command, "w"))) {
		trace(TRACE_ERROR,
		      "send_notification(): could not open pipe to sendmail using cmd [%s]",
		      sendmail);
		dm_free(sendmail_command);
		return 1;
	}

	trace(TRACE_DEBUG,
	      "send_notification(): pipe opened, sending data");

	fprintf(mailpipe, "To: %s\n", to);
	fprintf(mailpipe, "From: %s\n", from);
	fprintf(mailpipe, "Subject: %s\n", subject);
	fprintf(mailpipe, "\n");

	result = pclose(mailpipe);
	trace(TRACE_DEBUG, "send_notification(): pipe closed");

	if (result != 0)
		trace(TRACE_ERROR,
		      "send_notification(): reply could not be sent: sendmail error");
	dm_free(sendmail_command);
	return 0;
}


/*
 * Send an automatic reply using sendmail
 */
static int send_reply(struct list *headerfields, const char *body)
{
	struct element *el;
	struct mime_record *record;
	char *from = NULL, *to = NULL, *replyto = NULL, *subject = NULL;
	FILE *mailpipe = NULL;
	char *send_address;
	char *escaped_send_address;
	char comm[MAX_COMM_SIZE];
			    /**< command sent to sendmail (needs to escaped) */
	field_t sendmail;
	int result;
	unsigned int i, j;

	if (config_get_value("SENDMAIL", "SMTP", sendmail) < 0)
		trace(TRACE_FATAL,
		      "%s,%s: error getting config",
		      __FILE__, __func__);


	if (sendmail[0] == '\0')
		trace(TRACE_FATAL,
		      "send_reply(): SENDMAIL not configured (see config file). Stop.");

	trace(TRACE_DEBUG,
	      "send_reply(): found sendmail command to be [%s]", sendmail);

	/* find To: and Reply-To:/From: field */
	el = list_getstart(headerfields);

	while (el) {
		record = (struct mime_record *) el->data;

		if (strcasecmp(record->field, "from") == 0) {
			from = record->value;
			trace(TRACE_DEBUG, "send_reply(): found FROM [%s]",
			      from);
		} else if (strcasecmp(record->field, "reply-to") == 0) {
			replyto = record->value;
			trace(TRACE_DEBUG,
			      "send_reply(): found REPLY-TO [%s]",
			      replyto);
		} else if (strcasecmp(record->field, "subject") == 0) {
			subject = record->value;
			trace(TRACE_DEBUG,
			      "send_reply(): found SUBJECT [%s]", subject);
		} else if (strcasecmp(record->field, "deliver-to") == 0) {
			to = record->value;
			trace(TRACE_DEBUG, "send_reply(): found TO [%s]",
			      to);
		}

		el = el->nextnode;
	}

	if (!from && !replyto) {
		trace(TRACE_ERROR, "send_reply(): no address to send to");
		return 0;
	}


	trace(TRACE_DEBUG,
	      "send_reply(): header fields scanned; opening pipe to sendmail");
	send_address = replyto ? replyto : from;
	/* allocate a string twice the size of send_address */
	escaped_send_address =
		(char *) dm_malloc((strlen(send_address) + 1)
				   * 2 * sizeof(char));
	if (!escaped_send_address) {
		trace(TRACE_ERROR, "%s,%s: unable to allocate memory. Memory "
		      "full?", __FILE__, __func__);
		return 0;
	}
	memset(escaped_send_address, '\0', (strlen(send_address) + 1) * 2 * sizeof(char));
	i = 0;
	j = 0;
	/* get all characters from send_address, and escape every ' */
	while (i < (strlen(send_address) + 1)) {
		if (send_address[i] == '\'')
			escaped_send_address[j++] = '\\';
		escaped_send_address[j++] = send_address[i++];
	}
	(void) snprintf(comm, MAX_COMM_SIZE, "%s '%s'", sendmail,
		 escaped_send_address);

	if (!(mailpipe = popen(comm, "w"))) {
		trace(TRACE_ERROR,
		      "send_reply(): could not open pipe to sendmail using cmd [%s]",
		      comm);
		dm_free(escaped_send_address);
		return 1;
	}

	trace(TRACE_DEBUG, "send_reply(): sending data");

	fprintf(mailpipe, "To: %s\n", replyto ? replyto : from);
	fprintf(mailpipe, "From: %s\n", to ? to : "(unknown)");
	fprintf(mailpipe, "Subject: AW: %s\n",
		subject ? subject : "<no subject>");
	fprintf(mailpipe, "\n");
	fprintf(mailpipe, "%s\n", body ? body : "--");

	result = pclose(mailpipe);
	trace(TRACE_DEBUG, "send_reply(): pipe closed");
	if (result != 0)
		trace(TRACE_ERROR,
		      "send_reply(): reply could not be sent: sendmail error");
	dm_free(escaped_send_address);
	return 0;
}


/* Yeah, RAN. That's Reply And Notify ;-) */
static int execute_auto_ran(u64_t useridnr, struct list *headerfields)
{
	field_t val;
	int do_auto_notify = 0, do_auto_reply = 0;
	char *reply_body = NULL;
	char *notify_address = NULL;

	/* message has been succesfully inserted, perform auto-notification & auto-reply */
	if (config_get_value("AUTO_NOTIFY", "SMTP", val) < 0)
		trace(TRACE_FATAL, "%s,%s error getting config",
		      __FILE__, __func__);

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (config_get_value("AUTO_REPLY", "SMTP", val) < 0)
		trace(TRACE_FATAL, "%s,%s error getting config",
		      __FILE__, __func__);

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
				if (send_notification(notify_address,
						      AUTO_NOTIFY_SENDER,
						      AUTO_NOTIFY_SUBJECT) < 0) {
					trace(TRACE_ERROR, "%s,%s: error in call to send_notification.",
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
				if (send_reply(headerfields, reply_body) < 0) {
					trace(TRACE_ERROR, "%s,%s: error in call to send_reply",
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


/* read from instream, but simply discard all input! */
int discard_client_input(FILE * instream)
{
	char *tmpline;

	tmpline = (char *) dm_malloc(MAX_LINE_SIZE + 1);
	if (tmpline == NULL) {
		trace(TRACE_ERROR, "%s,%s: unable to allocate memory.",
		      __FILE__, __func__);
		return -1;
	}
	
	while (!feof(instream)) {
		if (fgets(tmpline, MAX_LINE_SIZE, instream) == NULL)
			break;

		trace(TRACE_DEBUG, "%s,%s: tmpline = [%s]", __FILE__,
		      __func__, tmpline);
		if (strcmp(tmpline, ".\r\n") == 0)
			break;
	}
	dm_free(tmpline);
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
		trace(TRACE_DEBUG, "%s,%s: inserting message: %s",
		      __FILE__, __func__, &message[offset]);
		if (db_insert_message_block(&message[offset],
					    block_size, msgidnr,
					    &tmp_messageblk_idnr,0) < 0) {
			trace(TRACE_ERROR, "%s,%s: "
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
		struct list *headerfields, 
		struct list *dsnusers, 
		struct list *returnpath)
{
	char *header, *body;
	u64_t headersize, bodysize, rfcsize;
	struct element *element, *ret_path;
	u64_t msgsize;

	/* Only the last step of the returnpath is used. */
	if (ret_path = list_getstart(returnpath))
		dbmail_message_set_header(message, "Return-Path", (char *)ret_path->data);

 	delivery_status_t final_dsn;

	/* first start a new database transaction */
	if (db_begin_transaction() < 0) {
		trace(TRACE_ERROR, "%s,%s: error executing "
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

	header = dbmail_message_hdrs_to_string(message);
	body = dbmail_message_body_to_string(message);
	headersize = (u64_t)dbmail_message_get_hdrs_size(message);
	bodysize = (u64_t)dbmail_message_get_body_size(message);
	rfcsize = (u64_t)dbmail_message_get_rfcsize(message);
	msgsize = (u64_t)dbmail_message_get_size(message);


	/* Loop through the users list. */
	for (element = list_getstart(dsnusers); element != NULL;
	     element = element->nextnode) {
		struct element *userid_elem;
		int has_2 = 0, has_4 = 0, has_5 = 0, has_5_2 = 0;
		deliver_to_user_t *delivery =
		    (deliver_to_user_t *) element->data;
		
		/* Each user may have a list of user_idnr's for local
		 * delivery. */
		for (userid_elem = list_getstart(delivery->userids);
		     userid_elem != NULL;
		     userid_elem = userid_elem->nextnode) {
			u64_t useridnr = *(u64_t *) userid_elem->data;
			trace(TRACE_DEBUG,
			      "%s, %s: calling sort_and_deliver for useridnr [%llu]",
			      __FILE__, __func__, useridnr);

			switch (sort_and_deliver(message, useridnr, delivery->mailbox)) {
			case SORT_SUCCESS:
				/* Indicate success. */
				trace(TRACE_DEBUG,
				      "%s, %s: successful sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_2 = 1;
				break;
			case SORT_FAILURE:
				/* Indicate permanent failure. */
				trace(TRACE_ERROR,
				      "%s, %s: permanent failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_5 = 1;
				break;
			case SORT_OVER_QUOTA:
			/* Indicate over quota. */
				trace(TRACE_ERROR,
				      "%s, %s: temporary failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_5_2 = 1;
				break;
			case SORT_WEIRD_ERROR:
			default:
				/* Assume a temporary failure */
				trace(TRACE_ERROR,
				      "%s, %s: temporary failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			if (execute_auto_ran(useridnr, headerfields) < 0)
				trace(TRACE_ERROR, "%s,%s: error in "
				      "execute_auto_ran(), continuing",
				      __FILE__, __func__);
		}		/* from: the useridnr for loop */

		final_dsn = dsnuser_worstcase_int(has_2, has_4, has_5, has_5_2);
		switch (final_dsn.class) {
		case DSN_CLASS_OK:
			delivery->dsn.class = DSN_CLASS_OK;	/* Success. */
			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 5;	/* Valid. */
			break;
		case DSN_CLASS_TEMP:
			/* sort_and_deliver returns TEMP is useridnr is 0, aka,
			 * if nothing was delivered at all, or for any other failures. */	

			/* If there's a problem with the delivery address, but
			 * there are proper forwarding addresses, we're OK. */
			if (list_totalnodes(delivery->forwards) > 0) {
				delivery->dsn.class = DSN_CLASS_OK;
				delivery->dsn.subject = 1;	/* Address related. */
				delivery->dsn.detail = 5;	/* Valid. */
				break;
			}
			/* Fall through to FAIL. */
		case DSN_CLASS_FAIL:
			delivery->dsn.class = DSN_CLASS_FAIL;	/* Permanent failure. */
			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 1;	/* Does not exist. */
			break;
		case DSN_CLASS_QUOTA:
			delivery->dsn.class = DSN_CLASS_FAIL;	/* Permanent failure. */
			delivery->dsn.subject = 2;	/* Mailbox related. */
			delivery->dsn.detail = 2;	/* Over quota limit. */
			break;
		case DSN_CLASS_NONE:
			/* Leave the DSN status at whatever dsnuser_resolve set it at. */
			break;
		}

		trace(TRACE_DEBUG, "insert_messages(): we need to deliver [%ld] "
		      "messages to external addresses", list_totalnodes(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (list_totalnodes(delivery->forwards) > 0) {

			trace(TRACE_DEBUG, "insert_messages(): delivering to external addresses");

			/* Forward using the temporary stored message. */
			if (forward(message->id, delivery->forwards, 
						(ret_path ? ret_path->data : "DBMAIL-MAILER"), 
						header, headersize) < 0)
				/* FIXME: if forward fails, we should do something 
				 * sensible. Currently, the message is just black-
				 * holed! */
				trace(TRACE_ERROR, "%s,%s: forward failed "
				      "message lost", __FILE__, __func__);
		}
	}			/* from: the delivery for loop */

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	if (db_delete_message(message->id) < 0) 
		trace(TRACE_ERROR, "%s,%s: failed to delete temporary message "
		      "[%llu]", __FILE__, __func__, message->id);
	trace(TRACE_DEBUG,
	      "insert_messages(): temporary message deleted from database");

	trace(TRACE_DEBUG, "insert_messages(): End of function");

	/* if committing the transaction fails, a rollback is performed */
	if (db_commit_transaction() < 0) 
		return -1;

	return 0;
}
