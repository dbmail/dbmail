/*
 Copyright (C) 2003 IC & S  dbmail@ic-s.nl

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

 $Id$
*/
#ifndef SHARED_MAILBOX_H
#define SHARED_MAILBOX_H
/**
   \brief header for shared folders 
*/
#include "dbmailtypes.h"
#include <regex.h>
/**
 * \brief check if mailbox_id is a shared mailbox
 * \param mailbox_id the folder to check
 * \return
 *    - -1 on error
 *    -  0 if not a shared mailbox
 *    -  1 if mailbox_id is a shared mailbox
 */
int shared_mailbox_is_shared(u64_t mailbox_id);

/**
 * \brief check if a message is inside a shared mailbox
 * \param message_id the message to check
 * \return
 *   - -1 on error
 *   -  0 if not in shared mailbox
 *   -  1 if in shared mailbox
 */
int shared_mailbox_msg_in_shared(u64_t message_id);

/**
 * \brief find the mailbox_idnr of a shared mailbox
 * \param mailbox_name name of mailbox 
 * \param user_idnr user_idnr of user to find mailbox for
 * \param mailbox_idnr pointer to mailbox idnr. will hold mailbox_idnr of
 *  found mailbox
 * \return
 *     - -1 on error
 *     -  1 on success
 */
int shared_mailbox_find(const char *mailbox_name, u64_t user_idnr, u64_t* mailbox_idnr);

/**
 * \brief list all mailboxes that a user can see, conforming to some regular expression 
 * \param user_idnr user to list mailboxes for
 * \param only_subscribed only list subscribed mailboxes
 * \param preg compiled regular expression
 * \param mailboxes will hold list of mailboxes
 * \param nr_mailboxes will hold number of mailboxes in mailboxes
 * \return 
 *     - -2 on memory error
 *     - -1 on database error
 *     -  0 if no mailboxes found
 *     -  1 on success
 */
int shared_mailbox_list_by_regex(u64_t user_idnr, int only_subscribed, 
				   regex_t *preg,
				   u64_t **mailboxes, unsigned int *nr_mailboxes);

/**
 * \brief calculate and update the number of bytes used by all emails in
 * a shared mailbox
 * \param mailbox_id id of shared mailbox
 * \return 
 *     - -1 on error
 *     - 1 on success
 */
int shared_mailbox_calculate_quotum_used(u64_t mailbox_id);

/**
 * \brief get maxmail size of a shared mailbox
 * \param mailbox_id mailbox to get maxmail size for
 * \param maxmail_size will hold maxmail_size (should not be NULL!)
 * \return 
 *    - -1 on failure
 *    -  1 on success
 */
int shared_mailbox_get_maxmail_size(u64_t mailbox_id, u64_t *maxmail_size);

/**
 * \brief get current mail size of shared mailbox
 * \param mailbox_id mailbox to get current mail size for
 * \param curmail_size will hold current mail size
 * \return 
 *      - -1 on error
 *      -  0 on success
 */
int shared_mailbox_get_curmail_size(u64_t mailbox_id, u64_t *curmail_size); 

/**
 * \brief subscribe or unsubscribe to a shared mailbox 
 * \param mailbox_id mailbox to subscribe to or unsubscribe from
 * \param user_id user to subscribe or unsubscribe
 * \param do_subscribe if 0, unsubscribe, if 1 subscribe
 * \return 
 *    - -1 on failure
 *    -  1 on success
 */
int shared_mailbox_subscribe(u64_t mailbox_id, u64_t user_id, 
			     int do_subscribe);
/**
 * \brief create a shared mailbox for a client 
 * \param name name of mailbox
 * \param client_id id of client
 * \param maxmail_size maximum size of shared mailbox
 * \param mailbox_idnr will hold id of mailbox after return.
 * \return
 *     - -1 on failure
 *     -  1 on success
 */
int shared_mailbox_create_mailbox_client(const char *name, u64_t client_id, 
					 u64_t maxmail_size, 
					 u64_t *mailbox_idnr);
#endif

