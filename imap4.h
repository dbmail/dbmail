/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * imap4.h
 */

#ifndef _IMAP4_H
#define _IMAP4_H

#include "dbmailtypes.h"
#include "clientinfo.h"

#define IMAP_SERVER_VERSION "0.9"
#define IMAP_CAPABILITY_STRING "IMAP4 IMAP4rev1 AUTH=LOGIN QUOTA"
#define IMAP_TIMEOUT_MSG "* BYE dbmail IMAP4 server signing off due to timeout\r\n"

/* max number of BAD/NO responses */
#define MAX_FAULTY_RESPONSES 5

/* max number of retries when synchronizing mailbox with dbase */
#define MAX_RETRIES 20

int IMAPClientHandler(ClientInfo *ci);

typedef int (*IMAP_COMMAND_HANDLER)(char*, char**, ClientInfo*);

typedef struct 
{
  int noseen;                              /* set the seen flag ? */
  int itemtype;                            /* the item to be fetched */
  int argstart;                            /* start index in the arg array */
  int argcnt;                              /* number of args belonging to this bodyfetch */
  long long octetstart,octetcnt;           /* number of octets to be retrieved */

  char partspec[IMAP_MAX_PARTSPEC_LEN];    /* part specifier (i.e. '2.1.3' */

} body_fetch_t;


typedef struct 
{
/*  int nbodyfetches;
  body_fetch_t *bodyfetches;
*/
  body_fetch_t bodyfetch;

  int msgparse_needed;

  int getBodyTotal,getBodyTotalPeek;
  int getInternalDate,getFlags,getUID;
  int getMIME_IMB,getEnvelope,getSize;
  int getMIME_IMB_noextension;
  int getRFC822Header,getRFC822Text;
  int getRFC822,getRFC822Peek;
} fetch_items_t;



typedef struct 
{
  int state;                /* IMAP state of client */
  u64_t userid;             /* userID of client in dbase */
  mailbox_t mailbox;        /* currently selected mailbox */
} imap_userdata_t;

#endif



