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

#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
#define MAX_U64_STRINGSIZE 40
#define MAX_COMM_SIZE 512
#define RING_SIZE 6

#define AUTO_NOTIFY_SENDER "autonotify@dbmail"
#define AUTO_NOTIFY_SUBJECT "NEW MAIL NOTIFICATION"

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

#define DBMAIL_TEMPMBOX "INBOX"

/**
 * store a messagebody (without headers in one or more blocks in the database
 * \param message the message
 * \param message_size size of message
 * \param msgidnr idnr of message
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
static int store_message_in_blocks(const char* message,
				   u64_t message_size,
				   u64_t msgidnr);

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

	if (GetConfigValue("SENDMAIL", "SMTP", sendmail) < 0) 
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

	sendmail_command = (char *) my_malloc(sendmail_command_maxlen *
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
		my_free(sendmail_command);
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
	my_free(sendmail_command);
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

	if (GetConfigValue("SENDMAIL", "SMTP", sendmail) < 0)
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
		} else if (strcasecmp(record->field, "delivered-to") == 0) {
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
		(char *) my_malloc((strlen(send_address) + 1)
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
		my_free(escaped_send_address);
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
	my_free(escaped_send_address);
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
	if (GetConfigValue("AUTO_NOTIFY", "SMTP", val) < 0)
		trace(TRACE_FATAL, "%s,%s error getting config",
		      __FILE__, __func__);

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (GetConfigValue("AUTO_REPLY", "SMTP", val) < 0)
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
					my_free(notify_address);
					return -1;
				}
				my_free(notify_address);
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
					my_free(reply_body);
					return -1;
				}
				my_free(reply_body);
				
			}
		}
	}

	return 0;
}


/* read from instream, but simply discard all input! */
int discard_client_input(FILE * instream)
{
	char *tmpline;

	tmpline = (char *) my_malloc(MAX_LINE_SIZE + 1);
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
	my_free(tmpline);
	return 0;
}

/**
 * store a temporary copy of a message.
 * \param header the header to the message
 * \param body body of the message
 * \param headersize size of header
 * \param bodysize size of body
 * \param rfcsize rfc size of message
 * \param[out] temp_message_idnr message idnr of temporary message
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
static int store_message_temp(const char *header, const char *body, 
			      u64_t headersize,
			      u64_t bodysize, u64_t rfcsize,
			      /*@out@*/ u64_t * temp_message_idnr)
{
	int result;
	u64_t user_idnr;
	u64_t msgidnr;
	u64_t messageblk_idnr;
	char unique_id[UID_SIZE];
	
	result = auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr);
	if (result < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user " "[%s]\n",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
	}
	if (result == 0) {
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user "
		      "[%s]. Make sure this system user is in the database!\n",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		return -1;
	}
	create_unique_id(unique_id, user_idnr);
	/* create a message record */
	switch (db_insert_message(user_idnr, DBMAIL_TEMPMBOX,
				  CREATE_IF_MBOX_NOT_FOUND, unique_id,
				  &msgidnr)) {
	case -1:
		trace(TRACE_ERROR,
		      "store_message_temp(): returned -1, aborting");
		return -1;
	}

	switch (db_insert_message_block
		(header, headersize, msgidnr, &messageblk_idnr,1)) {
	case -1:
		trace(TRACE_ERROR,
		      "store_message_temp(): error inserting msgblock [header]");
		return -1;
	}
	trace(TRACE_DEBUG,
	      "store_message_temp(): allocating [%ld] bytes of memory "
	      "for readblock", READ_BLOCK_SIZE);
	
	/* store body in several blocks (if needed */
	if (store_message_in_blocks(body, bodysize, msgidnr) < 0) {
		trace(TRACE_STOP,
		      "store_message_temp(): db_insert_message_block "
		      "failed");
		return -1;
	}

	if (db_update_message(msgidnr, unique_id, (headersize + bodysize), rfcsize) < 0) {
		trace(TRACE_ERROR, "%s,%s: error updating message [%llu]",
		      __FILE__, __func__, msgidnr);
		return -1;
	}

	*temp_message_idnr = msgidnr;
	return 1;
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
int insert_messages(const char *header, const char* body, u64_t headersize,
		    u64_t bodysize, u64_t rfcsize,
		    struct list *headerfields,
		    struct list *dsnusers, struct list *returnpath)
{
	struct element *element, *ret_path;
	u64_t msgsize, tmpmsgidnr;

	msgsize = headersize + bodysize;

	/* first start a new database transaction */
	if (db_begin_transaction() < 0) {
		trace(TRACE_ERROR, "%s,%s: error executing "
		      "db_begin_transaction(). aborting delivery...",
		      __FILE__, __func__);
		return -1;
	}

	switch (store_message_temp
		(header, body, headersize, 
		 bodysize, rfcsize, &tmpmsgidnr)) {
	case -1:
		/* Major trouble. Bail out immediately. */
		trace(TRACE_ERROR,
		      "%s, %s: failed to store temporary message.",
		      __FILE__, __func__);
		db_rollback_transaction();
		return -1;
	default:
		trace(TRACE_DEBUG, "%s, %s: temporary msgidnr is [%llu]",
		      __FILE__, __func__, tmpmsgidnr);
		break;
	}

	/* Loop through the users list. */
	for (element = list_getstart(dsnusers); element != NULL;
	     element = element->nextnode) {
		struct element *userid_elem;
		int has_2 = 0, has_4 = 0, has_5 = 0;
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

			switch (sort_and_deliver(tmpmsgidnr,
						 header, headersize,
						 msgsize, rfcsize,
						 useridnr,
						 delivery->mailbox)) {
			case DSN_CLASS_OK:
				/* Indicate success. */
				trace(TRACE_DEBUG,
				      "%s, %s: successful sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				/* Indicate permanent failure. */
				trace(TRACE_ERROR,
				      "%s, %s: permanent failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __func__, useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_TEMP:
			case -1:
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

		switch (dsnuser_worstcase_int(has_2, has_4, has_5)) {
		case DSN_CLASS_OK:
			delivery->dsn.class = DSN_CLASS_OK;	/* Success. */
			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 5;	/* Valid. */
			break;
		case DSN_CLASS_TEMP:
			/* this following statement seems a bit dirty.. If 
			 * this is not used the MTA will always receive a 
			 * TEMP_FAIL messages, even when the only action 
			 * that is taken is to forward to an external address*/
			if ((has_4 == 0) && 
			    (list_totalnodes(delivery->forwards) > 0)) 
				delivery->dsn.class = DSN_CLASS_OK;
			else
				/* Temporary transient failure. */
				delivery->dsn.class = DSN_CLASS_TEMP;

			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 5;	/* Valid. */
			break;
		case DSN_CLASS_FAIL:
			delivery->dsn.class = DSN_CLASS_FAIL;	/* Permanent failure. */
			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 1;	/* Does not exist. */
			break;
		}

		trace(TRACE_DEBUG,
		      "insert_messages(): we need to deliver [%ld] "
		      "messages to external addresses",
		      list_totalnodes(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (list_totalnodes(delivery->forwards) > 0) {

			trace(TRACE_DEBUG,
			      "insert_messages(): delivering to external addresses");

			/* Only the last step of the returnpath is used. */
			ret_path = list_getstart(returnpath);

			/* Forward using the temporary stored message. */
			if (forward(tmpmsgidnr, delivery->forwards,
				(ret_path ? ret_path->
				 data : "DBMAIL-MAILER"), header,
				headersize) < 0)
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
	if (db_delete_message(tmpmsgidnr) < 0) 
		trace(TRACE_ERROR, "%s,%s: failed to delete temporary message "
		      "[%llu]", __FILE__, __func__, tmpmsgidnr);
	trace(TRACE_DEBUG,
	      "insert_messages(): temporary message deleted from database");

	trace(TRACE_DEBUG, "insert_messages(): End of function");

	/* if committing the transaction fails, a rollback is performed */
	if (db_commit_transaction() < 0) 
		return -1;

	return 0;
}
