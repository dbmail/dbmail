/* $Id$
 * imap4.h
 */

#ifndef _IMAP4_H
#define _IMAP4_H

#include "serverservice.h"

int imap_process(ClientInfo *ci);
int imap_login(ClientInfo *ci);


enum IMAP4_CLIENT_STATES { IMAPCS_INITIAL_CONNECT, IMAPCS_NON_AUTHENTICATED, 
			   IMAPCS_AUTHENTICATED, IMAPCS_SELECTED, IMAPCS_LOGOUT };

typedef void (*IMAP_COMMAND_HANDLER)(char*, char**, ClientInfo*);

typedef struct 
{
  int state;                /* IMAP state of client */
  unsigned long userid;     /* userID of client in dbase */
} imap_userdata_t;


#endif



