/* $Id$
 * imap4.h
 */

#ifndef _IMAP4_H
#define _IMAP4_H

#include "serverservice.h"

#define IMAP_SERVER_VERSION "0.1"
#define IMAP_CAPABILITY_STRING "IMAP4 IMAP4rev1 AUTH=LOGIN"

/* maximum size of a mailbox name */
#define IMAP_MAX_MAILBOX_NAMELEN 100

int imap_process(ClientInfo *ci);
int imap_login(ClientInfo *ci);


enum IMAP4_CLIENT_STATES { IMAPCS_INITIAL_CONNECT, IMAPCS_NON_AUTHENTICATED, 
			   IMAPCS_AUTHENTICATED, IMAPCS_SELECTED, IMAPCS_LOGOUT };

enum IMAP4_FLAGS { IMAPFLAG_SEEN = 0x01, IMAPFLAG_ANSWERED = 0x02, 
		   IMAPFLAG_DELETED = 0x04, IMAPFLAG_FLAGGED = 0x08,
		   IMAPFLAG_DRAFT = 0x10, IMAPFLAG_RECENT = 0x20 };

enum IMAP4_PERMISSION { IMAPPERM_READ = 0x01, IMAPPERM_READWRITE = 0x02 };



typedef int (*IMAP_COMMAND_HANDLER)(char*, char**, ClientInfo*);

typedef struct 
{
  unsigned long uid,msguidnext;
  unsigned exists,recent,unseen;
  unsigned flags;
  int permission;
  unsigned long *seq_list;
} mailbox_t;


typedef struct 
{
  int state;                /* IMAP state of client */
  unsigned long userid;     /* userID of client in dbase */
  mailbox_t mailbox;        /* currently selected mailbox */
} imap_userdata_t;


#endif



