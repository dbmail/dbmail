/* $Id$
 
 Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx

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

  * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <ctype.h>
#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "pipe.h"
#include "forward.h"
#include "sort.h"
#include "dbmail.h"
#include "debug.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dbmd5.h"
#include "misc.h"

#ifdef SIEVE
#include "sortsieve.h"
#endif

/* Run the user's sorting rules on this message
 * Retrieve the action list as either
 * a linked list of things to do, or a 
 * single thing to do. Not sure yet...
 *
 * Then do it!
 * */
dsn_class_t sort_and_deliver(u64_t msgidnr,
			     const char *header, u64_t headersize,
			     u64_t totalmsgsize, u64_t totalrfcsize,
			     u64_t useridnr, const char *mailbox)
{
	field_t val;
	int do_regex = 0, do_sieve = 0;
	struct list actions;
	struct element *tmp;
	int actiontaken = 0, ret = 0;
	u64_t mboxidnr, newmsgidnr;
	char unique_id[UID_SIZE];
	char *inbox = "INBOX";


	GetConfigValue("SQLREGEX", "SMTP", val);
	if (strcasecmp(val, "yes") == 0)
		do_regex = 1;

	GetConfigValue("LIBSIEVE", "SMTP", val);
	if (strcasecmp(val, "yes") == 0)
		do_sieve = 1;

	list_init(&actions);

	if (do_regex) {
		/* Call out to Jonas' regex sorting function!
		 * */
		// ret = db_regexsort(useridnr, header, actions);
		trace(TRACE_ERROR,
		      "%s, %s: Regex sort is enabled in dbmail.conf, but has not been compiled",
		      __FILE__, __func__);
	}

	if (do_sieve) {
		/* Don't code the Sieve guts right here,
		 * call out to a function that encapsulates it!
		 * */
#ifdef SIEVE
		ret =
		    sortsieve_msgsort(useridnr, header, headersize,
				      totalmsgsize, &actions);
#else
		/* Give the postmaster a clue as to why Sieve isn't working... */
		trace(TRACE_ERROR,
		      "%s, %s: Sieve enabled in dbmail.conf, but Sieve support has not been compiled",
		      __FILE__, __func__);
#endif				/* SIEVE */
	}

	if (mailbox == NULL)
		mailbox = inbox;

	/* actions is a list of things to do with this message
	 * each data pointer in the actions list references
	 * a structure like this:
	 *
	 * typedef sort_action {
	 *   int method,
	 *   char *destination,
	 *   char *message
	 * } sort_action_t;
	 * 
	 * Where message is some descriptive text, used
	 * primarily for rejection noticed, and where
	 * destination is either a mailbox name or a 
	 * forwarding address, and method is one of these:
	 *
	 * SA_KEEP,
	 * SA_DISCARD,
	 * SA_REDIRECT,
	 * SA_REJECT,
	 * SA_FILEINTO
	 * (see RFC 3028 [SIEVE] for details)
	 *
	 * SA_SIEVE:
	 * In addition, this implementation allows for 
	 * the internel Regex matching to call a Sieve
	 * script into action. In this case, the method
	 * is SA_SIEVE and the destination is the script's name.
	 * Note that Sieve must be enabled in the configuration
	 * file or else an error will be generated.
	 *
	 * In the absence of any valid actions (ie. actions
	 * is an empty list, or all attempts at performing the
	 * actions fail...) an implicit SA_KEEP is performed,
	 * using INBOX as the destination (hardcoded).
	 * */

	if (list_totalnodes(&actions) > 0) {
		tmp = list_getstart(&actions);
		while (tmp != NULL) {
			/* Try not to think about the structures too hard ;-) */
			switch ((int) ((sort_action_t *) tmp->data)->
				method) {
			case SA_SIEVE:
				{
					/* Run the script specified by destination and
					 * add the resulting list onto the *end* of the
					 * actions list. Note that this is a deep hack...
					 * */
					if ((char *) ((sort_action_t *)
						      tmp->data)->
					    destination != NULL) {
						struct list localtmplist;
						struct element
						    *localtmpelem;
//                      if (sortsieve_msgsort(useridnr, header, headersize, (char *)((sort_action_t *)tmp->data)->destination, localtmplist))
						{
							/* FIXME: This can all be replaced with some
							 * function called list_append(), if written! */
							/* Fast forward to the end of the actions list */
							localtmpelem =
							    list_getstart
							    (&actions);
							while (localtmpelem
							       != NULL) {
								localtmpelem
								    =
								    localtmpelem->
								    nextnode;
							}
							/* And tack on the start of the Sieve list */
							localtmpelem->
							    nextnode =
							    list_getstart
							    (&localtmplist);
							/* Remeber to increment the node count, too */
							actions.
							    total_nodes +=
							    list_totalnodes
							    (&localtmplist);
						}
					}
					break;
				}
			case SA_FILEINTO:
				{
					char *fileinto_mailbox =
					    (char *) ((sort_action_t *)
						      tmp->data)->
					    destination;

					/* If the action doesn't come with a mailbox, use the default. */

					if (fileinto_mailbox == NULL) {
						/* Cast the const away because fileinto_mailbox may need to be freed. */
						fileinto_mailbox =
						    (char *) mailbox;
						trace(TRACE_MESSAGE,
						      "sort_and_deliver(): mailbox not specified, using [%s]",
						      fileinto_mailbox);
					}


					/* Did we fail to create the mailbox? */
					if (db_find_create_mailbox
					    (fileinto_mailbox, useridnr,
					     &mboxidnr) != 0) {
						/* FIXME: Serious failure situation! This needs to be
						 * passed up the chain to notify the user, sender, etc.
						 * Perhaps we should *force* the implicit-keep to occur,
						 * or give another try at using INBOX. */
						trace(TRACE_ERROR,
						      "sort_and_deliver(): mailbox [%s] not found nor created, message may not have been delivered",
						      fileinto_mailbox);
					} else {
						switch (db_copymsg
							(msgidnr, mboxidnr,
							 useridnr,
							 &newmsgidnr)) {
						case -2:
							/* Couldn't deliver because the quota has been reached */
							break;
						case -1:
							/* Couldn't deliver because something something went wrong */
							trace(TRACE_ERROR,
							      "sort_and_deliver(): error copying message to user [%llu]",
							      useridnr);
							/* Don't worry about error conditions.
							 * It's annoying if the message isn't delivered,
							 * but as long as *something* happens it's OK.
							 * Otherwise, actiontaken will be 0 and another
							 * delivery attempt will be made before passing
							 * up the error at the end of the function.
							 * */
							break;
						default:
							trace
							    (TRACE_MESSAGE,
							     "sort_and_deliver(): message id=%llu, size=%llu is inserted",
							     newmsgidnr,
							     totalmsgsize);

							/* Create a unique ID for this message;
							 * Each message for each user must have a unique ID! 
							 * */
							create_unique_id
							    (unique_id,
							     newmsgidnr);
							db_message_set_unique_id
							    (newmsgidnr,
							     unique_id);

							actiontaken = 1;
							break;
						}
					}

					/* If these are not same pointers, then we need to free. */
					if (fileinto_mailbox != mailbox)
						my_free(fileinto_mailbox);

					break;
				}
			case SA_DISCARD:
				{
					/* Basically do nothing! */
					actiontaken = 1;
					break;
				}
			case SA_REJECT:
				{
					// FIXME: I'm happy with this code, but it's not quite right...
					// Plus we want to specify a message to go along with it!
					actiontaken = 1;
					break;
				}
			case SA_REDIRECT:
				{
					char *forward_id;
					struct list targets;

					list_init(&targets);
					list_nodeadd(&targets,
						     (char
						      *) ((sort_action_t *)
							  tmp->data)->
						     destination,
						     strlen((char
							     *) ((sort_action_t *) tmp->data)->destination) + 1);
					my_free((char *) ((sort_action_t *)
							  tmp->data)->
						destination);

					/* Put the destination into the targets list */
					/* The From header will contain... */
					forward_id =
					    auth_get_userid(useridnr);
					forward(msgidnr, &targets,
						forward_id, header,
						headersize);

					list_freelist(&targets.start);
					my_free(forward_id);
					actiontaken = 1;
					break;
				}
				/*
				   case SA_KEEP:
				   default:
				   {
				   // Don't worry! This is handled by implicit keep :-)
				   break;
				   }
				 */
			}	/* case */
			tmp = tmp->nextnode;
		}		/* while */
		list_freelist(&actions.start);
	} /* if */
	else {
		/* Might as well be explicit about this... */
		actiontaken = 0;
	}

	/* This is that implicit keep I mentioned earlier...
	 * If possible, put the message in the specified
	 * mailbox, otherwise use INBOX. */
	if (actiontaken == 0) {
		/* Did we fail to create the mailbox? */
		if (db_find_create_mailbox(mailbox, useridnr, &mboxidnr) !=
		    0) {
			/* Serious failure situation! */
			trace(TRACE_ERROR,
			      "sort_and_deliver(): INBOX not found");
		} else {
			switch (db_copymsg
				(msgidnr, mboxidnr, useridnr,
				 &newmsgidnr)) {
			case -2:
				/* Couldn't deliver because the quotum is exceeded. */
				trace(TRACE_DEBUG,
				      "%s, %s: error copying message to user [%llu], maxmail exceeded",
				      __FILE__, __func__, useridnr);
				break;
			case -1:
				/* Couldn't deliver because something something went wrong. */
				trace(TRACE_ERROR,
				      "%s, %s: error copying message to user [%llu]",
				      __FILE__, __func__, useridnr);
				break;
			default:
				trace(TRACE_MESSAGE,
				      "%s, %s: message id=%llu, size=%llu is inserted",
				      __FILE__, __func__, newmsgidnr,
				      totalmsgsize);
				actiontaken = 1;
				break;
			}
		}
	}

	if (actiontaken)
		return DSN_CLASS_OK;
	return DSN_CLASS_TEMP;
}
