/* Central switching station for email on its
 * way to be delivered. From here we call out
 * to the sorting module, if applicable, to
 * give additional information on what to do
 * with a message.
 *
 * (c) 2004-2005 Aaron Stone <aaron@serendpity.cx>
 *
 * $Id: $
 */

#include <config.h>
#include "debug.h"
#include "dbmailtypes.h"

/* Run the user's sorting rules on this message
 * Retrieve the action list as either
 * a linked list of things to do, or a 
 * single thing to do. Not sure yet...
 *
 * Then do it!
 * */
dsn_class_t sort_and_deliver(struct DbmailMessage *message, u64_t useridnr,
		const char *mailbox, mailbox_source_t source)
{
	u64_t mboxidnr, newmsgidnr;

	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);
	u64_t msgidnr = message->id;
	
	/* This is the only condition when called from pipe.c, actually. */
	if (! mailbox) {
		mailbox = "INBOX";
		source = BOX_DEFAULT;
	}
	
	/* FIXME: This is where we call the sorting engine into action. */

	if (db_find_create_mailbox(mailbox, source, useridnr, &mboxidnr) != 0) {
		trace(TRACE_ERROR, "%s,%s: mailbox [%s] not found",
				__FILE__, __func__,
				mailbox);
		return DSN_CLASS_FAIL;
	} else {
		switch (db_copymsg(msgidnr, mboxidnr, useridnr, &newmsgidnr)) {
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
}

