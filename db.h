/*
 * dbase driver header file
 * Functions for database communication 
 *
 * should connect to any dbXXX.c file where XXXX is the dbase to be used
 */

#ifndef  _DB_H
#define  _DB_H

#include "debug.h"
#include "dbmailtypes.h"
#include "mime.h"
#include "list.h"
#include "memblock.h"

#define DEF_QUERYSIZE 1024
#define DUMP_BUF_SIZE 1024

#define MAX_EMAIL_SIZE 250

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



/* 
 * PROTOTYPES 
 */

int db_connect();
int db_disconnect();
int db_query (const char *thequery);


char *db_get_config_item (char *item, int type);
int db_clear_config();
int db_insert_config_item (char *item, char *val);

int db_get_user_aliases(u64_t userid, struct list *aliases);
int db_addalias (u64_t useridnr, char *alias, int clientid);
int db_removealias (u64_t useridnr, const char *alias);

u64_t db_get_inboxid (u64_t *useridnr);
u64_t db_get_useridnr (u64_t messageidnr);
u64_t db_get_message_mailboxid (u64_t *messageidnr);
u64_t db_insert_message (u64_t *useridnr);
u64_t db_update_message (u64_t *messageidnr, char *unique_id,
				 u64_t messagesize);
u64_t db_insert_message_block (char *block, u64_t messageidnr);

int db_log_ip(const char *ip);
int db_cleanup_iplog(const char *lasttokeep);

int db_icheck_messageblks(int *nlost, u64_t **lostlist);
int db_icheck_messages(int *nlost, u64_t **lostlist);
int db_icheck_mailboxes(int *nlost, u64_t **lostlist);

int db_delete_messageblk(u64_t uid);
int db_delete_message(u64_t uid);
int db_delete_mailbox(u64_t uid);

u64_t db_insert_result (char *sequence_identifier);
int db_send_message_lines (void *fstream, u64_t messageidnr, long lines, int no_end_dot);
int db_createsession (u64_t useridnr, struct session *sessionptr);
void db_session_cleanup (struct session *sessionptr);
int db_update_pop (struct session *sessionptr);
u64_t db_set_deleted ();
u64_t db_deleted_purge();
u64_t db_check_sizelimit (u64_t addblocksize, u64_t messageidnr, 
				  u64_t *useridnr);
int db_imap_append_msg(char *msgdata, u64_t datalen, u64_t mboxid, u64_t uid);


/* mailbox functionality */
u64_t db_findmailbox(const char *name, u64_t useridnr);
int db_findmailbox_by_regex(u64_t ownerid, const char *pattern, 
			    u64_t **children, unsigned *nchildren,
			    int only_subscribed);
int db_getmailbox(mailbox_t *mb, u64_t userid);
int db_createmailbox(const char *name, u64_t ownerid);
int db_listmailboxchildren(u64_t uid, u64_t useridnr, 
			   u64_t **children, int *nchildren,
			   const char *filter);
int db_removemailbox(u64_t uid, u64_t ownerid);
int db_isselectable(u64_t uid);
int db_noinferiors(u64_t uid);
int db_setselectable(u64_t uid, int val);
int db_removemsg(u64_t uid);
int db_movemsg(u64_t to, u64_t from);
int db_copymsg(u64_t msgid, u64_t destmboxid);
int db_getmailboxname(u64_t uid, char *name);
int db_setmailboxname(u64_t uid, const char *name);
int db_expunge(u64_t uid,u64_t **msgids,u64_t *nmsgs);
u64_t db_first_unseen(u64_t uid);
int db_subscribe(u64_t mboxid);
int db_unsubscribe(u64_t mboxid);

/* message functionality */
int db_get_msgflag(const char *name, u64_t mailboxuid, u64_t msguid);
int db_set_msgflag(const char *name, u64_t mailboxuid, u64_t msguid, int val);
int db_get_msgdate(u64_t mailboxuid, u64_t msguid, char *date);

int db_get_main_header(u64_t msguid, struct list *hdrlist);

int db_mailbox_msg_match(u64_t mailboxuid, u64_t msguid);

#endif
