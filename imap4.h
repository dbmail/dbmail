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

/* length of internaldate string */
#define IMAP_INTERNALDATE_LEN 30

/* max length of number/dots part specifier */
#define IMAP_MAX_PARTSPEC_LEN 100

int imap_process(ClientInfo *ci);
int imap_login(ClientInfo *ci);


enum IMAP4_CLIENT_STATES { IMAPCS_INITIAL_CONNECT, IMAPCS_NON_AUTHENTICATED, 
			   IMAPCS_AUTHENTICATED, IMAPCS_SELECTED, IMAPCS_LOGOUT };

enum IMAP4_FLAGS { IMAPFLAG_SEEN = 0x01, IMAPFLAG_ANSWERED = 0x02, 
		   IMAPFLAG_DELETED = 0x04, IMAPFLAG_FLAGGED = 0x08,
		   IMAPFLAG_DRAFT = 0x10, IMAPFLAG_RECENT = 0x20 };

enum IMAP4_PERMISSION { IMAPPERM_READ = 0x01, IMAPPERM_READWRITE = 0x02 };

enum IMAP4_FLAG_ACTIONS { IMAPFA_NONE, IMAPFA_REPLACE, IMAPFA_ADD, IMAPFA_REMOVE };

typedef int (*IMAP_COMMAND_HANDLER)(char*, char**, ClientInfo*);

enum BODY_FETCH_ITEM_TYPES { BFIT_TEXT, BFIT_HEADER, BFIT_MIME,
			     BFIT_HEADER_FIELDS,
			     BFIT_HEADER_FIELDS_NOT };

typedef struct 
{
  int noseen;                /* set the seen flag ? */
  int itemtype;              /* the item to be fetched */
  int argstart;              /* start index in the arg array */
  int argcnt;                /* number of args belonging to this bodyfetch */
  int octetstart,octetcnt;   /* number of octets to be retrieved */

  char partspec[IMAP_MAX_PARTSPEC_LEN]; /* part specifier (i.e. '2.1.3' */

} body_fetch_t;


typedef struct 
{
  int nbodyfetches;
  body_fetch_t *bodyfetches;

  int getTotal;
  int getInternalDate,getFlags,getUID;
  int getMIME_IMB,getEnvelope,getSize;
  int getRFC822Header,getRFC822Text;
} fetch_items_t;



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



