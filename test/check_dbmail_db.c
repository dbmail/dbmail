/*
 *  Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
 *  Copyright (c) 2006-2012 NFG Net Facilities Group BV support@nfg.nl
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *   
 *
 *
 *  
 *
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *
 *   Run 'make check' to see some action.
 *
 */ 

// Test dsnuser_resolve through all of its pathways.
#include <check.h>
#include "check_dbmail.h"

extern char *configFile;
extern int quiet;
extern int reallyquiet;

uint64_t useridnr = 0;
uint64_t useridnr_domain = 0;

char *testuser = "testuser1";
uint64_t testidnr = 0;

char *username = "testfaildsn";
char *username_domain = "testfailuser@nonexistantdomain";
char *username_mailbox = "testfailuser+foomailbox@nonexistantdomain";

char *alias = "testfailalias@nonexistantdomain";
char *alias_mailbox = "testfailalias+foomailbox@nonexistantdomain";

char *userpart_catchall = "testfailcatchall@";
char *domain_catchall = "@nonexistantdomain";

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	configure_debug(511,0);
	if (config_read(configFile) < 0) {
		printf( "Config file not found: %s\n", configFile );
		exit(1);
	}
	GetDBParams();
	db_connect();
	auth_connect();

	GList *alias_add = NULL;
	alias_add = g_list_append(alias_add, alias);
	alias_add = g_list_append(alias_add, userpart_catchall);
	alias_add = g_list_append(alias_add, domain_catchall);

	if (! (auth_user_exists(testuser, &testidnr))) {
		do_add(testuser, "test", "md5-hash", 0, 0, NULL, NULL);
		auth_user_exists(testuser, &testidnr);
	}
	if (! (auth_user_exists(username, &useridnr))) {
		do_add(username, "testpass", "md5-hash", 0, 0, alias_add, NULL);
		auth_user_exists(username, &useridnr);
	}
	if (! (auth_user_exists(username_domain, &useridnr_domain))) {
		do_add(username_domain, "testpass", "md5-hash", 0, 0, NULL, NULL);
		auth_user_exists(username_domain, &useridnr_domain);
	}

}

void teardown(void)
{
	uint64_t mailbox_id=0;
	if (db_findmailbox("INBOX/Trash",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);
			
	if (db_findmailbox("testcreatebox",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);

	if (db_findmailbox("testdeletebox",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);

	if (db_findmailbox("testpermissionbox",testidnr,&mailbox_id)) {
		db_mailbox_set_permission(mailbox_id, IMAPPERM_READWRITE);
		db_delete_mailbox(mailbox_id,0,0);
	}

	db_disconnect();
	auth_disconnect();
}

START_TEST(test_db_stmt_prepare)
{
	Connection_T c; PreparedStatement_T s;
	c = db_con_get();
	s = db_stmt_prepare(c, "SELECT 1=1");
	fail_unless(s != NULL, "db_stmt_prepare failed");
	db_con_close(c);

}
END_TEST

START_TEST(test_db_stmt_set_str)
{
	Connection_T c; PreparedStatement_T s; ResultSet_T r;
	c = db_con_get();
	s = db_stmt_prepare(c, "select user_idnr from dbmail_users where userid=?");
	fail_unless(s != NULL, "db_stmt_prepare failed");
	fail_unless(db_stmt_set_str(s, 1, "testuser1"), "db_stmt_set_str failed");
	
	r = db_stmt_query(s);
	fail_unless(r != NULL, "db_stmt_query failed");
	fail_unless(db_result_next(r), "db_result_next failed");
	db_con_close(c);

}
END_TEST

START_TEST(test_Connection_executeQuery)
{
	Connection_T c; ResultSet_T r = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT foo FROM bar");
	CATCH(SQLException)
		LOG_SQLERROR;
	END_TRY;
	fail_unless(r==NULL, "Connection_executeQuery should have failed");

	r = db_query(c, "SELECT 1=1");
	fail_unless(r!=NULL, "Connection_executeQuery should have succeeded");
	fail_unless(db_result_next(r), "db_result_next failed");
	fail_unless(strlen(db_result_get(r,0)), "db_result_get failed");
}
END_TEST
START_TEST(test_db_mailbox_set_permission)
{

	int result;
	uint64_t mailbox_id;
	result = db_find_create_mailbox("testpermissionbox", BOX_DEFAULT, testidnr, &mailbox_id);
	fail_unless(mailbox_id, "db_find_create_mailbox [testpermissionbox] owned by [%" PRIu64 "] failed [%" PRIu64 "].", testidnr, mailbox_id);

	result = db_mailbox_set_permission(mailbox_id, IMAPPERM_READ);
	fail_unless(result==TRUE,"db_mailbox_set_permission failed");

	result = db_delete_mailbox(mailbox_id,0,0);
	fail_unless(result != DM_SUCCESS,"db_delete_mailbox should have failed on readonly mailbox");

	result = db_mailbox_set_permission(mailbox_id, IMAPPERM_READWRITE);
	fail_unless(result==TRUE,"db_mailbox_set_permission failed");

	result = db_delete_mailbox(mailbox_id,0,0);
	fail_unless(result == DM_SUCCESS,"db_delete_mailbox should have succeeded on readwrite mailbox");


	return;
}
END_TEST
/**
 * \brief get number of rows in result set.
 * \return 
 *     - 0 on failure or no rows
 *     - number of rows otherwise
 */
//unsigned db_num_rows(void);

/**
 * \brief get number of fields in result set.
 * \return
 *      - 0 on failure or no fields
 *      - number of fields otherwise
 */
//unsigned db_num_fields(void);

/** 
 * \brief get a single result field row from the result set
 * \param row result row to get it from
 * \param field field (=column) to get result from
 * \return
 *     - pointer to string holding result. 
 *     - NULL if no such result
 */
//const char *db_result_get(unsigned row, unsigned field);

/** \ingroup db_result_get_group
 * \brief Returns the result as an Integer
 */
//int db_result_get_int(unsigned row, unsigned field);

/** \ingroup db_result_get_group
 * \brief Returns the result as an Integer from 0 to 1
 */
//int db_result_get_bool(unsigned row, unsigned field);

/** \ingroup db_result_get_group
 * \brief Returns the result as an Unsigned 64 bit Integer
 */
//uint64_t db_result_get_u64(unsigned row, unsigned field);

/**
 * \brief return the ID generated for an AUTO_INCREMENT column
 *        by the previous column.
 * \param sequence_identifier sequence identifier
 * \return 
 *       - 0 if no such id (if for instance no AUTO_INCREMENT
 *          value was generated).
 *       - the id otherwise
 */
//uint64_t db_insert_result(const char *sequence_identifier);


/**
 * \brief get length in bytes of a result field in a result set.
 * \param row row of field
 * \param field field number (0..nfields)
 * \return 
 *      - -1 if invalid field
 *      - length otherwise
 */
//uint64_t db_get_length(unsigned row, unsigned field);

/**
 * \brief get number of rows affected by a query
 * \return
 *    -  -1 on error (e.g. no result set)
 *    -  number of affected rows otherwise
 */
//uint64_t db_get_affected_rows(void);

/**
 * \brief switch from normal result set to the authentication result
 * set
 */
//void db_use_auth_result(void);

/**
 * \brief switch from authentication result set to normal result set
 */
//void db_store_auth_result(void);

/**
 * \brief get void pointer to result set.
 * \return a pointer to a result set
 * \bug this is really ugly and should be dealt with differently!
 */
//void *db_result_get_set(void);

/**
 * \brief set the new result set 
 * \param res the new result set
 * \bug this is really ugly and should be dealt with differently!
 */
//void db_set_result_set(void *res);

/**
 * begin transaction
 * \return 
 *     - -1 on error
 *     -  0 otherwise
 */
//int db_begin_transaction(void);

/**
 * commit transaction
 * \return
 *      - -1 on error
 *      -  0 otherwise
 */
//int db_commit_transaction(void);

/**
 * rollback transaction
 * \return 
 *     - -1 on error
 *     -  0 otherwise
 */
//int db_rollback_transaction(void);

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
//int db_get_physmessage_id(uint64_t message_idnr, /*@out@*/ uint64_t * physmessage_id);

/**
 * \brief return number of bytes used by user identified by userid
 * \param user_idnr id of the user (from users table)
 * \param curmail_size will hold current mailsize used (should hold a valid 
 * pointer on call).
 * \return
 *          - -1 on failure<BR>
 *          -  1 otherwise
 */
//int dm_quota_user_get(uint64_t user_idnr, uint64_t * curmail_size);

/**
 * \brief finds all users which need to have their curmail_size (amount
 * of space used by user) updated. Then updates this number in the
 * database
 * \return 
 *     - -2 on memory error
 *     - -1 on database error
 *     -  0 on success
 */
//int dm_quota_rebuild(void);

/**
 * \brief performs a recalculation of used quotum of a user and puts
 *  this value in the users table.
 * \param user_idnr
 * \return
 *   - -1 on db_failure
 *   -  0 on success
 */
//int dm_quota_rebuild_user(uint64_t user_idnr);

/**
 * \brief get user idnr of a message. 
 * \param message_idnr idnr of message
 * \return 
 *              - -1 on failure
 * 		- 0 if message is located in a shared mailbox.
 * 		- user_idnr otherwise
 */
//uint64_t db_get_useridnr(uint64_t message_idnr);

/**
 * \brief insert a new physmessage. This inserts only an new record in the
 * physmessage table with a timestamp
 * \param physmessage_id will hold the id of the physmessage on return. Must 
 * hold a valid pointer on call.
 * \return 
 *     - -1 on failure
 *     -  1 on success
 */
//int db_insert_physmessage(/*@out@*/ uint64_t * physmessage_id);

/**
 * \brief insert a physmessage with an internal date.
 * \param internal_date the internal date in "YYYY-MM-DD HH:Mi:SS"
 * \param physmessage_id will hold the id of the physmessage on return. Must
 * hold a valid pointer on call.
 * \return 
 *    - -1 on failure
 *    -  1 on success
 */
//int db_insert_physmessage_with_internal_date(timestring_t internal_date,
//					     uint64_t * physmessage_id);

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
//int db_update_message(uint64_t message_idnr, const char *unique_id,
//		      uint64_t message_size, uint64_t rfc_size);
/**
 * \brief set unique id of a message 
 * \param message_idnr
 * \param unique_id unique id of message
 * \return 
 *     - -1 on database error
 *     -  0 on success
 */
//int db_message_set_unique_id(uint64_t message_idnr, const char *unique_id);

/**
 * \brief set messagesize and rfcsize of a message
 * \param physmessage_id 
 * \param message_size
 * \param rfc_size
 * \return
 *    - -1 on failure
 *    -  0 on succes
 */
//int db_physmessage_set_sizes(uint64_t physmessage_id, uint64_t message_size,
//			     uint64_t rfc_size);

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
//int db_insert_message_block_physmessage(const char *block,
//					uint64_t block_size,
//					uint64_t physmessage_id,
//					uint64_t * messageblock_idnr,
//					unsigned is_header);
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
//int db_insert_message_block(const char *block, uint64_t block_size,
//			    uint64_t message_idnr, 
//			    /*@out@*/ uint64_t * messageblock_idnr,
//			    unsigned is_header);
/**
 * \brief log IP-address for POP/IMAP_BEFORE_SMTP. If the IP-address
 *        is already logged, it's timestamp is renewed.
 * \param ip the ip (xxx.xxx.xxx.xxx)
 * \return
 *        - -1 on database failure
 *        - 0 on success
 * \bug timestamp!
 */
//int db_log_ip(const char *ip);

/**
* \brief clean up ip log
* \param lasttokeep all ip log entries before this timestamp will
*                     deleted
* \return 
*       - -1 on database failure
*       - 0 on success
*/
//int db_cleanup_iplog(const char *lasttokeep, uint64_t *affected_rows);
//int db_count_iplog(const char *lasttokeep, uint64_t *affected_rows);

/**
 * \brief cleanup database tables
 * \return 
 *       - -1 on database failure
 *       - 0 on success
 */
//int db_cleanup(void);

/**
 * \brief execute cleanup of database tables (have no idea why there
 *        are two functions for this..)
 * \param tables array of char* holding table names 
 * \param num_tables number of tables in tables
 * \return 
 *    - -1 on db failure
 *    - 0 on success
 */
//int db_do_cleanup(const char **tables, int num_tables);

/**
* \brief remove all mailboxes for a user
* \param user_idnr
* \return 
*      - -2 on memory failure
*      - -1 on database failure
*      - 0 on success
*/
//int db_empty_mailbox(uint64_t user_idnr);

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
//int db_icheck_messageblks(GList **lost);

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
//int db_icheck_messages(GList **lost);

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
//int db_icheck_mailboxes(GList **lost);

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
//int db_icheck_null_messages(GList **lost);

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
//int db_icheck_null_physmessages(GList **lost);

/**
 * \brief check for is_header flag on messageblks
 *
 */
//int db_icheck_isheader(GList  **lost);
//int db_set_isheader(GList *lost);

/** 
 * \brief check for cached header values
 *
 */
//int db_icheck_headercache(GList **lost);
//int db_set_headercache(GList *lost);

/**
 * \brief check for rfcsize in physmessage table
 *
 */
//int db_icheck_rfcsize(GList **lost);
//int db_update_rfcsize(GList *lost);

/**
 * \brief set status of a message
 * \param message_idnr
 * \param status new status of message
 * \return result of Connection_executeQuery()
 */
//int db_set_message_status(uint64_t message_idnr, MessageStatus_t status);

/**
* \brief delete a message block
* \param messageblk_idnr
* \return result of Connection_executeQuery()
*/
//int db_delete_messageblk(uint64_t messageblk_idnr);
/**
 * \brief delete a physmessage
 * \param physmessage_id
 * \return
 *      - -1 on error
 *      -  1 on success
 */
//int db_delete_physmessage(uint64_t physmessage_id);

/**
 * \brief delete a message 
 * \param message_idnr
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
//int db_delete_message(uint64_t message_idnr);

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
//int db_send_message_lines(void *fstream, uint64_t message_idnr,
//			  long lines, int no_end_dot);
/**
 * \brief create a new POP3 session. (was createsession() in dbmysql.c)
 * \param user_idnr user idnr 
 * \param session_ptr pointer to POP3 session 
 */
//int db_createsession(uint64_t user_idnr, PopSession_t * session_ptr);
/** 
 * \brief Clean up a POP3 Session
 * \param session_ptr pointer to POP3 session
 */
//void db_session_cleanup(PopSession_t * session_ptr);
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
//int db_update_pop(PopSession_t * session_ptr);
/**
 * \brief set deleted status (=3) for all messages that are marked for
 *        delete (=2)
 * \param affected_rows will hold the number of affected messages on return. 
 * must hold a valid pointer on call.
 * \return
 *    - -1 on database failure;
 *    - 1 otherwise
 */
//int db_set_deleted(uint64_t * affected_rows);
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
//int db_deleted_purge(uint64_t * affected_rows);
//int db_deleted_count(uint64_t * affected_rows);
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
//uint64_t db_check_sizelimit(uint64_t addblocksize, uint64_t message_idnr,
//			 uint64_t * user_idnr);
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
//int db_imap_append_msg(const char *msgdata, uint64_t datalen,
//		       uint64_t mailbox_idnr, uint64_t user_idnr,
//		       timestring_t internal_date, uint64_t * msg_idnr);

/**
 * Produces a regexp that will case-insensitively match the mailbox name
 * according to the modified UTF-7 rules given in section 5.1.3 of IMAP.
 * \param column name of the name column.
 * \param mailbox name of the mailbox.
 * \param filter use /% for children or "" for just the box.
 * \return pointer to a newly allocated string.
 */
START_TEST(test_mailbox_match_new)
{
	char *trythese[] = {
		"Inbox",
		"Listen/F% %/Users",
		"xx1_&AOcA4w-o",
		"INBOX/&BekF3AXVBd0-",
		NULL };
	char *getthese[] = {
		NULL, "Inbox",
		NULL, "Listen/F% %/Users",
		"___\\_&AOcA4w__", "xx1\\________-o",
		"______&BekF3AXVBd0_", "INBOX/____________-",
		NULL, NULL };
	int i;
	struct mailbox_match *result;

	for (i = 0; trythese[i] != NULL; i++) {
		result = mailbox_match_new(trythese[i]);

		fail_unless (MATCH(result->insensitive, getthese[(i*2)+1]), "failed insensitive: [%s] [%s]\n", result->insensitive, getthese[(i*2)+1]);
		if (getthese[i*2])
			fail_unless (MATCH(result->sensitive, getthese[i*2]), "failed sensitive: [%s] [%s]\n", result->sensitive, getthese[i*2]);

		mailbox_match_free(result);
	}
}
END_TEST

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
//int db_findmailbox(const char *name, uint64_t user_idnr,
//		   /*@out@*/ uint64_t * mailbox_idnr);
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
//int db_findmailbox_by_regex(uint64_t owner_idnr, const char *pattern,
//			    uint64_t ** children, unsigned *nchildren,
//			    int only_subscribed);
START_TEST(test_db_findmailbox_by_regex)
{
	GList *children = NULL;
	uint64_t mailbox_id = 0;

	db_createmailbox("INBOX/Trash", testidnr, &mailbox_id);
	db_findmailbox_by_regex(testidnr, "INBOX/Trash", &children, 0);
}
END_TEST


/**
 * \brief find owner of a mailbox
 * \param mboxid id of mailbox
 * \param owner_id will hold owner of mailbox after return
 * \return
 *    - -1 on db error
 *    -  0 if owner not found
 *    -  1 if owner found
 */
//int db_get_mailbox_owner(uint64_t mboxid, /*@out@*/ uint64_t * owner_id);

/**
 * \brief check if a user is owner of the specified mailbox 
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return
 *     - -1 on db error
 *     -  0 if not user
 *     -  1 if user
 */
//int db_user_is_mailbox_owner(uint64_t userid, uint64_t mboxid);

/**
 * \brief create a new mailbox
 * \param name name of mailbox
 * \param owner_idnr owner of mailbox
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
//int db_createmailbox(const char *name, uint64_t owner_idnr,
//		     uint64_t * mailbox_idnr);

START_TEST(test_db_createmailbox)
{
	uint64_t owner_id=99999999;
	uint64_t mailbox_id=0;
	int result;

	result = db_createmailbox("INBOX", owner_id, &mailbox_id);
	fail_unless(result == -1, "db_createmailbox should have failed");

	result = db_createmailbox("testcreatebox", testidnr, &mailbox_id);
	fail_unless(result==DM_SUCCESS,"db_createmailbox failed");

	
}
END_TEST

/** Create a mailbox, recursively creating its parents.
 * \param mailbox Name of the mailbox to create
 * \param owner_idnr Owner of the mailbox
 * \param mailbox_idnr Fills the pointer with the mailbox id
 * \param message Returns a static pointer to the return message
 * \return
 *    0 Everything's good
 *    1 Cannot create mailbox
 *   -1 Database error
 */
START_TEST(test_db_mailbox_create_with_parents)
{
	uint64_t mailbox_idnr = 0, mb1 = 0, mb2 = 0, mb3 = 0;
	const char *message;
	int result;
	int only_empty = 0, update_curmail_size = 0 ;

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar/Baz", BOX_COMMANDLINE,
			useridnr, &mailbox_idnr, &message);
	fail_unless(result == 0 && mailbox_idnr != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	/* At this point, useridnr should have and be subscribed to several boxes... */
	result = db_findmailbox("inbox/foo/BAR/baz", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"inbox/foo/BAR/baz\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("InBox/Foo/bar", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"InBox/Foo/bar\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar-Baz", BOX_COMMANDLINE, useridnr, &mb1, &message);
	fail_unless(result == 0 && mb1 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar_Baz", BOX_COMMANDLINE, useridnr, &mb2, &message);
	fail_unless(result == 0 && mb2 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar=Baz", BOX_COMMANDLINE, useridnr, &mb3, &message);
	fail_unless(result == 0 && mb3 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_delete_mailbox(mb1, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");
	result = db_delete_mailbox(mb2, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");
	result = db_delete_mailbox(mb3, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("INBOX/FOO", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"INBOX/FOO\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("INBox", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"INBox\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "We just deleted inbox. Something silly.");
	/* Cool, we've cleaned up after ourselves. */

}
END_TEST

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
//int db_delete_mailbox(uint64_t mailbox_idnr, int only_empty,
//		      int update_curmail_size);
START_TEST(test_db_delete_mailbox)
{
	uint64_t mailbox_id = 999999999;
	int result;

	result = db_delete_mailbox(mailbox_id, 0, 0);
	fail_unless(result != DM_SUCCESS, "db_delete_mailbox should have failed");

	result = db_createmailbox("testdeletebox",testidnr, &mailbox_id);
	fail_unless(result == DM_SUCCESS,"db_createmailbox failed");
	result = db_delete_mailbox(mailbox_id,0,1);
	fail_unless(result == DM_SUCCESS,"db_delete_mailbox failed");

}
END_TEST

/* Insert or update a replycache entry.
 * int db_replycache_register(const char *to, const char *from, const char *handle);

 * Returns DM_SUCCESS if the (to, from) pair hasn't been seen in days.
 * int db_replycache_validate(const char *to, const char *from, const char *handle, int days);

 * Remove a replycache entry.
 * int db_replycache_unregister(const char *to, const char *from, const char *handle);
 */
START_TEST(test_db_replycache)
{
	int result;

	result = db_replycache_register("test_to", "test_from", "test_handle");
	fail_unless(result == TRUE, "failed to register");

	/* Should always be DM_SUCCESS */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 0);
	//fail_unless(result == DM_SUCCESS, "failed with days = 0");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 1);
	fail_unless(result != DM_SUCCESS, "failed with days = 1");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 2);
	fail_unless(result != DM_SUCCESS, "failed with days = 2");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 1100);
	fail_unless(result != DM_SUCCESS, "failed with days = 1100");

	result = db_replycache_unregister("test_to", "test_from", "test_handle");
	fail_unless(result == TRUE, "failed to unregister");
}
END_TEST


/**
 * \brief find a mailbox, create if not found
 * \param name name of mailbox
 * \param owner_idnr owner of mailbox
 * \return 
 *    - -1 on failure
 *    -  0 on success
 */
//int db_find_create_mailbox(const char *name, mailbox_source source,
//		uint64_t owner_idnr, /*@out@*/ uint64_t * mailbox_idnr);
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
//int db_listmailboxchildren(uint64_t mailbox_idnr, uint64_t user_idnr,
//			   uint64_t ** children, int *nchildren,
//			   const char *filter);

/**
 * \brief check if mailbox is selectable
 * \param mailbox_idnr
 * \return 
 *    - -1 on failure
 *    - 0 if not selectable
 *    - 1 if selectable
 */
//int db_isselectable(uint64_t mailbox_idnr);
/**
 * \brief check if mailbox has no_inferiors flag set.
 * \param mailbox_idnr
 * \return
 *    - -1 on failure
 *    - 0 flag not set
 *    - 1 flag set
 */
//int db_noinferiors(uint64_t mailbox_idnr);

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
//int db_setselectable(uint64_t mailbox_idnr, int select_value);
/** 
 * \brief remove all messages from a mailbox
 * \param mailbox_idnr
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
//int db_removemsg(uint64_t user_idnr, uint64_t mailbox_idnr);
/**
 * \brief move all messages from one mailbox to another.
 * \param mailbox_to idnr of mailbox to move messages from.
 * \param mailbox_from idnr of mailbox to move messages to.
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
//int db_movemsg(uint64_t mailbox_to, uint64_t mailbox_from);
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
//int db_copymsg(uint64_t msg_idnr, uint64_t mailbox_to,
//	       uint64_t user_idnr, uint64_t * newmsg_idnr);
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
//int db_getmailboxname(uint64_t mailbox_idnr, uint64_t user_idnr, char *name);
/**
 * \brief set name of mailbox
 * \param mailbox_idnr
 * \param name new name of mailbox
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
//int db_setmailboxname(uint64_t mailbox_idnr, const char *name);
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
//int db_expunge(uint64_t mailbox_idnr,
//	       uint64_t user_idnr, uint64_t ** msg_idnrs, uint64_t * nmsgs);
/**
 * \brief get first unseen message in a mailbox
 * \param mailbox_idnr
 * \return 
 *     - -1 on failure
 *     - msg_idnr of first unseen message in mailbox otherwise
 */
//uint64_t db_first_unseen(uint64_t mailbox_idnr);
/**
 * \brief subscribe to a mailbox.
 * \param mailbox_idnr mailbox to subscribe to
 * \param user_idnr user to subscribe
 * \return 
 * 		- -1 on failure
 * 		- 0 on success
 */
//int db_subscribe(uint64_t mailbox_idnr, uint64_t user_idnr);
/**
 * \brief unsubscribe from a mailbox
 * \param mailbox_idnr
 * \param user_idnr
 * \return
 * 		- -1 on failure
 * 		- 0 on success
 */
//int db_unsubscribe(uint64_t mailbox_idnr, uint64_t user_idnr);

START_TEST(test_db_get_sql)
{
	const char *s = db_get_sql(SQL_CURRENT_TIMESTAMP);
	fail_unless(s != NULL);
}
END_TEST


Suite *dbmail_db_suite(void)
{
	Suite *s = suite_create("Dbmail Basic Database Functions");

	TCase *tc_db = tcase_create("DB");
	suite_add_tcase(s, tc_db);
	tcase_add_checked_fixture(tc_db, setup, teardown);

	tcase_add_test(tc_db, test_db_stmt_prepare);
	tcase_add_test(tc_db, test_db_stmt_set_str);

	tcase_add_test(tc_db, test_Connection_executeQuery);
	tcase_add_test(tc_db, test_db_createmailbox);
	tcase_add_test(tc_db, test_db_delete_mailbox);
	tcase_add_test(tc_db, test_db_replycache);
	tcase_add_test(tc_db, test_db_mailbox_set_permission);
	tcase_add_test(tc_db, test_db_mailbox_create_with_parents);
	tcase_add_test(tc_db, test_mailbox_match_new);
	tcase_add_test(tc_db, test_db_findmailbox_by_regex);
	tcase_add_test(tc_db, test_db_get_sql);


	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_db_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

