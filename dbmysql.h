/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * driver header file
 * Functions for database communication */

#ifndef  _DBMYSQL_H
#define  _DBMYSQL_H

#include "/usr/include/mysql/mysql.h"
#include "debug.h"
#include "imap4.h"
#include "mime.h"
#include "list.h"
#include "memblock.h"

#define MAX_SEARCH_LEN 1024

/* size of the messageblk's */
#define READ_BLOCK_SIZE 524288		/* be carefull, MYSQL has a limit */

/* config types */
#define CONFIG_MANDATORY 1			
#define CONFIG_EMPTY 0


struct session;
struct list;

enum table_aliases /* prototype for aliases table */
{
	ALIASES_ALIAS_IDNR,
	ALIASES_ALIAS,
	ALIASES_USERIDNR
};

enum table_user /* prototype for user table */
{
	USER_USERIDNR,
	USER_USERID,
	USER_PASSWD,
	USER_CLIENTID,
	USER_MAXMAIL
};

enum table_mailbox /* prototype for mailbox table */
{
	MAILBOX_MAILBOXIDNR,
	MAILBOX_OWNERIDNR,
	MAILBOX_NAME,
	MAILBOX_SEEN_FLAG,
	MAILBOX_ANSWERED_FLAG,
	MAILBOX_DELETED_FLAG,
	MAILBOX_FLAGGED_FLAG,
	MAILBOX_RECENT_FLAG,
	MAILBOX_DRAFT_FLAG,
	MAILBOX_NO_INFERIORS,
	MAILBOX_NO_SELECT,
	MAILBOX_PERMISSIONS
};
	
enum table_message /* prototype for message table */
{
	MESSAGE_MESSAGEIDNR,
	MESSAGE_MAILBOXIDNR,
	MESSAGE_MESSAGESIZE,
	MESSAGE_SEEN_FLAG,
	MESSAGE_ANSWERED_FLAG,
	MESSAGE_DELETED_FLAG,
	MESSAGE_FLAGGED_FLAG,
	MESSAGE_RECENT_FLAG,
	MESSAGE_DRAFT_FLAG,
	MESSAGE_UNIQUE_ID,
	MESSAGE_INTERNALDATE,
	MESSAGE_STATUS
};

enum table_messageblk /* prototype for messageblk table */
{
	MESSAGEBLK_MESSAGEBLKNR,
	MESSAGEBLK_MESSAGEIDNR,
	MESSAGEBLK_MESSAGEBLK,
	MESSAGEBLK_BLOCKSIZE
};


enum IMAP_SEARCH_TYPES { IST_SET, IST_SET_UID, IST_FLAG, IST_HDR, IST_HDRDATE_BEFORE,
			 IST_HDRDATE_ON, IST_HDRDATE_SINCE,
			 IST_IDATE, IST_DATA_BODY, IST_DATA_TEXT, IST_SIZE_LARGER, IST_SIZE_SMALLER, 
			 IST_SUBSEARCH_AND, IST_SUBSEARCH_OR, IST_SUBSEARCH_NOT };

typedef struct 
{
  int type;
  unsigned long size;
  char search[MAX_SEARCH_LEN];
  char hdrfld[MIME_FIELD_MAX];
  struct list sub_search;
} search_key_t;
  

typedef struct
{
  int inited;
  int nblocks,currblock,blockpos;
  MYSQL_RES *result;
  MYSQL_ROW block;
} msgfetch_info_t;


typedef struct 
{
  unsigned long block,pos;
} db_pos_t;


typedef struct 
{
  struct list mimeheader;           /* the MIME header of this part (if present) */
  struct list rfcheader;            /* RFC822 header of this part (if present) */
  int message_has_errors;           /* if set the content-type is meaningless */
  db_pos_t bodystart,bodyend;       /* the body of this part */
  unsigned long bodysize;
  unsigned long bodylines;
  unsigned long rfcheadersize;         
  struct list children;             /* the children (multipart msg) */
  unsigned long rfcheaderlines;
  unsigned long mimerfclines;          
    /* the total number of lines (only specified in case of a MIME msg containing an RFC822 msg) */
} mime_message_t;

 
typedef struct
{
  mime_message_t msg;
  MEM *memdump,*tmpdump;
  int num,file_dumped,msg_parsed;
  long dumpsize;
} cache_t;


/* 
 * PROTOTYPES 
 */

int db_connect();
int db_query (const char *query);
int db_check_user (char *username, struct list *userids, int checks);
char *db_get_config_item (char *item, int type);
int db_clear_config();
int db_insert_config_item (char *item, char *value);
unsigned long db_adduser (char *username, char *password, char *clientid, char *maxmail);
int db_addalias (unsigned long useridnr, char *alias, int clientid);
unsigned long db_get_inboxid (unsigned long *useridnr);
unsigned long db_get_useridnr (unsigned long messageidnr);
unsigned long db_get_message_mailboxid (unsigned long *messageidnr);
unsigned long db_insert_message (unsigned long *useridnr);
unsigned long db_update_message (unsigned long *messageidnr, char *unique_id,
				 unsigned long messagesize);
unsigned long db_insert_message_block (char *block, int messageidnr);
int db_check_id (char *id);

int db_icheck_messageblks(int *nlost, unsigned long **lostlist);
int db_icheck_messages(int *nlost, unsigned long **lostlist);
int db_icheck_mailboxes(int *nlost, unsigned long **lostlist);

int db_delete_messageblk(unsigned long uid);
int db_delete_message(unsigned long uid);
int db_delete_mailbox(unsigned long uid);


int db_disconnect();
unsigned long db_insert_result ();
int db_send_message_lines (void *fstream, unsigned long messageidnr, long lines, int no_end_dot);
unsigned long db_validate (char *user, char *password);
unsigned long db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp);
int db_createsession (unsigned long useridnr, struct session *sessionptr);
void db_session_cleanup (struct session *sessionptr);
int db_update_pop (struct session *sessionptr);
unsigned long db_set_deleted ();
unsigned long db_deleted_purge();
unsigned long db_check_sizelimit (unsigned long addblocksize, unsigned long messageidnr, 
				  unsigned long *useridnr);
char *db_get_userid (unsigned long *useridnr);
int db_imap_append_msg(char *msgdata, unsigned long datalen, unsigned long mboxid, unsigned long uid);


/* mailbox functionality */
unsigned long db_findmailbox(const char *name, unsigned long useridnr);
int db_findmailbox_by_regex(unsigned long ownerid, const char *pattern, 
			    unsigned long **children, unsigned *nchildren,
			    int only_subscribed);
int db_getmailbox(mailbox_t *mb, unsigned long userid);
int db_createmailbox(const char *name, unsigned long ownerid);
int db_listmailboxchildren(unsigned long uid, unsigned long useridnr, 
			   unsigned long **children, int *nchildren,
			   const char *filter);
int db_removemailbox(unsigned long uid, unsigned long ownerid);
int db_isselectable(unsigned long uid);
int db_noinferiors(unsigned long uid);
int db_setselectable(unsigned long uid, int value);
int db_removemsg(unsigned long uid);
int db_movemsg(unsigned long to, unsigned long from);
int db_copymsg(unsigned long msgid, unsigned long destmboxid);
int db_getmailboxname(unsigned long uid, char *name);
int db_setmailboxname(unsigned long uid, const char *name);
int db_expunge(unsigned long uid,unsigned long **msgids,int *nmsgs);
unsigned long db_first_unseen(unsigned long uid);
int db_subscribe(unsigned long mboxid);
int db_unsubscribe(unsigned long mboxid);

/* message functionality */
int db_get_msgflag(const char *name, unsigned long mailboxuid, unsigned long msguid);
int db_set_msgflag(const char *name, unsigned long mailboxuid, unsigned long msguid, int val);
int db_get_msgdate(unsigned long mailboxuid, unsigned long msguid, char *date);

int db_init_msgfetch(unsigned long uid);
int db_update_msgbuf(int minlen);
void db_close_msgfetch();
void db_give_msgpos(db_pos_t *pos);
unsigned long db_give_range_size(db_pos_t *start, db_pos_t *end);

void db_free_msg(mime_message_t *msg);
void db_reverse_msg(mime_message_t *msg);

int db_fetch_headers(unsigned long msguid, mime_message_t *msg);
int db_add_mime_children(struct list *brothers, char *splitbound, int *level, int maxlevel);
int db_start_msg(mime_message_t *msg, char *stopbound, int *level, int maxlevel);
int db_parse_as_text(mime_message_t *msg);

long db_dump_range(MEM *outmem,db_pos_t start, db_pos_t end, unsigned long msguid);
int db_msgdump(mime_message_t *msg, unsigned long msguid, int level);

int db_mailbox_msg_match(unsigned long mailboxuid, unsigned long msguid);

int db_search(int *rset, int setlen, const char *key, mailbox_t *mb);
int db_search_parsed(int *rset, int setlen, search_key_t *sk, mailbox_t *mb);

int db_search_messages(char **search_keys, unsigned long **search_results, int *nsresults,
		       unsigned long mboxid);

#endif
