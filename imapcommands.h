/*
 * imapcommands.h
 *
 * IMAP server command prototypes
 */

#ifndef _IMAP_COMMANDS_H
#define _IMAP_COMMANDS_H

#include "imap4.h"

void _ic_login(char *tag, char **args, ClientInfo *ci);
void _ic_authenticate(char *tag, char **args, ClientInfo *ci);
void _ic_select(char *tag, char **args, ClientInfo *ci);
void _ic_list(char *tag, char **args, ClientInfo *ci);
void _ic_logout(char *tag, char **args, ClientInfo *ci);

#endif
