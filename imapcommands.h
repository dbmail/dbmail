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
 * imapcommands.h
 *
 * IMAP server command prototypes
 */

#ifndef _IMAP_COMMANDS_H
#define _IMAP_COMMANDS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imap4.h"

/* any-state commands */
int _ic_capability(char *tag, char **args, ClientInfo * ci);
int _ic_noop(char *tag, char **args, ClientInfo * ci);
int _ic_logout(char *tag, char **args, ClientInfo * ci);

/* non-auth state commands */
int _ic_login(char *tag, char **args, ClientInfo * ci);
int _ic_authenticate(char *tag, char **args, ClientInfo * ci);

/* auth state commands */
int _ic_select(char *tag, char **args, ClientInfo * ci);
int _ic_examine(char *tag, char **args, ClientInfo * ci);
int _ic_create(char *tag, char **args, ClientInfo * ci);
int _ic_delete(char *tag, char **args, ClientInfo * ci);
int _ic_rename(char *tag, char **args, ClientInfo * ci);
int _ic_subscribe(char *tag, char **args, ClientInfo * ci);
int _ic_unsubscribe(char *tag, char **args, ClientInfo * ci);
int _ic_list(char *tag, char **args, ClientInfo * ci);
int _ic_lsub(char *tag, char **args, ClientInfo * ci);
int _ic_status(char *tag, char **args, ClientInfo * ci);
int _ic_append(char *tag, char **args, ClientInfo * ci);

/* selected-state commands */
int _ic_check(char *tag, char **args, ClientInfo * ci);
int _ic_close(char *tag, char **args, ClientInfo * ci);
int _ic_expunge(char *tag, char **args, ClientInfo * ci);
int _ic_search(char *tag, char **args, ClientInfo * ci);
int _ic_fetch(char *tag, char **args, ClientInfo * ci);
int _ic_store(char *tag, char **args, ClientInfo * ci);
int _ic_copy(char *tag, char **args, ClientInfo * ci);
int _ic_uid(char *tag, char **args, ClientInfo * ci);

/* quota commands */
int _ic_getquotaroot(char *tag, char **args, ClientInfo * ci);
int _ic_getquota(char *tag, char **args, ClientInfo * ci);

/* acl commands */

/**
 * \brief SETACL command 
 */
int _ic_setacl(char *tag, char **args, ClientInfo * ci);
/**
 * DELETEACL command
 */
int _ic_deleteacl(char *tag, char **args, ClientInfo * ci);
/**
 * GETACL command
 */
int _ic_getacl(char *tag, char **args, ClientInfo * ci);
/**
 * LISTRIGHTS command
 */
int _ic_listrights(char *tag, char **args, ClientInfo * ci);
/**
 * MYRIGHTS command
 */
int _ic_myrights(char *tag, char **args, ClientInfo * ci);
/**
 * NAMESPACE command
 */
int _ic_namespace(char *tag, char **args, ClientInfo * ci);
#endif
