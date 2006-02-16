/* Central switching station for email on its
 * way to be delivered. From here we call out
 * to the sorting module, if applicable, to
 * give additional information on what to do
 * with a message.
 *
 * (c) 2004-2006 Aaron Stone <aaron@serendpity.cx>
 *
 * $Id: $
 */

#include "dbmail.h"

/* Figure out where to deliver the message, then deliver it.
 * */
dsn_class_t sort_and_deliver(struct DbmailMessage *message,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source,
		const char *fromaddr UNUSED)
{
	int cancelkeep = 0;
	dsn_class_t ret;
	field_t val;
	
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
		char *subaddress;
		// FIXME: Where can I get access to the address?
		res = find_bounded((char *)destination, '+', '@', &subaddress, &sublen, &subpos);
		if (res == 0 && sublen > 0) {
			// FIXME: I forget who frees the mailbox.
			mailbox = subaddress;
			source = BOX_ADDRESSPART;
		}
	}

	/* Sieve. */
	config_get_value("SIEVE", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		sort_result_t *sort_result;
		sort_result = sort_process(useridnr, message);
		if (sort_result) {
			cancelkeep = sort_get_cancelkeep(sort_result);
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
	} else {
		ret = sort_deliver_to_mailbox(message, useridnr, mailbox, source);
	}

	return ret;
}

dsn_class_t sort_deliver_to_mailbox(struct DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source)
{
	u64_t mboxidnr, newmsgidnr;
	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);

	if (db_find_create_mailbox(mailbox, source, useridnr, &mboxidnr) != 0) {
		trace(TRACE_ERROR, "%s,%s: mailbox [%s] not found",
				__FILE__, __func__,
				mailbox);
		return DSN_CLASS_FAIL;
	}

	switch (db_copymsg(message->id, mboxidnr, useridnr, &newmsgidnr)) {
	case -2:
		trace(TRACE_DEBUG, "%s, %s: error copying message to user [%llu],"
				"maxmail exceeded", 
				__FILE__, __func__, 
				useridnr);
		return DSN_CLASS_QUOTA;
	case -1:
		trace(TRACE_ERROR, "%s, %s: error copying message to user [%llu]", 
				__FILE__, __func__, 
				useridnr);
		return DSN_CLASS_TEMP;
	default:
		trace(TRACE_MESSAGE, "%s, %s: message id=%llu, size=%d is inserted", 
				__FILE__, __func__, 
				newmsgidnr, msgsize);
		message->id = newmsgidnr;
		return DSN_CLASS_OK;
	}
}
