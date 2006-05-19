/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * $Id: db.h 2095 2006-04-30 15:59:15Z paul $
 *
 * dbase driver header file
 * Functions for database communication 
 *
 * should connect to any dbXXX.c file where XXXX is the dbase to be used
 */

#ifndef  _DB_H
#define  _DB_H

#include "dbmail.h"

#define DEF_QUERYSIZE 1024
#define DUMP_BUF_SIZE 1024

#define ICHECK_RESULTSETSIZE 1000000

#define MAX_EMAIL_SIZE 250

/* size of the messageblk's */
#define READ_BLOCK_SIZE (512ul*1024ul)	/* be carefull, MYSQL has a limit */

/* config types */
#define CONFIG_MANDATORY 1
#define CONFIG_EMPTY 0

/* name of internal delivery user. */
#define DBMAIL_DELIVERY_USERNAME "__@!internal_delivery_user!@__"
/**
 * standard user for ACL anyone (see RFC 2086)
 */
#define DBMAIL_ACL_ANYONE_USER "anyone"

/* 
 * PROTOTYPES 
 */

/**
 * \brief load the database driver module
 * \return
 *   - 1 on modules unsupported
 *   - 0 on success
 *   - -1 on failure to load module
 *   - -2 on missing symbols
 *   - -3 on memory error
 * \file dbmodule.c FIXME: Is \file the right doc convention?
 * \side-effects: calls TRACE_FATAL for errors, so no real return values anyways.
 */
int db_load_driver(void);

/* Implemented differently for MySQL and PostgreSQL: */
/**
 * \brief connect to the database
 * \return 
 *         - -1 on failure
 *         - 0 otherwise
 */
int db_connect(void);

/*
 * make sure we're running against a current database layout 
 */

int db_check_version(void);


/**
 * \brief check database connection. If it is dead, reconnect
 * \return
 *    - -1 on failure (no connection to db possible)
 *    -  0 on success
 */
int db_check_connection(void);

/**
 * \brief check database for existence of usermap table
 * \return
 * -  0 on table not found
 * -  1 on table found
 */
int db_use_usermap(void);

/**
 * \brief check if username exists in the usermap table
 * \param ci clientinfo_t of connected client
 * \param userid login to lookup
 * \param real_username contains userid after successful lookup
 * \return
 *  - -1 on db failure
 *  -  0 on success
 *  -  1 on not found
 */
int db_usermap_resolve(clientinfo_t *ci, const char *userid, char * real_username);

/**
 * \brief disconnect from database server
 * \return 0
 */
int db_disconnect(void);

/**
 * \brief execute a database query
 * \param the_query the SQL query to execute
 * \return 
 *         - 0 on success
 *         - 1 on failure
 */
int db_query(const char *the_query);

/**
 * \brief get number of rows in result set.
 * \return 
 *     - 0 on failure or no rows
 *     - number of rows otherwise
 */
unsigned db_num_rows(void);

/**
 * \brief get number of fields in result set.
 * \return
 *      - 0 on failure or no fields
 *      - number of fields otherwise
 */
unsigned db_num_fields(void);

/**
 * \brief clear the result set of a query
 */
void db_free_result(void);

/** \defgroup db_get_result_group Variations of db_get_result()
 * \brief get a single result field row from the result set
 * \param row result row to get it from
 * \param field field (=column) to get result from
 * \return
 *     - pointer to string holding result. 
 *     - NULL if no such result
 */
/*@dependent@*/ const char *db_get_result(unsigned row, unsigned field);

/** \ingroup db_get_result_group
 * \brief Returns the result as an Integer
 */
int db_get_result_int(unsigned row, unsigned field);

/** \ingroup db_get_result_group
 * \brief Returns the result as an Integer from 0 to 1
 */
int db_get_result_bool(unsigned row, unsigned field);

/** \ingroup db_get_result_group
 * \brief Returns the result as an Unsigned 64 bit Integer
 */
u64_t db_get_result_u64(unsigned row, unsigned field);

/**
 * \brief return the ID generated for an AUTO_INCREMENT column
 *        by the previous column.
 * \param sequence_identifier sequence identifier
 * \return 
 *       - 0 if no such id (if for instance no AUTO_INCREMENT
 *          value was generated).
 *       - the id otherwise
 */
u64_t db_insert_result(const char *sequence_identifier);

/**
 * \brief escape a string for use in query
 * \param to string to copy escaped string to. Must be allocated by caller
 * \param from original string
 * \param length of orginal string
 * \return length of escaped string
 * \attention behaviour is undefined if to and from overlap
 */
unsigned long db_escape_string(char *to,
			       const char *from, unsigned long length);

/**
 * \brief escape a binary data for use in query
 * \param to string to copy escaped string to. Must be allocated by caller
 * \param from original string
 * \param length of orginal string
 * \return length of escaped string
 * \attention behaviour is undefined if to and from overlap
 */
unsigned long db_escape_binary(char *to,
			       const char *from, unsigned long length);

/**
 * \brief get length in bytes of a result field in a result set.
 * \param row row of field
 * \param field field number (0..nfields)
 * \return 
 *      - -1 if invalid field
 *      - length otherwise
 */
u64_t db_get_length(unsigned row, unsigned field);

/**
 * \brief get number of rows affected by a query
 * \return
 *    -  -1 on error (e.g. no result set)
 *    -  number of affected rows otherwise
 */
u64_t db_get_affected_rows(void);

/**
 * \brief switch from the normal result set to the msgbuf
 * result set from hereon. A call to db_store_msgbuf_result()
 * will reverse this situation again 
 */
void db_use_msgbuf_result(void);

/**
 * \brief switch from msgbuf result set to the normal result
 * set for all following database operations. This function
 * should be called after db_use_msgbuf_result() when the
 * msgbuf result has to be used later on.
 */
void db_store_msgbuf_result(void);

/**
 * \brief switch from normal result set to the authentication result
 * set
 */
void db_use_auth_result(void);

/**
 * \brief switch from authentication result set to normal result set
 */
void db_store_auth_result(void);

/**
 * \brief get void pointer to result set.
 * \return a pointer to a result set
 * \bug this is really ugly and should be dealt with differently!
 */
void *db_get_result_set(void);

/**
 * \brief set the new result set 
 * \param res the new result set
 * \bug this is really ugly and should be dealt with differently!
 */
void db_set_result_set(void *res);

/**
 * begin transaction
 * \return 
 *     - -1 on error
 *     -  0 otherwise
 */
int db_begin_transaction(void);

/**
 * commit transaction
 * \return
 *      - -1 on error
 *      -  0 otherwise
 */
int db_commit_transaction(void);

/**
 * rollback transaction
 * \return 
 *     - -1 on error
 *     -  0 otherwise
 */
int db_rollback_transaction(void);

int mailbox_is_writable(u64_t mailbox_idnr);

/**
 * set savepoint to transaction
 * \param name
 * \return
 *     - -1 on error
 *     -  0 otherwise
 */
int db_savepoint_transaction(const char*);

/**
 * rollback transaction to savepoint
 * \param name
 * \return
 *     - -1 on error
 *     -  0 otherwise
 */
int db_rollback_savepoint_transaction(const char*);

/* shared implementattion from hereon */
/**
 * \brief get the physmessage_id from a message_idnr
 * \param message_idnr
 * \param physmessage_id will hold physmessage_id on return. Must hold a valid
 * pointer on call.
 * \return 
 *     - -1 on error
 *     -  0 if a physmessage_id was found
 *     -  1 if no message with this message_idnr found
 * \attention function will fail and halt program if physmessage_id is
 * NULL on call.
 */
int db_get_physmessage_id(u64_t message_idnr, /*@out@*/ u64_t * physmessage_id);

/**
 * \brief return number of bytes used by user identified by userid
 * \param user_idnr id of the user (from users table)
 * \param curmail_size will hold current mailsize used (should hold a valid 
 * pointer on call).
 * \return
 *          - -1 on failure<BR>
 *          -  1 otherwise
 */
int db_get_quotum_used(u64_t user_idnr, u64_t * curmail_size);

/**
 * \brief finds all users which need to have their curmail_size (amount
 * of space used by user) updated. Then updates this number in the
 * database
 * \return 
 *     - -2 on memory error
 *     - -1 on database error
 *     -  0 on success
 */
int db_calculate_quotum_all(void);

/**
 * \brief performs a recalculation of used quotum of a user and puts
 *  this value in the users table.
 * \param user_idnr
 * \return
 *   - -1 on db_failure
 *   -  0 on success
 */
int db_calculate_quotum_used(u64_t user_idnr);
/**
 * \brief get a specific sieve script for a user
 * \param user_idnr user id
 * \param scriptname string with name of the script to get
 * \param script pointer to string that will hold the script itself
 * \return
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free the returned script
 */
int db_get_sievescript_byname(u64_t user_idnr, char *scriptname,
			      char **script);
/**
 * \brief Check if the user has an active sieve script.
 * \param user_idnr user id
 * \return
 *        - -1 on error
 *        - 0 when user has an active script
 *        - 1 when user doesn't have an active script
 */
int db_check_sievescript_active(u64_t user_idnr);
/**
 * \brief get the name of the active sieve script for a user
 * \param user_idnr user id
 * \param scriptname pointer to string that will hold the script name
 * \return
 *        - -3 on failure to find a matching row in database
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free the returned script name
 */
int db_get_sievescript_active(u64_t user_idnr, char **scriptname);
/**
 * \brief get a list of sieve scripts for a user
 * \param user_idnr user id
 * \param scriptlist pointer to struct dm_list that will hold script names
 * \return
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free the struct dm_list and its contents
 */
int db_get_sievescript_listall(u64_t user_idnr, struct dm_list *scriptlist);
/**
 * \brief rename a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the current name of the script
 * \param newname is the new name the script will be changed to
 * \return
 *        - -3 on non-existent script name
 *        - -2 on NULL scriptname or script
 *        - -1 on database failure
 *        - 0 on success
 */
int db_rename_sievescript(u64_t user_idnr, char *scriptname,
			   char *newname);
/**
 * \brief add a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be added
 * \param scriptname is the script itself to be added
 * \return
 *        - -3 on non-existent script name
 *        - -2 on NULL scriptname or script
 *        - -1 on database failure
 *        - 0 on success
 */
int db_add_sievescript(u64_t user_idnr, char *scriptname, char *script);
/**
 * \brief deactivate a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be activated
 * \return
 *        - -3 on non-existent script name
 *        - -2 on bad or wrong script name
 *        - -1 on database failure
 *        - 0 on success
 */
int db_deactivate_sievescript(u64_t user_idnr, char *scriptname);
/**
 * \brief activate a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be activated
 * \return
 *        - -3 on non-existent script name
 *        - -2 on bad or wrong script name
 *        - -1 on database failure
 *        - 0 on success
 */
int db_activate_sievescript(u64_t user_idnr, char *scriptname);
/**
 * \brief delete a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be deleted
 * \return
 *        - -3 on non-existent script name
 *        - -1 on database failure
 *        - 0 on success
 */
int db_delete_sievescript(u64_t user_idnr, char *scriptname);
/**
 * \brief checks to see if the user has space for a script
 * \param user_idnr user id
 * \param scriptlen is the size of the script we might insert
 * \return
 *        - -3 if there is not enough space
 *        - -1 on database failure
 *        - 0 on success
 */
int db_check_sievescript_quota(u64_t user_idnr, u64_t scriptlen);
/**
 * \brief sets the sieve script quota for a user
 * \param user_idnr user id
 * \param quotasize is the desired new quota size
 * \return
 *        - -1 on database failure
 *        - 0 on success
 */
int db_set_sievescript_quota(u64_t user_idnr, u64_t quotasize);
/**
 * \brief gets the current sieve script quota for a user
 * \param user_idnr user id
 * \param quotasize will be filled with the current quota size
 * \return
 *        - -1 on database failure
 *        - 0 on success
 */
int db_get_sievescript_quota(u64_t user_idnr, u64_t * quotasize);
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
 * \brief get user idnr of a message. 
 * \param message_idnr idnr of message
 * \return 
 *              - -1 on failure
 * 		- 0 if message is located in a shared mailbox.
 * 		- user_idnr otherwise
 */
u64_t db_get_useridnr(u64_t message_idnr);

/**
 * \brief insert a new physmessage. This inserts only an new record in the
 * physmessage table with a timestamp
 * \param physmessage_id will hold the id of the physmessage on return. Must 
 * hold a valid pointer on call.
 * \return 
 *     - -1 on failure
 *     -  1 on success
 */
int db_insert_physmessage(/*@out@*/ u64_t * physmessage_id);

/**
 * \brief insert a physmessage with an internal date.
 * \param internal_date the internal date in "YYYY-MM-DD HH:Mi:SS"
 * \param physmessage_id will hold the id of the physmessage on return. Must
 * hold a valid pointer on call.
 * \return 
 *    - -1 on failure
 *    -  1 on success
 */
int db_insert_physmessage_with_internal_date(timestring_t internal_date,
					     u64_t * physmessage_id);

/**
 * \brief update unique_id, message_size and rfc_size of
 *        a message identified by message_idnr
 * \param message_idnr
 * \param unique_id unique id of message
 * \param message_size size of message
 * \param rfc_size
 * \return 
 *      - -1 on database error
 *      - 0 on success
 */
int db_update_message(u64_t message_idnr, const char *unique_id,
		      u64_t message_size, u64_t rfc_size);
/**
 * \brief set unique id of a message 
 * \param message_idnr
 * \param unique_id unique id of message
 * \return 
 *     - -1 on database error
 *     -  0 on success
 */
int db_message_set_unique_id(u64_t message_idnr, const char *unique_id);

/**
 * \brief set messagesize and rfcsize of a message
 * \param physmessage_id 
 * \param message_size
 * \param rfc_size
 * \return
 *    - -1 on failure
 *    -  0 on succes
 */
int db_physmessage_set_sizes(u64_t physmessage_id, u64_t message_size,
			     u64_t rfc_size);

/**
 * \brief insert a messageblock for a specific physmessage
 * \param block the block
 * \param block_size size of block
 * \param physmessage_id id of physmessage to which the block belongs.
 * \param messageblock_idnr will hold id of messageblock after call. Should
 * hold a valid pointer on call.
 * \return
 *      - -1 on failure
 *      -  0 on success
 */
int db_insert_message_block_physmessage(const char *block,
					u64_t block_size,
					u64_t physmessage_id,
					u64_t * messageblock_idnr,
					unsigned is_header);
/**
* \brief insert a message block into the message block table
* \param block the message block (which is a string)
* \param block_size length of the block
* \param message_idnr id of the message the block belongs to
* \param messageblock_idnr will hold id of messageblock after call. Should
* be a valid pointer on call.
* \return 
*        - -1 on failure
*        - 0 otherwise
*/
int db_insert_message_block(const char *block, u64_t block_size,
			    u64_t message_idnr, 
			    /*@out@*/ u64_t * messageblock_idnr,
			    unsigned is_header);
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
int db_cleanup_iplog(const char *lasttokeep, u64_t *affected_rows);
int db_count_iplog(const char *lasttokeep, u64_t *affected_rows);

/**
 * \brief cleanup database tables
 * \return 
 *       - -1 on database failure
 *       - 0 on success
 */
int db_cleanup(void);

/**
 * \brief execute cleanup of database tables (have no idea why there
 *        are two functions for this..)
 * \param tables array of char* holding table names 
 * \param num_tables number of tables in tables
 * \return 
 *    - -1 on db failure
 *    - 0 on success
 */
int db_do_cleanup(const char **tables, int num_tables);

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
*        a physmessage. This can only occur when in use with a 
*        database that does not check for foreign key constraints.
* \param lost_list pointer to a list which will contain all lost blocks.
*        this list needs to be empty on call to this function.
* \return 
*      - -2 on memory error
*      - -1 on database error
*      - 0 on success
* \attention caller should free this memory
*/
int db_icheck_messageblks(struct dm_list *lost_list);

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
int db_icheck_messages(struct dm_list *lost_list);

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
int db_icheck_mailboxes(struct dm_list *lost_list);

/**
 * \brief check for all messages that are not connected to physmessage
 * records. This function is not nessecary when using foreign key
 * constraints. 
 * \param lost_list pointer to a list which will contain all message_idnr
 *        of messages that are not connected to physmessage records.
 *        this list needs to be empty on call to this function.
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      - 0 on success
 * \attention caller should free this memory
 */
int db_icheck_null_messages(struct dm_list *lost_list);

/**
 * \brief check for all physmessage records that have no messageblks 
 * associated with them.
 * \param null_list pointer to a list which will contain all physmessage_ids
 * of messages that are not connected to physmessage records.
 * this list needs to be empty on call to this function.
 * \return
 *     - -2 on memory error
 *     - -1 on database error
 *     -  0 on success.
 * \attention caller should free this memory
 */
int db_icheck_null_physmessages(struct dm_list *lost_list);

/**
 * \brief check for is_header flag on messageblks
 *
 */
int db_icheck_isheader(GList  **lost);
int db_set_isheader(GList *lost);

/** 
 * \brief check for cached header values
 *
 */
int db_icheck_headercache(GList **lost);
int db_set_headercache(GList *lost);

/**
 * \brief check for rfcsize in physmessage table
 *
 */
int db_icheck_rfcsize(GList **lost);
int db_update_rfcsize(GList *lost);

/**
 * \brief set status of a message
 * \param message_idnr
 * \param status new status of message
 * \return result of db_query()
 */
int db_set_message_status(u64_t message_idnr, MessageStatus_t status);

/**
* \brief delete a message block
* \param messageblk_idnr
* \return result of db_query()
*/
int db_delete_messageblk(u64_t messageblk_idnr);
/**
 * \brief delete a physmessage
 * \param physmessage_id
 * \return
 *      - -1 on error
 *      -  1 on success
 */
int db_delete_physmessage(u64_t physmessage_id);

/**
 * \brief delete a message 
 * \param message_idnr
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int db_delete_message(u64_t message_idnr);

/**
 * \brief delete a mailbox. 
 * \param mailbox_idnr
 * \param only_empty if non-zero the mailbox will only be emptied,
 *        i.e. all messages in it will be deleted.
 * \param update_curmail_size if non-zero the curmail_size of the
 *        user will be updated.
* \return 
*    - -1 on database failure
*    - 0 on success
* \attention this function is unable to delete shared mailboxes
*/
int db_delete_mailbox(u64_t mailbox_idnr, int only_empty,
		      int update_curmail_size);

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
int db_send_message_lines(void *fstream, u64_t message_idnr,
			  long lines, int no_end_dot);
/**
 * \brief create a new POP3 session. (was createsession() in dbmysql.c)
 * \param user_idnr user idnr 
 * \param session_ptr pointer to POP3 session 
 */
int db_createsession(u64_t user_idnr, PopSession_t * session_ptr);
/** 
 * \brief Clean up a POP3 Session
 * \param session_ptr pointer to POP3 session
 */
void db_session_cleanup(PopSession_t * session_ptr);
/**
 * \brief update POP3 session
 * \param session_ptr pointer to POP3 session
 * \return
 *     - -1 on failure
 *     - 0 on success
 * \attention does not update shared mailboxes (which should
 * not be nessecary, because the shared mailboxes are not 
 * touched by POP3
 */
int db_update_pop(PopSession_t * session_ptr);
/**
 * \brief set deleted status (=3) for all messages that are marked for
 *        delete (=2)
 * \param affected_rows will hold the number of affected messages on return. 
 * must hold a valid pointer on call.
 * \return
 *    - -1 on database failure;
 *    - 1 otherwise
 */
int db_set_deleted(u64_t * affected_rows);
/**
 * \brief purge all messages from the database with a "delete"-status 
 * (status = 3)
 * \param affected_rows will hold the number of affected rows on return. Must
 * hold a valid pointer on call
 * \return 
 *     - -2 on memory failure
 *     - -1 on database failure
 *     - 0 if no messages deleted (affected_rows will also hold '0')
 *     - 1 if a number of messages deleted (affected_rows will hold the number
 *       of deleted messages.
 */
int db_deleted_purge(u64_t * affected_rows);
int db_deleted_count(u64_t * affected_rows);
/**
 * \brief check if a block of a certain size can be inserted.
 * \param addblocksize size of added blocks (UNUSED)
 * \param message_idnr 
 * \param *user_idnr pointer to user_idnr. This will be set to the
 *        idnr of the user associated with the message
 * \return
 *      - -2 on database failure after a limit overrun (if this
 *           occurs the DB might be inconsistent and dbmail-util
 *           needs to be run)
 *      - -1 on database failure
 *      - 0 on success
 *      - 1 on limit overrun.
 * \attention when inserting a block would cause a limit run-overrun.
 *            the message insert is automagically rolled back
 */
u64_t db_check_sizelimit(u64_t addblocksize, u64_t message_idnr,
			 u64_t * user_idnr);
/**
 * \brief insert a message into the database
 * \param msgdata the message
 * \param datalen length of message
 * \param mailbox_idnr mailbox to put it in
 * \param user_idnr
 * \param internal_date internal date of message. May be '\0'.
 * \return 
 *     - -1 on failure
 *     - 0 on success
 *     - 1 on invalid message
 *     - 2 on mail quotum exceeded
 */
int db_imap_append_msg(const char *msgdata, u64_t datalen,
		       u64_t mailbox_idnr, u64_t user_idnr,
		       timestring_t internal_date, u64_t * msg_idnr);

/* mailbox functionality */
/** 
 * \brief find mailbox "name" for a user
 * \param name name of mailbox
 * \param user_idnr
 * \param mailbox_idnr will hold mailbox_idnr after return. Must hold a valid
 * pointer on call.
 * \return
 *      - -1 on failure
 *      - 0 if mailbox not found
 *      - 1 if found
 */
int db_findmailbox(const char *name, u64_t user_idnr,
		   /*@out@*/ u64_t * mailbox_idnr);
/**
 * \brief finds all the mailboxes owned by owner_idnr who match 
 *        the pattern.
 * \param owner_idnr
 * \param pattern pattern
 * \param children pointer to a list of mailboxes conforming to
 *        pattern. This will be filled when the function
 *        returns and needs to be free-d by caller
 * \param nchildren number of mailboxes in children
 * \param only_subscribed only search in subscribed mailboxes.
 * \return 
 *      - -1 on failure
 *      - 0 on success
 */
int db_findmailbox_by_regex(u64_t owner_idnr, const char *pattern,
			    u64_t ** children, unsigned *nchildren,
			    int only_subscribed);
/**
 * \brief get info on a mailbox. Info is filled in in the
 *        mailbox_t struct.
 * \param mb the mailbox_t to fill in. (mb->uid needs to be
 *        set already!
 * \return
 *     - -1 on failure
 *     - 0 on success
 */
int db_getmailbox_flags(mailbox_t *mb);
int db_getmailbox_count(mailbox_t *mb);
int db_getmailbox(mailbox_t * mb);

/**
 * \brief find owner of a mailbox
 * \param mboxid id of mailbox
 * \param owner_id will hold owner of mailbox after return
 * \return
 *    - -1 on db error
 *    -  0 if owner not found
 *    -  1 if owner found
 */
int db_get_mailbox_owner(u64_t mboxid, /*@out@*/ u64_t * owner_id);

/**
 * \brief check if a user is owner of the specified mailbox 
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return
 *     - -1 on db error
 *     -  0 if not user
 *     -  1 if user
 */
int db_user_is_mailbox_owner(u64_t userid, u64_t mboxid);

/**
 * \brief create a new mailbox
 * \param name name of mailbox
 * \param owner_idnr owner of mailbox
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
int db_createmailbox(const char *name, u64_t owner_idnr,
		     u64_t * mailbox_idnr);
/**
 * \brief set permission on a mailbox (readonly/readwrite)
 * \param mailbox_id idnr of mailbox
 * \return
 *  - -1 on database failure
 *  - 0 on success
 *  - 1 on failure
 */
int db_mailbox_set_permission(u64_t mailbox_id, int permission);

/**
 * \brief find a mailbox, create if not found
 * \param name name of mailbox
 * \param owner_idnr owner of mailbox
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
int db_find_create_mailbox(const char *name, mailbox_source_t source,
		u64_t owner_idnr, /*@out@*/ u64_t * mailbox_idnr);
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
			   u64_t ** children, int *nchildren,
			   const char *filter);

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
int db_removemsg(u64_t user_idnr, u64_t mailbox_idnr);
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
 * \param user_idnr user to copy the messages for.
 * \return 
 * 		- -2 if the quotum is exceeded
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_copymsg(u64_t msg_idnr, u64_t mailbox_to,
	       u64_t user_idnr, u64_t * newmsg_idnr);
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
int db_getmailboxname(u64_t mailbox_idnr, u64_t user_idnr, char *name);
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
	       u64_t user_idnr, u64_t ** msg_idnrs, u64_t * nmsgs);
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
 * \param mailbox_idnr mailbox to subscribe to
 * \param user_idnr user to subscribe
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_subscribe(u64_t mailbox_idnr, u64_t user_idnr);
/**
 * \brief unsubscribe from a mailbox
 * \param mailbox_idnr
 * \param user_idnr
 * \return
 * 		- -1 on failure
 * 		- 0 on success
 */
int db_unsubscribe(u64_t mailbox_idnr, u64_t user_idnr);

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
int db_set_msgflag(u64_t msg_idnr, u64_t mailbox_idnr, int *flags,
		   int action_type);
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

int db_set_msgflag_recent(u64_t msg_idnr, u64_t mailbox_idnr);

int db_set_msgflag_recent_range(u64_t msg_idnr_lo, u64_t msg_idnr_hi, u64_t mailbox_idnr);


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
 * \param rfc_size will hold RFCSIZE after return. Must be a valid pointer
 * on call.
 * \return
 * 		- -1 on failure
 * 		- 1 on success
 */
int db_get_rfcsize(u64_t msg_idnr, u64_t mailbox_idnr, u64_t * rfc_size);
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
			 u64_t mailbox_idnr, int get_flags,
			 int get_internaldate, int get_rfcsize,
			 int get_msg_idnr, msginfo_t ** result,
			 unsigned *resultsetlen);

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
int db_get_main_header(u64_t msg_idnr, struct dm_list *hdrlist, const char *headername);
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

/**
 * \brief check if a user has a certain right to a mailbox
 * \param mailbox
 * \param user_idnr id of user
 * \param right_flag string holding the flag to check for
 * \return
 *     - -1 on db error
 *     -  0 if no right
 *     -  1 if user has the right
 */
int db_acl_has_right(mailbox_t *mailbox, u64_t user_idnr, const char *right_flag);
/**
 * \brief get all permissions on a mailbox for a user
 * \param mailbox
 * \param user_idnr id of user
 * \param map result 
 * 
 */
int db_acl_get_acl_map(mailbox_t *mailbox, u64_t userid, struct ACLMap *map);

/**
 * \brief set one right in an acl for a user
 * \param userid id of user
 * \param mboxid id of mailbox
 * \param right_flag string holding the acl to set
 * \param set 0 if flag will be set to 0, 1 otherwise
 * \return 
 *     - -1 on error
 *     -  1 on success
 * \note if the user has no acl for this mailbox, it
 *       will be created.
 */
int db_acl_set_right(u64_t userid, u64_t mboxid,
		     const char *right_flag, int set);

/**
 * \brief delete an ACL for a user, mailbox
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return
 *      - -1 on db failure
 *      -  1 on success
 */
int db_acl_delete_acl(u64_t userid, u64_t mboxid);

/**
 * \brief get a list of all identifiers which have rights on a mailbox.
 * \param mboxid id of mailbox
 * \param identifier_list list of identifiers
 * \return 
 *     - -2 on mem failure
 *     - -1 on db failure
 *     -  1 on success
 * \note identifier_list needs to be empty on call.
 */
int db_acl_get_identifier(u64_t mboxid, 
			  /*@out@*/ struct dm_list *identifier_list);
/**
 * constructs a string for use in queries. This is used to not be dependent
 * on the internal representation of a date in the database. Whenever the
 * date is queried for in a query, this function is used to get the right
 * database function for the query (TO_CHAR(date,format) for Postgres, 
 * DATE_FORMAT(date, format) for MySQL).
 */
char *date2char_str(const char *column);


int db_getmailbox_list_result(u64_t mailbox_idnr, u64_t user_idnr, mailbox_t * mb);

/* 
 * db-user accessors
 */

int db_user_exists(const char *username, u64_t * user_idnr);
int db_user_create_shadow(const char *username, u64_t * user_idnr);
int db_user_create(const char *username, const char *password, const char *enctype,
		 u64_t clientid, u64_t maxmail, u64_t * user_idnr); 
int db_user_find_create(u64_t user_idnr);
int db_user_delete(const char * username);
int db_user_rename(u64_t user_idnr, const char *new_name); 

int db_change_mailboxsize(u64_t user_idnr, u64_t new_size);

/* auto-reply cache */
int db_replycache_register(const char *to, const char *from,
		const char *handle);
int db_replycache_validate(const char *to, const char *from,
		const char *handle, int days);

/* get driver specific SQL snippets */
const char * db_get_sql(sql_fragment_t frag);

#endif
