/*
 * dbase driver header file
 * Functions for database communication 
 *
 * should connect to any dbXXX.c file where XXXX is the dbase to be used
 */

#ifndef  _DB_H
#define  _DB_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "debug.h"
#include "dbmailtypes.h"
#include "mime.h"
#include "list.h"
#include "memblock.h"

/* define UNUSED for parameters that are not used in functions */
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif


#define DEF_QUERYSIZE 1024
#define DUMP_BUF_SIZE 1024

#define ICHECK_RESULTSETSIZE 1000000

#define MAX_EMAIL_SIZE 250

/* size of the messageblk's */
#define READ_BLOCK_SIZE (512ul*1024ul)		/* be carefull, MYSQL has a limit */

/* config types */
#define CONFIG_MANDATORY 1			
#define CONFIG_EMPTY 0


struct list;

/* users, aliases, mailboxes, messages, messageblks */
#define DB_NTABLES 5
    

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
	
/** structure of message table.
 * \date 19-08-2003  added MESSAGE_RFCSIZE
 */
enum table_message
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
	MESSAGE_STATUS,
	MESSAGE_RFCSIZE
};

enum table_messageblk /* prototype for messageblk table */
{
	MESSAGEBLK_MESSAGEBLKNR,
	MESSAGEBLK_MESSAGEIDNR,
	MESSAGEBLK_MESSAGEBLK,
	MESSAGEBLK_BLOCKSIZE
};

/**
 * global variables
 */
char query[DEF_QUERYSIZE]; /**< can be used for making queries to db backend */

/* 
 * PROTOTYPES 
 */

/* Implemented differently for MySQL and PostgreSQL: */
/**
 * \brief connect to the database
 * \return 
 *         - -1 on failure
 *         - 0 otherwise
 */
int db_connect();

/**
 * \brief disconnect from database server
 * \return 0
 */
int db_disconnect();

/**
 * \brief check database connection. If it is dead, reconnect
 * \return
 *    - -1 on failure (no connection to db possible)
 *    -  0 on success
 */
int db_check_connection();

/**
 * \brief execute a database query
 * \param the_query the SQL query to execute
 * \return 
 *         - 0 on success
 *         - 1 on failure
 */
int db_query (const char *the_query);

/**
 * \brief get number of rows in result set.
 * \return 
 *     - 0 on failure or no rows
 *     - number of rows otherwise
 */
int db_num_rows();

/**
 * \brief get number of fields in result set.
 * \return
 *      - 0 on failure or no fields
 *      - number of fields otherwise
 */
int db_num_fields();

/**
 * \brief clear the result set of a query
 */
void db_free_result();

/**
 * \brief get a single result field row from the result set
 * \param row result row to get it from
 * \param field field (=column) to get result from
 * \return
 *     - string holding result. 
 *     - NULL if no such result
 */
char *db_get_result(int row, int field);

/**
 * \brief return the ID generated for an AUTO_INCREMENT column
 *        by the previous column.
 * \param sequence_identifier sequence identifier
 * \return 
 *       - 0 if no such id (if for instance no AUTO_INCREMENT
 *          value was generated).
 *       - the id otherwise
 */
u64_t db_insert_result (const char *sequence_identifier);

/**
 * \brief escape a string for use in query
 * \param to string to copy escaped string to. Must be allocated by caller
 * \param from original string
 * \param length of orginal string
 * \return length of escaped string
 * \attention behaviour is undefined if to and from overlap
 */
unsigned long  db_escape_string(char *to, 
		const char *from, unsigned long length);
/**
 * \brief clean up tables. This is implemented differently in 
 * the different RDBMS's
 * \param tables array of char* holding names of tables to clean up
 * \param num_tables number of tables
 * \return 
 *     - -1 on failure
 *     - 0 on success
 */
int db_do_cleanup(const char **tables, int num_tables);

/**
 * \brief get length in bytes of a result field in a result set.
 * \param row row of field
 * \param field field number (0..nfields)
 * \return 
 *      - -1 if invalid field
 *      - length otherwise
 */
u64_t db_get_length(int row, int field);

/**
 * \brief get number of rows affected by a query
 * \return
 *    -  -1 on error (e.g. no result set)
 *    -  number of affected rows otherwise
 */
u64_t db_get_affected_rows();

/**
 * \brief switch from the normal result set to the msgbuf
 * result set from hereon. A call to db_store_msgbuf_result()
 * will reverse this situation again 
 */
void db_use_msgbuf_result();

/**
 * \brief switch from msgbuf result set to the normal result
 * set for all following database operations. This function
 * should be called after db_use_msgbuf_result() when the
 * msgbuf result has to be used later on.
 */
void db_store_msgbuf_result();

/**
 * \brief switch from normal result set to the authentication result
 * set
 */
void db_use_auth_result();

/**
 * \brief switch from authentication result set to normal result set
 */
void db_store_auth_result();

/**
 * \brief get void pointer to result set.
 * \return a pointer to a result set
 * \bug this is really ugly and should be dealt with differently!
 */
void* db_get_result_set();

/**
 * \brief set the new result set 
 * \param res the new result set
 * \bug this is really ugly and should be dealt with differently!
 */
void db_set_result_set(void* res);

/* shared implementattion from hereon */

/**
 * \brief return number of bytes used by user identified by userid
 * \param user_idnr id of the user (from users table)
 * \return
 *          - -1 on failure<BR>
 *          - 0 on empty mailbox<BR>
 *          - used number of bytes otherwise
 */
u64_t db_get_quotum_used(u64_t user_idnr);
/**
 * \brief get user id from alias
 * \param alias the alias
 * \return 
 *         - -1 on failure<BR>
 *         -  0 if no such alias found<BR>
 *         -  the uid otherwise
 */
u64_t db_get_user_from_alias(const char *alias);
/**
 * \brief get deliver_to from alias
 * \param alias the alias
 * \return 
 *         - NULL on failure
 *         - "" if no such alias found
 *         - deliver_to address otherwise
 * \attention caller needs to free the return value
 */
char* db_get_deliver_from_alias(const char *alias);
/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 * \attention aliases list needs to be empty. Method calls list_init()
 *            which sets list->start to NULL.
 */
int db_get_user_aliases(u64_t user_idnr, struct list *aliases);
/**
 * \brief add an alias for a user
 * \param user_idnr user's id
 * \param alias new alias
 * \param clientid client id
 * \return 
 *        - -1 on failure
 *        -  0 on success
 *        -  1 if alias already exists for given user
 */
int db_addalias(u64_t user_idnr, const char *alias, u64_t clientid);
/**
 * \brief add an alias to deliver to an extern address
 * \param alias the alias
 * \param deliver_to extern address to deliver to
 * \param clientid client idnr
 * \return 
 *        - -1 on failure
 *        - 0 on success
 *        - 1 if deliver_to already exists for given alias
 */
int db_addalias_ext(const char *alias, const char *deliver_to, u64_t clientid);
/**
 * \brief remove alias for user
 * \param user_idnr user id
 * \param alias the alias
 * \return
 *         - -1 on failure
 *         - 0 on success
 */
int db_removealias(u64_t user_idnr, const char *alias);
/**
 * \brief remove external delivery address for an alias
 * \param alias the alias
 * \param deliver_to the deliver to address the alias is
 *        pointing to now
 * \return
 *        - -1 on failure
 *        - 0 on success
 */
int db_removealias_ext(const char *alias, const char *deliver_to);
/**
 * \brief get auto-notification address for a user
 * \param user_idnr user id
 * \param notify_address pointer to string that will hold the address
 * \return
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free deliver_to address
 */
int db_get_notify_address(u64_t user_idnr, char **notify_address);
/** 
 * \brief get reply body for auto-replies
 * \param user_idnr user idnr 
 * \param body pointer to string that will hold reply body
 * \return 
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free reply_body
 */
int db_get_reply_body(u64_t user_idnr, char **body);

/**
 * \brief get mailbox idnr for a mailbox
 * \param user_idnr user idnr of owner of mailbox
 * \param mailbox name of mailbox
 * \return 
 *        - 0 if no such mailbox found
 *        - mailboxid_nr otherwise
 */
u64_t db_get_mailboxid(u64_t user_idnr, const char *mailbox);
/**
 * \brief get mailbox idnr of a message
 * \param message_idnr idnr of message
 * \return
 * 		- 0 if not found or failure
 * 		- mailbox_idnr otherwise
 */
u64_t db_get_message_mailboxid(u64_t message_idnr);
	
/**
 * \brief get user idnr of a message
 *        (was db_get_useridnr() in dbmysql.c)
 * \param message_idnr idnr of message
 * \return 
 * 		- 0 on failure
 * 		- user_idnr otherwise
 */
u64_t db_get_useridnr(u64_t message_idnr);

/**
 * \brief insert a message into inbox
 * \param user_idnr user idnr
 * \param deliver_to mailbox to deliver to
 * \param unique_id unique id of message
 * \return result of db_insert_result()
 */
u64_t db_insert_message(u64_t user_idnr, 
		const char *deliver_to, 
		const char *unique_id);

/**
 * \brief update unique_id, message_size and rfc_size of
 *        a message identified by message_idnr
 * \param message_idnr
 * \param unique_id unique id of message
 * \param message_size size of message
 * \param rfc_size
 * \return 0
 */
u64_t db_update_message(u64_t message_idnr, const char *unique_id,
			u64_t message_size, u64_t rfc_size);
/** 
* \brief update multiple messages with unique id  unique_id
* \param unique_id
* \param message_size size of message
* \param rfc_size
* \return 
*        - -1 on failure
*        - 0 on success
*/
int db_update_message_multiple(const char *unique_id, u64_t message_size,
		u64_t rfc_size);
/**
* \brief insert a message block into the message block table
* \param block the message block (which is a string)
* \param block_size length of the block
* \param message_idnr id of the message the block belongs to
* \return 
*        - -1 on failure
*        - result of db_insert_result() otherwise
*/
u64_t db_insert_message_block(const char *block, u64_t block_size,
		u64_t message_idnr);
/**
* \brief as insert_message_block but inserts multiple rows
*        at a time.
* \param unique_id unique id of message
* \param block the message block
* \param block_size length of block
* \return 
*          - -1 on failure
*          - 0 on success
*/
int db_insert_message_block_multiple(const char *unique_id,
		const char *block, u64_t block_size);
/**
* \brief perform a rollback for a message that has just been 
* 		inserted
* \param owner_idnr idrn of owner of message
* \param unique_id unique id of message
* \return 
* 		- -1 on failure
* 		- 0 on success
*/
int db_rollback_insert(u64_t owner_idnr, const char *unique_id);

/**
 * \brief log IP-address for POP/IMAP_BEFORE_SMTP. If the IP-address
 *        is already logged, it's timestamp is renewed.
 * \param ip the ip (xxx.xxx.xxx.xxx)
 * \return
 *        - -1 on database failure
 *        - 0 on success
 * \bug timestamp!
 */
int db_log_ip(const char *ip);

/**
* \brief clean up ip log
* \param lasttokeep all ip log entries before this timestamp will
*                     deleted
* \return 
*       - -1 on database failure
*       - 0 on success
*/
int db_cleanup_iplog(const char *lasttokeep);

/**
 * \brief cleanup database tables
 * \return 
 *       - -1 on database failure
 *       - 0 on success
 */
int db_cleanup();

/**
* \brief remove all mailboxes for a user
* \param user_idnr
* \return 
*      - -2 on memory failure
*      - -1 on database failure
*      - 0 on success
*/
int db_empty_mailbox(u64_t user_idnr);

/**
* \brief check for all message blocks  that are not connected to
*        messages.
* \param lost_list pointer to a list which will contain all lost blocks.
*        this list needs to be empty on call to this function.
* \return 
*      - -2 on memory error
*      - -1 on database error
*      - 0 on success
* \attention caller should free this memory
*/
int db_icheck_messageblks(struct list *lost_list);

/**
 * \brief check for all messages that are not connected to
 *        mailboxes
 * \param lost_list pointer to a list which will contain all lost messages
 *        this list needs to be empty on call to this function.
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      - 0 on success
 * \attention caller should free this memory
 */
int db_icheck_messages(struct list *lost_list);

/**
 * \brief check for all mailboxes that are not connected to
 *        users
 * \param lost_list pointer to a list which will contain all lost mailboxes
 *        this list needs to be empty on call to this function.
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      - 0 on success
 * \attention caller should free this memory
 */
int db_icheck_mailboxes(struct list *lost_list);

/**
 * \brief check for all messages that have 0 blocks
 * \param lost_list pointer to a list which will contain all message_idnr
 *        of messages that have 0 blocks.
 *        this list needs to be empty on call to this function.
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      - 0 on success
 * \attention caller should free this memory
 */
int db_icheck_null_messages(struct list *lost_list);

/**
 * \brief set status of a message
 * \param message_idnr
 * \param status new status of message
 * \return result of db_query()
 */
int db_set_message_status(u64_t message_idnr, int status);

/**
* \brief delete a message block
* \param messageblk_idnr
* \return result of db_query()
*/
int db_delete_messageblk(u64_t messageblk_idnr);
/**
 * \brief delete a message 
 * \param message_idnr
 * \return result of db_query()
 */
int db_delete_message(u64_t message_idnr);

/**
 * \brief delete a mailbox. 
 * \param mailbox_idnr
 * \param only_empty if non-zero the mailbox will only be emptied,
 *        i.e. all messages in it will be deleted.
* \return 
*    - -1 on database failure
*    - 0 on success
*/
int db_delete_mailbox(u64_t mailbox_idnr, int only_empty);

/**
 * \brief write lines of message to fstream. Does not write the header
 * \param fstream the stream to write to
 * \param message_idnr idrn of message to write
 * \param lines number of lines to write. If <PRE>lines == -2</PRE>, then
 *              the whole message (excluding the header) is written.
 * \param no_end_dot if 
 *                    - 0 \b do write the final "." signalling
 *                    the end of the message
 *                    - otherwise do \b not write the final "."
 * \return 
 * 		- 0 on failure
 * 		- 1 on success
 */
int db_send_message_lines (void *fstream, u64_t message_idnr,
		long lines, int no_end_dot);
/**
 * \brief create a new POP3 session. (was createsession() in dbmysql.c)
 * \param user_idnr user idnr 
 * \param session_ptr pointer to POP3 session 
 */
int db_createsession (u64_t user_idnr, PopSession_t *session_ptr);
/** 
 * \brief Clean up a POP3 Session
 * \param session_ptr pointer to POP3 session
 */
void db_session_cleanup (PopSession_t *session_ptr);
/**
 * \brief update POP3 session
 * \param session_ptr pointer to POP3 session
 * \return
 *     - -1 on failure
 *     - 0 on success
 */
int db_update_pop (PopSession_t *session_ptr);
/**
 * \brief set deleted status (=3) for all messages that are marked for
 *        delete (=2)
 * \return
 *    - -1 on database failure;
 *    - number of rows affected (>0)
 */
u64_t db_set_deleted ();
/**
 * \brief purge all messages from the database with a "delete"-status 
 * (status = 3)
 * \return 
 *     - -2 on memory failure
 *     - -1 on database failure
 *     - number of rows affected in the database (which is >= 0)
 */
u64_t db_deleted_purge();
/**
 * \brief check if a block of a certain size can be inserted.
 * \param addblocksize size of added blocks (UNUSED)
 * \param message_idnr 
 * \param *user_idnr pointer to user_idnr. This will be set to the
 *        idnr of the user associated with the message
 * \return
 *      - -2 on database failure after a limit overrun (if this
 *           occurs the DB might be inconsistent and dbmail-maintenance
 *           needs to be run)
 *      - -1 on database failure
 *      - 0 on success
 *      - 1 on limit overrun.
 * \attention when inserting a block would cause a limit run-overrun.
 *            the message insert is automagically rolled back
 */
u64_t db_check_sizelimit (u64_t addblocksize, u64_t message_idnr, 
				  u64_t *user_idnr);
/**
 * \brief insert a message into the database
 * \param msgdata the message
 * \param datalen length of message
 * \param mailbox_idnr mailbox to put it in
 * \param user_idnr
 * \return 
 *     - -1 on failure
 *     - 0 on success
 *     - 1 on invalid message
 *     - 2 on mail quotum exceeded
 */
int db_imap_append_msg(const char *msgdata, u64_t datalen,
		u64_t mailbox_idnr, u64_t user_idnr);

/* mailbox functionality */
/** 
 * \brief find mailbox "name" for a user
 * \param name name of mailbox
 * \param user_idnr
 * \return
 *      - -1 on failure
 *      - 0 if mailbox not found
 *      - mailbox_idnr otherwise
 */
u64_t db_findmailbox(const char *name, u64_t user_idnr);
/**
 * \brief finds all the mailboxes owned by owner_idnr who match 
 *        the regex pattern pattern.
 * \param owner_idnr
 * \param pattern regex pattern
 * \param children pointer to a list of mailboxes conforming to
 *        regex pattern. This will be filled when the function
 *        returns and needs to be free-d by caller
 * \param nchildren number of mailboxes in children
 * \param only_subscribed only search in subscribed mailboxes.
 * \return 
 *      - -1 on failure
 *      - 0 on success
 *      - 1 on invalid regex pattern
 */
int db_findmailbox_by_regex(u64_t owner_idnr, const char *pattern, 
			    u64_t **children, unsigned *nchildren,
			    int only_subscribed);
/**
 * \brief get info on a mailbox. Info is filled in in the
 *        mailbox_t struct.
 * \param mb the mailbox_t to fill in. (mb->uid needs to be
 *        set already!
 * \param owner_idnr owner of mailbox
 * \return
 *     - -1 on failure
 *     - 0 on success
 */
int db_getmailbox(mailbox_t *mb, u64_t owner_idnr);
/**
 * \brief create a new mailbox
 * \param name name of mailbox
 * \param owner_idnr owner of mailbox
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
int db_createmailbox(const char *name, u64_t owner_idnr);
/**
 * \brief produce a list containing the UID's of the specified
 *        mailbox' children matching the search criterion
 * \param mailbox_idnr id of parent mailbox
 * \param user_idnr
 * \param children will hold list of children
 * \param nchildren length of list of children
 * \param filter search filter
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
int db_listmailboxchildren(u64_t mailbox_idnr, u64_t user_idnr, 
			   u64_t **children, int *nchildren,
			   const char *filter);
/**
 * \brief remove mailbox
 * \param mailbox_idnr
 * \param owner_idnr
 * \return 
 *     - -1 on failure
 *     - 0 on success
 * \bug the mailbox should have no children. However
 * this is not checked by the function. 
 * \bug This function is redundant. functions should not
 * call this function but should use db_delete_mailbox instead
 */
int db_removemailbox(u64_t mailbox_idnr, u64_t owner_idnr);
/**
 * \brief check if mailbox is selectable
 * \param mailbox_idnr
 * \return 
 *    - -1 on failure
 *    - 0 if not selectable
 *    - 1 if selectable
 */
int db_isselectable(u64_t mailbox_idnr);
/**
 * \brief check if mailbox has no_inferiors flag set.
 * \param mailbox_idnr
 * \return
 *    - -1 on failure
 *    - 0 flag not set
 *    - 1 flag set
 */
int db_noinferiors(u64_t mailbox_idnr);

/**
 * \brief set selectable flag of a mailbox on/of
 * \param mailbox_idnr
 * \param select_value 
 *            - 0 for not select
 *            - 1 for select
 * \return
 *     - -1 on failure
 *     - 0 on success
 */
int db_setselectable(u64_t mailbox_idnr, int select_value);
/** 
 * \brief remove all messages from a mailbox
 * \param mailbox_idnr
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_removemsg(u64_t mailbox_idnr);
/**
 * \brief move all messages from one mailbox to another.
 * \param mailbox_to idnr of mailbox to move messages from.
 * \param mailbox_from idnr of mailbox to move messages to.
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_movemsg(u64_t mailbox_to, u64_t mailbox_from);
/**
 * \brief copy a message to a mailbox
 * \param msg_idnr
 * \param mailbox_to mailbox to copy to
 * \return 
 * 		- -2 if the quotum is exceeded
 * 		- -1 on failure
 * 		- 0 on success
 * \bug needs a seperate table for copying the
 * messages. This is ugly and should be fixed. 
 * possibly by storing all info dynamically in
 * the c-code, or by changing the way a copy works.
 * Note: PostgreSQL used to work without the copy-temp-tables,
 * because of some non-ANSI SQL behaviour, but now it works
 * in the same way as other DBMSs
 * \todo change this functions to use C-functions instead of
 * the ugly extra tables for copying message and message blocks
 */
int db_copymsg(u64_t msg_idnr, u64_t mailbox_to);
/**
 * \brief get name of mailbox
 * \param mailbox_idnr
 * \param name will hold the name
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 * \attention name should be large enough to hold the name 
 * (preferably of size IMAP_MAX_MAILBOX_NAMELEN + 1)
 */
int db_getmailboxname(u64_t mailbox_idnr, char *name);
/**
 * \brief set name of mailbox
 * \param mailbox_idnr
 * \param name new name of mailbox
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_setmailboxname(u64_t mailbox_idnr, const char *name);
/**
 * \brief remove all messages from a mailbox that have the delete flag
 *        set. Remove is done by setting status to 2. A list holding
 *        messages_idnrs of all removed messages is made in msg_idnrs.
 *        nmsgs will hold the number of deleted messages.
 * \param mailbox_idnr
 * \param msg_idnrs pointer to a list of msg_idnrs. If NULL, or nmsg
 *        is NULL, no list will be made.
 * \param nmsgs will hold the number of memebers in the list. If NULL,
 *        or msg_idnrs is NULL, no list will be made.
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 * \attention caller should free msg_idnrs and nmsg
 */
int db_expunge(u64_t mailbox_idnr,
		u64_t **msg_idnrs,
		u64_t *nmsgs);
/**
 * \brief get first unseen message in a mailbox
 * \param mailbox_idnr
 * \return 
 *     - -1 on failure
 *     - msg_idnr of first unseen message in mailbox otherwise
 */
u64_t db_first_unseen(u64_t mailbox_idnr);
/**
 * \brief subscribe to a mailbox.
 * \param mailbox_idnr
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_subscribe(u64_t mailbox_idnr);
/**
 * \brief unsubscribe from a mailbox
 * \param mailbox_idnr
 * \return
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_unsubscribe(u64_t mailbox_idnr);

/* message functionality */

/**
 * \brief get a flag for a message specified by flag_name.
 * \param flag_name name of flag
 * \param msg_idnr
 * \param mailbox_idnr
 * \return 
 * 		- -1 on failure
 * 		- 0 if flag is not set or a non-existent flag is asked.
 * 		- 1 if flag is set.
 */
int db_get_msgflag(const char *flag_name, 
		u64_t msg_idnr, u64_t mailbox_idnr);

/**
 * \brief get all flags for a message
 * \param msg_idnr
 * \param mailbox_idnr
 * \param flags An array of IMAP_NFLAGS elements. 
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_get_msgflag_all(u64_t msg_idnr, u64_t mailbox_idnr, int *flags);

/**
 * \brief get all all flags for all messages in a range
 * \param msg_idnr_low lowest id of message to get flags for
 * \param msg_idnr_high highest id of message to get flags for
 * \param mailbox_idnr mailbox of messages
 * \param flags will contain list of flags after function has returned
 * \param resultsetlen length of flags list.
 * \return 
 *     - -1 on failure
 *     -  0 on success
 * \todo implement this function or remove it. There is no function within
 * dbmail using this function. It is only implemented for PostgreSQL in
 * the original code.
 */
int db_get_msgflag_all_range(u64_t msg_idnr_low, u64_t msg_idnr_high, 
		u64_t mailbox_idnr, int **flags, unsigned *resultsetlen);

/**
 * \brief set flags for a message
 * \param msg_idnr
 * \param mailbox_idnr
 * \param flags Array of IMAP_NFLAGS elements. See
 *               imapcommand.c for details.
 * \param action_type 
 *        - IMAPFA_REPLACE new set will be exactly as flags,
 *          with '1' for set, and '0' for not set.
 *        - IMAPFA_ADD set all flags which have a '1' as value
 *          in flags
 *        - IMAPFA_REMOVE clear all flags that have '1' as value
 *          in flags
 * \return 
 * 		- -1 on failure
 * 		-  0 on success
 */
int db_set_msgflag(u64_t msg_idnr, u64_t mailbox_idnr, int *flags, int action_type);
/**
 * \brief set flags for a range of messages in a mailbox
 * \param msg_idnr_low beginning of range
 * \param msg_idnr_high end of range
 * \param mailbox_idnr
 * \param flags Array of IMAP_NFLAGS elements. See
 *               imapcommand.c for details.
 * \param action_type 
 *        - IMAPFA_REPLACE new set will be exactly as flags,
 *          with '1' for set, and '0' for not set.
 *        - IMAPFA_ADD set all flags which have a '1' as value
 *          in flags
 *        - IMAPFA_REMOVE clear all flags that have '1' as value
 *          in flags
 * \return 
 * 		- -1 on failure
 * 		-  0 on success
 */ 
int db_set_msgflag_range(u64_t msg_idnr_low, 
		u64_t msg_idnr_high, u64_t mailbox_idnr, 
		int *flags, int action_type);
/**
 * \brief retrieve internal message date
 * \param mailbox_idnr
 * \param msg_idnr
 * \param date string of size IMAP_INTERNALDATE_LEN which will
 *        hold the date after call.
 * \return 
 *     - -1 on failure
 *     -  0 on success
 */
int db_get_msgdate(u64_t mailbox_idnr, u64_t msg_idnr, char *date);
/**
 * \brief set the RFCSIZE field of a message
 * \param rfcsize new rfc size
 * \param msg_idnr
 * \param mailbox_idnr
 * \return
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_set_rfcsize(u64_t rfcsize, u64_t msg_idnr, u64_t mailbox_idnr);
/**
 * \brief get the RFCSIZE field of a message
 * \param msg_idnr
 * \param mailbox_idnr
 * \return
 * 		- -1 on failure
 * 		- rfcsize otherwise
 */
u64_t db_get_rfcsize(u64_t msg_idnr, u64_t mailbox_idnr);
/**
 * \brief get info on a range of messages.
 * \param msg_idnr_low beginning of range
 * \param msg_idnr_high end of range
 * \param mailbox_idnr
 * \param get_flags if non-zero, get message flags
 * \param get_internaldate if non-zero get internal date
 * \param get_rfcsize if non-zero get rfcsize
 * \param get_msg_idnr if non-zero get message idnr
 * \param result will hold a list of msginfo_t
 * \param resultsetlen length of result set
 * \return 
 *    - -2 on memory error
 *    - -1 on database error
 *    - 0 on success
 * \attention caller should free result.
 */
int db_get_msginfo_range(u64_t msg_idnr_low, u64_t msg_idnr_high,
		u64_t mailbox_idnr, int get_flags, int get_internaldate,
		int get_rfcsize, int get_msg_idnr,
		msginfo_t **result, unsigned *resultsetlen);

/**
 * \brief builds a list containing the fields of
 * the main header.
 * \param msg_idnr
 * \param hdrlist will hold the list when finished
 * \return
 *    - -3 parse error
 *    - -2 memory error
 *    - -1 database error
 *    - 0 success
 * \attention hdrlist should be empty on call.
 */
int db_get_main_header(u64_t msg_idnr, struct list *hdrlist);
/**
 * \brief check if a message belongs to a mailbox
 * \param mailbox_idnr
 * \param message_idnr
 * \return 
 *    - -1 on failure
 *    -  0 if message does not belong to mailbox
 *    - 1 if message belongs to mailbox
 */
int db_mailbox_msg_match(u64_t mailbox_idnr, u64_t message_idnr);

#endif
