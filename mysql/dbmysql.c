/* $Id$
 *  (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 *
 * mysql driver file
 * Functions for connecting and talking to the Mysql database */

#include "../db.h"
#include "/usr/include/mysql/mysql.h"
#include "../config.h"
#include "../pop3.h"
#include "../list.h"
#include "../mime.h"
#include "../pipe.h"
#include "../memblock.h"
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include "../rfcmsg.h"
#include "../auth.h"


MYSQL conn;  
MYSQL_RES *res;
MYSQL_RES *checkres;
MYSQL_ROW row;
char *query = 0;

typedef MYSQL db_conn_t;

field_t _db_host, _db_user, _db_db, _db_pass;

const char *db_flag_desc[] = 
{
  "seen_flag", "answered_flag", "deleted_flag", "flagged_flag", "draft_flag", "recent_flag"
};

const char *DB_TABLENAMES[DB_NTABLES] = 
  {
    "users", "aliases", "mailboxes", "messages", "messageblks" 
  };



int db_connect ()
{
  query = (char*)my_malloc(DEF_QUERYSIZE);
  if (!query)
    {
      trace(TRACE_WARNING,"db_connect(): not enough memory for query\n");
      return -1;
    }

  /* connecting */
  mysql_init(&conn);
  if (mysql_real_connect (&conn, _db_host, _db_user, _db_pass, _db_db, 0, NULL, 0) == NULL)
    {
      trace(TRACE_ERROR,"dbconnect(): mysql_real_connect failed: %s",mysql_error(&conn));
      return -1;
    }

#ifdef mysql_errno
  if (mysql_errno(&conn)) {
    trace(TRACE_ERROR,"dbconnect(): mysql_real_connect failed: %s",mysql_error(&conn));
    return -1;
  }
#endif
	
  return 0;
}

void *db_get_connection()
{
  return &conn;
}


u64_t db_insert_result (const char *sequence_identifier)
{
  u64_t insert_result;
  insert_result=mysql_insert_id(&conn);
  return insert_result;
}


int db_query (const char *thequery)
{
  unsigned int querysize = 0;

  if (thequery != NULL)
    {
      querysize = strlen(thequery);

      if (querysize > 0 )
	{
	  trace(TRACE_DEBUG, "db_query(): executing [%s]", thequery);
	  if (mysql_real_query(&conn, thequery, querysize) <0) 
	    {
	      trace(TRACE_ERROR,"db_query(): query [%s] failed: ", thequery);
	      trace(TRACE_ERROR,"db_query(): mysql_real_query failed: %s\n",mysql_error(&conn)); 
	      return -1;
	    }
	}
      else
	{
	  trace (TRACE_ERROR,"db_query(): querysize is wrong: [%d]\n",querysize);
	  return -1;
	}
    }
  else
    {
      trace (TRACE_ERROR,"db_query(): query buffer is NULL, this is not supposed to happen\n",
	     querysize);
      return -1;
    }
  return 0;
}


/*
 * returns the number of bytes used by user userid or 
 * (u64_t)(-1) on dbase failure
 */
u64_t db_get_quotum_used(u64_t userid)
{
  u64_t q=0;

  snprintf(query, DEF_QUERYSIZE, "SELECT SUM(m.messagesize) FROM messages m, mailboxes mb "
	   "WHERE m.mailbox_idnr = mb.mailbox_idnr AND mb.owner_idnr = %llu",
	   userid);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_get_quotum_used(): query to select mailbox id's failed");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR, "db_get_quotum_used(): could not store query result");
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row && row[0])
    {
      q = strtoull(row[0], NULL, 10);
    }
  else
    {
      trace(TRACE_ERROR, "db_get_quotum_used(): messagesize requested but not found in result");
      mysql_free_result(res);
      return -1;
    }
      
  mysql_free_result(res);

  return q;
}


/*
 * db_get_user_from_alias()
 * 
 * looks up a user ID in the alias table
 */
u64_t db_get_user_from_alias(const char *alias)
{
  u64_t uid;

  snprintf (query, DEF_QUERYSIZE,
	    "SELECT deliver_to FROM aliases WHERE alias = '%s'", alias);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_user_from_alias(): could not execute query");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR, "db_get_user_from_alias(): could not store query result");
      return -1;
    }

  if (mysql_num_rows(res) == 0)
    {
      /* no such user */
      mysql_free_result(res);
      return 0;
    }
  
  row = mysql_fetch_row(res);
  uid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  mysql_free_result(res);

  return uid;
}


/* 
 * adds an alias for a specific user 
 *
 * returns -1 on error, 0 on success, 1 if alias already exists
 */
int db_addalias (u64_t useridnr, char *alias, u64_t clientid)
{
  /* check if this alias already exists */
  snprintf (query, DEF_QUERYSIZE,
            "SELECT alias_idnr FROM aliases WHERE alias = '%s' AND deliver_to = '%llu' "
	    "AND client_idnr = %llu", alias, useridnr, clientid);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias(): query for searching alias failed");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR, "db_addalias(): could not store query result");
      return -1;
    }

  if (mysql_num_rows(res)>0) 
    {
      trace(TRACE_INFO, "db_addalias(): alias [%s] for user [%llu] already exists", 
	    alias, useridnr);
      
      mysql_free_result(res);
      return 1;
    }
  
  mysql_free_result(res);

  snprintf (query, DEF_QUERYSIZE,
	    "INSERT INTO aliases (alias,deliver_to,client_idnr) VALUES ('%s','%llu',%llu)",
	   alias, useridnr, clientid);
	

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias(): query for adding alias failed");
      return -1;
    }
  
  return 0;
}


/*
 * db_addalias_ext()
 *
 * returns -1 on error, 0 on success, 1 if alias already exists
 */
int db_addalias_ext(char *alias, char *deliver_to, u64_t clientid)
{
  /* check if this alias already exists */
  if (clientid != 0)
    {
      snprintf (query, DEF_QUERYSIZE,
		"SELECT alias_idnr FROM aliases WHERE alias = '%s' AND deliver_to = '%s' "
		"AND client_idnr = %llu", alias, deliver_to, clientid);
    }
  else
    {
      snprintf (query, DEF_QUERYSIZE,
		"SELECT alias_idnr FROM aliases WHERE alias = '%s' AND deliver_to = '%s' ",
		alias, deliver_to);
    }

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias_ext(): query for searching alias failed");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR, "db_addalias_ext(): could not store query result");
      return -1;
    }

  if (mysql_num_rows(res)>0) 
    {
      trace(TRACE_INFO, "db_addalias_ext(): alias [%s] --> [%s] already exists", 
	    alias, deliver_to);
      
      mysql_free_result(res);
      return 1;
    }
  
  mysql_free_result(res);

  snprintf (query, DEF_QUERYSIZE,
	    "INSERT INTO aliases (alias,deliver_to,client_idnr) VALUES ('%s','%s',%llu)",
	   alias, deliver_to, clientid);
	
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias_ext(): query for adding alias failed");
      return -1;
    }
  
  return 0;
}


int db_removealias (u64_t useridnr,const char *alias)
{
  snprintf (query, DEF_QUERYSIZE,
	    "DELETE FROM aliases WHERE deliver_to='%llu' AND alias = '%s'", useridnr, alias);
	   
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_removealias(): query for removing alias failed : [%s]", query);
      return -1;
    }
  
  return 0;
}
  

int db_removealias_ext(const char *alias, const char *deliver_to)
{
  snprintf (query, DEF_QUERYSIZE,
	    "DELETE FROM aliases WHERE deliver_to='%s' AND alias = '%s'", deliver_to, alias);
	   
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_removealias_ext(): query for removing alias failed");
      return -1;
    }
  
  return 0;
}


/*
 * db_get_nofity_address()
 *
 * gets the auto-notification address for a user
 * caller should free notify_address when done
 */
int db_get_nofity_address(u64_t userid, char **notify_address)
{
  *notify_address = NULL;

  snprintf(query, DEF_QUERYSIZE, "SELECT notify_address FROM auto_notifications WHERE user_idnr = %llu", userid);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_get_nofity_address(): could not select notification address");
      return -1;
    }
 
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_nofity_address(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res) > 0)
    {
      row = mysql_fetch_row(res);

      if (row && row[0] && strlen(row[0]))
	{
	  if ( !(*notify_address = (char*)malloc(strlen(row[0]) + 1)) )
	    {
	      trace(TRACE_ERROR, "db_get_nofity_address(): could not allocate memory for address");
	      mysql_free_result(res);
	      return -2;
	    }

	  sprintf(*notify_address, "%s", row[0]);
	  trace(TRACE_DEBUG, "db_get_nofity_address(): found address [%s]", *notify_address);
	}
    }

  mysql_free_result(res);
  return 0;
}
  

int db_get_reply_body(u64_t userid, char **body)
{
  *body = NULL;

  snprintf(query, DEF_QUERYSIZE, "SELECT reply_body FROM auto_replies WHERE user_idnr = %llu", userid);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_get_reply_body(): could not select reply body");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_reply_body(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res) > 0)
    {
      row = mysql_fetch_row(res);

      if (row && row[0] && strlen(row[0]))
	{
	  if ( !(*body = (char*)malloc(strlen(row[0]) + 1)) )
	    {
	      trace(TRACE_ERROR, "db_get_reply_body(): could not allocate memory for body");
	      mysql_free_result(res);
	      return -2;
	    }

	  sprintf(*body, "%s", row[0]);
	  trace(TRACE_DEBUG, "db_get_reply_body(): found body [%s]", *body);
	}
    }

  mysql_free_result(res);
  return 0;
}

/* 
 * returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found 
 */
u64_t db_get_mailboxid (u64_t useridnr, const char *mailbox)
{
  u64_t inboxid;

  snprintf (query, DEF_QUERYSIZE,"SELECT mailbox_idnr FROM mailboxes WHERE "
	    "name=\'%s\' AND owner_idnr=%llu",
	   mailbox, useridnr);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR, "db_get_mailboxid(): query failed");
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_mailboxid(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): user has no mailbox named [%s]", mailbox);
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): fetch_row call failed");
    }

  inboxid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0; 

  mysql_free_result (res);
  
  return inboxid;
}


u64_t db_get_message_mailboxid (u64_t message_idnr)
{
  /* returns the mailbox id of a message */
  u64_t mailboxid;
  
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailbox_idnr FROM messages WHERE message_idnr = %llu",
	   message_idnr);

  trace(TRACE_DEBUG,"db_get_message_mailboxid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_message_mailboxid(): mysql_store_result failed: %s",
	    mysql_error(&conn));
      
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_message_mailboxid(): this message had no mailboxid? "
	     "Message without a mailbox!");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): fetch_row call failed");
    }

  mailboxid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
	
  mysql_free_result (res);
  
	
  return mailboxid;
}


/* 
 * returns the userid from a message_idnr 
 */
u64_t db_get_useridnr (u64_t message_idnr)
{
  u64_t userid;

  snprintf (query, DEF_QUERYSIZE, "SELECT owner_idnr FROM mailboxes mb, messages m WHERE "
	    "mb.mailbox_idnr = m.mailbox_idnr AND m.message_idnr = %llu",
	    message_idnr);

  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_useridnr(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): this is not right!");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): fetch_row call failed");
    }
	
  userid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
	
  mysql_free_result (res);
  
  return userid;
}


/* 
 * inserts into inbox ! 
 */
u64_t db_insert_message (u64_t useridnr, const char *deliver_to_mailbox,
			 const char *uniqueid)
{
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);
  
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO messages(mailbox_idnr,messagesize,unique_id,"
	    "internal_date,recent_flag,status)"
	    " VALUES (%llu, 0, \"%s\", \"%s\", 1, '005')",
	    deliver_to_mailbox ? db_get_mailboxid(useridnr, deliver_to_mailbox) : 
	    db_get_mailboxid (useridnr, "INBOX"),
	    uniqueid ? uniqueid : "", timestr);
  
  if (db_query (query)==-1)
    {
      trace(TRACE_STOP,"db_insert_message(): dbquery failed");
    }	
  
  return db_insert_result("");
}


u64_t db_update_message (u64_t message_idnr, const char *unique_id,
			 u64_t messagesize, u64_t rfcsize)
{
  snprintf (query, DEF_QUERYSIZE,
	    "UPDATE messages SET messagesize=%llu, unique_id=\"%s\", status='000', rfcsize = %llu "
	    "where message_idnr=%llu",
	   messagesize, unique_id, rfcsize, message_idnr);
  
  if (db_query (query)==-1)
    {
      trace(TRACE_STOP,"db_update_message(): dbquery failed");
    }
	
  
  return 0;
}


/*
 * db_update_message_multiple()
 *
 * updates a range of messages. A unique ID is created fro each of them.
 */
int db_update_message_multiple(const char *unique_id, u64_t messagesize, u64_t rfcsize)
{
  u64_t *uids;
  int n,i;
  char newunique[UID_SIZE];

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE "
	   "unique_id = '%s'", unique_id);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_update_message_multiple(): could not select messages: [%s]",
	    mysql_error(&conn));
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_update_message_multiple(): mysql_store_result failed: [%s]",
	    mysql_error(&conn));
      return -1;
    }

  n = mysql_num_rows(res);
  if (n <= 0)
    {
      trace(TRACE_INFO,"db_update_message_multiple(): nothing to update (?)");
      mysql_free_result(res);
      return 0;
    }

  if ( (uids = (u64_t*)my_malloc(n * sizeof(u64_t))) == 0)
    {
      trace(TRACE_ERROR,"db_update_message_multiple(): out of memory");
      mysql_free_result(res);
      return -1;
    }

  for (i=0; i<n; i++)
    {
      row = mysql_fetch_row(res);
      uids[i] = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
    }

  mysql_free_result(res);
  
  /* now update for each message */
  for (i=0; i<n; i++)
    {
      snprintf(newunique, UID_SIZE, "%lluA%lu", uids[i], time(NULL));
      snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET "
	       "messagesize=%llu, rfcsize = %llu, unique_id='%s', status='000' "
	       "WHERE message_idnr=%llu", messagesize, rfcsize, newunique, uids[i]);

      trace(TRACE_ERROR,"message [%llu] inserted, [%llu] bytes", uids[i], messagesize);

      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_update_message_multiple(): "
		"could not update message data for message [%llu]: [%s]", uids[i],
		mysql_error(&conn));
	  /* try to continue anyways */
	}
    }

  my_free(uids);
  return 0;
}


/*
 * insert a msg block
 * returns msgblkid on succes, -1 on failure
 */
u64_t db_insert_message_block (const char *block, u64_t len, u64_t msgid)
{
  char *escaped_query = NULL;
  unsigned maxesclen = (READ_BLOCK_SIZE+1) * 2 + DEF_QUERYSIZE, startlen = 0, esclen = 0;

  if (block == NULL)
    {
      trace (TRACE_ERROR,"db_insert_message_block(): got NULL as block, "
	     "insertion not possible\n");
      return -1;
    }

  if (len > READ_BLOCK_SIZE)
    {
      trace (TRACE_ERROR,"db_insert_message_block(): blocksize [%llu], maximum is [%llu]",
	     len, READ_BLOCK_SIZE);
      return -1;
    }

  escaped_query = (char*)my_malloc(sizeof(char) * maxesclen);
  if (!escaped_query)
    {
      trace(TRACE_ERROR,"db_insert_message_block(): not enough memory");
      return -1;
    }

  startlen = snprintf(escaped_query, maxesclen, "INSERT INTO messageblks"
		      "(messageblk,blocksize,message_idnr) VALUES ('");
      
  /* escape & add data */
  esclen = mysql_real_escape_string(&conn, &escaped_query[startlen], block, len); 
           
  snprintf(&escaped_query[esclen + startlen /*- (escaped_query[esclen + startlen - 1] ? 0 : 1)*/ ],
	   maxesclen - esclen - startlen, "', %llu, %llu)", len, msgid);

  if (db_query(escaped_query) == -1)
    {
      my_free(escaped_query);

      trace(TRACE_ERROR,"db_insert_message_block(): dbquery failed\n");
      return -1;
    }

  /* all done, clean up & exit */
  my_free(escaped_query);

  return db_insert_result("");
}


/*
 * db_insert_message_block_multiple()
 *
 * As db_insert_message_block() but inserts multiple rows at a time.
 */
int db_insert_message_block_multiple(const char *uniqueid, const char *block, u64_t len)
{
  char *escaped_query = NULL;
  unsigned maxesclen = READ_BLOCK_SIZE * 2 + DEF_QUERYSIZE, startlen = 0, esclen = 0;

  if (block == NULL)
    {
      trace (TRACE_ERROR,"db_insert_message_block_multiple(): got NULL as block, "
	     "insertion not possible\n");
      return -1;
    }

  if (len > READ_BLOCK_SIZE)
    {
      trace (TRACE_ERROR,"db_insert_message_block_multiple(): blocksize [%llu], "
	     "maximum is [%llu]",
	     len, READ_BLOCK_SIZE);
      return -1;
    }

  escaped_query = (char*)my_malloc(sizeof(char) * maxesclen);
  if (!escaped_query)
    {
      trace(TRACE_ERROR,"db_insert_message_block_multiple(): not enough memory");
      return -1;
    }

  snprintf(escaped_query, maxesclen, "INSERT INTO messageblks"
	   "(messageblk,blocksize,message_idnr) SELECT '");

  startlen = sizeof("INSERT INTO messageblks"
		    "(messageblk,blocksize,message_idnr) SELECT '") - 1;

  /* escape & add data */
  esclen = mysql_real_escape_string(&conn, &escaped_query[startlen], block, len); 
           
  snprintf(&escaped_query[esclen + startlen /*- (escaped_query[esclen + startlen - 1] ? 0 : 1)*/ ],
	   maxesclen - esclen - startlen, 
	   "', %llu, message_idnr FROM messages WHERE unique_id = '%s'", len, uniqueid);

  if (db_query(escaped_query) == -1)
    {
      my_free(escaped_query);

      trace(TRACE_ERROR,"db_insert_message_block_multiple(): dbquery failed\n");
      return -1;
    }

  /* all done, clean up & exit */
  my_free(escaped_query);

  return 0;
}
  

/*
 * db_rollback_insert()
 *
 * Performs a rollback for a message that is being inserted.
 */
int db_rollback_insert(u64_t ownerid, const char *unique_id)
{
  u64_t msgid;
  int result;

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages m, mailboxes mb WHERE "
	   "mb.owner_idnr = %llu AND m.mailbox_idnr = mb.mailbox_idnr AND m.unique_id = '%s'",
	   ownerid, unique_id);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_rollback_insert(): could not select message-id: %s", mysql_error(&conn));
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_rollback_insert(): mysql_store_result failed: %s",mysql_error(&conn));
      return -1;
    }
  
  if (mysql_num_rows(res) < 1)
    {
      trace(TRACE_ERROR,"db_rollback_insert(): non-existent message specified");
      mysql_free_result(res);
      return 0;
    }

  row = mysql_fetch_row(res);
  msgid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  mysql_free_result(res);

  result = 0;

  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgid);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_rollback_insert(): could not delete message blocks, msg ID [%llu]", msgid);
      result = -1;
    }

  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_rollback_insert(): could not delete message, msg ID [%llu]", msgid);
      result = -1;
    }

  return result;
}


/* get a list of aliases associated with user userid */
/* return -1 on db error, -2 on mem error, 0 on succes */
int db_get_user_aliases(u64_t userid, struct list *aliases)
{
  if (!aliases)
    {
      trace(TRACE_ERROR,"db_get_user_aliases(): got a NULL pointer as argument\n");
      return -2;
    }

  list_init(aliases);

  /* do a inverted (DESC) query because adding the names to the final list inverts again */
  snprintf(query, DEF_QUERYSIZE, "SELECT alias FROM aliases WHERE deliver_to = '%llu' ORDER BY alias "
	   "DESC", userid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_get_user_aliases(): could not retrieve  list\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_user_aliases(): mysql_store_result failed: %s",mysql_error(&conn));
      return -1;
    }
  
  while ((row = mysql_fetch_row(res)))
    {
      if (!list_nodeadd(aliases, row[0], strlen(row[0])+1))
	{
	  list_freelist(&aliases->start);
	  mysql_free_result(res);
	  return -2;
	}
    }
      
  mysql_free_result(res);
  return 0;
}




/* 
   this function writes "lines" to fstream.
   if lines == -2 then the whole message is dumped to fstream 
   newlines are rewritten to crlf 
   This is excluding the header 
*/
int db_send_message_lines (void *fstream, u64_t message_idnr, long lines, int no_end_dot)
{
  char *buffer = NULL;
  char *nextpos, *tmppos = NULL;
  int block_count;
  unsigned long *lengths;
  u64_t rowlength;
  
  trace (TRACE_DEBUG,"db_send_message_lines(): request for [%d] lines",lines);

  
  memtst ((buffer=(char *)my_malloc(READ_BLOCK_SIZE*2))==NULL);

  snprintf (query, DEF_QUERYSIZE, 
	    "SELECT * FROM messageblks WHERE message_idnr=%llu ORDER BY messageblk_idnr ASC",
	   message_idnr);
  trace (TRACE_DEBUG,"db_send_message_lines(): executing query [%s]",query);

  if (db_query(query)==-1)
    {
      my_free(buffer);
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_send_message_lines: mysql_store_result failed: %s",mysql_error(&conn));
      my_free(buffer);
      return 0;
    }
  
  trace (TRACE_DEBUG,"db_send_message_lines(): sending [%d] lines from message [%llu]",
	 lines,message_idnr);
  
  block_count=0;

  while (((row = mysql_fetch_row(res))!=NULL) && ((lines>0) || (lines==-2) || (block_count==0)))
  {
      nextpos = row[2];
      lengths = mysql_fetch_lengths(res);
      rowlength = lengths[2];
		
      /* reset our buffer */
      memset (buffer, '\0', (READ_BLOCK_SIZE)*2);
		
      while ((*nextpos!='\0') && (rowlength>0) && ((lines>0) || (lines==-2) || (block_count==0)))
      {
          if (*nextpos=='\n')
          {
              /* first block is always the full header 
                 so this should not be counted when parsing
                 if lines == -2 none of the lines should be counted 
                 since the whole message is requested */
              if ((lines!=-2) && (block_count!=0))
                  lines--;
						
              if (tmppos!=NULL)
              {
                  if (*tmppos=='\r')
                      sprintf (buffer,"%s%c",buffer,*nextpos);
                  else 
                      sprintf (buffer,"%s\r%c",buffer,*nextpos);
              }
              else 
                  sprintf (buffer,"%s\r%c",buffer,*nextpos);
          }
          else
          {
              if (*nextpos=='.')
              {
                  if (tmppos!=NULL)
                  {
                      if (*tmppos=='\n')
                          sprintf (buffer,"%s.%c",buffer,*nextpos);
                      else
                          sprintf (buffer,"%s%c",buffer,*nextpos);
                  }
                  else 
                      sprintf (buffer,"%s%c",buffer,*nextpos);
              }
              else	
                  sprintf (buffer,"%s%c",buffer,*nextpos);
          }
	  
          tmppos=nextpos;
	  
          /* get the next character */
          nextpos++;
          rowlength--;
	   
          if (rowlength%3000==0)  /* purge buffer at every 3000 bytes  */
          {
              /* fprintf ((FILE *)fstream,"%s",buffer); */
              /* fflush ((FILE *)fstream); */

              fwrite (buffer, sizeof(char), strlen(buffer), (FILE *)fstream);
          
              /*  cleanup the buffer  */
              memset (buffer, '\0', (READ_BLOCK_SIZE*2));
          }
      }

      /* next block in while loop */
      block_count++;
      trace (TRACE_DEBUG,"db_send_message_lines(): getting nextblock [%d]\n",block_count);
		
      /* flush our buffer */
      /* fprintf ((FILE *)fstream,"%s",buffer); */
      fwrite (buffer, sizeof(char), strlen(buffer), (FILE *)fstream);
      /* fflush ((FILE *)fstream); */
  }

  /* delimiter */
  if (no_end_dot == 0)
      fprintf ((FILE *)fstream,"\r\n.\r\n");
	
  mysql_free_result(res);
	
  my_free(buffer);
  return 1;
}


void db_session_cleanup (struct session *sessionptr)
{
  /* cleanups a session 
     removes a list and all references */

  sessionptr->totalsize=0;
  sessionptr->virtual_totalsize=0;
  sessionptr->totalmessages=0;
  sessionptr->virtual_totalmessages=0;
  list_freelist(&(sessionptr->messagelst.start));
}


/* returns 1 with a successfull session, -1 when something goes wrong 
 * sessionptr is changed with the right session info
 * useridnr is the userid index for the user whose mailbox we're viewing 
 */
int db_createsession (u64_t useridnr, struct session *sessionptr)
{
  /* first we do a query on the messages of this user */
  struct message tmpmessage;
  u64_t messagecounter=0;
	
  
  /* query is <2 because we don't want deleted messages 
   * the unique_id should not be empty, this could mean that the message is still being delivered */
  snprintf (query, DEF_QUERYSIZE, "SELECT * FROM messages WHERE mailbox_idnr=%llu AND status<002 AND "
	   "unique_id!=\"\" order by status ASC",
	   (db_get_mailboxid(useridnr, "INBOX")));

  if (db_query(query)==-1)
    {
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_createsession(): mysql_store_result failed:  %s",mysql_error(&conn));
      
      return -1;
    }
		
  sessionptr->totalmessages=0;
  sessionptr->totalsize=0;

  
  if ((messagecounter=mysql_num_rows(res))<1)
    {
      /* there are no messages for this user */
      
      mysql_free_result(res);
      return 1;
    }

  /* messagecounter is total message, +1 tot end at message 1 */
  messagecounter+=1;
	 
  /* filling the list */
	
  trace (TRACE_DEBUG,"db_createsession(): adding items to list");
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      tmpmessage.msize = 
	row[MESSAGE_MESSAGESIZE] ? strtoull(row[MESSAGE_MESSAGESIZE], NULL, 10) : 0;

      tmpmessage.realmessageid = 
	row[MESSAGE_MESSAGEIDNR] ? strtoull(row[MESSAGE_MESSAGEIDNR], NULL, 10) : 0;

      tmpmessage.messagestatus = 
	row[MESSAGE_STATUS] ? strtoull(row[MESSAGE_STATUS], NULL, 10) : 0;

      strncpy(tmpmessage.uidl,row[MESSAGE_UNIQUE_ID],UID_SIZE);
		
      tmpmessage.virtual_messagestatus =
	row[MESSAGE_STATUS] ? strtoull(row[MESSAGE_STATUS], NULL, 10) : 0;
	
      sessionptr->totalmessages+=1;
      sessionptr->totalsize+=tmpmessage.msize;
      /* descending to create inverted list */
      messagecounter-=1;
      tmpmessage.messageid=messagecounter;
      list_nodeadd (&sessionptr->messagelst, &tmpmessage, sizeof (tmpmessage));
    }
	
  trace (TRACE_DEBUG,"db_createsession(): adding succesfull");
	
  /* setting all virtual values */
  sessionptr->virtual_totalmessages=sessionptr->totalmessages;
  sessionptr->virtual_totalsize=sessionptr->totalsize;

  mysql_free_result(res);
  

  return 1;
}
		
int db_update_pop (struct session *sessionptr)
{
  
  struct element *tmpelement;
		
  

  /* get first element in list */
  tmpelement=list_getstart(&sessionptr->messagelst);
	

  while (tmpelement!=NULL)
    {
      /* check if they need an update in the database */
      if (((struct message *)tmpelement->data)->virtual_messagestatus!=
	  ((struct message *)tmpelement->data)->messagestatus) 
	{
	  /* yes they need an update, do the query */
	  snprintf (query,DEF_QUERYSIZE,
		    "UPDATE messages set status=%llu WHERE message_idnr=%llu AND status<002",
		   ((struct message *)tmpelement->data)->virtual_messagestatus,
		   ((struct message *)tmpelement->data)->realmessageid);
	
	  /* FIXME: a message could be deleted already if it has been accessed
	   * by another interface and be deleted by sysop
	   * we need a check if the query failes because it doesn't exists anymore
	   * now it will just bailout */
	
	  if (db_query(query)==-1)
	    {
	      trace(TRACE_ERROR,"db_update_pop(): could not execute query: []");
	      
	      return -1;
	    }
	}
      tmpelement=tmpelement->nextnode;
    }
  
  return 0;
}

u64_t db_check_mailboxsize (u64_t mailboxid)
{
  MYSQL_RES *localres;
  MYSQL_ROW localrow;
  
  /* checks the size of a mailbox */
  u64_t size;

  /* checking current size */
  snprintf (query, DEF_QUERYSIZE,
	    "SELECT SUM(messagesize) FROM messages WHERE mailbox_idnr = %llu AND status<002",
	   mailboxid);

  trace (TRACE_DEBUG,"db_check_mailboxsize(): executing query [%s]\n",
	 query);

  if (db_query(query) != 0)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): could not execute query [%s]\n",
	     query);
      
      return -1;
    }
  
  if ((localres = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): mysql_store_result failed: %s\n",
	     mysql_error(&conn));
      
      return -1;
    }

  if (mysql_num_rows(localres)<1)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): weird, cannot execute SUM query\n");

      mysql_free_result(localres);
      return 0;
    }

  localrow = mysql_fetch_row(localres);

  size = (localrow && localrow[0]) ? strtoull(localrow[0], NULL, 10) : 0;
  mysql_free_result(localres);
  

  return size;
}


u64_t db_check_sizelimit (u64_t addblocksize, u64_t message_idnr, 
				  u64_t *useridnr)
{
  /* returns -1 when a block cannot be inserted due to dbase failure
   *          1 when a block cannot be inserted due to quotum exceed
   *         -2 when a block cannot be inserted due to quotum exceed and a dbase failure occured
   * also does a complete rollback when this occurs 
   * returns 0 when situation is ok 
   */

  u64_t currmail_size = 0, maxmail_size = 0, j, n;

  *useridnr = db_get_useridnr (message_idnr);
	
  /* checking current size */
  snprintf (query, DEF_QUERYSIZE,"SELECT mailbox_idnr FROM mailboxes WHERE owner_idnr = %llu",
	   *useridnr);


  if (db_query(query) != 0)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): could not execute query [%s]\n",
	     query);
      
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): mysql_store_result failed: %s\n",
	     mysql_error(&conn));
      
      return -1;
    }


  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): user has NO mailboxes\n");
      
      mysql_free_result(res);
      return 0;
    }

  while ((row = mysql_fetch_row(res))!=NULL)
    {
      trace (TRACE_DEBUG,"db_check_sizelimit(): checking mailbox [%s]\n",row[0]);

      n = row[0] ? strtoull(row[0], NULL, 10) : 0;
      j = db_check_mailboxsize(n);

      if (j == (u64_t)-1)
	{
	  trace(TRACE_ERROR,"db_check_sizelimit(): could not verify mailboxsize\n");

	  mysql_free_result(res);
	  return -1;
	}

      currmail_size += j;
    }

  mysql_free_result(res);

  /* current mailsize from INBOX is now known, now check the maxsize for this user */
  maxmail_size = auth_getmaxmailsize(*useridnr);


  trace (TRACE_DEBUG, "db_check_sizelimit(): comparing currsize + blocksize [%llu], "
	 "maxsize [%llu]\n",
	 currmail_size, maxmail_size);
	

  /* currmail already represents the current size of messages from this user */
	
  if (((currmail_size) > maxmail_size) && (maxmail_size != 0))
    {
      trace (TRACE_INFO,"db_check_sizelimit(): mailboxsize of useridnr %llu exceed with %llu bytes\n", 
	     *useridnr, (currmail_size)-maxmail_size);

      /* user is exceeding, we're going to execute a rollback now */
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblks WHERE message_idnr = %llu", 
	       message_idnr);

      if (db_query(query) != 0)
	{
	  trace (TRACE_ERROR,"db_check_sizelimit(): rollback of mailbox add failed\n");
	  return -2;
	}

      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messages WHERE message_idnr = %llu",
	       message_idnr);

      if (db_query(query) != 0)
	{
	  trace (TRACE_ERROR,"db_check_sizelimit(): rollblock of mailbox add failed."
		 " DB might be inconsistent."
		 " run dbmail-maintenance\n");
	  return -2;
	}

      return 1;
    }

  return 0;
}


/* purges all the messages with a deleted status */
u64_t db_deleted_purge()
{
  unsigned i,n;
  u64_t *msgids = NULL;
  u64_t affected_rows=0;

  /* first we're deleting all the messageblks */
  snprintf (query,DEF_QUERYSIZE,"SELECT message_idnr FROM messages WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
	
  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR,"db_deleted_purge(): Cound not fetch message ID numbers: [%s]",
	    mysql_error(&conn));
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_deleted_purge(): mysql_store_result failed: [%s]",mysql_error(&conn));
      return -1;
    }
  
  if ( (n = mysql_num_rows(res)) < 1)
    {
      mysql_free_result(res);
      return 0;
    }
	
  if (! (msgids = my_malloc(sizeof(u64_t)*n)) )
    {
      trace(TRACE_ERROR, "db_deleted_purge(): out of memory");
      mysql_free_result(res);
      return -2;
    }
  memset(msgids, 0, n*sizeof(u64_t));

  for (i=0; i<n && (row = mysql_fetch_row(res))!=NULL; i++)
    msgids[i] = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
      
  mysql_free_result(res);

  for (i=0; i<n; i++)
    {
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblks WHERE message_idnr=%llu", msgids[i]);
      trace (TRACE_DEBUG,"db_deleted_purge(): trying to delete blocks for  message [%llu]", msgids[i]);
      if (db_query(query)==-1)
	{
	  trace(TRACE_ERROR, "db_deleted_purge(): could not delete blocks for message [%llu]: %s",
		msgids[i], mysql_error(&conn));
	  my_free(msgids);
	  msgids = 0;
	  mysql_free_result(res);
	  return -1;
	}

      mysql_free_result(res);
    }

  my_free(msgids);
  msgids = 0;

  /* messageblks are deleted. Now delete the messages */
  snprintf (query,DEF_QUERYSIZE,"DELETE FROM messages WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR, "db_deleted_purge(): could not delete messages: %s", 
	    mysql_error(&conn));
    
      mysql_free_result(res);
      return -1;
    }
	
  affected_rows = mysql_affected_rows(&conn);
  mysql_free_result(res);
  

  return affected_rows;
}


/* sets al messages with status 002 to status 003 for final
 * deletion 
 */
u64_t db_set_deleted ()
{
  /* first we're deleting all the messageblks */
  snprintf (query,DEF_QUERYSIZE,"UPDATE messages SET status=003 WHERE status=002");
  trace (TRACE_DEBUG,"db_set_deleted(): executing query [%s]",query);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR,"db_set_deleted(): Could not execute query [%s]",query);
      return -1;
    }
 
  
  return mysql_affected_rows(&conn);
}



/*
 * will add the ip number to a table
 * needed for POP/IMAP_BEFORE_SMTP
 */
int db_log_ip(const char *ip)
{
  char timestr[30];
  time_t td;
  struct tm tm;
  u64_t id=0;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  snprintf(query, DEF_QUERYSIZE, "SELECT idnr FROM pbsp WHERE ipnumber = '%s'", ip);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_log_ip(): could not access ip-log table (pop/imap-before-smtp): %s",
	    mysql_error(&conn));
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_log_ip(): could not check ip-log (pop/imap-before-smtp): %s",
	    mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  id = row ? strtoull(row[0], NULL, 10) : 0;

  mysql_free_result(res);

  if (id)
    {
      /* this IP is already in the table, update the 'since' field */
      snprintf(query, DEF_QUERYSIZE, "UPDATE pbsp SET since = '%s' WHERE idnr=%llu",timestr,id);

      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR,"db_log_ip(): could not update ip-log (pop/imap-before-smtp): %s",
		mysql_error(&conn));
	  return -1;
	}
    }
  else
    {
      /* IP not in table, insert row */
      snprintf(query, DEF_QUERYSIZE, "INSERT INTO pbsp (since, ipnumber) VALUES ('%s','%s')", 
	       timestr, ip);

      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR,"db_log_ip(): could not log IP number to dbase (pop/imap-before-smtp): %s",
		mysql_error(&conn));
	  return -1;
	}
    }

  trace(TRACE_DEBUG,"db_log_ip(): ip [%s] logged\n",ip);
  return 0;
}
  

/*
 * removes all entries from the IP log with a date/time before lasttokeep
 */
int db_cleanup_iplog(const char *lasttokeep)
{
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM pbsp WHERE since < '%s'",lasttokeep);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_cleanup_log(): error executing query [%s] : [%s]",
	    query, mysql_error(&conn));
      return -1;
    }

  return 0;
}

/* cleaning up the tables */
int db_cleanup()
{
  int result = 0;
  int i;

  for (i=0; i<DB_NTABLES; i++)
    {
      snprintf(query, DEF_QUERYSIZE, "OPTIMIZE TABLE %s", DB_TABLENAMES[i]);
      
      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR,"db_cleanup(): error optimizing table %s", DB_TABLENAMES[i]);
	  result = -1;
	}

      /* store & free result set */
      if ((res = mysql_store_result(&conn)) != NULL)
	mysql_free_result(res);
      
    }

  return result;
}

/*
 * db_empty_mailbox()
 *
 * empties the mailbox associated with user userid.
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 *
 */
int db_empty_mailbox(u64_t userid)
{
  u64_t *mboxids = NULL;
  unsigned n,i;
  int result;

  snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes WHERE owner_idnr=%llu",
	   userid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_empty_mailbox(): error executing query: [%s]",
	    mysql_error(&conn));
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_empty_mailbox(): mysql_store_result failed: [%s]",mysql_error(&conn));
      return -1;
    }

  n = mysql_num_rows(res);
  if (n==0)
    {
      mysql_free_result(res);
      trace(TRACE_WARNING, "db_empty_mailbox(): user [%llu] does not have any mailboxes?", userid);
      return 0;
    }

  mboxids = (u64_t*)my_malloc(n * sizeof(u64_t));
  if (!mboxids)
    {
      trace(TRACE_ERROR, "db_empty_mailbox(): not enough memory");
      mysql_free_result(res);
      return -2;
    }

  i=0;
  while ( (row = mysql_fetch_row(res)) )
    mboxids[i++] = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;

  mysql_free_result(res);
  
  for (result=0, i=0; i<n; i++)
    {
      if (db_delete_mailbox(mboxids[i], 1) == -1)
	{
	  trace(TRACE_ERROR, "db_empty_mailbox(): error emptying mailbox [%llu]", mboxids[i]);
	  result = -1;
	}
    }

  my_free(mboxids);
  return result;
}


/* 
 * will check for messageblks that are not
 * connected to messages 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing items being the messageblknr's of the
 * lost messageblks
 *
 * the caller should free this memory!
 */
int db_icheck_messageblks(struct list *lostlist)
{
  u64_t nblks, start, j;
  u64_t ncurr = 0;
  struct doubleID_t { u64_t msgblkid, msgid; } *currids = NULL;
  list_init(lostlist);

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk_idnr FROM messageblks ORDER BY messageblk_idnr DESC LIMIT 0,1");
  if (db_query(query)==-1)
    {
      trace (TRACE_ERROR,"db_icheck_messageblks(): Could not execute query [%s]",query);
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_icheck_messageblks(): mysql_store_result failed:  %s",mysql_error(&conn));
      return -1;
    }

  if (mysql_num_rows(res) != 1)
    {
      trace(TRACE_WARNING, "db_icheck_messageblks(): empty messageblk table");
      mysql_free_result(res);
      return 0; /* nothing in table ? */
    }

  row = mysql_fetch_row(res);
  nblks = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  mysql_free_result(res);

  for (start=0; start < nblks; start += ICHECK_RESULTSETSIZE)
    {
      snprintf(query, DEF_QUERYSIZE, "SELECT messageblk_idnr, message_idnr FROM messageblks LIMIT %llu,%llu", 
	       start, (u64_t)ICHECK_RESULTSETSIZE);

      if (db_query(query)==-1)
	{
	  trace (TRACE_ERROR,"db_icheck_messageblks(): Could not execute query [%s]",query);
	  return -1;
	}
      
      if ((res = mysql_store_result(&conn)) == NULL)
	{
	  trace (TRACE_ERROR,"db_icheck_messageblks(): mysql_store_result failed:  %s",mysql_error(&conn));
	  return -1;
	}

      trace(TRACE_DEBUG, "db_icheck_messageblks(): fetching another set of %llu messageblk id's..",
	    (u64_t)ICHECK_RESULTSETSIZE);

      
      ncurr = mysql_num_rows(res);
      currids = (struct doubleID_t*)my_malloc(sizeof(struct doubleID_t) * ncurr);
      if (!currids)
	{
	  trace(TRACE_ERROR,"db_icheck_messageblks(): out of memory when allocatin %d items\n",ncurr);
	  mysql_free_result(res);
	  return -2;
	}

      /*  copy current data set */
      for (j=0; j<ncurr; j++)
	{
	  row = mysql_fetch_row(res);
	  currids[j].msgblkid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
	  currids[j].msgid = (row && row[1]) ? strtoull(row[1], NULL, 10) : 0;
	}

      mysql_free_result(res); /* free result set */

      /* now check for each msgblkID if the associated msgID exists */
      /* if not, the msgblkID is added to 'lostlist' */
      
      for (j=0; j<ncurr; j++)
	{
	  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE message_idnr = %llu",
		   currids[j].msgid);
 
	  if (db_query(query)==-1)
	    {
	      list_freelist(&lostlist->start);
	      my_free(currids);
	      currids = NULL;
	      trace (TRACE_ERROR,"db_icheck_messageblks(): Could not execute query [%s]",query);
	      return -1;
	    }
	  
	  if ((res = mysql_store_result(&conn)) == NULL)
	    {
	      trace (TRACE_ERROR,"db_icheck_messageblks(): mysql_store_result failed:  %s",mysql_error(&conn));
	      return -1;
	    }

	  if (mysql_num_rows(res) == 0)
	    {
	      /* this is a lost block */
	      trace(TRACE_INFO,"db_icheck_messageblks(): found lost block, ID [%llu]", currids[j].msgblkid);

	      if (! list_nodeadd(lostlist, &currids[j].msgblkid, sizeof(currids[j].msgblkid)) )
		{
		  trace(TRACE_ERROR,"db_icheck_messageblks(): could not add block to list");
		  list_freelist(&lostlist->start);
		  my_free(currids);
		  currids = NULL;
		  mysql_free_result(res);
		  return -2;
		}
	    }

	  mysql_free_result(res); /* free result set */
	}

      /* free set of ID's */
      my_free(currids);
      currids = NULL;
    }

  return 0;
}


/* 
 * will check for messages that are not
 * connected to mailboxes 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing items being the messageidnr's of the
 * lost messages
 *
 * the caller should free this memory!
 */
int db_icheck_messages(struct list *lostlist)
{
  u64_t nmsgs, start, j;
  u64_t ncurr = 0;
  struct doubleID_t { u64_t msgid, mboxid; } *currids = NULL;
  list_init(lostlist);

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages ORDER BY message_idnr DESC LIMIT 0,1");
  if (db_query(query)==-1)
    {
      trace (TRACE_ERROR,"db_icheck_messages(): Could not execute query [%s]",query);
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_icheck_messages(): mysql_store_result failed:  %s",mysql_error(&conn));
      return -1;
    }

  if (mysql_num_rows(res) != 1)
    {
      trace(TRACE_WARNING, "db_icheck_messages(): empty message table");
      mysql_free_result(res);
      return 0; /* nothing in table ? */
    }

  row = mysql_fetch_row(res);
  nmsgs = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  mysql_free_result(res);

  for (start=0; start < nmsgs; start += ICHECK_RESULTSETSIZE)
    {
      snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr, mailbox_idnr FROM messages LIMIT %llu,%llu", 
	       start, (u64_t)ICHECK_RESULTSETSIZE);

      if (db_query(query)==-1)
	{
	  trace (TRACE_ERROR,"db_icheck_messages(): Could not execute query [%s]",query);
	  return -1;
	}
      
      if ((res = mysql_store_result(&conn)) == NULL)
	{
	  trace (TRACE_ERROR,"db_icheck_messages(): mysql_store_result failed:  %s",mysql_error(&conn));
	  return -1;
	}

      trace(TRACE_DEBUG, "db_icheck_messages(): fetching another set of %llu message id's..",
	    (u64_t)ICHECK_RESULTSETSIZE);

      
      ncurr = mysql_num_rows(res);
      currids = (struct doubleID_t*)my_malloc(sizeof(struct doubleID_t) * ncurr);
      if (!currids)
	{
	  trace(TRACE_ERROR,"db_icheck_messages(): out of memory when allocatin %d items\n",ncurr);
	  mysql_free_result(res);
	  return -2;
	}

      /*  copy current data set */
      for (j=0; j<ncurr; j++)
	{
	  row = mysql_fetch_row(res);
	  currids[j].msgid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
	  currids[j].mboxid = (row && row[1]) ? strtoull(row[1], NULL, 10) : 0;
	}

      mysql_free_result(res); /* free result set */

      /* now check for each msgID if the associated mailboxID exists */
      /* if not, the msgID is added to 'lostlist' */
      
      for (j=0; j<ncurr; j++)
	{
	  snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes WHERE mailbox_idnr = %llu",
		   currids[j].mboxid);
 
	  if (db_query(query)==-1)
	    {
	      list_freelist(&lostlist->start);
	      my_free(currids);
	      currids = NULL;
	      trace (TRACE_ERROR,"db_icheck_messages(): Could not execute query [%s]",query);
	      return -1;
	    }
	  
	  if ((res = mysql_store_result(&conn)) == NULL)
	    {
	      trace (TRACE_ERROR,"db_icheck_messages(): mysql_store_result failed:  %s",mysql_error(&conn));
	      return -1;
	    }

	  if (mysql_num_rows(res) == 0)
	    {
	      /* this is a lost block */
	      trace(TRACE_INFO,"db_icheck_messages(): found lost message, ID [%llu]", currids[j].msgid);

	      if (! list_nodeadd(lostlist, &currids[j].msgid, sizeof(currids[j].msgid)) )
		{
		  trace(TRACE_ERROR,"db_icheck_messages(): could not add message to list");
		  list_freelist(&lostlist->start);
		  my_free(currids);
		  currids = NULL;
		  mysql_free_result(res);
		  return -2;
		}
	    }

	  mysql_free_result(res); /* free result set */
	}

      /* free set of ID's */
      my_free(currids);
      currids = NULL;
    }

  return 0;
}


/* 
 * will check for mailboxes that are not
 * connected to users 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing items being the mailboxidnr's of the
 * lost mailboxes
 *
 * the caller should free this memory!
 */
int db_icheck_mailboxes(struct list *lostlist)
{
  u64_t nmboxs, start, j;
  u64_t ncurr = 0;
  struct doubleID_t { u64_t mboxid, userid; } *currids = NULL;
  list_init(lostlist);

  snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes ORDER BY mailbox_idnr DESC LIMIT 0,1");
  if (db_query(query)==-1)
    {
      trace (TRACE_ERROR,"db_icheck_mailboxes(): Could not execute query [%s]",query);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_icheck_mailboxes(): mysql_store_result failed:  %s",mysql_error(&conn));
      return -1;
    }

  if (mysql_num_rows(res) != 1)
    {
      trace(TRACE_WARNING, "db_icheck_mailboxes(): empty mailbox table");
      mysql_free_result(res);
      return 0; /* nothing in table ? */
    }

  row = mysql_fetch_row(res);
  nmboxs = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  mysql_free_result(res);

  for (start=0; start < nmboxs; start += ICHECK_RESULTSETSIZE)
    {
      snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr, owner_idnr FROM mailboxes LIMIT %llu,%llu", 
	       start, (u64_t)ICHECK_RESULTSETSIZE);

      if (db_query(query)==-1)
	{
	  trace (TRACE_ERROR,"db_icheck_mailboxes(): Could not execute query [%s]",query);
	  return -1;
	}
      
      if ((res = mysql_store_result(&conn)) == NULL)
	{
	  trace (TRACE_ERROR,"db_icheck_mailboxes(): mysql_store_result failed:  %s",mysql_error(&conn));
	  return -1;
	}

      trace(TRACE_DEBUG, "db_icheck_mailboxes(): fetching another set of %llu mailbox id's..",
	    (u64_t)ICHECK_RESULTSETSIZE);

      
      ncurr = mysql_num_rows(res);
      currids = (struct doubleID_t*)my_malloc(sizeof(struct doubleID_t) * ncurr);
      if (!currids)
	{
	  trace(TRACE_ERROR,"db_icheck_mailboxes(): out of memory when allocatin %d items\n",ncurr);
	  mysql_free_result(res);
	  return -2;
	}

      /*  copy current data set */
      for (j=0; j<ncurr; j++)
	{
	  row = mysql_fetch_row(res);
	  currids[j].mboxid = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
	  currids[j].userid = (row && row[1]) ? strtoull(row[1], NULL, 10) : 0;
	}

      mysql_free_result(res); /* free result set */

      /* now check for each mailboxID if the associated userID exists */
      /* if not, the mailboxID is added to 'lostlist' */
      
      for (j=0; j<ncurr; j++)
	{
	  snprintf(query, DEF_QUERYSIZE, "SELECT user_idnr FROM users WHERE user_idnr = %llu",
		   currids[j].userid);
 
	  if (db_query(query)==-1)
	    {
	      list_freelist(&lostlist->start);
	      my_free(currids);
	      currids = NULL;
	      trace (TRACE_ERROR,"db_icheck_mailboxes(): Could not execute query [%s]",query);
	      return -1;
	    }
	  
	  if ((res = mysql_store_result(&conn)) == NULL)
	    {
	      trace (TRACE_ERROR,"db_icheck_mailboxes(): mysql_store_result failed:  %s",mysql_error(&conn));
	      mysql_free_result(res);
	      return -1;
	    }

	  if (mysql_num_rows(res) == 0)
	    {
	      /* this is a lost block */
	      trace(TRACE_INFO,"db_icheck_mailboxes(): found lost mailbox, ID [%llu]", currids[j].mboxid);

	      if (! list_nodeadd(lostlist, &currids[j].mboxid, sizeof(currids[j].mboxid)) )
		{
		  trace(TRACE_ERROR,"db_icheck_mailboxes(): could not add mailbox to list");
		  list_freelist(&lostlist->start);
		  my_free(currids);
		  currids = NULL;
		  mysql_free_result(res);
		  return -2;
		}
	    }

	  mysql_free_result(res); /* free result set */
	}

      /* free set of ID's */
      my_free(currids);
      currids = NULL;
    }

  return 0;
}


/*
 * deletes the specified block. used by maintenance
 */
int db_delete_messageblk(u64_t uid)
{
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE messageblk_idnr = %llu",uid);
  return db_query(query);
}


/*
 * deletes the specified message. used by maintenance
 */
int db_delete_message(u64_t uid)
{
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu",uid);
  if (db_query(query) == -1)
    return -1;

  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu",uid);
  return db_query(query);
}

/*
 * deletes the specified mailbox. used by maintenance
 * 
 * if only_empty is non-zero the mailbox will not be deleted but just emptied.
 */
int db_delete_mailbox(u64_t uid, int only_empty)
{
  u64_t *msgids=NULL;
  unsigned i,n;

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE mailbox_idnr = %llu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_delete_mailbox(): could not select message ID's for mailbox [%llu]: [%s]", 
	    uid, mysql_error(&conn));
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_delete_mailbox(): mysql_store_result() failed [%s]",mysql_error(&conn));
      return -1;
    }

  /* first save this result for we cannot keep the result set when executing other queries */
  n = mysql_num_rows(res);
  if (n == 0)
    {
      mysql_free_result(res);

      trace(TRACE_INFO, "db_delete_mailbox(): mailbox is empty");
      if (!only_empty)
	{
	  /* delete mailbox */
	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailboxes WHERE mailbox_idnr = %llu",uid);

	  if (db_query(query) == -1)
	    {
	      trace(TRACE_ERROR, "db_delete_mailbox(): could not delete mailbox [%llu]: [%s]", 
		    uid, mysql_error(&conn));	  
	      return -1;
	    }
	}
      return 0;
    }

  if (! (msgids = (u64_t*)my_malloc(sizeof(u64_t) * n)) )
    {
      mysql_free_result(res);
      trace(TRACE_ERROR, "db_delete_mailbox(): not enough memory");
    }

  i=0;
  while ((row = mysql_fetch_row(res)))
    msgids[i++] = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;

  mysql_free_result(res);

  for (i=0; i<n; i++)
    {
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgids[i]);
      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_delete_mailbox(): could not delete messageblks for message [%llu]", 
		msgids[i]);
	  my_free(msgids);
	  return -1;
	}

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu",msgids[i]);
      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_delete_mailbox(): could not delete message [%llu]", msgids[i]);
	  my_free(msgids);
	  return -1;
	}
    }

  my_free(msgids);
  msgids = NULL;

  if (!only_empty)
    {
      /* delete mailbox */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailboxes WHERE mailbox_idnr = %llu",uid);

      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_delete_mailbox(): could not delete mailbox [%llu]", uid);	  
	  return -1;
	}
    }
  return 0;
}


int db_disconnect()
{
  my_free(query);
  query = NULL;

  mysql_close(&conn);
  return 0;
}


/*
 * db_imap_append_msg()
 *
 * inserts a message
 *
 * returns: 
 * -1 serious dbase/memory error
 *  0 ok
 *  1 invalid msg
 *  2 mail quotum exceeded
 *
 */
int db_imap_append_msg(const char *msgdata, u64_t datalen, u64_t mboxid, u64_t uid)
{
  char timestr[30];
  time_t td;
  struct tm tm;
  u64_t msgid,cnt;
  int result;
  char unique_id[UID_SIZE]; /* unique id */
  
  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  /* create a msg 
   * status and seen_flag are set to 001, which means the message has been read 
   */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO messages "
	   "(mailbox_idnr,messagesize,unique_id,internal_date,status,"
	   " seen_flag) VALUES (%llu, 0, \"\", \"%s\",001,1)",
	   mboxid, timestr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_imap_append_msg(): could not create message\n");
      my_free(query);
      return -1;
    }

  /* fetch the id of the new message */
  msgid = mysql_insert_id(&conn);
  
  result = db_check_sizelimit(datalen, msgid, &uid);
  if (result == -1 || result == -2)
    {     
      trace(TRACE_ERROR, "db_imap_append_msg(): dbase error checking size limit\n");
      
      return -1;
    }

  if (result)
    {     
      trace(TRACE_INFO, "db_imap_append_msg(): user %llu would exceed quotum\n",uid);
      
      return 2;
    }


  /* ok insert blocks */
  /* first the header: scan until double newline */
  for (cnt=1; cnt<datalen; cnt++)
    if (msgdata[cnt-1] == '\n' && msgdata[cnt] == '\n')
      break;

  if (cnt == datalen)
    {
      trace(TRACE_INFO, "db_imap_append_msg(): no double newline found [invalid msg]\n");
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
	      "dbase invalid now..\n", msgid);

      return 1;
    }

  if (cnt == datalen-1)
    {
      /* msg consists of a single header */
      trace(TRACE_INFO, "db_imap_append_msg(): msg only contains a header\n");

      if (db_insert_message_block(msgdata, datalen, msgid) == -1 || 
	  db_insert_message_block(" \n", 2, msgid)   == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }
  else
    {
      /* 
       * output header: 
       * the first cnt bytes is the header
       */
      cnt++;

      if (db_insert_message_block(msgdata, cnt, msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

      /* output message */
      while ((datalen - cnt) > READ_BLOCK_SIZE)
	{
	  if (db_insert_message_block(&msgdata[cnt], READ_BLOCK_SIZE, msgid) == -1)
	    {
	      trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		      "dbase invalid now..\n", msgid);

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		      "for msg id [%llu], dbase could be invalid now..\n", msgid);

	      
	      return -1;
	    }

	  cnt += READ_BLOCK_SIZE;
	}


      if (db_insert_message_block(&msgdata[cnt], datalen-cnt, msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblks WHERE message_idnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		  "for msg id [%llu], dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }  
  
  /* create a unique id */
  snprintf (unique_id,UID_SIZE,"%lluA%lu",msgid,td);

  /* set info on message */
  db_update_message (msgid, unique_id, datalen, 0);
  
  return 0;
}


/*
 * db_insert_message_complete()
 *
 * Inserts a complete message into the messages/messageblks tables.
 *
 * This function 'hacks' into the internal MEM structure for maximum speed.
 * The MEM data is supposed to contain ESCAPED data so inserts can be done directly.
 *
 * returns -1 on failure, 0 on success
 */
int db_insert_message_complete(u64_t useridnr, MEM *hdr, MEM *body, 
			       u64_t hdrsize, u64_t bodysize, u64_t rfcsize)

{
  return 0;
}

/*
 * db_findmailbox()
 *
 * checks wheter the mailbox designated by 'name' exists for user 'useridnr'
 *
 * returns 0 if the mailbox is not found, 
 * (unsigned)(-1) on error,
 * or the UID of the mailbox otherwise.
 */
u64_t db_findmailbox(const char *name, u64_t useridnr)
{
  u64_t id;

  snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes WHERE name='%s' AND owner_idnr=%llu",
	   name, useridnr);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_findmailbox(): could not select mailbox '%s'\n",name);
      return (u64_t)(-1);
    }

  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_findmailbox(): mysql_store_result failed:  %s\n",mysql_error(&conn));
      return (u64_t)(-1);
    }
  
  
  row = mysql_fetch_row(res);
  id = (row) ? strtoull(row[0], NULL, 10) : 0;

  mysql_free_result(res);

  return id;
}
  

/*
 * db_findmailbox_by_regex()
 *
 * finds all the mailboxes owned by ownerid who match the regex pattern pattern.
 */
int db_findmailbox_by_regex(u64_t ownerid, const char *pattern, 
			    u64_t **children, unsigned *nchildren, int only_subscribed)
{
  
  int result;
  u64_t *tmp = NULL;
  regex_t preg;
  *children = NULL;

  if ((result = regcomp(&preg, pattern, REG_ICASE|REG_NOSUB)) != 0)
    {
      trace(TRACE_ERROR, "db_findmailbox_by_regex(): error compiling regex pattern: %d\n",
	    result);
      
      return 1;
    }

  if (only_subscribed)
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailbox_idnr FROM mailboxes WHERE "
	     "owner_idnr=%llu AND is_subscribed != 0", ownerid);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailbox_idnr FROM mailboxes WHERE "
	     "owner_idnr=%llu", ownerid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): error during mailbox query\r\n");
      return (-1);
    }

  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): mysql_store_result failed:  %s\n",
	    mysql_error(&conn));
      return (-1);
    }
  
  if (mysql_num_rows(res) == 0)
    {
      /* none exist, none matched */
      *nchildren = 0;
      mysql_free_result(res);
      return 0;
    }

  /* alloc mem */
  tmp = (u64_t *)my_malloc(sizeof(u64_t) * mysql_num_rows(res));
  if (!tmp)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): not enough memory\n");
      return (-1);
    }
     
  /* store matches */
  *nchildren = 0;
  while ((row = mysql_fetch_row(res)))
    {
      if (regexec(&preg, row[0], 0, NULL, 0) == 0)
	tmp[(*nchildren)++] = strtoull(row[1], NULL, 10);
    }

  mysql_free_result(res);

  if (*nchildren == 0)
    {
      my_free(tmp);
      return 0;
    }

  /* realloc mem */
  *children = (u64_t *)realloc(tmp, sizeof(u64_t) * *nchildren);
  if (!(*children))
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): realloc failed\n");
      my_free(tmp);
      return -1;
    }

  return 0;
}


/*
 * db_getmailbox()
 * 
 * gets mailbox info from dbase, builds the message sequence number list
 *
 * returns 
 *  -1  error
 *   0  success
 */
int db_getmailbox(mailbox_t *mb, u64_t userid)
{
  u64_t i;

  /* free existing MSN list */
  if (mb->seq_list)
    {
      my_free(mb->seq_list);
      mb->seq_list = NULL;
    }

  mb->flags = 0;
  mb->exists = 0;
  mb->unseen = 0;
  mb->recent = 0;
  mb->msguidnext = 0;

  /* select mailbox */
  snprintf(query, DEF_QUERYSIZE, 
	   "SELECT permission,"
	   "seen_flag,"
	   "answered_flag,"
	   "deleted_flag,"
	   "flagged_flag,"
	   "recent_flag,"
	   "draft_flag "
	   " FROM mailboxes WHERE mailbox_idnr = %llu", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not select mailbox\n");
      return -1;
    }


  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed:  %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      trace(TRACE_ERROR,"db_getmailbox(): invalid mailbox id specified\n");
      mysql_free_result(res);
      return -1;
    }

  mb->permission = atoi(row[0]);

  if (row[1]) mb->flags |= IMAPFLAG_SEEN;
  if (row[2]) mb->flags |= IMAPFLAG_ANSWERED;
  if (row[3]) mb->flags |= IMAPFLAG_DELETED;
  if (row[4]) mb->flags |= IMAPFLAG_FLAGGED;
  if (row[5]) mb->flags |= IMAPFLAG_RECENT;
  if (row[6]) mb->flags |= IMAPFLAG_DRAFT;

  mysql_free_result(res);

  /* select messages */
  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr, seen_flag, recent_flag "
	   "FROM messages WHERE mailbox_idnr = %llu "
	   "AND status<2 AND unique_id!=\"\" ORDER BY message_idnr ASC", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not retrieve messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  mb->exists = mysql_num_rows(res);

  /* alloc mem */
  mb->seq_list = (u64_t*)my_malloc(sizeof(u64_t) * mb->exists);
  if (!mb->seq_list)
    {
      /* out of mem */
      mysql_free_result(res);
      return -1;
    }

  i=0;
  while ((row = mysql_fetch_row(res)))
    {
      if (row[1][0] == '0') mb->unseen++;
      if (row[2][0] == '1') mb->recent++;

      mb->seq_list[i++] = strtoull(row[0],NULL,10);
    }
  
  mysql_free_result(res);

  
  /* now determine the next message UID */
  /*
   * NOTE expunged messages are selected as well in order to be able to restore them 
   */

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE unique_id!=\"\""
	   "ORDER BY message_idnr DESC LIMIT 0,1");
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not determine highest message ID\n");

      my_free(mb->seq_list);
      mb->seq_list = NULL;

      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      
      my_free(mb->seq_list);
      mb->seq_list = NULL;

      return -1;
    }

  row = mysql_fetch_row(res);
  mb->msguidnext = (row && row[0]) ? strtoull(row[0], NULL, 10)+1 : 1;

  mysql_free_result(res);
  
  
  /* done */
  return 0;
}


/*
 * db_createmailbox()
 *
 * creates a mailbox for the specified user
 * does not perform hierarchy checks
 * 
 * returns -1 on error, 0 on succes
 */
int db_createmailbox(const char *name, u64_t ownerid)
{
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO mailboxes (name, owner_idnr,"
	   "seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, permission)"
	   " VALUES ('%s', %llu, 1, 1, 1, 1, 1, 1, 2)", name,ownerid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_createmailbox(): could not create mailbox\n");
      
      return -1;
    }

  
  return 0;
}
  

/*
 * db_listmailboxchildren()
 *
 * produces a list containing the UID's of the specified mailbox' children 
 * matching the search criterion
 *
 * returns -1 on error, 0 on succes
 */
int db_listmailboxchildren(u64_t uid, u64_t useridnr, 
			   u64_t **children, int *nchildren, 
			   const char *filter)
{
  int i;

  /* retrieve the name of this mailbox */
  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailboxes WHERE"
	   " mailbox_idnr = %llu AND owner_idnr = %llu", uid, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes WHERE name LIKE '%s/%s'"
	     " AND owner_idnr = %llu",
	     row[0],filter,useridnr);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT mailbox_idnr FROM mailboxes WHERE name LIKE '%s'"
	     " AND owner_idnr = %llu",filter,useridnr);

  mysql_free_result(res);
  
  /* now find the children */
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      /* empty set */
      *children = NULL;
      *nchildren = 0;
      mysql_free_result(res);
      return 0;
    }

  *nchildren = mysql_num_rows(res);
  if (*nchildren == 0)
    {
      *children = NULL;
      mysql_free_result(res);
      return 0;
    }
  *children = (u64_t*)my_malloc(sizeof(u64_t) * (*nchildren));

  if (!(*children))
    {
      /* out of mem */
      trace(TRACE_ERROR,"db_listmailboxchildren(): out of memory\n");
      mysql_free_result(res);
      
      return -1;
    }

  i = 0;
  do
    {
      if (i == *nchildren)
	{
	  /*  big fatal */
	  my_free(*children);
	  *children = NULL;
	  *nchildren = 0;
	  mysql_free_result(res);
	  trace(TRACE_ERROR, "db_listmailboxchildren: data when none expected.\n");
	  
	  return -1;
	}

      (*children)[i++] = strtoull(row[0], NULL, 10);
    }
  while ((row = mysql_fetch_row(res)));

  mysql_free_result(res);
  

  return 0; /* success */
}
 

/*
 * db_removemailbox()
 *
 * removes the mailbox indicated by UID/ownerid and all the messages associated with it
 * the mailbox SHOULD NOT have any children but no checks are performed
 *
 * returns -1 on failure, 0 on succes
 */
int db_removemailbox(u64_t uid, u64_t ownerid)
{
  if (db_removemsg(uid) == -1) /* remove all msg */
    {
      return -1;
    }

  /* now remove mailbox */
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailboxes WHERE mailbox_idnr = %llu", uid);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_removemailbox(): could not remove mailbox\n");
      return -1;
    }
  
  /* done */
  return 0;
}


/*
 * db_isselectable()
 *
 * returns 1 if the specified mailbox is selectable, 0 if not and -1 on failure
 */  
int db_isselectable(u64_t uid)
{
  

  snprintf(query, DEF_QUERYSIZE, "SELECT no_select FROM mailboxes WHERE mailbox_idnr = %llu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_isselectable(): could not retrieve select-flag\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_isselectable(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      return 0;
    }

  if (atoi(row[0]) == 0)
    {    
      mysql_free_result(res);
      return 1;
    }

  mysql_free_result(res);
  return 0;
}
  

/*
 * db_noinferiors()
 *
 * checks if mailbox has no_inferiors flag set
 *
 * returns
 *   1  flag is set
 *   0  flag is not set
 *  -1  error
 */
int db_noinferiors(u64_t uid)
{
  

  snprintf(query, DEF_QUERYSIZE, "SELECT no_inferiors FROM mailboxes WHERE mailbox_idnr = %llu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_noinferiors(): could not retrieve noinferiors-flag\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_noinferiors(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      return 0;
    }

  if (atoi(row[0]) == 1)
    {    
      mysql_free_result(res);
      return 1;
    }

  mysql_free_result(res);
  return 0;
}


/*
 * db_setselectable()
 *
 * set the noselect flag of a mailbox on/off
 * returns 0 on success, -1 on failure
 */
int db_setselectable(u64_t uid, int value)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailboxes SET no_select = %d WHERE mailbox_idnr = %llu",
	   (!value), uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_setselectable(): could not set noselect-flag\n");
      return -1;
    }

  return 0;
}


/*
 * db_removemsg()
 *
 * removes ALL messages from a mailbox
 * removes by means of setting status to 3
 *
 * returns -1 on failure, 0 on success
 */
int db_removemsg(u64_t uid)
{
  

  /* update messages belonging to this mailbox: mark as deleted (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET status=3 WHERE"
	   " mailbox_idnr = %llu", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_removemsg(): could not update messages in mailbox\n");
      return -1;
    }

  return 0; /* success */
}


/*
 * db_expunge()
 *
 * removes all messages from a mailbox with delete-flag
 * removes by means of setting status to 3
 * makes a list of delete msg UID's 
 *
 * allocated memory should be freed by client; if msgids and/or nmsgs are NULL 
 * no list of deleted msg UID's will be made
 *
 * returns -1 on failure, 0 on success
 */
int db_expunge(u64_t uid,u64_t **msgids,u64_t *nmsgs)
{
  u64_t i;

  if (nmsgs && msgids)
    {
      /* first select msg UIDs */
      snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE"
	       " mailbox_idnr = %llu AND deleted_flag=1 AND status<2 ORDER BY message_idnr DESC", uid);

      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_expunge(): could not select messages in mailbox\n");
	  return -1;
	}
      
      if ((res = mysql_store_result(&conn)) == NULL)
	{
	  trace(TRACE_ERROR,"db_expunge(): mysql_store_result failed: %s\n",mysql_error(&conn));
	  return -1;
	}

      /* now alloc mem */
      *nmsgs = mysql_num_rows(res);
      *msgids = (u64_t *)my_malloc(sizeof(u64_t) * (*nmsgs));
      if (!(*msgids))
	{
	  /* out of mem */
	  *nmsgs = 0;
	  mysql_free_result(res);
	  return -1;
	}

      /* save ID's in array */
      i = 0;
      while ((row = mysql_fetch_row(res)) && i<*nmsgs)
	{
	  (*msgids)[i++] = strtoull(row[0], NULL, 10);
	}
      mysql_free_result(res);
    }

  /* update messages belonging to this mailbox: mark as expunged (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET status=3 WHERE"
	   " mailbox_idnr = %llu AND deleted_flag=1 AND status<2", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_expunge(): could not update messages in mailbox\n");
      if (msgids)
	my_free(*msgids);

      if (nmsgs)
	*nmsgs = 0;

      return -1;
    }

  return 0; /* success */
}
    

/*
 * db_movemsg()
 *
 * moves all msgs from one mailbox to another
 * returns -1 on error, 0 on success
 */
int db_movemsg(u64_t to, u64_t from)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET mailbox_idnr=%llu WHERE"
	   " mailbox_idnr = %llu", to, from);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_movemsg(): could not update messages in mailbox\n");
      return -1;
    }

  return 0; /* success */
}


/*
 * db_copymsg()
 *
 * copies a msg to a specified mailbox
 * returns 0 on success, -1 on failure, -2 on quotum exceeded/quotum would exceed
 */
int db_copymsg(u64_t msgid, u64_t destmboxid)
{
  u64_t newid,tmpid, curr_quotum, userid, maxmail, msgsize;
  time_t td;

  time(&td);              /* get time */

  /* check if there is space left for this message */
  userid = db_get_useridnr(msgid);
  if (userid == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): error fetching userid");
      return -1;
    }

  curr_quotum = db_get_quotum_used(userid);
  if (curr_quotum == -1 || curr_quotum == -2)
    {
      trace(TRACE_ERROR, "db_copymsg(): error fetching used quotum for user [%llu]", userid);
      return -1;
    }
     
  maxmail = auth_getmaxmailsize(userid);
  if (maxmail == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): error fetching max quotum for user [%llu]", userid);
      return -1;
    }

  if (maxmail > 0)
    {
      if (curr_quotum >= maxmail)
	{
	  trace(TRACE_INFO, "db_copymsg(): quotum already exceeded\n");
	  return -2;
	}

      snprintf(query, DEF_QUERYSIZE, "SELECT messagesize FROM messages WHERE message_idnr = %llu", msgid);
  
      if (db_query(query) == -1)
	{
	  trace(TRACE_ERROR, "db_copymsg(): could not fetch message size for message id [%llu]\n", msgid);
	  return -1;
	}

      if ((res = mysql_store_result(&conn)) == NULL)
	{
	  trace(TRACE_ERROR,"db_copymsg(): mysql_store_result failed: %s\n",mysql_error(&conn));
	  return -1;
	}

      if (mysql_num_rows(res) != 1)
	{
	  trace(TRACE_ERROR, "db_copymsg(): message [%llu] does not exist/has multiple entries\n", msgid);
	  mysql_free_result(res);
	  return -1;
	}

      row = mysql_fetch_row(res);
  
      if (row && row[0])
	msgsize = strtoull(row[0], NULL, 10);
      else
	{
	  trace(TRACE_ERROR, "db_copymsg(): no result set after requesting msgsize of msg [%llu]\n",
		msgid);
	  mysql_free_result(res);
	  return -1;
	}
      
      mysql_free_result(res);

      if (msgsize > maxmail - curr_quotum)
	{
	  trace(TRACE_INFO, "db_copymsg(): quotum would exceed");
	  return -2;
	}
    }


  /* copy: */

  /* first to temporary table */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessage (mailbox_idnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
	   " unique_id, internal_date) "
	   "SELECT mailbox_idnr, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
	   "FROM messages WHERE messages.message_idnr = %llu",
	   msgid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message\n");
      return -1;
    }

  tmpid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessageblk (message_idnr, messageblk, blocksize) "
	   "SELECT %llu, messageblk, blocksize FROM messageblks "
	   "WHERE messageblks.message_idnr = %llu ORDER BY messageblks.messageblk_idnr", tmpid, msgid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message blocks\n");

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE message_idnr = %llu", tmpid);

      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      return -1;
    }


  /* now to actual tables */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO messages (mailbox_idnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
	   " unique_id, internal_date) "
	   "SELECT %llu, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
	   "FROM tmpmessage WHERE tmpmessage.message_idnr = %llu",
	   destmboxid, tmpid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message\n");

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE message_idnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE message_idnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");
      
      return -1;
    }

  /* retrieve id of new message */
  newid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO messageblks (message_idnr, messageblk, blocksize) "
	   "SELECT %llu, messageblk, blocksize FROM tmpmessageblk "
	   "WHERE tmpmessageblk.message_idnr = %llu ORDER BY tmpmessageblk.messageblk_idnr", newid, tmpid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message blocks\n");

      /* delete temporary messages */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE message_idnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE message_idnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");

      /* delete inserted message */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messages WHERE message_idnr = %llu",newid);
      if (db_query(query) == -1)
	trace(TRACE_FATAL, "db_copymsg(): could not delete faulty message, dbase contains "
	      "invalid data now; msgid [%llu]\n",newid);
      
      return -1;
    }

  /* delete temporary messages */
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE message_idnr = %llu", tmpid);
  if (db_query(query) == -1)
    trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE message_idnr = %llu", tmpid);
  if (db_query(query) == -1)
    trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");

  /* all done, validate new msg by creating a new unique id for the copied msg */
  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET unique_id=\"%lluA%lu\" "
	   "WHERE message_idnr=%llu", newid, td, newid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not set unique ID for copied msg\n");
      return -1;
    }

  return 0; /* success */
}


/*
 * db_getmailboxname()
 *
 * retrieves the name of a specified mailbox
 * *name should be large enough to contain the name (IMAP_MAX_MAILBOX_NAMELEN)
 * returns -1 on error, 0 on success
 */
int db_getmailboxname(u64_t uid, char *name)
{
  

  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailboxes WHERE mailbox_idnr = %llu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailboxname(): could not retrieve name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailboxname(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      *name = '\0';
      return 0;
    }

  strncpy(name, row[0], IMAP_MAX_MAILBOX_NAMELEN);
  mysql_free_result(res);
  return 0;
}
  

/*
 * db_setmailboxname()
 *
 * sets the name of a specified mailbox
 * returns -1 on error, 0 on success
 */
int db_setmailboxname(u64_t uid, const char *name)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailboxes SET name = '%s' WHERE mailbox_idnr = %llu",
	   name, uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_setmailboxname(): could not set name\n");
      return -1;
    }

  return 0;
}


/*
 * db_first_unseen()
 *
 * return the message UID of the first unseen msg or -1 on error
 */
u64_t db_first_unseen(u64_t uid)
{
  
  u64_t id;

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE mailbox_idnr = %llu "
	   "AND status<2 AND seen_flag = 0 AND unique_id != \"\" "
	   "ORDER BY message_idnr ASC LIMIT 0,1", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_first_unseen(): could not select messages\n");
      return (u64_t)(-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_first_unseen(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (u64_t)(-1);
    }
  
  row = mysql_fetch_row(res);
  if (row)
    id = strtoull(row[0],NULL,10);
  else
    id = 0; /* none found */
      
  mysql_free_result(res);
  return id;
}
  

/*
 * db_subscribe()
 *
 * subscribes to a certain mailbox
 */
int db_subscribe(u64_t mboxid)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailboxes SET is_subscribed = 1 WHERE mailbox_idnr = %llu", 
	   mboxid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_subscribe(): could not update mailbox\n");
      return (-1);
    }

  return 0;
}
  

/*
 * db_unsubscribe()
 *
 * unsubscribes to a certain mailbox
 */
int db_unsubscribe(u64_t mboxid)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailboxes SET is_subscribed = 0 WHERE mailbox_idnr = %llu", 
	   mboxid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_unsubscribe(): could not update mailbox\n");
      return (-1);
    }

  return 0;
}

  
/*
 * db_get_msgflag()
 *
 * gets a flag value specified by 'name' (i.e. 'seen' would check the Seen flag)
 *
 * returns:
 *  -1  error
 *   0  flag not set
 *   1  flag set
 */
int db_get_msgflag(const char *name, u64_t msguid, u64_t mailboxuid)
{
  
  char flagname[DEF_QUERYSIZE/2]; /* should be sufficient ;) */
  int val=0;

  /* determine flag */
  if (strcasecmp(name,"seen") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "seen_flag");
  else if (strcasecmp(name,"deleted") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "deleted_flag");
  else if (strcasecmp(name,"answered") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "answered_flag");
  else if (strcasecmp(name,"flagged") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "flagged_flag");
  else if (strcasecmp(name,"recent") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "recent_flag");
  else if (strcasecmp(name,"draft") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "draft_flag");
  else
    return 0; /* non-existent flag is not set */

  snprintf(query, DEF_QUERYSIZE, "SELECT %s FROM messages WHERE "
	   "message_idnr = %llu AND status<2 AND unique_id != \"\" "
	   "AND mailbox_idnr = %llu", flagname, msguid, mailboxuid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msgflag(): could not select message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msgflag(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }
  
  row = mysql_fetch_row(res);
  if (row && row[0])
    val = atoi(row[0]);
  else
    val = 0; /* none found */
      
  mysql_free_result(res);
  return val;
}


/*
 * db_get_msgflag_all()
 *
 * gets all flags for a specified message
 *
 * flags are placed in *flags, an array build up according to IMAP_FLAGS 
 * (see top of imapcommands.c)
 *
 * returns:
 *  -1  error
 *   0  success
 */
int db_get_msgflag_all(u64_t msguid, u64_t mailboxuid, int *flags)
{
  int i;

  memset(flags, 0, sizeof(int) * IMAP_NFLAGS);

  snprintf(query, DEF_QUERYSIZE, "SELECT seen_flag, answered_flag, deleted_flag, "
	   "flagged_flag, draft_flag, recent_flag FROM messages WHERE "
	   "message_idnr = %llu AND status<2 AND unique_id != '' "
	   "AND mailbox_idnr = %llu", msguid, mailboxuid);
 
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msgflag_all(): could not select message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msgflag_all(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }
  
   if (mysql_num_rows(res) > 0)
    {
      row = mysql_fetch_row(res);

      for (i=0; i<IMAP_NFLAGS && row; i++)
	{
	  if (row[i] && row[i][0] != '0') flags[i] = 1;
	}
    }
 
   mysql_free_result(res);
   return 0;
}


/*
 * db_set_msgflag()
 *
 * sets flags for a message
 * Flag set is specified in *flags; indexed as follows (see top of imapcommands.c file)
 * [0] "Seen", 
 * [1] "Answered", 
 * [2] "Deleted", 
 * [3] "Flagged", 
 * [4] "Draft", 
 * [5] "Recent"
 *
 * a value of zero represents 'off', a value of one "sets" the flag
 *
 * action_type can be one of the IMAP_FLAG_ACTIONS:
 *
 * IMAPFA_REPLACE  new set will be exactly as *flags with '1' to set, '0' to clear
 * IMAPFA_ADD      set all flags which have '1' as value in *flags
 * IMAPFA_REMOVE   clear all flags which have '1' as value in *flags
 * 
 * returns:
 *  -1  error
 *   0  success 
 */
int db_set_msgflag(u64_t msguid, u64_t mailboxuid, int *flags, int action_type)
{
  int i,placed=0;
  int left = DEF_QUERYSIZE - sizeof("answered_flag=1,");

  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET ");
  
  for (i=0; i<IMAP_NFLAGS; i++)
    {
      switch (action_type)
	{
	case IMAPFA_ADD:
	  if (flags[i] > 0)
	    {
	      strncat(query, db_flag_desc[i], left);
	      left -= sizeof("answered_flag");
	      strncat(query, "=1,", left);
	      left -= sizeof(" =1, ");
	      placed = 1;
	    }
	  break;

	case IMAPFA_REMOVE:
	  if (flags[i] > 0)
	    {
	      strncat(query, db_flag_desc[i], left);
	      left -= sizeof("answered_flag");
	      strncat(query, "=0,", left);
	      left -= sizeof("=0,");
	      placed = 1;
	    }
	  break;

	case IMAPFA_REPLACE:
	  strncat(query, db_flag_desc[i], left);
	  left -= sizeof("answered_flag");

	  if (flags[i] == 0)
	    strncat(query, "=0,", left);
	  else
	    strncat(query, "=1,", left);

	  left -= sizeof("=1,");
	  placed = 1;

	  break;
	}
    }

  if (!placed) 
    return 0; /* nothing to update */
  
  
  /* last character in string is comma, replace it --> strlen()-1 */
  snprintf(&query[strlen(query)-1], left, " WHERE "
	   "message_idnr = %llu AND status<2 "
	   "AND mailbox_idnr = %llu", msguid, mailboxuid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_set_msgflag(): could not set flag\n");
      return (-1);
    }

  return 0;
}


/*
 * db_set_msgflag_range()
 *
 * as db_set_msgflag() but acts on a range of messages
 * 
 * returns:
 *  -1  error
 *   0  success 
 */
int db_set_msgflag_range(u64_t msguidlow, u64_t msguidhigh, u64_t mailboxuid, 
			 int *flags, int action_type)
{
  int i,placed=0;
  int left = DEF_QUERYSIZE - sizeof("answered_flag=1,");

  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET ");
  
  for (i=0; i<IMAP_NFLAGS; i++)
    {
      switch (action_type)
	{
	case IMAPFA_ADD:
	  if (flags[i] > 0)
	    {
	      strncat(query, db_flag_desc[i], left);
	      left -= sizeof("answered_flag");
	      strncat(query, "=1,", left);
	      left -= sizeof(" =1, ");
	      placed = 1;
	    }
	  break;

	case IMAPFA_REMOVE:
	  if (flags[i] > 0)
	    {
	      strncat(query, db_flag_desc[i], left);
	      left -= sizeof("answered_flag");
	      strncat(query, "=0,", left);
	      left -= sizeof("=0,");
	      placed = 1;
	    }
	  break;

	case IMAPFA_REPLACE:
	  strncat(query, db_flag_desc[i], left);
	  left -= sizeof("answered_flag");

	  if (flags[i] == 0)
	    strncat(query, "=0,", left);
	  else
	    strncat(query, "=1,", left);

	  left -= sizeof("=1,");
	  placed = 1;

	  break;
	}
    }

  if (!placed) 
    return 0; /* nothing to update */
  
  
  /* last character in string is comma, replace it --> strlen()-1 */
  snprintf(&query[strlen(query)-1], left, " WHERE "
	   "message_idnr >= %llu AND message_idnr <= %llu AND "
	   "status<2 AND mailbox_idnr = %llu", msguidlow, msguidhigh, mailboxuid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_set_msgflag_range(): could not set flag\n");
      return (-1);
    }

  return 0;
}


/*
 * db_get_msgdate()
 *
 * retrieves msg internal date; 'date' should be large enough (IMAP_INTERNALDATE_LEN)
 * returns -1 on error, 0 on success
 */
int db_get_msgdate(u64_t mailboxuid, u64_t msguid, char *date)
{
  snprintf(query, DEF_QUERYSIZE, "SELECT internal_date FROM messages WHERE mailbox_idnr = %llu "
	   "AND message_idnr = %llu AND unique_id!=\"\"", mailboxuid, msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msgdate(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msgdate(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  row = mysql_fetch_row(res);
  if (row)
    {
      strncpy(date, row[0], IMAP_INTERNALDATE_LEN);
      date[IMAP_INTERNALDATE_LEN - 1] = '\0';
    }
  else
    {
      /* no date ? let's say 1 jan 1970 */
      strncpy(date, "1970-01-01 00:00:01", IMAP_INTERNALDATE_LEN);
      date[IMAP_INTERNALDATE_LEN - 1] = '\0';
    }

  mysql_free_result(res);
  return 0;
}


/*
 * db_set_rfcsize()
 *
 * sets the RFCSIZE field for a message.
 *
 * returns -1 on failure, 0 on success
 */
int db_set_rfcsize(u64_t size, u64_t msguid, u64_t mailboxuid)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET rfcsize = %llu "
	   "WHERE message_idnr = %llu AND mailbox_idnr = %llu",
	   size, msguid, mailboxuid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_set_rfcsize(): could not insert RFC size into table\n");
      return -1;
    }

  return 0;
}


u64_t db_get_rfcsize(u64_t msguid, u64_t mailboxuid)
{
  u64_t size;

  snprintf(query, DEF_QUERYSIZE, "SELECT rfcsize FROM messages WHERE message_idnr = %llu "
	   "AND status<2 AND unique_id != '' AND mailbox_idnr = %llu", msguid, mailboxuid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_rfcsize(): could not fetch RFC size from table\n");
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_rfcsize(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  if (mysql_num_rows(res) < 1)
    {
      trace(TRACE_ERROR, "db_get_rfcsize(): message not found\n");
      mysql_free_result(res);
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row && row[0])
    size = strtoull(row[0], NULL, 10);
  else
    size = 0;

  mysql_free_result(res);
  return size;
}


/*
 * db_get_msginfo_range()
 *
 * retrieves message info in a single query for a range of messages.
 *
 * returns 0 on succes, -1 on dbase error, -2 on memory error
 *
 * caller should free *result
 */
int db_get_msginfo_range(u64_t msguidlow, u64_t msguidhigh, u64_t mailboxuid,
			 int getflags, int getinternaldate, int getsize, int getuid,
			 msginfo_t **result, unsigned *resultsetlen)
{
  unsigned nrows, i, j;

  *result = 0;
  *resultsetlen = 0;

  snprintf(query, DEF_QUERYSIZE, "SELECT seen_flag, answered_flag, deleted_flag, "
	   "flagged_flag, draft_flag, recent_flag, internal_date, rfcsize, message_idnr "
	   "FROM messages WHERE "
	   "message_idnr >= %llu AND message_idnr <= %llu AND mailbox_idnr = %llu "
	   "AND status<2 AND unique_id != '' "
	   "ORDER BY message_idnr ASC", msguidlow, msguidhigh, mailboxuid);
 
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msginfo_range(): could not select message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msginfo_range(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return (-1);
    }
  
   if ((nrows = mysql_num_rows(res)) == 0)
    {
      mysql_free_result(res);
      return 0;
    }

   *result = (msginfo_t*)my_malloc(nrows * sizeof(msginfo_t));
   if (! (*result)) 
     {
      trace(TRACE_ERROR,"db_get_msginfo_range(): out of memory\n");
      mysql_free_result(res);
      return -2;
     }

   memset(*result, 0, nrows * sizeof(msginfo_t));

   for (i=0; i<nrows; i++)
     {
       row = mysql_fetch_row(res);
       if (!row)
	 {
	   trace(TRACE_ERROR, "db_get_msginfo_range(): unexpected end of data\n");
	   my_free(*result);
	   mysql_free_result(res);
	   return -1;
	 }

       if (getflags)
	 {
	   for (j=0; j<IMAP_NFLAGS; j++)
	     (*result)[i].flags[j] = (row[j] && row[j][0] != '0') ? 1 : 0;
	 }

       if (getinternaldate && row[IMAP_NFLAGS])
	 strncpy((*result)[i].internaldate, row[IMAP_NFLAGS], IMAP_INTERNALDATE_LEN);

       if (getsize && row[IMAP_NFLAGS + 1])
	 (*result)[i].rfcsize = strtoull(row[IMAP_NFLAGS + 1], NULL, 10);

       if (getuid && row[IMAP_NFLAGS + 2])
	 (*result)[i].uid = strtoull(row[IMAP_NFLAGS + 2], NULL, 10);
     }

   mysql_free_result(res);
   *resultsetlen = nrows;

   return 0;
}


/*
 * db_get_main_header()
 *
 * builds a list containing the fields of the main header (== the first) of
 * a message
 *
 * returns:
 * 0   success
 * -1  dbase error
 * -2  out of memory
 * -3  parse error
 */
int db_get_main_header(u64_t msguid, struct list *hdrlist)
{
  u64_t dummy = 0, sizedummy = 0;
  int result;

  if (!hdrlist)
    return 0;

  if (hdrlist->start)
    list_freelist(&hdrlist->start);

  list_init(hdrlist);

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblks WHERE "
	   "message_idnr = %llu ORDER BY messageblk_idnr LIMIT 1", msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_main_header(): could not get message header\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_main_header(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return (-1);
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      /* no header for this message ?? */
      trace (TRACE_ERROR,"db_get_main_header(): error, no header for this message?");
      mysql_free_result(res);
      return -1;
    }

  result = mime_readheader(row[0], &dummy, hdrlist, &sizedummy);

  mysql_free_result(res);

  if (result == -1)
    {
      /* parse error */
      trace(TRACE_ERROR,"db_get_main_header(): error parsing header of message %llu\n",msguid);
      if (hdrlist->start)
	{
	  list_freelist(&hdrlist->start);
	  list_init(hdrlist);
	}

      return -3;
    }

  if (result == -2)
    {
      /* out of memory */
      trace(TRACE_ERROR,"db_get_main_header(): out of memory\n");
      if (hdrlist->start)
	{
	  list_freelist(&hdrlist->start);
	  list_init(hdrlist);
	}
      
      return -2;
    }

  /* success ! */
  return 0;
}




/*
 * searches the given range within a msg for key
 */
int db_search_range(db_pos_t start, db_pos_t end, const char *key, u64_t msguid)
{
  int i,startpos,endpos,j;
  int distance;

  if (start.block > end.block)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      return 0;
    }

  if (start.block == end.block && start.pos > end.pos)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      return 0;
    }

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblks WHERE message_idnr = %llu"
	   " ORDER BY messageblk_idnr", 
	   msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_search_range(): could not get message\n");
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_search_range(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return 0;
    }

  for (row = mysql_fetch_row(res), i=0; row && i < start.block; i++, row = mysql_fetch_row(res)) ;
      
  if (!row)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      mysql_free_result(res);
      return 0;
    }

  /* just one block? */
  if (start.block == end.block)
    {
      for (i=start.pos; i<=end.pos-strlen(key); i++)
	{
	  if (strncasecmp(&row[0][i], key, strlen(key)) == 0)
	    {
	      mysql_free_result(res);
	      return 1;
	    }
	}

      mysql_free_result(res);
      return 0;
    }


  /* 
   * multiple block range specified
   */
  
  for (i=start.block; i<=end.block; i++)
    {
      if (!row)
	{
	  trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
	  mysql_free_result(res);
	  return 0;
	}

      startpos = (i == start.block) ? start.pos : 0;
      endpos   = (i == end.block) ? end.pos+1 : (mysql_fetch_lengths(res))[0];

      distance = endpos - startpos;

      for (j=0; j<distance-strlen(key); j++)
	{
	  if (strncasecmp(&row[0][i], key, strlen(key)) == 0)
	    {
	      mysql_free_result(res);
	      return 1;
	    }
	}
	
      row = mysql_fetch_row(res); /* fetch next row */
    }

  mysql_free_result(res);

  return 0;
}


/*
 * db_mailbox_msg_match()
 *
 * checks if a msg belongs to a mailbox 
 */
int db_mailbox_msg_match(u64_t mailboxuid, u64_t msguid)
{
  
  int val;

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE message_idnr = %llu AND "
	   "mailbox_idnr = %llu AND status<002 AND unique_id!=\"\"", msguid, mailboxuid); 

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_mailbox_msg_match(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_mailbox_msg_match(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  val = mysql_num_rows(res);
  mysql_free_result(res);

  return val;
}

