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
#define IMAP_INTERNALDATE_LEN 30

int imap_process(ClientInfo *ci);
int imap_login(ClientInfo *ci);


enum IMAP4_CLIENT_STATES { IMAPCS_INITIAL_CONNECT, IMAPCS_NON_AUTHENTICATED, 
			   IMAPCS_AUTHENTICATED, IMAPCS_SELECTED, IMAPCS_LOGOUT };

enum IMAP4_FLAGS { IMAPFLAG_SEEN = 0x01, IMAPFLAG_ANSWERED = 0x02, 
		   IMAPFLAG_DELETED = 0x04, IMAPFLAG_FLAGGED = 0x08,
		   IMAPFLAG_DRAFT = 0x10, IMAPFLAG_RECENT = 0x20 };

enum IMAP4_PERMISSION { IMAPPERM_READ = 0x01, IMAPPERM_READWRITE = 0x02 };



typedef int (*IMAP_COMMAND_HANDLER)(char*, char**, ClientInfo*);

enum BODY_FETCH_ITEM_TYPES { BFIT_TEXT, BFIT_HEADER,
			     BFIT_HEADER_FIELDS,
			     BFIT_HEADER_FIELDS_NOT };

typedef struct 
{
  int noseen;                /* set the seen flag ? */
  int itemtype;              /* the item to be fetched */
  int argstart;              /* start index in the arg array */
  int argcnt;                /* number of args belonging to this bodyfetch */
  int octetstart,octetcnt;   /* number of octets to be retrieved */
} body_fetch_t;


typedef struct 
{
  int nbodyfetches;
  body_fetch_t *bodyfetches;

  int getInternalDate,getFlags,getUID;
  int getMIME_IMB,getEnvelope,getSize;
  int getRFC822Header,getRFC822Text;
} fetch_items_t;


typedef struct
{
  char *name;
  char *val;
} parameter_t;


typedef struct
{
  char *name;
  char *source_route;
  char *mailboxname;
  char *hostname;
} address_t;

typedef struct
{
  char *date,*subject;
  
  int nfrom,nsender,nreplyto,nto,ncc,nbcc;
  address_t *from,*sender,*replyto,*to,*cc,*bcc;

  char *inreplyto,*messageID;
} envelope_t;

union bodystruc_t; /* fwd declaration */

typedef struct 
{
  char *content_type,*content_subtype;

  parameter_t *param_list;
  int nparams;

  char *content_id,*content_desc,*content_encoding;
  int size;

  union
  {
    struct
    {
      envelope_t *envelope;
      union bodystruc_t *bodystruc;
      int nlines;
    } message_rfc822;

    struct
    {
      int nlines;
    } text;
  } xtra;

  char *content_md5,*content_dispostion,*content_language;
} singlepart_t;


typedef struct
{
  int nparts;
  union bodystruc_t *parts;

  char *subtype;
  int nparams;
  parameter_t *param_list;
  
  char *content_disposition,*content_language;
} multipart_t;
  

union bodystruc_t
{
  singlepart_t single;
  multipart_t multi;
};
    
typedef union bodystruc_t bodystruc_t;


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



