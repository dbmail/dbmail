/*
 Copyright (C) 2004-2006 Aaron Stone aaron@serendipity.cx

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2 of
 the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* 
 *
 * Central switching station for email on its way to be delivered.
 * From here we call out to the sorting module, if applicable, to
 * give additional information on what to do with a message.
 *
 * Upstream: pipe.c, insert_messages calls sort_and_deliver.
 * Way upstream: main.c and lmtp.c call insert_messages.
 *
 */

#include "dbmail.h"
#define THIS_MODULE "sort"

/* Figure out where to deliver the message, then deliver it.
 * */
dsn_class_t sort_and_deliver(struct DbmailMessage *message,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source)
{
	int cancelkeep = 0;
	int reject = 0;
	dsn_class_t ret;
	field_t val;
	char *subaddress = NULL;

	/* Catch the brute force delivery right away.
	 * We skip the Sieve scripts, and down the call
	 * chain we don't check permissions on the mailbox. */
	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_MESSAGE, "Beginning brute force delivery for user [%llu] to mailbox [%s].",
				useridnr, mailbox);
		return sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
	}

	TRACE(TRACE_INFO, "Destination [%s] useridnr [%llu], mailbox [%s], source [%d]",
			destination, useridnr, mailbox, source);
	
	/* This is the only condition when called from pipe.c, actually. */
	if (! mailbox) {
		mailbox = "INBOX";
		source = BOX_DEFAULT;
	}

	/* Subaddress. */
	config_get_value("SUBADDRESS", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		int res;
		size_t sublen, subpos;
		res = find_bounded((char *)destination, '+', '@', &subaddress, &sublen, &subpos);
		if (res == 0 && sublen > 0) {
			/* We'll free this towards the end of the function. */
			mailbox = subaddress;
			source = BOX_ADDRESSPART;
			TRACE(TRACE_INFO, "Setting BOX_ADDRESSPART mailbox to [%s]", mailbox);
		}
	}

	/* Give Sieve access to the envelope recipient. */
	dbmail_message_set_envelope_recipient(message, destination);

	/* Sieve. */
	config_get_value("SIEVE", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0
	&& db_check_sievescript_active(useridnr) == 0) {
		TRACE(TRACE_INFO, "Calling for a Sieve sort");
		sort_result_t *sort_result;
		sort_result = sort_process(useridnr, message);
		if (sort_result) {
			cancelkeep = sort_get_cancelkeep(sort_result);
			reject = sort_get_reject(sort_result);
			sort_free_result(sort_result);
		}
	}

	/* Sieve actions:
	 * (m = must implement, s = should implement, e = extension)
	 * m Keep - implicit default action.
	 * m Discard - requires us to skip the default action.
	 * m Redirect - add to the forwarding list.
	 * s Fileinto - change the destination mailbox.
	 * s Reject - nope, sorry. we killed bounce().
	 * e Vacation - share with the auto reply code.
	 */

	if (cancelkeep) {
		// The implicit keep has been cancelled.
		// This may necessarily imply that the message
		// is being discarded -- dropped flat on the floor.
		ret = DSN_CLASS_OK;
		TRACE(TRACE_INFO, "Keep was cancelled. Message may be discarded.");
	} else {
		ret = sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
		TRACE(TRACE_INFO, "Keep was not cancelled. Message will be delivered by default.");
	}

	/* Might have been allocated by the subaddress calculation. NULL otherwise. */
	g_free(subaddress);

	/* Reject probably implies cancelkeep,
	 * but we'll not assume that and instead
	 * just test this as a separate block. */
	if (reject) {
		TRACE(TRACE_INFO, "Message will be rejected.");
		ret = DSN_CLASS_FAIL;
	}

	return ret;
}

dsn_class_t sort_deliver_to_mailbox(struct DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source,
		int *msgflags)
{
	u64_t mboxidnr, newmsgidnr;
	field_t val;
	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);

	TRACE(TRACE_INFO,"useridnr [%llu] mailbox [%s]", useridnr, mailbox);

	if (db_find_create_mailbox(mailbox, source, useridnr, &mboxidnr) != 0) {
		TRACE(TRACE_ERROR, "mailbox [%s] not found", mailbox);
		return DSN_CLASS_FAIL;
	}

	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_INFO, "Brute force delivery; skipping ACL checks on mailbox.");
	} else {
		// Check ACL's on the mailbox. It must be read-write,
		// it must not be no_select, and it may require an ACL for
		// the user whose Sieve script this is, since it's possible that
		// we've looked up a #Public or a #Users mailbox.
		TRACE(TRACE_DEBUG, "Checking if we have the right to post incoming messages");
        
		mailbox_t mbox;
		memset(&mbox, '\0', sizeof(mbox));
		mbox.uid = mboxidnr;
		
		switch (acl_has_right(&mbox, useridnr, ACL_RIGHT_POST)) {
		case -1:
			TRACE(TRACE_MESSAGE, "error retrieving right for [%llu] to deliver mail to [%s]",
					useridnr, mailbox);
			return DSN_CLASS_TEMP;
		case 0:
			// No right.
			TRACE(TRACE_MESSAGE, "user [%llu] does not have right to deliver mail to [%s]",
					useridnr, mailbox);
			// Switch to INBOX.
			if (strcmp(mailbox, "INBOX") == 0) {
				// Except if we've already been down this path.
				TRACE(TRACE_MESSAGE, "already tried to deliver to INBOX");
				return DSN_CLASS_FAIL;
			}
			return sort_deliver_to_mailbox(message, useridnr, "INBOX", BOX_DEFAULT, msgflags);
		case 1:
			// Has right.
			TRACE(TRACE_INFO, "user [%llu] has right to deliver mail to [%s]",
					useridnr, mailbox);
			break;
		default:
			TRACE(TRACE_ERROR, "invalid return value from acl_has_right");
			return DSN_CLASS_FAIL;
		}
	}

	// if the mailbox already holds this message we're done
	GETCONFIGVALUE("suppress_duplicates", "DELIVERY", val);
	if (strcasecmp(val,"yes")==0) {
		char *messageid = dbmail_message_get_header(message, "message-id");
		if ( messageid && ((db_mailbox_has_message_id(mboxidnr, messageid)) > 0) ) {
			TRACE(TRACE_MESSAGE, "suppress_duplicate: [%s]", messageid);
			return DSN_CLASS_OK;
		}
	}

	// Ok, we have the ACL right, time to deliver the message.
	switch (db_copymsg(message->id, mboxidnr, useridnr, &newmsgidnr)) {
	case -2:
		TRACE(TRACE_DEBUG, "error copying message to user [%llu],"
				"maxmail exceeded", useridnr);
		return DSN_CLASS_QUOTA;
	case -1:
		TRACE(TRACE_ERROR, "error copying message to user [%llu]", 
				useridnr);
		return DSN_CLASS_TEMP;
	default:
		TRACE(TRACE_MESSAGE, "message id=%llu, size=%zd is inserted", 
				newmsgidnr, msgsize);
		if (msgflags) {
			TRACE(TRACE_MESSAGE, "message id=%llu, setting imap flags", 
				newmsgidnr);
			db_set_msgflag(newmsgidnr, mboxidnr, msgflags, IMAPFA_ADD);
		}
		message->id = newmsgidnr;
		return DSN_CLASS_OK;
	}
}

