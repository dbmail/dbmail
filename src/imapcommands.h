/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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

/*
 * imapcommands.h
 *
 * IMAP server command prototypes
 */

#ifndef DM_IMAP_COMMANDS_H
#define DM_IMAP_COMMANDS_H

#include "dbmail.h"

/* any-state commands */
int _ic_starttls(ImapSession *self);
int _ic_capability(ImapSession *self);
int _ic_noop(ImapSession *self);
int _ic_logout(ImapSession *self);
int _ic_id(ImapSession *self);

/* non-auth state commands */
int _ic_login(ImapSession *self);
int _ic_authenticate(ImapSession *self);

/* auth state commands */
int _ic_select(ImapSession *self);
int _ic_examine(ImapSession *self);
int _ic_enable(ImapSession *);
int _ic_create(ImapSession *self);
int _ic_delete(ImapSession *self);
int _ic_rename(ImapSession *self);
int _ic_subscribe(ImapSession *self);
int _ic_unsubscribe(ImapSession *self);
int _ic_list(ImapSession *self);
int _ic_lsub(ImapSession *self);
int _ic_status(ImapSession *self);
int _ic_append(ImapSession *self);

/* selected-state commands */
int _ic_sort(ImapSession *self);
int _ic_check(ImapSession *self);
int _ic_close(ImapSession *self);
int _ic_idle(ImapSession *self);
int _ic_unselect(ImapSession *self);
int _ic_expunge(ImapSession *self);
int _ic_search(ImapSession *self);
int _ic_fetch(ImapSession *self);
int _ic_store(ImapSession *self);
int _ic_copy(ImapSession *self);
int _ic_uid(ImapSession *self);
int _ic_thread(ImapSession *self);

/* quota commands */
int _ic_getquotaroot(ImapSession *self);
int _ic_getquota(ImapSession *self);

/* acl commands */

/**
 * \brief SETACL command 
 */
int _ic_setacl(ImapSession *self);
/**
 * DELETEACL command
 */
int _ic_deleteacl(ImapSession *self);
/**
 * GETACL command
 */
int _ic_getacl(ImapSession *self);
/**
 * LISTRIGHTS command
 */
int _ic_listrights(ImapSession *self);
/**
 * MYRIGHTS command
 */
int _ic_myrights(ImapSession *self);
/**
 * NAMESPACE command
 */
int _ic_namespace(ImapSession *self);
#endif
