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
		const char *mailbox, mailbox_source_t source)
{
	int cancelkeep = 0;
	int reject = 0;
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
			trace(TRACE_INFO, "%s, %s: Setting BOX_ADDRESSPART mailbox to [%s]",
					__FILE__, __func__, mailbox);
		}
	}

	/* Give Sieve access to the envelope recipient. */
	dbmail_message_set_envelope_recipient(message, destination);

	config_get_value("SIEVE", "DELIVERY", val);
#if defined(SIEVE) || defined(SHARED)
	/* Sieve. */
	if (strcasecmp(val, "yes") == 0
	&& db_check_sievescript_active(useridnr) == 0) {
		trace(TRACE_INFO, "%s, %s: Calling for a Sieve sort",
				__FILE__, __func__);
		sort_result_t *sort_result;
		sort_result = sort_process(useridnr, message);
		if (sort_result) {
			cancelkeep = sort_get_cancelkeep(sort_result);
			reject = sort_get_reject(sort_result);
			sort_free_result(sort_result);
		}
	}
#else
	/* No Sieve. */
	if (strcasecmp(val, "yes") == 0) {
		trace(TRACE_WARNING, "%s, %s: SIEVE sorting enabled in DELIVERY section of dbmail.conf,"
				" but this build of DBMail was statically configured without Sieve.",
				__FILE__, __func__);
	}
#endif

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
		trace(TRACE_INFO, "%s, %s: Keep was cancelled. Message may be discarded.",
				__FILE__, __func__);
	} else {
		ret = sort_deliver_to_mailbox(message, useridnr, mailbox, source);
		trace(TRACE_INFO, "%s, %s: Keep was not cancelled. Message will be delivered by default.",
				__FILE__, __func__);
	}

	/* Reject probably implies cancelkeep,
	 * but we'll not assume that and instead
	 * just test this as a separate block. */
	if (reject) {
		trace(TRACE_INFO, "%s, %s: Message will be rejected.",
				__FILE__, __func__);
		ret = DSN_CLASS_FAIL;
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

