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


/*
 * queries to create/drop temporary tables
 */
const char *create_tmp_tables_queries[] = 
{ "CREATE TABLE tmpmessage ("
  "messageidnr bigint(21) DEFAULT '0' NOT NULL auto_increment,"
  "mailboxidnr int(21) DEFAULT '0' NOT NULL,"
  "messagesize bigint(21) DEFAULT '0' NOT NULL,"
  "seen_flag tinyint(1) default '0' not null,"
  "answered_flag tinyint(1) default '0' not null,"
  "deleted_flag tinyint(1) default '0' not null,"
  "flagged_flag tinyint(1) default '0' not null,"
  "recent_flag tinyint(1) default '0' not null,"
  "draft_flag tinyint(1) default '0' not null,"
  "unique_id varchar(70) NOT NULL,"
  "internal_date datetime default '0' not null,"
  "status tinyint(3) unsigned zerofill default '000' not null,"
  "PRIMARY KEY (messageidnr),"
  "KEY messageidnr (messageidnr),"
  "UNIQUE messageidnr_2 (messageidnr))" ,

  "CREATE TABLE tmpmessageblk ("
  "messageblknr bigint(21) DEFAULT '0' NOT NULL auto_increment,"
  "messageidnr bigint(21) DEFAULT '0' NOT NULL,"
  "messageblk longtext NOT NULL,"
  "blocksize bigint(21) DEFAULT '0' NOT NULL,"
  "PRIMARY KEY (messageblknr),"
  "KEY messageblknr (messageblknr),"
  "KEY msg_index (messageidnr),"
  "UNIQUE messageblknr_2 (messageblknr)"
  ") TYPE=MyISAM "
};

const char *drop_tmp_tables_queries[] = { "DROP TABLE tmpmessage", "DROP TABLE tmpmessageblk" };

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
  mysql_real_connect (&conn,HOST,USER,PASS,MAILDATABASE,0,NULL,0); 

#ifdef mysql_errno
  if (mysql_errno(&conn)) {
    trace(TRACE_ERROR,"dbconnect(): mysql_real_connect failed: %s",mysql_error(&conn));
    return -1;
  }
#endif
	
  /* selecting the right database 
	  don't know if this needs to stay */
/*   if (mysql_select_db(&conn,MAILDATABASE)) {
    trace(TRACE_ERROR,"dbconnect(): mysql_select_db failed: %s",mysql_error(&conn));
    return -1;
  }  */

  return 0;
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
	  if (mysql_real_query(&conn, thequery, querysize) <0) 
	    {
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
 * clears the configuration table
 */
int db_clear_config()
{
  return db_query("DELETE FROM config");
}


int db_insert_config_item (char *item, char *value)
{
  /* insert_config_item will insert a configuration item in the database */

  /* allocating memory for query */
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO config (item,value) VALUES ('%s', '%s')",item, value);
  trace (TRACE_DEBUG,"insert_config_item(): executing query: [%s]",query);

  if (db_query(query)==-1)
    {
      trace (TRACE_DEBUG,"insert_config_item(): item [%s] value [%s] failed",item,value);
      return -1;
    }
  else 
    {
      return 0;
    }
}


char *db_get_config_item (char *item, int type)
{
  /* retrieves an config item from database */
  char *result = NULL;
  
	
  snprintf (query, DEF_QUERYSIZE, "SELECT value FROM config WHERE item = '%s'",item);
  trace (TRACE_DEBUG,"db_get_config_item(): retrieving config_item %s by query %s\n",
	 item, query);

  if (db_query(query)==-1)
    {
      if (type == CONFIG_MANDATORY)
	trace (TRACE_FATAL,"db_get_config_item(): query failed could not get value for %s. "
	       "This is needed to continue\n",item);
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): query failed. Could not get value for %s\n",
		 item);

      return NULL;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      if (type == CONFIG_MANDATORY)
	trace(TRACE_FATAL,"db_get_config_item(): mysql_store_result failed: %s\n",
	      mysql_error(&conn));
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): mysql_store_result failed (fatal): %s\n",
		 mysql_error(&conn));

      return 0;
    }

  if ((row = mysql_fetch_row(res))==NULL)
    {
      if (type == CONFIG_MANDATORY)
	trace (TRACE_FATAL,"db_get_config_item(): configvalue not found for %s. rowfetch failure. "
	       "This is needed to continue\n",item);
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): configvalue not found. rowfetch failure.  "
		 "Could not get value for %s\n",item);

      mysql_free_result(res);
      return NULL;
    }
	
  if (row[0]!=NULL)
    {
      result=(char *)my_malloc(strlen(row[0])+1);
      if (result!=NULL)
	strcpy (result,row[0]);
      trace (TRACE_DEBUG,"Ok result [%s]\n",result);
    }
	
  mysql_free_result(res);
  return result;
}


int db_addalias (u64_t useridnr, char *alias, int clientid)
{
  /* adds an alias for a specific user */
  snprintf (query, DEF_QUERYSIZE,
	    "INSERT INTO aliases (alias,deliver_to,client_id) VALUES ('%s','%llu',%d)",
	   alias, useridnr, clientid);
	
  trace (TRACE_DEBUG,"db_addalias(): executing query for user: [%s]", query);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias(): query for adding alias failed : [%s]", query);
      return -1;
    }
  
  return 0;
}


int db_removealias (u64_t useridnr,const char *alias)
{
  snprintf (query, DEF_QUERYSIZE,
	    "DELETE FROM aliases WHERE deliver_to=%llu AND alias = '%s'", useridnr, alias);
	   
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_removealias(): query for removing alias failed : [%s]", query);
      return -1;
    }
  
  return 0;
}
  


u64_t db_get_inboxid (u64_t *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  u64_t inboxid;

  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM mailbox WHERE name='INBOX' AND owneridnr=%llu",
	   *useridnr);

  trace(TRACE_DEBUG,"db_get_inboxid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_mailboxid(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): user has no INBOX");
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


u64_t db_get_message_mailboxid (u64_t *messageidnr)
{
  /* returns the mailbox id of a message */
  u64_t mailboxid;
  
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM message WHERE messageidnr = %llu",
	   *messageidnr);

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


u64_t db_get_useridnr (u64_t messageidnr)
{
  /* returns the userid from a messageidnr */
  u64_t mailboxidnr;
  u64_t userid;
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM message WHERE messageidnr = %llu",
	   messageidnr);

  trace(TRACE_DEBUG,"db_get_useridnr(): executing query : [%s]",query);
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

  mailboxidnr = (row && row[0]) ? strtoull(row[0], NULL, 10) : -1;
  mysql_free_result(res);
	
  if (mailboxidnr == -1)
    {
      
      return 0;
    }

  snprintf (query, DEF_QUERYSIZE, "SELECT owneridnr FROM mailbox WHERE mailboxidnr = %llu",
	   mailboxidnr);

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
u64_t db_insert_message (u64_t *useridnr)
{
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);
  
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO message(mailboxidnr,messagesize,unique_id,internal_date)"
	   " VALUES (%llu,0,\" \",\"%s\")",
	   db_get_inboxid(useridnr), timestr);

  trace (TRACE_DEBUG,"db_insert_message(): inserting message query [%s]",query);
  if (db_query (query)==-1)
    {
      trace(TRACE_STOP,"db_insert_message(): dbquery failed");
    }	
  
  return db_insert_result("");
}


u64_t db_update_message (u64_t *messageidnr, char *unique_id,
		u64_t messagesize)
{
  snprintf (query, DEF_QUERYSIZE,
	   "UPDATE message SET messagesize=%llu, unique_id=\"%s\" where messageidnr=%llu",
	   messagesize, unique_id, *messageidnr);
  
  trace (TRACE_DEBUG,"db_update_message(): updating message query [%s]",query);
  if (db_query (query)==-1)
    {
      
      trace(TRACE_STOP,"db_update_message(): dbquery failed");
    }
	
  
  return 0;
}


/*
 * insert a msg block
 * returns msgblkid on succes, -1 on failure
 */
u64_t db_insert_message_block (char *block, u64_t messageidnr)
{
  char *escblk=NULL, *tmpquery=NULL;
  int len,esclen=0;

  if (block != NULL)
    {
      len = strlen(block);

      trace (TRACE_DEBUG,"db_insert_message_block(): inserting a %d bytes block\n",
	     len);

      /* allocate memory twice as much, for eacht character might be escaped 
	 added aditional 250 bytes for possible function err */

      memtst((escblk=(char *)my_malloc(((len*2)+250)))==NULL); 

      /* escape the string */
      if ((esclen = mysql_escape_string(escblk, block, len)) > 0)
	{
	  /* add an extra 500 characters for the query */
	  memtst((tmpquery=(char *)my_malloc(esclen + 500))==NULL);
	
	  snprintf (tmpquery, esclen+500,
		   "INSERT INTO messageblk(messageblk,blocksize,messageidnr) "
		   "VALUES (\"%s\",%d,%llu)",
		   escblk,len,messageidnr);

	  if (db_query (tmpquery)==-1)
	    {
	      my_free(escblk);
	      my_free(tmpquery);
	      trace(TRACE_ERROR,"db_insert_message_block(): dbquery failed\n");
	      return -1;
	    }

	  /* freeing buffers */
	  my_free(tmpquery);
	  my_free(escblk);
	  return db_insert_result("");
	}
      else
	{
	  trace (TRACE_ERROR,"db_insert_message_block(): mysql_real_escape_string() "
		 "returned empty value\n");

	  my_free(escblk);
	  return -1;
	}
    }
  else
    {
      trace (TRACE_ERROR,"db_insert_message_block(): value of block cannot be NULL, "
	     "insertion not possible\n");
      return -1;
    }

  return -1;
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
int db_send_message_lines (void *fstream, u64_t messageidnr, long lines, int no_end_dot)
{
  char *buffer = NULL;
  char *nextpos, *tmppos = NULL;
  int block_count;
  unsigned long *lengths;
  u64_t rowlength;
  
  trace (TRACE_DEBUG,"db_send_message_lines(): request for [%d] lines",lines);

  
  memtst ((buffer=(char *)my_malloc(READ_BLOCK_SIZE*2))==NULL);

  snprintf (query, DEF_QUERYSIZE, 
	    "SELECT * FROM messageblk WHERE messageidnr=%llu ORDER BY messageblknr ASC",
	   messageidnr);
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
	 lines,messageidnr);
  
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
  snprintf (query, DEF_QUERYSIZE, "SELECT * FROM message WHERE mailboxidnr=%llu AND status<002 AND "
	   "unique_id!=\"\" order by status ASC",
	   (db_get_inboxid(&useridnr)));

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
		    "UPDATE message set status=%llu WHERE messageidnr=%llu AND status<002",
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
	    "SELECT SUM(messagesize) FROM message WHERE mailboxidnr = %llu AND status<002",
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


u64_t db_check_sizelimit (u64_t addblocksize, u64_t messageidnr, 
				  u64_t *useridnr)
{
  /* returns -1 when a block cannot be inserted due to dbase failure
   *          1 when a block cannot be inserted due to quotum exceed
   *         -2 when a block cannot be inserted due to quotum exceed and a dbase failure occured
   * also does a complete rollback when this occurs 
   * returns 0 when situation is ok 
   */

  u64_t currmail_size = 0, maxmail_size = 0, j, n;

  *useridnr = db_get_useridnr (messageidnr);
	
  /* checking current size */
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM mailbox WHERE owneridnr = %llu",
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
  maxmail_size = db_getmaxmailsize(*useridnr);


  trace (TRACE_DEBUG, "db_check_sizelimit(): comparing currsize + blocksize  [%d], maxsize [%d]\n",
	 currmail_size, maxmail_size);
	

  /* currmail already represents the current size of messages from this user */
	
  if (((currmail_size) > maxmail_size) && (maxmail_size != 0))
    {
      trace (TRACE_INFO,"db_check_sizelimit(): mailboxsize of useridnr %llu exceed with %llu bytes\n", 
	     *useridnr, (currmail_size)-maxmail_size);

      /* user is exceeding, we're going to execute a rollback now */
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblk WHERE messageidnr = %llu", 
	       messageidnr);

      if (db_query(query) != 0)
	{
	  trace (TRACE_ERROR,"db_check_sizelimit(): rollback of mailbox add failed\n");
	  return -2;
	}

      snprintf (query,DEF_QUERYSIZE,"DELETE FROM message WHERE messageidnr = %llu",
	       messageidnr);

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
  
  u64_t affected_rows=0;

  
	
  /* first we're deleting all the messageblks */
  snprintf (query,DEF_QUERYSIZE,"SELECT messageidnr FROM message WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
	
  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR,"db_deleted_purge(): Cound not execute query [%s]",query);
      
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_deleted_purge(): mysql_store_result failed:  %s",mysql_error(&conn));
      
      return -1;
    }
  
  if (mysql_num_rows(res)<1)
    {
      
      mysql_free_result(res);
      return 0;
    }
	
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblk WHERE messageidnr=%s",row[0]);
      trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
      if (db_query(query)==-1)
	{
	  
	  mysql_free_result(res);
	  return -1;
	}
    }

  /* messageblks are deleted. Now delete the messages */
  snprintf (query,DEF_QUERYSIZE,"DELETE FROM message WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
  if (db_query(query)==-1)
    {
      
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
  snprintf (query,DEF_QUERYSIZE,"UPDATE message SET status=003 WHERE status=002");
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



/* 
 * will check for messageblks that are not
 * connected to messages 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing nlost items being the messageblknr's of the
 * lost messageblks
 *
 * the caller should free this memory!
 */
int db_icheck_messageblks(int *nlost, u64_t **lostlist)
{
  int i;
  *nlost = 0;
  *lostlist = NULL;

  /* this query can take quite some time! */
  snprintf (query,DEF_QUERYSIZE,"SELECT messageblk.messageblknr FROM messageblk "
	    "LEFT JOIN message ON messageblk.messageidnr = message.messageidnr "
	    "WHERE message.messageidnr IS NULL");

  trace (TRACE_DEBUG,"db_icheck_messageblks(): executing query [%s]",query);

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
    
  *nlost = mysql_num_rows(res);
  trace(TRACE_DEBUG,"db_icheck_messageblks(): found %d lost message blocks\n", *nlost);

  if (*nlost == 0)
    return 0;


  *lostlist = (u64_t*)my_malloc(sizeof(u64_t) * (*nlost));
  if (!*lostlist)
    {
      *nlost = 0;
      trace(TRACE_ERROR,"db_icheck_messageblks(): out of memory when allocatin %d items\n",*nlost);
      return -2;
    }

  i = 0;
  while ((row = mysql_fetch_row(res)) && i<*nlost)
    (*lostlist)[i++] = strtoull(row[0], NULL, 10);

  return 0;
}


/* 
 * will check for messages that are not
 * connected to mailboxes 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing nlost items being the messageid's of the
 * lost messages
 *
 * the caller should free this memory!
 */
int db_icheck_messages(int *nlost, u64_t **lostlist)
{
  int i;
  *nlost = 0;
  *lostlist = NULL;

  snprintf (query,DEF_QUERYSIZE,"SELECT message.messageidnr FROM message "
	    "LEFT JOIN mailbox ON message.mailboxidnr = mailbox.mailboxidnr "
	    "WHERE mailbox.mailboxidnr IS NULL");

  trace (TRACE_DEBUG,"db_icheck_messages(): executing query [%s]",query);

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
    
  *nlost = mysql_num_rows(res);
  trace(TRACE_DEBUG,"db_icheck_messages(): found %d lost messages\n", *nlost);

  if (*nlost == 0)
    return 0;


  *lostlist = (u64_t*)my_malloc(sizeof(u64_t) * (*nlost));
  if (!*lostlist)
    {
      *nlost = 0;
      trace(TRACE_ERROR,"db_icheck_messages(): out of memory when allocating %d items\n",*nlost);
      return -2;
    }

  i = 0;
  while ((row = mysql_fetch_row(res)) && i<*nlost)
    (*lostlist)[i++] = strtoull(row[0], NULL, 10);

  return 0;
}


/* 
 * will check for mailboxes that are not
 * connected to users 
 *
 * returns -1 on dbase error, -2 on memory error, 0 on succes
 * on succes lostlist will be a list containing nlost items being the mailboxid's of the
 * lost mailboxes
 *
 * the caller should free this memory!
 */
int db_icheck_mailboxes(int *nlost, u64_t **lostlist)
{
  int i;
  *nlost = 0;
  *lostlist = NULL;

  snprintf (query,DEF_QUERYSIZE,"SELECT mailbox.mailboxidnr FROM mailbox "
	    "LEFT JOIN user ON mailbox.owneridnr = user.useridnr "
	    "WHERE user.useridnr IS NULL");

  trace (TRACE_DEBUG,"db_icheck_mailboxes(): executing query [%s]",query);

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
    
  *nlost = mysql_num_rows(res);
  trace(TRACE_DEBUG,"db_icheck_mailboxes(): found %d lost mailboxes\n", *nlost);

  if (*nlost == 0)
    return 0;


  *lostlist = (u64_t*)my_malloc(sizeof(u64_t) * (*nlost));
  if (!*lostlist)
    {
      *nlost = 0;
      trace(TRACE_ERROR,"db_icheck_mailboxes(): out of memory when allocating %d items\n",*nlost);
      return -2;
    }

  i = 0;
  while ((row = mysql_fetch_row(res)) && i<*nlost)
    (*lostlist)[i++] = strtoull(row[0], NULL, 10);

  return 0;
}


/*
 * deletes the specified block. used by maintenance
 */
int db_delete_messageblk(u64_t uid)
{
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageblknr = %llu",uid);
  return db_query(query);
}


/*
 * deletes the specified message. used by maintenance
 */
int db_delete_message(u64_t uid)
{
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu",uid);
  if (db_query(query) == -1)
    return -1;

  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu",uid);
  return db_query(query);
}

/*
 * deletes the specified mailbox. used by maintenance
 */
int db_delete_mailbox(u64_t uid)
{
  u64_t msgid;
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %llu",uid);

  if (db_query(query) == -1)
    return -1;

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_delete_mailbox(): mysql_store_result() failed [%s]\n",mysql_error(&conn));
      return -1;
    }

  while ((row = mysql_fetch_row(res)))
    {
      msgid = strtoull(row[0], NULL, 10);

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu",msgid);
      if (db_query(query) == -1)
	return -1;

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu",msgid);
      if (db_query(query) == -1)
	return -1;
    }
  
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailbox WHERE mailboxidnr = %llu",uid);
  return db_query(query);
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
int db_imap_append_msg(char *msgdata, u64_t datalen, u64_t mboxid, u64_t uid)
{
  char timestr[30];
  time_t td;
  struct tm tm;
  u64_t msgid,cnt;
  int result;
  char savechar;
  char unique_id[UID_SIZE]; /* unique id */
  
  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  /* create a msg 
   * status and seen_flag are set to 001, which means the message has been read 
   */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO message "
	   "(mailboxidnr,messagesize,unique_id,internal_date,status,"
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
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu", msgid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
	      "dbase invalid now..\n", msgid);

      
      return 1;
    }

  if (cnt == datalen-1)
    {
      /* msg consists of a single header */
      trace(TRACE_INFO, "db_imap_append_msg(): msg only contains a header\n");

      if (db_insert_message_block(msgdata, msgid) == -1 || 
	  db_insert_message_block(" \n", msgid)   == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }
  else
    {
      /* output header */
      cnt++;
      savechar = msgdata[cnt];                        /* remember char */
      msgdata[cnt] = 0;                               /* terminate string */
      if (db_insert_message_block(msgdata, msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%llu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

      msgdata[cnt] = savechar;                        /* restore */

      /* output message */
      while ((datalen - cnt) > READ_BLOCK_SIZE)
	{
	  savechar = msgdata[cnt + READ_BLOCK_SIZE];        /* remember char */
	  msgdata[cnt + READ_BLOCK_SIZE] = 0;               /* terminate string */

	  if (db_insert_message_block(&msgdata[cnt], msgid) == -1)
	    {
	      trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		      "dbase invalid now..\n", msgid);

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		      "for msg id [%llu], dbase could be invalid now..\n", msgid);

	      
	      return -1;
	    }

	  msgdata[cnt + READ_BLOCK_SIZE] = savechar;        /* restore */

	  cnt += READ_BLOCK_SIZE;
	}


      if (db_insert_message_block(&msgdata[cnt], msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%llu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %llu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		  "for msg id [%llu], dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }  
  
  /* create a unique id */
  snprintf (unique_id,UID_SIZE,"%lluA%lu",msgid,td);

  /* set info on message */
  db_update_message (&msgid, unique_id, datalen);
  
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

  snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name='%s' AND owneridnr=%llu",
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
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailboxidnr FROM mailbox WHERE "
	     "owneridnr=%llu AND is_subscribed != 0", ownerid);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailboxidnr FROM mailbox WHERE "
	     "owneridnr=%llu", ownerid);

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
	   " FROM mailbox WHERE mailboxidnr = %llu", mb->uid);

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
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr, seen_flag, recent_flag "
	   "FROM message WHERE mailboxidnr = %llu "
	   "AND status<2 AND unique_id!=\"\" ORDER BY messageidnr ASC", mb->uid);

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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE unique_id!=\"\""
	   "ORDER BY messageidnr DESC LIMIT 0,1");
  
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
  mb->msguidnext = row[0] ? strtoull(row[0], NULL, 10)+1 : 1;

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
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO mailbox (name, owneridnr,"
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
  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailbox WHERE"
	   " mailboxidnr = %llu AND owneridnr = %llu", uid, useridnr);

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
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s/%s'"
	     " AND owneridnr = %llu",
	     row[0],filter,useridnr);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s'"
	     " AND owneridnr = %llu",filter,useridnr);

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
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailbox WHERE mailboxidnr = %llu", uid);
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
  

  snprintf(query, DEF_QUERYSIZE, "SELECT no_select FROM mailbox WHERE mailboxidnr = %llu",uid);

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
  

  snprintf(query, DEF_QUERYSIZE, "SELECT no_inferiors FROM mailbox WHERE mailboxidnr = %llu",uid);

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
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET no_select = %d WHERE mailboxidnr = %llu",
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
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %llu", uid);

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
      snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE"
	       " mailboxidnr = %llu AND deleted_flag=1 AND status<2 ORDER BY messageidnr DESC", uid);

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
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %llu AND deleted_flag=1 AND status<2", uid);

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
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET mailboxidnr=%llu WHERE"
	   " mailboxidnr = %llu", to, from);

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
 * returns 0 on success, -1 on failure
 */
int db_copymsg(u64_t msgid, u64_t destmboxid)
{
  u64_t newid,tmpid;
  time_t td;

  time(&td);              /* get time */

  /* copy: */

  /* first to temporary table */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessage (mailboxidnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
       " unique_id, internal_date) "
	   "SELECT mailboxidnr, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
       "FROM message WHERE message.messageidnr = %llu",
	   msgid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message\n");
      return -1;
    }

  tmpid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessageblk (messageidnr, messageblk, blocksize) "
	   "SELECT %llu, messageblk, blocksize FROM messageblk "
	   "WHERE messageblk.messageidnr = %llu ORDER BY messageblk.messageblknr", tmpid, msgid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message blocks\n");

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE messageidnr = %llu", tmpid);

      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      return -1;
    }


  /* now to actual tables */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO message (mailboxidnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
	   " unique_id, internal_date) "
	   "SELECT %llu, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
	   "FROM tmpmessage WHERE tmpmessage.messageidnr = %llu",
	   destmboxid, tmpid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message\n");

      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE messageidnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE messageidnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");
      
      return -1;
    }

  /* retrieve id of new message */
  newid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO messageblk (messageidnr, messageblk, blocksize) "
	   "SELECT %llu, messageblk, blocksize FROM tmpmessageblk "
	   "WHERE tmpmessageblk.messageidnr = %llu ORDER BY tmpmessageblk.messageblknr", newid, tmpid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message blocks\n");

      /* delete temporary messages */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE messageidnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE messageidnr = %llu", tmpid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");

      /* delete inserted message */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %llu",newid);
      if (db_query(query) == -1)
	trace(TRACE_FATAL, "db_copymsg(): could not delete faulty message, dbase contains "
	      "invalid data now; msgid [%llu]\n",newid);
      
      return -1;
    }

  /* delete temporary messages */
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessage WHERE messageidnr = %llu", tmpid);
  if (db_query(query) == -1)
    trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message\n");
      
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM tmpmessageblk WHERE messageidnr = %llu", tmpid);
  if (db_query(query) == -1)
    trace(TRACE_ERROR, "db_copymsg(): could not delete temporary message blocks\n");

  /* all done, validate new msg by creating a new unique id for the copied msg */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET unique_id=\"%lluA%lu\" "
	   "WHERE messageidnr=%llu", newid, td, newid);

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
  

  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailbox WHERE mailboxidnr = %llu",uid);

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
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET name = '%s' WHERE mailboxidnr = %llu",
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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %llu "
	   "AND status<2 AND seen_flag = 0 AND unique_id != \"\" "
	   "ORDER BY messageidnr ASC LIMIT 0,1", uid);

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
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET is_subscribed = 1 WHERE mailboxidnr = %llu", 
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
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET is_subscribed = 0 WHERE mailboxidnr = %llu", 
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
int db_get_msgflag(const char *name, u64_t mailboxuid, u64_t msguid)
{
  
  char flagname[DEF_QUERYSIZE/2]; /* should be sufficient ;) */
  int val;

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

  snprintf(query, DEF_QUERYSIZE, "SELECT %s FROM message WHERE mailboxidnr = %llu "
	   "AND status<2 AND messageidnr = %llu AND unique_id != \"\"", flagname, mailboxuid, msguid);

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
  if (row)
    val = atoi(row[0]);
  else
    val = 0; /* none found */
      
  mysql_free_result(res);
  return val;
}


/*
 * db_set_msgflag()
 *
 * sets a flag specified by 'name' to on/off
 *
 * returns:
 *  -1  error
 *   0  success
 */
int db_set_msgflag(const char *name, u64_t mailboxuid, u64_t msguid, int val)
{
  
  char flagname[DEF_QUERYSIZE/2]; /* should be sufficient ;) */

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
    return 0; /* non-existent flag is cannot set */

  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET %s = %d WHERE mailboxidnr = %llu "
	   "AND status<2 AND messageidnr = %llu", flagname, val, mailboxuid, msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_set_msgflag(): could not set flag\n");
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
  snprintf(query, DEF_QUERYSIZE, "SELECT internal_date FROM message WHERE mailboxidnr = %llu "
	   "AND messageidnr = %llu AND unique_id!=\"\"", mailboxuid, msguid);

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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE "
	   "messageidnr = %llu ORDER BY messageblknr LIMIT 1", msguid);

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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE messageidnr = %llu"
	   " ORDER BY messageblknr", 
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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE messageidnr = %llu AND "
	   "mailboxidnr = %llu AND status<002 AND unique_id!=\"\"", msguid, mailboxuid); 

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

