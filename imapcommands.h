/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * imapcommands.h
 *
 * IMAP server command prototypes
 */

#ifndef _IMAP_COMMANDS_H
#define _IMAP_COMMANDS_H

#include "imap4.h"

/* any-state commands */
int _ic_capability(char *tag, char **args, ClientInfo *ci);
int _ic_noop(char *tag, char **args, ClientInfo *ci);
int _ic_logout(char *tag, char **args, ClientInfo *ci);

/* non-auth state commands */
int _ic_login(char *tag, char **args, ClientInfo *ci);
int _ic_authenticate(char *tag, char **args, ClientInfo *ci);

/* auth state commands */
int _ic_select(char *tag, char **args, ClientInfo *ci);
int _ic_examine(char *tag, char **args, ClientInfo *ci);
int _ic_create(char *tag, char **args, ClientInfo *ci);
int _ic_delete(char *tag, char **args, ClientInfo *ci);
int _ic_rename(char *tag, char **args, ClientInfo *ci);
int _ic_subscribe(char *tag, char **args, ClientInfo *ci);
int _ic_unsubscribe(char *tag, char **args, ClientInfo *ci);
int _ic_list(char *tag, char **args, ClientInfo *ci);
int _ic_lsub(char *tag, char **args, ClientInfo *ci);
int _ic_status(char *tag, char **args, ClientInfo *ci);
int _ic_append(char *tag, char **args, ClientInfo *ci);

/* selected-state commands */
int _ic_check(char *tag, char **args, ClientInfo *ci);
int _ic_close(char *tag, char **args, ClientInfo *ci);
int _ic_expunge(char *tag, char **args, ClientInfo *ci);
int _ic_search(char *tag, char **args, ClientInfo *ci);
int _ic_fetch(char *tag, char **args, ClientInfo *ci);
int _ic_store(char *tag, char **args, ClientInfo *ci);
int _ic_copy(char *tag, char **args, ClientInfo *ci);
int _ic_uid(char *tag, char **args, ClientInfo *ci);


#endif
