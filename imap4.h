/*
 * imap4.h
 */

#ifndef _IMAP4_H
#define _IMAP4_H

#include "imap4.h"
#include "serverservice.h"

const char *IMAP_COMMANDS[] = 
{
  "login", "authenticate", "select", "list", "logout"
};

typedef void (*IMAP_COMMAND_HANDLER)(char *tag, char **args, ClientInfo *ci);

#endif



