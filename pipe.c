/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * (c) 2000-2002 IC&S, The Netherlands 
 *
 * Functions for reading the pipe from the MTA */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"
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

#define AUTO_NOTIFY_SENDER "autonotify@dbmail"
#define AUTO_NOTIFY_SUBJECT "NEW MAIL NOTIFICATION"

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

#define DBMAIL_DELIVERY_USERNAME "__@!internal_delivery_user!@__"
#define DBMAIL_TEMPMBOX "INBOX"

extern struct list smtpItems, sysItems;

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

	GetConfigValue("SENDMAIL", &smtpItems, sendmail);
	if (sendmail[0] == '\0')
		trace(TRACE_FATAL,
		      "send_notification(): SENDMAIL not configured (see config file). Stop.");

	trace(TRACE_DEBUG,
	      "send_notification(): found sendmail command to be [%s]",
	      sendmail);


	sendmail_command = (char *) my_malloc(strlen((char *) (to)) + strlen(sendmail) + 2);	/* +2 for extra space and \0 */
	if (!sendmail_command) {
		trace(TRACE_ERROR, "send_notification(): out of memory");
		return -1;
	}

	trace(TRACE_DEBUG, "send_notification(): allocated memory for"
	      " external command call");
	sprintf(sendmail_command, "%s %s", sendmail, to);

	trace(TRACE_INFO, "send_notification(): opening pipe to command "
	      "%s", sendmail_command);


	if (!(mailpipe = popen(sendmail_command, "w"))) {
		trace(TRACE_ERROR,
		      "send_notification(): could not open pipe to sendmail using cmd [%s]",
		      sendmail);
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

	GetConfigValue("SENDMAIL", &smtpItems, sendmail);
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
	    (char *) my_malloc(strlen((send_address) + 1)
			       * 2 * sizeof(char));
	i = 0;
	j = 0;
	/* get all characters from send_address, and escape every ' */
	while (i < (strlen(send_address) + 1)) {
		if (send_address[i] == '\'')
			escaped_send_address[j++] = '\\';
		escaped_send_address[j++] = send_address[i++];
	}
	snprintf(comm, MAX_COMM_SIZE, "%s '%s'", sendmail,
		 escaped_send_address);

	if (!(mailpipe = popen(comm, "w"))) {
		trace(TRACE_ERROR,
		      "send_reply(): could not open pipe to sendmail using cmd [%s]",
		      comm);
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
	GetConfigValue("AUTO_NOTIFY", &smtpItems, val);
	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	GetConfigValue("AUTO_REPLY", &smtpItems, val);
	if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	if (do_auto_notify) {
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
				send_notification(notify_address,
						  AUTO_NOTIFY_SENDER,
						  AUTO_NOTIFY_SUBJECT);
				my_free(notify_address);
			}
		}
	}

	if (do_auto_reply) {
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
				send_reply(headerfields, reply_body);
				my_free(reply_body);
			}
		}
	}

	return 0;
}


/* read from instream, but simply discard all input! */
void discard_client_input(FILE * instream)
{
	char *tmpline;

	memtst((tmpline = (char *) my_malloc(MAX_LINE_SIZE + 1)) == NULL);
	while (!feof(instream)) {
		fgets(tmpline, MAX_LINE_SIZE, instream);

		if (!tmpline)
			break;

		trace(TRACE_DEBUG, "%s,%s: tmpline = [%s]", __FILE__,
		      __FUNCTION__, tmpline);
		if (strcmp(tmpline, ".\r\n") == 0)
			break;
	}
	my_free(tmpline);
}

/* Read from insteam until eof, and store to the
 * dedicated dbmail user account. Later, we'll
 * read the message back for forwarding and 
 * sorting for local users before db_copymsg()'ing
 * it into their own mailboxes.
 *
 * returns a message id number, or -1 on error.
 * */
static int store_message_temp(FILE * instream,
			      char *header, u64_t headersize,
			      u64_t headerrfcsize, u64_t * msgsize,
			      u64_t * rfcsize, u64_t * temp_message_idnr)
{
	int myeof = 0;
	u64_t msgidnr = 0;
	size_t i = 0, usedmem = 0, linemem = 0;
	u64_t totalmem = 0, rfclines = 0;
	char *strblock = NULL, *tmpline = NULL;
	char unique_id[UID_SIZE];
	u64_t messageblk_idnr;
	u64_t user_idnr;
	int result;

	result = auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr);
	if (result < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user " "[%s]\n",
		      __FILE__, __FUNCTION__, DBMAIL_DELIVERY_USERNAME);
		return -1;
	}
	if (result == 0) {
		trace(TRACE_ERROR,
		      "%s,%s: unable to find user_idnr for user "
		      "[%s]. Make sure this system user is in the database!\n",
		      __FILE__, __FUNCTION__, DBMAIL_DELIVERY_USERNAME);
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
		(header, headersize, msgidnr, &messageblk_idnr)) {
	case -1:
		trace(TRACE_ERROR,
		      "store_message_temp(): error inserting msgblock [header]");
		return -1;
	}

	trace(TRACE_DEBUG,
	      "store_message_temp(): allocating [%ld] bytes of memory for readblock",
	      READ_BLOCK_SIZE);

	memtst((strblock =
		(char *) my_malloc(READ_BLOCK_SIZE + 1)) == NULL);
	memset((void *) strblock, '\0', READ_BLOCK_SIZE + 1);
	memtst((tmpline = (char *) my_malloc(MAX_LINE_SIZE + 1)) == NULL);
	memset((void *) tmpline, '\0', MAX_LINE_SIZE + 1);

	while ((!feof(instream) && (!myeof)) || (linemem != 0)) {
		/* Copy the line that didn't fit before */
		if (linemem > 0) {
			strncpy(strblock, tmpline, linemem);
			usedmem += linemem;

			/* Resetting strlen for tmpline */
			tmpline[0] = '\0';
			linemem = 0;
		}

		/* We want to fill up each block if possible,
		 * unless of course we're at the end of the file */
		while (!feof(instream)
		       && (usedmem + linemem < READ_BLOCK_SIZE)) {
			fgets(tmpline, MAX_LINE_SIZE, instream);
			linemem = strlen(tmpline);
			/* The RFC size assumes all lines end in \r\n,
			 * so if we have a newline (\n) but don't have
			 * a carriage return (\r), count it in rfcsize. */
			if (linemem > 0 && tmpline[linemem - 1] == '\n')
				if (linemem == 1
				    || (linemem > 1
					&& tmpline[linemem - 2] != '\r'))
					rfclines++;

			if (ferror(instream)) {
				trace(TRACE_ERROR,
				      "store_message_temp(): error on instream: [%s]",
				      strerror(errno));
				/* FIXME: Umm, don't we need to free a few things?! */
				return -1;
			}

			/* This should be the one and only valid
			 * end to a message over SMTP/LMTP...
			 * FIXME: If there's a compatibility problem, it's probably here! */
			if (strcmp(tmpline, ".\r\n") == 0) {
				/* This is the end of the message! */
				myeof = 1;
				linemem = 0;
				break;
			} else {
				/* See if the line fits into this block */
				if (usedmem + linemem < READ_BLOCK_SIZE) {
					strncpy(strblock + usedmem,
						tmpline, linemem);
					usedmem += linemem;

					/* Resetting strlen for tmpline */
					tmpline[0] = '\0';
					linemem = 0;
				}
				/* Don't need an else, see above this while loop for more */
			}
		}

		/* replace all errorneous '\0' by ' ' (space) */
		for (i = 0; i < usedmem; i++) {
			if (strblock[i] == '\0') {
				strblock[i] = ' ';
			}
		}

		/* fread won't do this for us! */
		strblock[usedmem] = '\0';

		if (usedmem > 0) {	/* usedmem is 0 with an EOF */
			totalmem += usedmem;

			switch (db_insert_message_block
				(strblock, usedmem, msgidnr,
				 &messageblk_idnr)) {
			case -1:
				trace(TRACE_STOP,
				      "store_message_temp(): error inserting msgblock");
				return -1;
			}
		}

		/* resetting strlen for strblock */
		strblock[0] = '\0';
		usedmem = 0;
	}

	trace(TRACE_DEBUG, "store_message_temp(): end of instream");

	my_free(tmpline);
	trace(TRACE_DEBUG, "store_message_temp(): tmpline freed");

	my_free(strblock);
	trace(TRACE_DEBUG, "store_message_temp(): strblock freed");

	db_update_message(msgidnr, unique_id, (totalmem + headersize),
			  (totalmem + rfclines + headerrfcsize));

	/* Pass the message id out to the caller. */
	*temp_message_idnr = msgidnr;
	*rfcsize = totalmem + rfclines + headerrfcsize;
	*msgsize = totalmem + headersize;

	return 0;
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
int insert_messages(FILE * instream, char *header, u64_t headersize,
		    u64_t headerrfcsize, struct list *headerfields,
		    struct list *dsnusers, struct list *returnpath)
{
	struct element *element, *ret_path;
	u64_t msgsize, rfcsize, tmpmsgidnr;

	/* Read in the rest of the stream and store it into a temporary message */
	switch (store_message_temp
		(instream, header, headersize, headerrfcsize, &msgsize,
		 &rfcsize, &tmpmsgidnr)) {
	case -1:
		/* Major trouble. Bail out immediately. */
		trace(TRACE_ERROR,
		      "%s, %s: failed to store temporary message.",
		      __FILE__, __FUNCTION__);
		return -1;
	default:
		trace(TRACE_DEBUG, "%s, %s: temporary msgidnr is [%llu]",
		      __FILE__, __FUNCTION__, tmpmsgidnr);
		break;
	}

	/* Loop through the users list. */
	for (element = list_getstart(dsnusers); element != NULL;
	     element = element->nextnode) {
		struct element *userid_elem;
		int has_2 = 0, has_4 = 0, has_5 = 0;
		deliver_to_user_t *delivery =
		    (deliver_to_user_t *) element->data;

		/* Each user may have a list of user_idnr's for local delivery. */
		for (userid_elem = list_getstart(delivery->userids);
		     userid_elem != NULL;
		     userid_elem = userid_elem->nextnode) {
			u64_t useridnr = *(u64_t *) userid_elem->data;
			trace(TRACE_DEBUG,
			      "%s, %s: calling sort_and_deliver for useridnr [%llu]",
			      __FILE__, __FUNCTION__, useridnr);

			switch (sort_and_deliver(tmpmsgidnr,
						 header, headersize,
						 msgsize, rfcsize,
						 useridnr,
						 delivery->mailbox)) {
			case DSN_CLASS_OK:
				/* Indicate success. */
				trace(TRACE_DEBUG,
				      "%s, %s: successful sort_and_deliver for useridnr [%llu]",
				      __FILE__, __FUNCTION__, useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				/* Indicate permanent failure. */
				trace(TRACE_ERROR,
				      "%s, %s: permanent failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __FUNCTION__, useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_TEMP:
			case -1:
			default:
				/* Assume a temporary failure */
				trace(TRACE_ERROR,
				      "%s, %s: temporary failure sort_and_deliver for useridnr [%llu]",
				      __FILE__, __FUNCTION__, useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			execute_auto_ran(useridnr, headerfields);
		}		/* from: the useridnr for loop */

		switch (dsnuser_worstcase_int(has_2, has_4, has_5)) {
		case DSN_CLASS_OK:
			delivery->dsn.class = DSN_CLASS_OK;	/* Success. */
			delivery->dsn.subject = 1;	/* Address related. */
			delivery->dsn.detail = 5;	/* Valid. */
			break;
		case DSN_CLASS_TEMP:
			delivery->dsn.class = DSN_CLASS_TEMP;	/* Temporary transient failure. */
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
			forward(tmpmsgidnr, delivery->forwards,
				(ret_path ? ret_path->
				 data : "DBMAIL-MAILER"), header,
				headersize);
		}

	}			/* from: the delivery for loop */

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	db_delete_message(tmpmsgidnr);
	trace(TRACE_DEBUG,
	      "insert_messages(): temporary message deleted from database");

	trace(TRACE_DEBUG, "insert_messages(): End of function");

	return 0;
}
