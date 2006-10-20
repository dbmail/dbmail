/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id: imapcommands.h 2308 2006-10-20 13:36:50Z paul $
 *
 * imapcommands.h
 *
 * IMAP server command prototypes
 */

#ifndef _IMAP_COMMANDS_H
#define _IMAP_COMMANDS_H

#include "dbmail.h"
#include "dbmail-imapsession.h"

/* any-state commands */
int _ic_capability(struct ImapSession *self);
int _ic_noop(struct ImapSession *self);
int _ic_logout(struct ImapSession *self);

/* non-auth state commands */
int _ic_login(struct ImapSession *self);
int _ic_authenticate(struct ImapSession *self);

/* auth state commands */
int _ic_select(struct ImapSession *self);
int _ic_examine(struct ImapSession *self);
int _ic_create(struct ImapSession *self);
int _ic_delete(struct ImapSession *self);
int _ic_rename(struct ImapSession *self);
int _ic_subscribe(struct ImapSession *self);
int _ic_unsubscribe(struct ImapSession *self);
int _ic_list(struct ImapSession *self);
int _ic_lsub(struct ImapSession *self);
int _ic_status(struct ImapSession *self);
int _ic_append(struct ImapSession *self);

/* selected-state commands */
int _ic_sort(struct ImapSession *self);
int _ic_check(struct ImapSession *self);
int _ic_close(struct ImapSession *self);
int _ic_expunge(struct ImapSession *self);
int _ic_search(struct ImapSession *self);
int _ic_fetch(struct ImapSession *self);
int _ic_store(struct ImapSession *self);
int _ic_copy(struct ImapSession *self);
int _ic_uid(struct ImapSession *self);
int _ic_thread(struct ImapSession *self);

/* quota commands */
int _ic_getquotaroot(struct ImapSession *self);
int _ic_getquota(struct ImapSession *self);

/* acl commands */

/**
 * \brief SETACL command 
 */
int _ic_setacl(struct ImapSession *self);
/**
 * DELETEACL command
 */
int _ic_deleteacl(struct ImapSession *self);
/**
 * GETACL command
 */
int _ic_getacl(struct ImapSession *self);
/**
 * LISTRIGHTS command
 */
int _ic_listrights(struct ImapSession *self);
/**
 * MYRIGHTS command
 */
int _ic_myrights(struct ImapSession *self);
/**
 * NAMESPACE command
 */
int _ic_namespace(struct ImapSession *self);
#endif
